/* test_relay.c - unit tests for src/core/hs_relay.c (Linux, no VMS needed). */
#include <stdio.h>
#include <string.h>
#include "hs_relay.h"

static char screen[4096], child[4096];
static unsigned int nscreen, nchild;
static int events[16], nevents, timer_on;
static int failures, checks;

static void s_screen(void *c, const char *p, unsigned int n)
{ (void) c; memcpy(screen + nscreen, p, n); nscreen += n; screen[nscreen] = '\0'; }
static void s_child(void *c, const char *p, unsigned int n)
{ (void) c; memcpy(child + nchild, p, n); nchild += n; child[nchild] = '\0'; }
static void s_event(void *c, int ev) { (void) c; events[nevents++] = ev; }
static void s_timer(void *c, int on) { (void) c; timer_on = on; }

static const hs_sink sink = { s_screen, s_child, s_event, s_timer, 0 };

#define M HS_MARKER
#define TOP "\r\0" M            /* top-level prompt start (NOTES N9) */
#define TOPLEN (2 + sizeof M - 1)

static void reset(void) { nscreen = nchild = 0; screen[0] = child[0] = '\0'; nevents = 0; }

static void expect_str(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("FAIL %s\n  got:  \"%s\"\n  want: \"%s\"\n", what, got, want);
    }
}

static void expect_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) { failures++; printf("FAIL %s: got %d, want %d\n", what, got, want); }
}

/* Feed output given as a buffer with embedded NULs. */
static void out(hs_relay *r, const char *p, unsigned int n) { hs_relay_child_output(r, p, n); }
static void outs(hs_relay *r, const char *p) { out(r, p, (unsigned int) strlen(p)); }
static void prompt(hs_relay *r) { out(r, TOP "$ ", TOPLEN + 2); }
static void keys(hs_relay *r, const char *p) { hs_relay_user_input(r, p, (unsigned int) strlen(p)); }

/* Take the relay through boot and the hidden load to the first visible prompt. */
static void boot(hs_relay *r)
{
    hs_relay_init(r, &sink, "@HISTSH:HISTSH_CHILD", "RECALL/ERASE/INPUT=HISTSH$FILE",
                  "RECALL/OUTPUT=HISTSH$NEW");
    outs(r, "\r\n");
    prompt(r);                                  /* first prompt: boot command typed */
    outs(r, "@HISTSH:HISTSH_CHILD\r\n");        /* its echo: swallowed */
    outs(r, "Welcome from LOGIN.COM\r\n");      /* LOGIN.COM output: shown */
    prompt(r);                                  /* now the hidden load */
    outs(r, "RECALL/ERASE/INPUT=HISTSH$FILE\r\n");
    prompt(r);                                  /* load done: prompt shown */
}

static void test_boot(void)
{
    hs_relay r;
    reset();
    boot(&r);
    expect_str("boot child input", child,
               "@HISTSH:HISTSH_CHILD\rRECALL/ERASE/INPUT=HISTSH$FILE\r");
    expect_int("boot screen length", (int) nscreen,
               (int) strlen("\r\n\r") + 1 + (int) strlen("Welcome from LOGIN.COM\r\n\r") + 1 + 2);
    checks++;
    if (memmem(screen, nscreen, "RECALL", 6) || memmem(screen, nscreen, M, sizeof M - 1)) {
        failures++; printf("FAIL boot: hidden command or marker reached the screen\n");
    }
    expect_int("boot event", events[0], HS_EV_LOADED);
    expect_int("boot state", r.state, HS_IDLE);
    expect_int("boot timer off", timer_on, 0);
}

static void test_command_then_save(void)
{
    hs_relay r;
    reset(); boot(&r); reset();
    keys(&r, "SHOW TIME\r");
    expect_str("command reaches child", child, "SHOW TIME\r");
    outs(&r, "SHOW TIME\r\n\r  24-OCT-2026 13:00:00\r\n");
    prompt(&r);                                 /* command done: hidden save typed */
    expect_str("save typed", child, "SHOW TIME\rRECALL/OUTPUT=HISTSH$NEW\r");
    expect_int("save timer", timer_on, 1);
    expect_str("prompt held back during save", screen,
               "SHOW TIME\r\n\r  24-OCT-2026 13:00:00\r\n\r");
    keys(&r, "DIR");                            /* typed during the save: held */
    expect_str("keys held", child, "SHOW TIME\rRECALL/OUTPUT=HISTSH$NEW\r");
    outs(&r, "RECALL/OUTPUT=HISTSH$NEW\r\n");
    prompt(&r);
    expect_int("saved event", events[0], HS_EV_SAVED);
    expect_str("held keys replayed", child, "SHOW TIME\rRECALL/OUTPUT=HISTSH$NEW\rDIR");
    expect_str("one prompt shown", screen + strlen("SHOW TIME\r\n\r  24-OCT-2026 13:00:00\r\n\r") + 1,
               "$ ");
}

static void test_empty_line_no_save(void)
{
    hs_relay r;
    reset(); boot(&r); reset();
    keys(&r, "\r");
    outs(&r, "\r\n");
    prompt(&r);
    expect_str("empty line: no save", child, "\r");
    expect_int("empty line: idle", r.state, HS_IDLE);
}

static void test_typeahead_defers_save(void)
{
    hs_relay r;
    reset(); boot(&r); reset();
    keys(&r, "DIR\r");
    keys(&r, "SHOW");                           /* typed ahead while DIR runs */
    outs(&r, "DIR\r\n\r listing\r\n");
    prompt(&r);
    expect_str("typeahead: no hidden save", child, "DIR\rSHOW");
    expect_int("typeahead: idle", r.state, HS_IDLE);
    keys(&r, " TIME\r");
    outs(&r, " TIME\r\n\r now\r\n");
    prompt(&r);
    expect_str("save at the next prompt", child, "DIR\rSHOW TIME\rRECALL/OUTPUT=HISTSH$NEW\r");
}

static void test_continuation_prompt(void)
{
    hs_relay r;
    static const char cont[] = "\r_" M "$ ";
    reset(); boot(&r); reset();
    keys(&r, "WRITE SYS$OUTPUT -\r");
    outs(&r, "WRITE SYS$OUTPUT -\r\n");
    outs(&r, cont);
    expect_str("continuation: no save", child, "WRITE SYS$OUTPUT -\r");
    expect_str("continuation: marker stripped", screen, "WRITE SYS$OUTPUT -\r\n\r_$ ");
    keys(&r, "\"x\"\r");
    outs(&r, "\"x\"\r\n\rx\r\n");
    prompt(&r);
    expect_str("save after the full command", child,
               "WRITE SYS$OUTPUT -\r\"x\"\rRECALL/OUTPUT=HISTSH$NEW\r");
}

static void test_redraw_at_prompt(void)
{
    hs_relay r;
    reset(); boot(&r); reset();
    keys(&r, "\033[A");                         /* up-arrow: DCL redraws the line */
    out(&r, "\r\033[K\0\0" "\0" M "$ SHOW TIME", 6 + sizeof M - 1 + 11);
    expect_str("redraw: nothing typed by HISTSH", child, "\033[A");
    expect_str("redraw: marker stripped", screen, "\r\033[K");  /* NULs follow */
    expect_int("redraw: still idle", r.state, HS_IDLE);
}

static void test_split_marker_and_false_start(void)
{
    hs_relay r;
    static const char a[] = "\r\0\033[?77", b[] = "77h$ ";
    reset(); boot(&r); reset();
    keys(&r, "X\r");
    outs(&r, "\033[1mbold\033[m\r\n");          /* ESC sequences that aren't the marker */
    out(&r, a, sizeof a - 1);                   /* marker split across two reads */
    expect_str("false starts shown", screen, "\033[1mbold\033[m\r\n\r");
    out(&r, b, sizeof b - 1);
    expect_str("split marker found: save typed", child, "X\rRECALL/OUTPUT=HISTSH$NEW\r");
}

static void test_timeout(void)
{
    hs_relay r;
    reset(); boot(&r); reset();
    keys(&r, "A\r");
    prompt(&r);
    keys(&r, "B");
    hs_relay_timeout(&r);
    expect_int("timeout event", events[0], HS_EV_TIMEOUT);
    expect_int("timeout: idle", r.state, HS_IDLE);
    expect_str("timeout: held keys replayed", child, "A\rRECALL/OUTPUT=HISTSH$NEW\rB");
    outs(&r, "late output");
    expect_str("timeout: output passes again", screen + nscreen - 11, "late output");
}

int main(void)
{
    test_boot();
    test_command_then_save();
    test_empty_line_no_save();
    test_typeahead_defers_save();
    test_continuation_prompt();
    test_redraw_at_prompt();
    test_split_marker_and_false_start();
    test_timeout();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
