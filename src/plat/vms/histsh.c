/* histsh.c - HISTSH C1: run DCL on a pseudo-terminal and keep its recall buffer
   saved across sessions (docs/C1.md).

   Started by HISTSH_START.COM at the end of SYLOGIN. "HISTSH -v" prints the version. Returns SS$_NORMAL once the
   child DCL has logged out, or an error if it could not start the child, in which
   case the login carries on as plain DCL.

   Everything after start-up runs at AST level: the terminal read, the
   pseudo-terminal read, the hidden-command timer and the child's completion. ASTs
   are serialised, so the relay (src/core/hs_relay.c) never sees two callers. */
#define __NEW_STARLET 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stat.h>
#include <unixio.h>
#include <descrip.h>
#include <starlet.h>
#include <lib$routines.h>
#include <libdef.h>
#include <libclidef.h>
#include <ssdef.h>
#include <iodef.h>
#include <dvidef.h>
#include <unistd.h>
#include <dcdef.h>
#include <ttdef.h>
#include <tt2def.h>
#include <ptddef.h>
#include <efndef.h>
#include <iosbdef.h>
#include <va_rangedef.h>
#include "hs_relay.h"
#include "hs_hist.h"
#include "hs_version.h"

#define RBUF_OFF   0            /* PTD$ read buffer: word status, word count, data */
#define WBUF_OFF   1024         /* PTD$ write slots, WSLOT bytes each */
#define WSLOT      512
#define NWSLOTS    8
#define DATA_MAX   (WSLOT - 4)
#define BOOT_CMD   "@HISTSH:HISTSH_CHILD"   /* typed at the first prompt; never kept */
#define REMARK_CMD "@HISTSH:HISTSH_REMARK"  /* typed after SET PROMPT; never kept */

typedef struct { unsigned char cls, type; unsigned short width;
                 unsigned int basic, ext; } termchars;

static unsigned short tt_chan, pty_chan;
static termchars tt_saved;
static int tt_changed, ctrl_changed;
static unsigned int ctrl_saved;
static char *region;
static unsigned int wslot;
static hs_relay relay;
static volatile int child_done;
static unsigned int child_status, child_pid;
static char tt_buf[256];
static IOSB tt_iosb;
static char hist_file[256], new_file[256], load_file[256];
static unsigned int timer_q[2];

/* ---- small helpers ---- */

static void dsc(struct dsc$descriptor_s *d, const char *s, unsigned short n)
{
    d->dsc$w_length = n;
    d->dsc$b_dtype = DSC$K_DTYPE_T;
    d->dsc$b_class = DSC$K_CLASS_S;
    d->dsc$a_pointer = (char *) s;
}

static unsigned int set_lnm(const char *name, const char *value)
{
    struct dsc$descriptor_s n, v, t;
    dsc(&n, name, (unsigned short) strlen(name));
    dsc(&v, value, (unsigned short) strlen(value));
    dsc(&t, "LNM$PROCESS", 11);
    return lib$set_logical(&n, &v, &t, 0, 0);
}

static int trn_lnm(const char *name, char *out, unsigned short size)
{
    struct dsc$descriptor_s n, o;
    unsigned short len = 0;
    dsc(&n, name, (unsigned short) strlen(name));
    dsc(&o, out, (unsigned short) (size - 1));
    if (!(lib$get_logical(&n, &o, &len, 0, 0, 0, 0, 0) & 1)) return 0;
    out[len] = '\0';
    return 1;
}

/* The parent's prompt, from the HISTSH_PROMPT symbol HISTSH_START.COM sets
   (F$ENVIRONMENT("PROMPT")): a spawned DCL would otherwise start with "$ ". */
static void get_prompt(char *out, unsigned short size)
{
    struct dsc$descriptor_s n, o;
    unsigned short len = 0;
    dsc(&n, "HISTSH_PROMPT", 13);
    dsc(&o, out, (unsigned short) (size - 1));
    if (!(lib$get_symbol(&n, &o, &len, 0) & 1) || len == 0) { strcpy(out, "$ "); return; }
    out[len] = '\0';
}

static void tt_write(const char *p, unsigned int n)
{
    IOSB iosb;
    while (n) {
        unsigned int k = n > 4096 ? 4096 : n;
        sys$qiow(EFN$C_ENF, tt_chan, IO$_WRITEVBLK | IO$M_NOFORMAT, &iosb, 0, 0,
                 (void *) p, k, 0, 0, 0, 0);
        p += k; n -= k;
    }
}

static void say(const char *text)            /* a HISTSH message on its own line */
{
    tt_write("\r\n", 2);
    tt_write(text, (unsigned int) strlen(text));
    tt_write("\r\n", 2);
}

/* ---- relay sinks ---- */

static void sink_screen(void *ctx, const char *p, unsigned int n) { (void) ctx; tt_write(p, n); }

/* Bytes for the child that its terminal hasn't taken yet. PTD$WRITE reports in
   the buffer how many it took; when the typeahead buffer is full it says
   DATAOVERUN (seen on IA64 with a burst of keys), and the rest is sent again
   shortly after instead of being lost (NOTES N17). */
static char child_q[8192];
static unsigned int child_qn;
static void child_retry_ast(int prm);

static void child_flush(void)
{
    static unsigned int retry_q[2];
    while (child_qn) {
        unsigned int k = child_qn > DATA_MAX ? DATA_MAX : child_qn, took;
        unsigned short *h;
        char *slot = region + WBUF_OFF + WSLOT * wslot;
        wslot = (wslot + 1) % NWSLOTS;
        h = (unsigned short *) slot;
        memcpy(slot + 4, child_q, k);
        h[0] = 0; h[1] = 0;
        ptd$write(pty_chan, 0, 0, slot, k, 0, 0);
        took = (h[0] & 1) || h[0] == SS$_DATAOVERUN ? h[1] : 0;
        if (took > k) took = k;
        memmove(child_q, child_q + took, child_qn - took);
        child_qn -= took;
        if (took < k) {                      /* full: try the rest again in 20 ms */
            retry_q[0] = (unsigned int) -200000; retry_q[1] = 0xFFFFFFFFu;
            sys$setimr(EFN$C_ENF, (struct _generic_64 *) retry_q, child_retry_ast, 2, 0);
            return;
        }
    }
}

static void child_retry_ast(int prm) { (void) prm; if (!child_done) child_flush(); }

static void sink_child(void *ctx, const char *p, unsigned int n)
{
    int waiting = child_qn != 0;             /* a retry is already due */
    (void) ctx;
    if (n > sizeof child_q - child_qn) n = sizeof child_q - child_qn;
    memcpy(child_q + child_qn, p, n);
    child_qn += n;
    if (!waiting) child_flush();
}

static void timer_ast(int prm);

static void sink_timer(void *ctx, unsigned int ms)
{
    (void) ctx;
    sys$cantim(1, 0);
    if (ms == 0) return;
    timer_q[0] = (unsigned int) -(int) (ms * 10000u);       /* relative: negative 100 ns */
    timer_q[1] = 0xFFFFFFFFu;
    sys$setimr(EFN$C_ENF, (struct _generic_64 *) timer_q, timer_ast, 1, 0);
}

/* ---- the shared history file (docs/C2.md) ---- */

#define DCL_RECALL_MAX 254      /* most that DCL's own buffer takes (fewer if long: N13) */
#define LINE_MAX_LEN 4100

static hs_hist hist;            /* what up-arrow walks through */
static hs_lines snap;           /* DCL's buffer at the last snapshot */
static unsigned int hist_max = 2000;

/* Lines of a file, oldest first; skip_boot drops HISTSH's own boot command. */
static void read_lines(const char *file, hs_lines *out, int skip_boot)
{
    static char buf[LINE_MAX_LEN];
    FILE *f = fopen(file, "r");
    if (!f) return;
    while (fgets(buf, sizeof buf, f)) {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        if (n == 0) continue;
        if (skip_boot && strcmp(buf, BOOT_CMD) == 0) continue;
        hs_lines_push(out, buf);
    }
    fclose(f);
}

static void owner_only(const char *file) { chmod(file, 0600); }   /* (S,O:RW,G,W) */

/* Append one command to the shared file: open, append, close, retrying while
   another session has it open (NOTES N16), for at most about a second. */
static void append_line(const char *s)
{
    struct stat st;
    int created = stat(hist_file, &st) != 0, i;
    FILE *f = 0;
    for (i = 0; i < 100 && !(f = fopen(hist_file, "a")); i++) {
        float secs = 0.01f;
        int ftype = 4;                                   /* LIB$K_IEEE_S */
        lib$wait(&secs, 0, &ftype);
    }
    if (!f) return;
    fprintf(f, "%s\n", s);
    fclose(f);
    if (created) owner_only(hist_file);
}

/* One of HISTSH's own commands (the boot or remark command, or a hidden RECALL on one
   of its files): never history, whatever DCL's buffer says. */
static int own_command(const char *s)
{
    if (strcmp(s, BOOT_CMD) == 0 || strcmp(s, REMARK_CMD) == 0) return 1;
    return strncmp(s, "RECALL/", 7) == 0 && strstr(s, "HISTSH$") != 0;
}

/* A command DCL ran: into up-arrow's history and onto the end of the file. */
static void record(const char *s)
{
    if (own_command(s)) return;
    if (hs_hist_add(&hist, s)) append_line(s);
}

/* At start: the history for up-arrow is the newest hist_max lines of the file.
   A file grown past twice that is cut back to hist_max lines. */
static void load_history(void)
{
    hs_lines all;
    unsigned int i, first;
    hs_lines_init(&all);
    read_lines(hist_file, &all, 1);
    first = all.n > hist_max ? all.n - hist_max : 0;
    for (i = first; i < all.n; i++) hs_hist_add(&hist, all.e[i]);
    if (all.n > 2 * hist_max) {
        struct dsc$descriptor_s d;
        char old[270];
        FILE *f = fopen(new_file, "w");
        if (f) {
            for (i = 0; i < hist.l.n; i++) fprintf(f, "%s\n", hist.l.e[i]);
            fclose(f);
            if (rename(new_file, hist_file) == 0) {      /* a new version, then */
                sprintf(old, "%s;-1", hist_file);        /* drop the older ones */
                dsc(&d, old, (unsigned short) strlen(old));
                for (i = 0; i < 10; i++)
                    if (!(lib$delete_file(&d, 0, 0, 0, 0, 0, 0, 0, 0, 0) & 1)) break;
            }
        }
    }
    owner_only(hist_file);
    hs_lines_free(&all);
}

/* After the start-up snapshot: commands typed while LOGIN.COM ran are recorded
   (NOTES N12), and the load file is the newest of the history, for DCL's own
   buffer (the RECALL command). That is also the baseline for the next snapshot. */
static void build_load_file(void)
{
    hs_lines early;
    unsigned int i, first;
    FILE *f;
    hs_lines_init(&early);
    read_lines(new_file, &early, 1);
    remove(new_file);
    for (i = 0; i < early.n; i++) record(early.e[i]);
    hs_lines_free(&early);
    hs_lines_free(&snap);
    first = hist.l.n > DCL_RECALL_MAX ? hist.l.n - DCL_RECALL_MAX : 0;
    f = fopen(load_file, "w");
    if (f) {
        for (i = first; i < hist.l.n; i++) {
            fprintf(f, "%s\n", hist.l.e[i]);
            hs_lines_push(&snap, hist.l.e[i]);
        }
        fclose(f);
    }
}

/* After a hidden RECALL/OUTPUT: what DCL added since the last snapshot was run. */
static void finish_save(void)
{
    hs_lines now;
    unsigned int i;
    struct stat st;
    if (stat(new_file, &st) != 0) return;
    hs_lines_init(&now);
    read_lines(new_file, &now, 1);
    remove(new_file);
    for (i = hs_snapshot_new(&snap, &now); i < now.n; i++) record(now.e[i]);
    hs_lines_move(&snap, &now);
}

static void sink_event(void *ctx, int ev)
{
    static int warned;
    (void) ctx;
    if (ev == HS_EV_SNAPPED) build_load_file();
    else if (ev == HS_EV_LOADED) remove(load_file);
    else if (ev == HS_EV_SAVED) finish_save();
    else if (ev == HS_EV_TIMEOUT && !warned) {
        warned = 1;
        say("%HISTSH-W-NOPROMPT, DCL's prompt was not seen; history may not be saved");
    }
}

static void timer_ast(int prm) { (void) prm; hs_relay_timeout(&relay); }

/* ---- I/O ASTs ---- */

static void pty_read_ast(int prm);

static void pty_post_read(void)
{
    ptd$read(EFN$C_ENF, pty_chan, pty_read_ast, 0, region + RBUF_OFF, DATA_MAX);
}

static void pty_read_ast(int prm)
{
    unsigned short *h = (unsigned short *) (region + RBUF_OFF);
    (void) prm;
    if (h[0] & 1) hs_relay_child_output(&relay, region + RBUF_OFF + 4, h[1]);
    else if (h[0] == SS$_ABORT || h[0] == SS$_CANCEL || h[0] == SS$_HANGUP) return;
    if (!child_done) pty_post_read();
}

static void tt_read_ast(int prm);

static void tt_post_read(void)
{
    static unsigned int no_terminators[2] = { 0, 0 };
    sys$qio(EFN$C_ENF, tt_chan, IO$_READVBLK | IO$M_NOECHO | IO$M_NOFILTR, &tt_iosb,
            tt_read_ast, 0, tt_buf, 1, 0, (unsigned __int64) no_terminators, 0, 0);
}

static void tt_read_ast(int prm)
{
    static unsigned int no_terminators[2] = { 0, 0 };
    IOSB iosb;
    unsigned int n;
    (void) prm;
    if (child_done) return;
    if (!(tt_iosb.iosb$w_status & 1)) {
        if (tt_iosb.iosb$w_status == SS$_ABORT || tt_iosb.iosb$w_status == SS$_CANCEL) return;
        tt_post_read();
        return;
    }
    n = tt_iosb.iosb$w_bcnt;
    /* Take whatever else is already typed ahead in one go (timed read, 0 s). */
    if (sys$qiow(EFN$C_ENF, tt_chan, IO$_READVBLK | IO$M_NOECHO | IO$M_NOFILTR | IO$M_TIMED,
                 &iosb, 0, 0, tt_buf + n, sizeof tt_buf - n, 0,
                 (unsigned __int64) no_terminators, 0, 0) & 1)
        n += iosb.iosb$w_bcnt;
    hs_relay_user_input(&relay, tt_buf, n);
    tt_post_read();
}

static void done_ast(int prm) { (void) prm; child_done = 1; sys$wake(0, 0); }

/* ---- start-up and clean-up ---- */

static void restore(void)
{
    IOSB iosb;
    if (tt_changed)
        sys$qiow(EFN$C_ENF, tt_chan, IO$_SETMODE, &iosb, 0, 0, &tt_saved, sizeof tt_saved,
                 0, 0, 0, 0);
    tt_changed = 0;
    if (ctrl_changed) lib$enable_ctrl(&ctrl_saved, 0);
    ctrl_changed = 0;
}

static unsigned int exit_status;
static struct { unsigned int link; void (*handler)(void); unsigned int argc;
                unsigned int *status; } exit_block;

static void exit_handler(void) { restore(); if (pty_chan) ptd$delete(pty_chan); }

int main(int argc, char **argv)
{
    struct _va_range got;
    termchars pc, raw;
    struct dsc$descriptor_s d_tt, d_dev, d_prompt;
    char devnam[64], prompt[320], user_prompt[256], pidhex[16];
    unsigned short devlen = 0;
    struct { unsigned short len, code; void *buf; unsigned short *ret; unsigned int end; } dvi;
    IOSB iosb;
    unsigned int st, flags = 1;                              /* CLI$M_NOWAIT */
    unsigned int ctrl_off = LIB$M_CLI_CTRLY | LIB$M_CLI_CTRLT;
    static const hs_sink sink = { sink_screen, sink_child, sink_event, sink_timer, 0 };

    if (argc == 2 && strcmp(argv[1], "-v") == 0) {      /* MAKE_KIT checks this */
        printf("histsh %s\n", HS_VERSION);
        return SS$_NORMAL;
    }

    /* The user's terminal. */
    dsc(&d_tt, "SYS$COMMAND", 11);
    st = sys$assign(&d_tt, &tt_chan, 0, 0, 0);
    if (!(st & 1)) return st;
    st = sys$qiow(EFN$C_ENF, tt_chan, IO$_SENSEMODE, &iosb, 0, 0, &tt_saved, sizeof tt_saved,
                  0, 0, 0, 0);
    if (!(st & 1) || !(iosb.iosb$w_status & 1) || tt_saved.cls != DC$_TERM) return SS$_IVDEVNAM;

    /* Files and logicals the child inherits (docs/C1.md, start step 2). */
    if (!trn_lnm("HISTSH$HISTORY", hist_file, sizeof hist_file))
        strcpy(hist_file, "SYS$LOGIN:HISTSH_HISTORY.LIS");
    sprintf(pidhex, "%08X", (unsigned int) getpid());
    sprintf(new_file, "%s_%s", hist_file, pidhex);
    sprintf(load_file, "%s_L%s", hist_file, pidhex);
    set_lnm("HISTSH$CHILD", pidhex);
    set_lnm("HISTSH$MARKER", HS_MARKER);
    set_lnm("HISTSH$FILE", hist_file);
    set_lnm("HISTSH$NEW", new_file);
    set_lnm("HISTSH$LOAD", load_file);
    hs_relay_init(&relay, &sink, BOOT_CMD, "RECALL/ERASE/INPUT=HISTSH$LOAD",
                  "RECALL/OUTPUT=HISTSH$NEW");
    {
        char size[16];
        if (trn_lnm("HISTSH$SIZE", size, sizeof size) && atoi(size) > 0)
            hist_max = (unsigned int) atoi(size);
    }
    hs_hist_init(&hist, hist_max);
    hs_lines_init(&snap);
    load_history();
    hs_relay_set_history(&relay, &hist);
    hs_relay_set_remark(&relay, REMARK_CMD);

    /* A pseudo-terminal like the real one. */
    st = sys$expreg(2, &got, 0, 0);
    if (!(st & 1)) return st;
    region = (char *) got.va_range$ps_start_va;
    pc = tt_saved;
    pc.basic &= ~TT$M_NOECHO;
    pc.ext &= ~TT2$M_PASTHRU;
    pc.ext |= TT2$M_EDITING | TT2$M_ALTYPEAHD;    /* the larger typeahead buffer (N17) */
    st = ptd$create(&pty_chan, 0, &pc, sizeof pc, 0, 0, 0, &got);
    if (!(st & 1)) return st;
    dvi.len = sizeof devnam - 1; dvi.code = DVI$_DEVNAM; dvi.buf = devnam; dvi.ret = &devlen;
    dvi.end = 0;
    sys$getdviw(EFN$C_ENF, pty_chan, 0, &dvi, 0, 0, 0, 0);
    devnam[devlen] = '\0';

    exit_block.handler = exit_handler;
    exit_block.argc = 1;
    exit_block.status = &exit_status;
    sys$dclexh(&exit_block);

    /* The real terminal passes everything through: Ctrl/Y and Ctrl/T go to the child. */
    st = lib$disable_ctrl(&ctrl_off, &ctrl_saved);
    if (st & 1) ctrl_changed = 1;
    raw = tt_saved;
    raw.basic |= TT$M_NOECHO;
    raw.ext |= TT2$M_PASTHRU;
    st = sys$qiow(EFN$C_ENF, tt_chan, IO$_SETMODE, &iosb, 0, 0, &raw, sizeof raw, 0, 0, 0, 0);
    if ((st & 1) && (iosb.iosb$w_status & 1)) tt_changed = 1;

    pty_post_read();
    dsc(&d_dev, devnam, devlen);
    get_prompt(user_prompt, sizeof user_prompt);
    sprintf(prompt, "%s%s%s", HS_MARKER, user_prompt, HS_MARKER);   /* both ends: N12 */
    dsc(&d_prompt, prompt, (unsigned short) strlen(prompt));
    st = lib$spawn(0, &d_dev, &d_dev, &flags, 0, &child_pid, &child_status, 0,
                   done_ast, 0, &d_prompt, 0, 0);
    if (!(st & 1)) { restore(); return st; }

    tt_post_read();
    while (!child_done) sys$hiber();

    sys$cancel(tt_chan);
    restore();
    finish_save();          /* a save the child ran just before LOGOUT, if any */
    return SS$_NORMAL;
}
