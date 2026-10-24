/* probe_ptd.c - Phase 0 probes 1, 3 and 4 in one run.

   Creates a pseudo-terminal (PTD$CREATE) with line editing on, spawns DCL on it
   (LIB$SPAWN /NOWAIT, input and output on the FTA device), types a script at it and
   prints what DCL sends back, with read-event ASTs marked inline as {name}.

   Answers: is the child's DCL interactive enough to keep a recall buffer and honour
   up-arrow; does RECALL /OUTPUT and /INPUT work there (probe 1); which PTD$ read
   events fire, and when (probe 4).

   Bounded on purpose: each step waits a fixed time, the step list is fixed, and the
   child is ended at the end whatever state it is in. */
#define __NEW_STARLET 1
#include <stdio.h>
#include <string.h>
#include <descrip.h>
#include <starlet.h>
#include <lib$routines.h>
#include <ssdef.h>
#include <dvidef.h>
#include <dcdef.h>
#include <ttdef.h>
#include <tt2def.h>
#include <ptddef.h>
#include <gen64def.h>
#include <va_rangedef.h>
#include "probe_common.h"

#define RBUF_OFF   0       /* read buffer: word status, word count, data */
#define WBUF_OFF   1024    /* write buffer: same layout */
#define DATA_MAX   508

/* -DEVENTS: enable every event type and run the short event script instead. */
#ifdef EVENTS
#define EV_FIRST 0
#else
#define EV_FIRST PTD$C_START_READ
#endif

static unsigned short chan;
static char *region;
static char transcript[65536];
static unsigned int tlen;
static volatile int child_done;
static unsigned int child_status;

static void tput(const char *s, unsigned int n)
{
    if (tlen + n >= sizeof transcript) n = sizeof transcript - 1 - tlen;
    memcpy(transcript + tlen, s, n);
    tlen += n;
}

static void read_ast(int prm);

static void post_read(void)
{
    unsigned int st = ptd$read(0, chan, read_ast, 0, region + RBUF_OFF, DATA_MAX);
    if (!(st & 1)) { tput("{READ-POST-FAIL}", 16); }
}

static void read_ast(int prm)
{
    unsigned short *h = (unsigned short *) (region + RBUF_OFF);
    (void) prm;
    if (!(h[0] & 1)) {
        char m[48];
        sprintf(m, "{READ-STATUS %%X%04X}", h[0]);
        tput(m, (unsigned int) strlen(m));
        if (h[0] == SS$_ABORT || h[0] == SS$_CANCEL || h[0] == SS$_HANGUP) return;
    } else {
        tput(region + RBUF_OFF + 4, h[1]);
    }
    post_read();
}

static void event_ast(int type)
{
    static const char *names[] = { "XON", "BELL", "XOFF", "STOP", "RESUME", "CHAR",
        "ABORT", "SR", "MR", "ER", "ENR", "DISR" };
    char m[16];
    sprintf(m, "{%s}", (type >= 0 && type < 12) ? names[type] : "?");
    tput(m, (unsigned int) strlen(m));
    /* Notification is one-shot: enable it again. */
    ptd$set_event_notification(chan, event_ast, type, 0, (unsigned int) type);
}

static void done_ast(int prm) { (void) prm; child_done = 1; }

/* Print the transcript with control characters made visible, then empty it. */
static void flush_transcript(void)
{
    unsigned int i;
    sys$setast(0);
    for (i = 0; i < tlen; i++) {
        unsigned char c = (unsigned char) transcript[i];
        if (c == '\n') fputs("<LF>\n", stdout);
        else if (c == '\r') fputs("<CR>", stdout);
        else if (c == 27) fputs("<ESC>", stdout);
        else if (c < 32 || c == 127) printf("<%02X>", c);
        else putchar(c);
    }
    tlen = 0;
    sys$setast(1);
    putchar('\n');
}

static void type_text(const char *label, const char *s)
{
    unsigned short *h = (unsigned short *) (region + WBUF_OFF);
    unsigned int n = (unsigned int) strlen(s), st;
    float secs = 1.5f;
    int ftype = 4;                                  /* LIB$K_IEEE_S */
    printf("---- %s\n", label);
    memcpy(region + WBUF_OFF + 4, s, n);
    st = ptd$write(chan, 0, 0, region + WBUF_OFF, n, 0, 0);
    if (!(st & 1) || !(h[0] & 1)) printf("  write: %s / buffer %%X%04X\n", msg(st), h[0]);
    lib$wait(&secs, 0, &ftype);
    flush_transcript();
}

int main(void)
{
    struct _va_range want, got;
    struct { unsigned char cls, type; unsigned short width;
             unsigned int basic, ext; } tc;
    char devnam[64];
    unsigned short devlen = 0;
    ile3 dvi[2];
    struct dsc$descriptor_s ddev, dprompt;
    unsigned int st, pid = 0, flags = 1, i;          /* CLI$M_NOWAIT */
    float secs = 1.5f;
    int ftype = 4;
    static const char *steps[][2] = {
#ifdef EVENTS
        { "make a BEL symbol", "B = \" \"\rB[0,8] = 7\r" },
        { "bell", "WRITE SYS$OUTPUT B\r" },
        { "width change", "SET TERMINAL/WIDTH=100\r" },
        { "space then Ctrl/U at prompt", " \025" },
        { "logout", "LOGOUT\r" },
#else
        { "mode and terminal", "WRITE SYS$OUTPUT F$MODE(), \" \", F$GETDVI(\"SYS$COMMAND\",\"TT_EDITING\")\r" },
        { "first command",  "SHOW TIME\r" },
        { "second command", "WRITE SYS$OUTPUT \"two\"\r" },
        { "up-arrow twice then Return (expect SHOW TIME again)", "\033[A\033[A\r" },
        { "Ctrl/B once then Return (expect the line before)", "\002\r" },
        { "RECALL/ALL", "RECALL/ALL\r" },
        { "save buffer", "RECALL/OUTPUT=PTD_HIST.LIS\r" },
        { "show saved file", "TYPE PTD_HIST.LIS\r" },
        { "erase and reload", "RECALL/ERASE/INPUT=PTD_HIST.LIS\r" },
        { "RECALL/ALL after reload", "RECALL/ALL\r" },
        { "up-arrow after reload", "\033[A\r" },
        { "missing parameter prompt (COPY)", "COPY\r" },
        { "answer _From: with Ctrl/Y", "\031" },
        { "clean up", "DELETE PTD_HIST.LIS;*\r" },
        { "logout", "LOGOUT\r" },
#endif
    };

    /* Pages for the I/O buffers, as PTD$CREATE requires. */
    want.va_range$ps_start_va = (void *) 0x200;
    want.va_range$ps_end_va = (void *) 0x200;
    st = sys$expreg(2, (struct _va_range *) &got, 0, 0);
    report("SYS$EXPREG", st);
    if (!(st & 1)) return st;
    (void) want;
    region = (char *) got.va_range$ps_start_va;

    memset(&tc, 0, sizeof tc);
    tc.cls = DC$_TERM;
    tc.type = TT$_VT100;
    tc.width = 80;
    tc.basic = TT$M_SCOPE | TT$M_LOWER | TT$M_TTSYNC | TT$M_WRAP | TT$M_MECHTAB
             | (24u << 24);
    tc.ext = TT2$M_ANSICRT | TT2$M_DECCRT | TT2$M_AVO | TT2$M_EDIT | TT2$M_EDITING;
    st = ptd$create(&chan, 0, &tc, sizeof tc, 0, 0, 0, &got);
    report("PTD$CREATE", st);
    if (!(st & 1)) return st;

    ile3_set(&dvi[0], DVI$_DEVNAM, devnam, sizeof devnam - 1, &devlen);
    ile3_end(&dvi[1]);
    st = sys$getdviw(0, chan, 0, dvi, 0, 0, 0, 0);
    devnam[devlen] = '\0';
    printf("  device %s\n", devnam);

    for (i = EV_FIRST; i <= PTD$C_DISABLE_READ; i++) {
        if (i == PTD$C_SEND_XON || i == PTD$C_SEND_XOFF) continue;
        char what[40];
        st = ptd$set_event_notification(chan, event_ast, (int) i, 0, i);
        sprintf(what, "event notification %u", i);
        report(what, st);
    }
    post_read();

    desc_set(&ddev, devnam, devlen);
    desc_set(&dprompt, "$ ", 2);
    st = lib$spawn(0, &ddev, &ddev, &flags, 0, &pid, &child_status, 0,
                   done_ast, 0, &dprompt, 0, 0);
    report("LIB$SPAWN", st);
    if (!(st & 1)) { ptd$delete(chan); return st; }
    printf("  child pid %08X\n", pid);
    lib$wait(&secs, 0, &ftype);
    printf("---- startup\n");
    flush_transcript();

    for (i = 0; i < sizeof steps / sizeof steps[0] && !child_done; i++)
        type_text(steps[i][0], steps[i][1]);

    printf("---- end: child %s, status %%X%08X\n", child_done ? "ended" : "STILL RUNNING",
           child_status);
    if (!child_done) {
        st = sys$delprc(&pid, 0, 0);
        report("SYS$DELPRC (own subprocess)", st);
    }
    st = ptd$delete(chan);
    report("PTD$DELETE", st);
    return SS$_NORMAL;
}
