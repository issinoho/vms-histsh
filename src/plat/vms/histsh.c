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
#include "hs_version.h"

#define RBUF_OFF   0            /* PTD$ read buffer: word status, word count, data */
#define WBUF_OFF   1024         /* PTD$ write slots, WSLOT bytes each */
#define WSLOT      512
#define NWSLOTS    8
#define DATA_MAX   (WSLOT - 4)
#define BOOT_CMD   "@HISTSH:HISTSH_CHILD"   /* typed at the first prompt; never kept */

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

static void sink_child(void *ctx, const char *p, unsigned int n)
{
    (void) ctx;
    while (n) {
        unsigned int k = n > DATA_MAX ? DATA_MAX : n;
        char *slot = region + WBUF_OFF + WSLOT * wslot;
        wslot = (wslot + 1) % NWSLOTS;
        memcpy(slot + 4, p, k);
        ptd$write(pty_chan, 0, 0, slot, k, 0, 0);
        p += k; n -= k;
    }
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

/* Make `from` the history file: a new version of it, older versions removed. */
static void replace_history(const char *from)
{
    struct dsc$descriptor_s d;
    char old[270];
    int i;
    if (rename(from, hist_file) != 0) return;
    sprintf(old, "%s;-1", hist_file);
    dsc(&d, old, (unsigned short) strlen(old));
    for (i = 0; i < 10; i++)                 /* drop the older versions, bounded */
        if (!(lib$delete_file(&d, 0, 0, 0, 0, 0, 0, 0, 0, 0) & 1)) break;
}

/* After a hidden RECALL/OUTPUT: keep the new file only if it was written. */
static void finish_save(void)
{
    struct stat st;
    if (stat(new_file, &st) != 0) return;
    if (st.st_size == 0) { remove(new_file); return; }
    replace_history(new_file);
}

/* After the start-up snapshot: the load file is the saved history followed by what
   DCL's buffer held (any commands typed while LOGIN.COM ran), without HISTSH's
   own boot command, cut to the newest DCL_RECALL_MAX lines (NOTES N12). */
#define DCL_RECALL_MAX 254
#define LINE_MAX_LEN 4100

static char *lines[2 * DCL_RECALL_MAX + 2];
static int nlines;

static void keep_line(const char *text)
{
    size_t n = strlen(text);
    char *copy;
    if (nlines == (int) (sizeof lines / sizeof lines[0])) {   /* keep the newest */
        free(lines[0]);
        memmove(lines, lines + 1, sizeof lines - sizeof lines[0]);
        nlines--;
    }
    copy = malloc(n + 1);
    if (!copy) return;
    memcpy(copy, text, n + 1);
    lines[nlines++] = copy;
}

static void read_lines(const char *file, int skip_boot)
{
    static char buf[LINE_MAX_LEN];
    FILE *f = fopen(file, "r");
    if (!f) return;
    while (fgets(buf, sizeof buf, f)) {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        if (n == 0) continue;
        if (skip_boot && strcmp(buf, BOOT_CMD) == 0) continue;
        keep_line(buf);
    }
    fclose(f);
}

static void build_load_file(void)
{
    FILE *f;
    int i, first;
    nlines = 0;
    read_lines(hist_file, 1);
    read_lines(new_file, 1);
    remove(new_file);
    first = nlines > DCL_RECALL_MAX ? nlines - DCL_RECALL_MAX : 0;
    f = fopen(load_file, "w");
    if (f) {
        for (i = first; i < nlines; i++) fprintf(f, "%s\n", lines[i]);
        fclose(f);
    }
    for (i = 0; i < nlines; i++) free(lines[i]);
    nlines = 0;
}

static void sink_event(void *ctx, int ev)
{
    static int warned;
    (void) ctx;
    if (ev == HS_EV_SNAPPED) build_load_file();
    else if (ev == HS_EV_LOADED) {
        /* The buffer now holds exactly the load file: that is the new history,
           so commands typed during LOGIN.COM are kept even if the user logs out
           straight away. An empty one is just removed. */
        struct stat st;
        if (stat(load_file, &st) == 0 && st.st_size > 0) replace_history(load_file);
        else remove(load_file);
    }
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

    /* A pseudo-terminal like the real one. */
    st = sys$expreg(2, &got, 0, 0);
    if (!(st & 1)) return st;
    region = (char *) got.va_range$ps_start_va;
    pc = tt_saved;
    pc.basic &= ~TT$M_NOECHO;
    pc.ext &= ~TT2$M_PASTHRU;
    pc.ext |= TT2$M_EDITING;
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
