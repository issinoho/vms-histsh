/* probe_paste.c - a paste into a program on the child terminal (the CREATE bug).

   A 1.3 KB paste written with PTD$WRITE into CREATE on the child terminal made
   CREATE fail with DATAOVERUN on IA64, and the rest of the paste went to DCL. This
   probe asks:
     1. How many bytes does the child's typeahead buffer hold, and does PTD$ send
        an XOFF event (types 0/2) before it fills? (child busy in WAIT, 1 byte/write)
     2. bulk:   HISTSH's way (508-byte writes, resend what wasn't taken after 20 ms).
                Does CREATE get the whole paste?
     3. events: 32-byte writes, stopping on XOFF until XON.
     4. count:  before each write, IO$M_TYPEAHDCNT on the child's terminal; write
                only what fits under the capacity measured in 1.
   Run as "PROBE_PASTE H" to give the child terminal HOSTSYNC, "R" for READSYNC,
   "HR" for both, or "N" for neither.

   Each paste line starts with "!" so that anything DCL gets by mistake is a
   comment. Bounded: fixed waits, at most 15 s per paste, the child is ended at
   the end whatever its state. */
#define __NEW_STARLET 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <descrip.h>
#include <starlet.h>
#include <lib$routines.h>
#include <ssdef.h>
#include <iodef.h>
#include <dvidef.h>
#include <dcdef.h>
#include <ttdef.h>
#include <tt2def.h>
#include <ptddef.h>
#include <iosbdef.h>
#include <va_rangedef.h>
#include "probe_common.h"

#define RBUF_OFF   0
#define WBUF_OFF   1024
#define DATA_MAX   508

static unsigned short chan, ftchan;
static char *region;
static char transcript[65536];
static unsigned int tlen;
static volatile int child_done, xoff;
static volatile unsigned int sent, n_xoff, n_xon;
static unsigned int child_status;

static void tput(const char *s, unsigned int n)
{
    if (tlen + n >= sizeof transcript) n = sizeof transcript - 1 - tlen;
    memcpy(transcript + tlen, s, n);
    tlen += n;
}

static void read_ast(int prm);
static void post_read(void) { ptd$read(0, chan, read_ast, 0, region + RBUF_OFF, DATA_MAX); }

static void read_ast(int prm)
{
    unsigned short *h = (unsigned short *) (region + RBUF_OFF);
    (void) prm;
    if (!(h[0] & 1)) {
        char m[48];
        sprintf(m, "{READ-STATUS %%X%04X}", h[0]);
        tput(m, (unsigned int) strlen(m));
        if (h[0] == SS$_ABORT || h[0] == SS$_CANCEL || h[0] == SS$_HANGUP) return;
    } else tput(region + RBUF_OFF + 4, h[1]);
    post_read();
}

static void flow_ast(int type)        /* one-shot: enable again */
{
    char m[32];
    if (type == PTD$C_SEND_XOFF) { xoff = 1; n_xoff++; sprintf(m, "{XOFF@%u}", sent); }
    else                         { xoff = 0; n_xon++;  sprintf(m, "{XON@%u}", sent); }
    tput(m, (unsigned int) strlen(m));
    ptd$set_event_notification(chan, flow_ast, type, 0, (unsigned int) type);
}

static void done_ast(int prm) { (void) prm; child_done = 1; }

static void wait_ms(unsigned int ms)
{
    float secs = (float) ms / 1000.0f;
    int ftype = 4;                                  /* LIB$K_IEEE_S */
    lib$wait(&secs, 0, &ftype);
}

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

/* One PTD$WRITE; returns the bytes taken, and the buffer status in *status. */
static unsigned int pwrite(const char *p, unsigned int n, unsigned short *status)
{
    unsigned short *h = (unsigned short *) (region + WBUF_OFF);
    unsigned int took;
    memcpy(region + WBUF_OFF + 4, p, n);
    h[0] = 0; h[1] = 0;
    ptd$write(chan, 0, 0, region + WBUF_OFF, n, 0, 0);
    *status = h[0];
    took = (h[0] & 1) || h[0] == SS$_DATAOVERUN ? h[1] : 0;
    if (took > n) took = n;
    sent += took;
    return took;
}

static void type_cmd(const char *s, unsigned int settle_ms)
{
    unsigned short st;
    pwrite(s, (unsigned int) strlen(s), &st);
    wait_ms(settle_ms);
}

static unsigned int typeahead(void)
{
    IOSB iosb;
    unsigned short buf[4];
    memset(buf, 0, sizeof buf);
    if (!(sys$qiow(0, ftchan, IO$_SENSEMODE | IO$M_TYPEAHDCNT, &iosb, 0, 0, buf, 8,
                   0, 0, 0, 0) & 1) || !(iosb.iosb$w_status & 1)) return 99999;
    return buf[0];
}

/* The paste: !BEGIN, 20 lines of "!" + 63 base64 characters, !END. */
static char paste[2048];
static unsigned int paste_n;

static void make_paste(void)
{
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int seed = 12345, i, j;
    FILE *f;
    strcpy(paste, "!BEGIN\r");
    paste_n = 7;
    for (i = 0; i < 20; i++) {
        paste[paste_n++] = '!';
        for (j = 0; j < 63; j++) {
            seed = seed * 1103515245u + 12345u;
            paste[paste_n++] = b64[(seed >> 16) & 63];
        }
        paste[paste_n++] = '\r';
    }
    memcpy(paste + paste_n, "!END\r", 5);
    paste_n += 5;
    f = fopen("PASTE_REF.TXT", "w");
    for (i = 0; i < paste_n; i++) fputc(paste[i] == '\r' ? '\n' : paste[i], f);
    fclose(f);
}

enum { BULK, EVENTS, COUNT, STREAM };
static unsigned int stream_piece = 256;   /* STREAM: bytes that arrive per 10 ms */

/* with_cmd: the CREATE command is part of the paste, so the paste arrives while
   CREATE is still starting and has no read posted. */
static void do_paste(int mode, const char *name, unsigned int cap, int with_cmd)
{
    static char stream[2200];
    char cmd[128];
    unsigned int off = 0, ticks = 0, overruns = 0, writes = 0, maxta = 0, total;
    unsigned short st;
    sprintf(cmd, "CREATE PASTE_%s.TXT\r", name);
    printf("---- paste %s: %u bytes into CREATE%s\n", name, paste_n,
           with_cmd ? ", CREATE command in the same paste" : "");
    total = 0;
    if (with_cmd) { strcpy(stream, cmd); total = (unsigned int) strlen(cmd); }
    else { type_cmd(cmd, 1500); flush_transcript(); }
    memcpy(stream + total, paste, paste_n);
    total += paste_n;
    sent = 0; xoff = 0;
    if (mode == STREAM) {
        /* As HISTSH: bytes arrive from the real terminal into a queue; the queue
           is written (at most 508 a time) unless a 20 ms retry is pending. */
        unsigned int arrived = 0, retry_at = 0, now = 0;
        while (off < total && now < 1500 && !child_done) {
            if (arrived < total) {
                arrived += stream_piece;
                if (arrived > total) arrived = total;
            }
            if (now >= retry_at) {
                while (off < arrived) {
                    unsigned int k = arrived - off, took;
                    if (k > DATA_MAX) k = DATA_MAX;
                    took = pwrite(stream + off, k, &st);
                    writes++;
                    if (st == SS$_DATAOVERUN) {
                        char m[48];
                        overruns++;
                        sprintf(m, "{OVERRUN@%u took %u/%u}", off, took, k);
                        tput(m, (unsigned int) strlen(m));
                    }
                    off += took;
                    if (took < k) { retry_at = now + 2; break; }
                }
            }
            wait_ms(10); now++;
        }
        ticks = now;
    }
    while (mode != STREAM && off < total && ticks < 1500 && !child_done) {
        unsigned int k = total - off, took;
        if (mode == BULK && k > DATA_MAX) k = DATA_MAX;
        if (mode == EVENTS) {
            if (xoff) { wait_ms(10); ticks++; continue; }
            if (k > 32) k = 32;
        }
        if (mode == COUNT) {
            unsigned int ta = typeahead(), room;
            if (ta != 99999 && ta > maxta) maxta = ta;
            room = ta == 99999 || ta + 16 >= cap ? 0 : cap - ta - 16;
            if (room == 0) { wait_ms(10); ticks++; continue; }
            if (k > room) k = room;
            if (k > 64) k = 64;
        }
        took = pwrite(stream + off, k, &st);
        writes++;
        if (st == SS$_DATAOVERUN) {
            char m[48];
            overruns++;
            sprintf(m, "{OVERRUN@%u took %u/%u}", off, took, k);
            tput(m, (unsigned int) strlen(m));
        } else if (!(st & 1)) {
            char m[48];
            sprintf(m, "{WRITE %%X%04X@%u}", st, off);
            tput(m, (unsigned int) strlen(m));
        }
        off += took;
        if (took < k) { wait_ms(20); ticks += 2; }
    }
    type_cmd("\032", 2000);                 /* Ctrl/Z ends CREATE */
    printf("  %s: %u of %u bytes sent, %u writes, %u overruns, %u XOFF, %u XON, %u ticks",
           name, off, total, writes, overruns, n_xoff, n_xon, ticks);
    if (mode == COUNT) printf(", most typeahead seen %u", maxta);
    putchar('\n');
    sprintf(cmd, "DIFFERENCES/OUTPUT=NL: PASTE_REF.TXT PASTE_%s.TXT\r", name);
    type_cmd(cmd, 1500);
    type_cmd("WRITE SYS$OUTPUT \"DIFF \", $STATUS, \" (%X006C8009 = same)\"\r", 1000);
    flush_transcript();
}

int main(int argc, char **argv)
{
    struct _va_range got;
    struct { unsigned char cls, type; unsigned short width;
             unsigned int basic, ext; } tc;
    char devnam[64];
    unsigned short devlen = 0, st16;
    ile3 dvi[2];
    struct dsc$descriptor_s ddev, dprompt;
    unsigned int st, pid = 0, flags = 1, cap = 0, i, ta_full = 0;   /* CLI$M_NOWAIT */
    const char *opt = argc > 1 ? argv[1] : "N";
    int hostsync = strchr(opt, 'H') || strchr(opt, 'h');
    int readsync = strchr(opt, 'R') || strchr(opt, 'r');

    printf("==== probe_paste, child terminal %s\n", opt);
    st = sys$expreg(2, &got, 0, 0);
    if (!(st & 1)) { report("SYS$EXPREG", st); return st; }
    region = (char *) got.va_range$ps_start_va;

    memset(&tc, 0, sizeof tc);
    tc.cls = DC$_TERM;
    tc.type = TT$_VT100;
    tc.width = 80;
    tc.basic = TT$M_SCOPE | TT$M_LOWER | TT$M_TTSYNC | TT$M_WRAP | TT$M_MECHTAB
             | (24u << 24);
    if (hostsync) tc.basic |= TT$M_HOSTSYNC;
    if (readsync) tc.basic |= TT$M_READSYNC;
    if (strchr(opt, 'U') || strchr(opt, 'u')) {
        /* SHOW TERMINAL of a HISTSH child on IA64 (2026-11-01). */
        tc.basic = TT$M_HOSTSYNC | TT$M_TTSYNC | TT$M_LOWER | TT$M_MECHTAB | TT$M_WRAP
                 | TT$M_SCOPE | TT$M_EIGHTBIT | TT$M_MBXDSABL | (24u << 24);
        tc.ext = TT2$M_ALTYPEAHD | TT2$M_SETSPEED | TT2$M_EDITING | TT2$M_SECURE
               | TT2$M_DRCS | TT2$M_PRINTER | TT2$M_ANSICRT | TT2$M_AVO | TT2$M_EDIT
               | TT2$M_DECCRT | TT2$M_DECCRT2 | TT2$M_DECCRT3
#ifdef TT2$M_DECCRT4
               | TT2$M_DECCRT4
#endif
#ifdef TT2$M_DECCRT5
               | TT2$M_DECCRT5
#endif
               ;
#ifdef TT$_VT500_SERIES
        tc.type = TT$_VT500_SERIES;
#endif
        printf("  user's characteristics: type %u basic %%X%08X ext %%X%08X\n",
               tc.type, tc.basic, tc.ext);
    }
    tc.ext = TT2$M_ANSICRT | TT2$M_DECCRT | TT2$M_AVO | TT2$M_EDIT | TT2$M_EDITING
           | TT2$M_ALTYPEAHD;
    st = ptd$create(&chan, 0, &tc, sizeof tc, 0, 0, 0, &got);
    report("PTD$CREATE", st);
    if (!(st & 1)) return st;

    ile3_set(&dvi[0], DVI$_DEVNAM, devnam, sizeof devnam - 1, &devlen);
    ile3_end(&dvi[1]);
    sys$getdviw(0, chan, 0, dvi, 0, 0, 0, 0);
    devnam[devlen] = '\0';
    printf("  device %s\n", devnam);
    desc_set(&ddev, devnam, devlen);
    st = sys$assign(&ddev, &ftchan, 0, 0, 0);
    report("SYS$ASSIGN child terminal", st);

    report("event XON", ptd$set_event_notification(chan, flow_ast, PTD$C_SEND_XON, 0,
                                                    PTD$C_SEND_XON));
    report("event XOFF", ptd$set_event_notification(chan, flow_ast, PTD$C_SEND_XOFF, 0,
                                                     PTD$C_SEND_XOFF));
    post_read();
    make_paste();

    desc_set(&dprompt, "$ ", 2);
    st = lib$spawn(0, &ddev, &ddev, &flags, 0, &pid, &child_status, 0,
                   done_ast, 0, &dprompt, 0, 0);
    report("LIB$SPAWN", st);
    if (!(st & 1)) { ptd$delete(chan); return st; }
    wait_ms(2000);
    flush_transcript();

    /* 1. Capacity, with DCL busy and no read pending. */
    printf("---- capacity: 1 byte per write while the child WAITs\n");
    type_cmd("WAIT 0:0:4\r", 500);
    sent = 0; n_xoff = 0; n_xon = 0;
    for (i = 0; i < 3000; i++) {
        const char *c = i == 0 ? "!" : "x";
        unsigned int took = pwrite(c, 1, &st16);
        if (took == 0 || !(st16 & 1)) {
            printf("  write %u: took %u, status %%X%04X %s\n", i, took, st16, msg(st16));
            break;
        }
    }
    cap = sent;
    ta_full = typeahead();
    printf("  capacity %u bytes, typeahead count %u, XOFF %u, XON %u\n",
           cap, ta_full, n_xoff, n_xon);
    wait_ms(4500);                       /* WAIT ends; DCL reads the !xxx line */
    type_cmd("\r", 1000);
    printf("  after the WAIT: XOFF %u, XON %u\n", n_xoff, n_xon);
    flush_transcript();
    if (cap < 64) cap = 64;

    /* 1b. Overflow while DCL runs a procedure, then a READ from the terminal. */
    {
        FILE *f = fopen("T_READ.COM", "w");
        fputs("$ wait 0:0:3\n"
              "$ read/end=eof/error=bad sys$command x\n"
              "$ write sys$output \"READ OK, length \", f$length(x)\n"
              "$ exit\n"
              "$bad: write sys$output \"READ ERROR \", $status\n"
              "$ exit\n"
              "$eof: write sys$output \"READ EOF\"\n", f);
        fclose(f);
    }
    printf("---- overflow during a procedure's WAIT, then READ SYS$COMMAND\n");
    type_cmd("@T_READ\r", 500);
    for (i = 0; i < 3000; i++) {
        unsigned int took = pwrite(i == 0 ? "!" : "y", 1, &st16);
        if (took == 0 || !(st16 & 1)) break;
    }
    printf("  wrote %u bytes, last status %%X%04X\n", i + 1, st16);
    wait_ms(3500);
    type_cmd("\r", 1500);
    flush_transcript();

    n_xoff = 0; n_xon = 0; do_paste(BULK, "BULK", cap, 0);
    n_xoff = 0; n_xon = 0; do_paste(EVENTS, "EVENTS", cap, 0);
    n_xoff = 0; n_xon = 0; do_paste(COUNT, "COUNT", cap, 0);
    stream_piece = 256; n_xoff = 0; n_xon = 0; do_paste(STREAM, "STREAM256", cap, 0);
    stream_piece = 64;  n_xoff = 0; n_xon = 0; do_paste(STREAM, "STREAM64", cap, 0);
    stream_piece = 16;  n_xoff = 0; n_xon = 0; do_paste(STREAM, "STREAM16", cap, 0);
    stream_piece = 256; n_xoff = 0; n_xon = 0; do_paste(STREAM, "CMDSTREAM", cap, 1);
    n_xoff = 0; n_xon = 0; do_paste(BULK, "CMDBULK", cap, 1);
    n_xoff = 0; n_xon = 0; do_paste(EVENTS, "CMDEVENTS", cap, 1);
    n_xoff = 0; n_xon = 0; do_paste(COUNT, "CMDCOUNT", cap, 1);

    type_cmd("DELETE PASTE_*.TXT;*,T_READ.COM;*\r", 1000);
    type_cmd("LOGOUT\r", 2000);
    flush_transcript();
    printf("---- end: child %s, status %%X%08X\n", child_done ? "ended" : "STILL RUNNING",
           child_status);
    if (!child_done) report("SYS$DELPRC (own subprocess)", sys$delprc(&pid, 0, 0));
    sys$dassgn(ftchan);
    report("PTD$DELETE", ptd$delete(chan));
    return SS$_NORMAL;
}
