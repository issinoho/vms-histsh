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
static void s_timer(void *c, unsigned int ms) { (void) c; timer_on = (int) ms; }

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
static void prompt(hs_relay *r) { out(r, TOP "$ " M, TOPLEN + 2 + sizeof M - 1); }
static void keys(hs_relay *r, const char *p) { hs_relay_user_input(r, p, (unsigned int) strlen(p)); }

/* Take the relay through boot and the hidden load to the first visible prompt. */
static void boot(hs_relay *r)
{
    hs_relay_init(r, &sink, "@HISTSH:HISTSH_CHILD", "RECALL/ERASE/INPUT=HISTSH$LOAD",
                  "RECALL/OUTPUT=HISTSH$NEW");
    outs(r, "\r\n");
    prompt(r);                                  /* first prompt: boot command typed */
    outs(r, "@HISTSH:HISTSH_CHILD\r\n");        /* its echo: swallowed */
    outs(r, "Welcome from LOGIN.COM\r\n");      /* LOGIN.COM output: shown */
    prompt(r);                                  /* now the hidden snapshot */
    outs(r, "RECALL/OUTPUT=HISTSH$NEW\r\n");
    prompt(r);                                  /* then the hidden load */
    outs(r, "RECALL/ERASE/INPUT=HISTSH$LOAD\r\n");
    prompt(r);                                  /* load done: prompt shown */
}

static void test_boot(void)
{
    hs_relay r;
    reset();
    boot(&r);
    expect_str("boot child input", child,
               "@HISTSH:HISTSH_CHILD\rRECALL/OUTPUT=HISTSH$NEW\rRECALL/ERASE/INPUT=HISTSH$LOAD\r");
    expect_int("boot screen length", (int) nscreen,
               (int) strlen("\r\n\r") + 1 + (int) strlen("Welcome from LOGIN.COM\r\n\r") + 1 + 2);
    checks++;
    if (memmem(screen, nscreen, "RECALL", 6) || memmem(screen, nscreen, M, sizeof M - 1)) {
        failures++; printf("FAIL boot: hidden command or marker reached the screen\n");
    }
    expect_int("boot events", nevents, 2);
    expect_int("boot event 1", events[0], HS_EV_SNAPPED);
    expect_int("boot event 2", events[1], HS_EV_LOADED);
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
    expect_int("save timer", timer_on, HS_HIDDEN_MS);
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
    expect_str("typeahead: no hidden save at once", child, "DIR\rSHOW");
    expect_int("typeahead: settling", r.state, HS_SETTLE);
    outs(&r, "SHOW");                           /* DCL echoes the partial line */
    hs_relay_timeout(&r);                       /* quiet, but not at an empty prompt */
    expect_int("typeahead: user is mid-line", r.state, HS_IDLE);
    keys(&r, " TIME\r");
    outs(&r, " TIME\r\n\r now\r\n");
    prompt(&r);
    expect_str("save at the next prompt", child, "DIR\rSHOW TIME\rRECALL/OUTPUT=HISTSH$NEW\r");
}

/* Returns typed inside a program (EDT) are not DCL typeahead: once DCL is quiet at
   the prompt, the save goes ahead, and the prompt already shown is redrawn. */
static void test_keys_to_a_program(void)
{
    hs_relay r;
    reset(); boot(&r); reset();
    keys(&r, "EDIT X.TXT\r");
    outs(&r, "EDIT X.TXT\r\n[EDT screen]");
    keys(&r, "line one\rline two\r\032");    /* typed into EDT */
    outs(&r, "[EDT echoes]\r\n");
    prompt(&r);                                 /* EDT exited: prompt shown */
    expect_int("program: settling", r.state, HS_SETTLE);
    hs_relay_timeout(&r);                       /* nothing more from DCL: quiet prompt */
    expect_str("program: save typed after quiet", child + strlen("EDIT X.TXT\rline one\rline two\r\032"),
               "RECALL/OUTPUT=HISTSH$NEW\r");
    nscreen = 0; screen[0] = '\0';
    outs(&r, "RECALL/OUTPUT=HISTSH$NEW\r\n");
    prompt(&r);
    expect_str("program: prompt redrawn in place", screen, "\r\033[K$ ");
    expect_int("program: idle", r.state, HS_IDLE);
}

static void test_continuation_prompt(void)
{
    hs_relay r;
    static const char cont[] = "\r_" M "$ " M;
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
    out(&r, "\r\033[K\0\0" "\0" M "$ " M "SHOW TIME", 6 + 2 * (sizeof M - 1) + 11);
    expect_str("redraw: nothing typed by HISTSH", child, "\033[A");
    expect_str("redraw: marker stripped", screen, "\r\033[K");  /* NULs follow */
    expect_int("redraw: still idle", r.state, HS_IDLE);
}

/* Commands typed while LOGIN.COM still runs (IA64 rehearsal, 2026-10-24): the
   snapshot waits for a prompt with nothing typed ahead, and runs before the load,
   so those commands end up in the merged history instead of being erased. */
static void test_typing_during_login(void)
{
    hs_relay r;
    reset();
    hs_relay_init(&r, &sink, "@HISTSH:HISTSH_CHILD", "RECALL/ERASE/INPUT=HISTSH$LOAD",
                  "RECALL/OUTPUT=HISTSH$NEW");
    outs(&r, "\r\n");
    prompt(&r);
    outs(&r, "@HISTSH:HISTSH_CHILD\r\n");     /* LOGIN.COM is running now */
    keys(&r, "SHOW TIME\r");                   /* typed early: goes to DCL as typeahead */
    prompt(&r);                                 /* LOGIN.COM done; SHOW TIME is pending */
    expect_str("early: no snapshot while typed ahead", child,
               "@HISTSH:HISTSH_CHILD\rSHOW TIME\r");
    outs(&r, "SHOW TIME\r\n\r  24-OCT-2026\r\n");   /* DCL runs it */
    prompt(&r);
    expect_str("early: still waiting", child, "@HISTSH:HISTSH_CHILD\rSHOW TIME\r");
    hs_relay_timeout(&r);                       /* quiet at a fresh prompt: snapshot now */
    expect_str("early: snapshot after the early command", child,
               "@HISTSH:HISTSH_CHILD\rSHOW TIME\rRECALL/OUTPUT=HISTSH$NEW\r");
    outs(&r, "RECALL/OUTPUT=HISTSH$NEW\r\n");
    prompt(&r);
    expect_int("early: snapped", events[0], HS_EV_SNAPPED);
    outs(&r, "RECALL/ERASE/INPUT=HISTSH$LOAD\r\n");
    prompt(&r);
    expect_int("early: loaded", events[1], HS_EV_LOADED);
    expect_int("early: idle", r.state, HS_IDLE);
}

/* Up-arrows and Return arrive in one burst: DCL's redraws come after the Return,
   while the relay is RUNNING, and must not be taken for the next prompt (x86 run,
   2026-10-24: the hidden save was typed early and showed on the screen). */
static void test_burst_redraw_while_running(void)
{
    hs_relay r;
    static const char redraw[] = "\r\033[K\0\0\0" M "$ " M "SHOW TIME";
    static const char ctrlu[] = " \r\033[K\r\0" M "$ " M;
    reset(); boot(&r); reset();
    keys(&r, "\033[A\033[A\r");
    out(&r, redraw, sizeof redraw - 1);
    out(&r, redraw, sizeof redraw - 1);
    out(&r, ctrlu, sizeof ctrlu - 1);
    expect_str("burst: no hidden save on redraws", child, "\033[A\033[A\r");
    expect_int("burst: still running", r.state, HS_RUNNING);
    outs(&r, "\r\n\r  24-OCT-2026\r\n");
    prompt(&r);
    expect_str("burst: save at the fresh prompt", child,
               "\033[A\033[A\rRECALL/OUTPUT=HISTSH$NEW\r");
}

static void test_split_marker_and_false_start(void)
{
    hs_relay r;
    static const char a[] = "\r\0\033[?77", b[] = "77h$ " M;
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
    outs(&r, "A\r\n");
    prompt(&r);
    keys(&r, "B");
    hs_relay_timeout(&r);
    expect_int("timeout event", events[0], HS_EV_TIMEOUT);
    expect_int("timeout: idle", r.state, HS_IDLE);
    expect_str("timeout: held keys replayed", child, "A\rRECALL/OUTPUT=HISTSH$NEW\rB");
    outs(&r, "late output");
    expect_str("timeout: output passes again", screen + nscreen - 11, "late output");
}

/* C2: history keys at a fresh prompt (docs/C2.md). */
static void test_history_keys(void)
{
    hs_relay r;
    hs_hist h;
    hs_hist_init(&h, 100);
    hs_hist_add(&h, "SHOW TIME");
    hs_hist_add(&h, "DIR");
    reset(); boot(&r); hs_relay_set_history(&r, &h); reset();
    keys(&r, "\033[A");                         /* up: the newest */
    expect_str("up: line replaced", child, "\005\025DIR");
    keys(&r, "\033OA");                         /* up in application cursor mode */
    expect_str("up again: older", child, "\005\025DIR\005\025SHOW TIME");
    reset();
    keys(&r, "\033[B\033[A\033[B");             /* a burst: only the last is sent */
    expect_str("burst: one recall sent", child, "\005\025DIR");
    keys(&r, "\033[A");                         /* back to the oldest for what follows */
    reset();
    keys(&r, "\002");                           /* Ctrl/B: nothing older */
    expect_str("Ctrl/B at the oldest: nothing sent", child, "");
    expect_str("Ctrl/B at the oldest: bell", screen, "\007");
    keys(&r, "\033[B");                         /* down */
    expect_str("down: newer", child, "\005\025DIR");
    keys(&r, "\033[B");
    expect_str("down past the newest: empty line", child, "\005\025DIR\005\025");
    reset();
    keys(&r, "\033[C\033[D");                   /* right, left: DCL's */
    expect_str("other sequences pass whole", child, "\033[C\033[D");
    reset();
    keys(&r, "\033[A\r");                       /* up then Return: runs, and is a command */
    expect_str("recalled and run", child, "\005\025DIR\r");
    expect_int("recalled: dirty", r.dirty, 1);
    expect_int("recalled: running", r.state, HS_RUNNING);
    hs_hist_free(&h);
}

static void test_no_history_keys_mid_command(void)
{
    hs_relay r;
    hs_hist h;
    static const char cont[] = "\r_" M "$ " M;
    hs_hist_init(&h, 100);
    hs_hist_add(&h, "DIR");
    reset(); boot(&r); hs_relay_set_history(&r, &h); reset();
    keys(&r, "WRITE SYS$OUTPUT -\r");
    outs(&r, "WRITE SYS$OUTPUT -\r\n");
    outs(&r, cont);                              /* continuation prompt */
    keys(&r, "\033[A");
    expect_str("continuation: up-arrow goes to DCL", child, "WRITE SYS$OUTPUT -\r\033[A");
    hs_hist_free(&h);
}

/* C2b: Ctrl/R reverse incremental search. */
static void test_search_keys(void)
{
    hs_relay r;
    hs_hist h;
    hs_hist_init(&h, 100);
    hs_hist_add(&h, "DIRECTORY/SIZE");
    hs_hist_add(&h, "SHOW TIME");
    hs_hist_add(&h, "DIR [.SUB]");
    reset(); boot(&r); hs_relay_set_history(&r, &h); reset();
    keys(&r, "\022");
    expect_str("Ctrl/R: prompt drawn, nothing to DCL", child, "");
    expect_str("Ctrl/R: search line", screen, "\r\033[K(reverse-i-search)`': ");
    reset();
    keys(&r, "di");
    expect_str("typing narrows (case ignored)", screen + nscreen - strlen("(reverse-i-search)`di': DIR [.SUB]"),
               "(reverse-i-search)`di': DIR [.SUB]");
    keys(&r, "\022");                          /* older match */
    expect_str("Ctrl/R again: older match", screen + nscreen - strlen("DIRECTORY/SIZE"), "DIRECTORY/SIZE");
    expect_str("still nothing to DCL", child, "");
    reset();
    keys(&r, "\r");                            /* run it */
    expect_str("Return runs the match", child, "\005\025DIRECTORY/SIZE\022\r");
    expect_int("Return: running", r.state, HS_RUNNING);
    expect_int("Return: a command", r.dirty, 1);
    hs_hist_free(&h);
}

static void test_search_edit_and_cancel(void)
{
    hs_relay r;
    hs_hist h;
    hs_hist_init(&h, 100);
    hs_hist_add(&h, "SHOW TIME");
    hs_hist_add(&h, "DIR");
    reset(); boot(&r); hs_relay_set_history(&r, &h); reset();
    keys(&r, "\022TIME\033[D");                /* left-arrow: accept, then edit */
    expect_str("arrow accepts to edit", child, "\005\025SHOW TIME\022\033[D");
    expect_int("accepted: not searching", r.searching, 0);
    reset();
    keys(&r, "\022XYZ");
    expect_str("no match: failed search shown", screen + nscreen - strlen("(failed reverse-i-search)`XYZ': "),
               "(failed reverse-i-search)`XYZ': ");
    keys(&r, "\007");                           /* Ctrl/G: cancel */
    expect_str("cancel: DCL repaints its line", child, "\022");
    expect_int("cancel: idle", r.state, HS_IDLE);
    reset();
    keys(&r, "\022D\177");                     /* Delete shortens to nothing */
    expect_str("delete: back to the empty search", screen + nscreen - strlen("(reverse-i-search)`': "),
               "(reverse-i-search)`': ");
    hs_hist_free(&h);
}

/* C2c: after SET PROMPT the prompt has no marker; HISTSH puts it back. */
static void test_marker_repair(void)
{
    hs_relay r;
    hs_hist h;
    hs_hist_init(&h, 100);
    reset(); boot(&r); hs_relay_set_history(&r, &h); hs_relay_set_remark(&r, "@HISTSH:HISTSH_REMARK"); reset();
    keys(&r, "set prom \"X> \"\r");
    expect_int("SET PROMPT seen", r.prompt_change, 1);
    outs(&r, "set prom \"X> \"\r\n");
    out(&r, "\r\0X> ", 5);                     /* the new prompt: no marker */
    expect_int("bare prompt: waiting for quiet", r.state, HS_REPAIR);
    hs_relay_timeout(&r);
    expect_str("quiet: remark typed", child, "set prom \"X> \"\r@HISTSH:HISTSH_REMARK\r");
    outs(&r, "@HISTSH:HISTSH_REMARK\r\n");
    out(&r, "\r\0" M "X> " M, 5 + 2 * (sizeof M - 1));
    expect_str("marker back: then the save", child,
               "set prom \"X> \"\r@HISTSH:HISTSH_REMARK\rRECALL/OUTPUT=HISTSH$NEW\r");
    nscreen = 0; screen[0] = '\0';
    outs(&r, "RECALL/OUTPUT=HISTSH$NEW\r\n");
    out(&r, "\r\0" M "X> " M, 5 + 2 * (sizeof M - 1));
    expect_str("prompt redrawn in place", screen, "\r\033[KX> ");
    expect_int("repaired: idle, history keys back", r.state == HS_IDLE && r.keys_ok, 1);
    hs_hist_free(&h);
}

static void test_no_repair_without_set_prompt(void)
{
    hs_relay r;
    reset(); boot(&r); hs_relay_set_remark(&r, "@HISTSH:HISTSH_REMARK"); reset();
    keys(&r, "RUN PROG\r");                     /* not SET PROMPT */
    outs(&r, "RUN PROG\r\n");
    out(&r, "\r\0Name? ", 8);                  /* a prompt-like line from a program */
    expect_int("no SET PROMPT: no repair", r.state, HS_RUNNING);
    reset(); boot(&r); hs_relay_set_remark(&r, "@HISTSH:HISTSH_REMARK"); reset();
    keys(&r, "SET PROMPT=\"Y> \"\r");
    outs(&r, "SET PROMPT=\"Y> \"\r\n");
    out(&r, "\r\0Y> ", 5);
    keys(&r, "D");                              /* typed first: try again later */
    expect_int("typed first: running", r.state, HS_RUNNING);
    expect_str("typed first: no remark", child, "SET PROMPT=\"Y> \"\rD");
}

int main(void)
{
    test_boot();
    test_command_then_save();
    test_empty_line_no_save();
    test_typeahead_defers_save();
    test_keys_to_a_program();
    test_continuation_prompt();
    test_redraw_at_prompt();
    test_typing_during_login();
    test_burst_redraw_while_running();
    test_split_marker_and_false_start();
    test_timeout();
    test_history_keys();
    test_no_history_keys_mid_command();
    test_search_keys();
    test_search_edit_and_cancel();
    test_marker_repair();
    test_no_repair_without_set_prompt();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
