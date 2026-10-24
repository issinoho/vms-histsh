/* test_hist.c - unit tests for src/core/hs_hist.c (Linux, no VMS needed). */
#include <stdio.h>
#include <string.h>
#include "hs_hist.h"

static int failures, checks;

static void expect_int(const char *what, long got, long want)
{
    checks++;
    if (got != want) { failures++; printf("FAIL %s: got %ld, want %ld\n", what, got, want); }
}

static void expect_str(const char *what, const char *got, const char *want)
{
    checks++;
    if ((got == 0) != (want == 0) || (got && strcmp(got, want) != 0)) {
        failures++;
        printf("FAIL %s: got \"%s\", want \"%s\"\n", what, got ? got : "(null)", want ? want : "(null)");
    }
}

static void fill(hs_lines *l, const char *const *v)
{
    hs_lines_free(l);
    while (*v) hs_lines_push(l, *v++);
}

static void test_snapshot(void)
{
    static const char *const p0[] = { 0 };
    static const char *const p[] = { "A", "B", "C", 0 };
    static const char *const n_add[] = { "A", "B", "C", "D", 0 };
    static const char *const n_same[] = { "A", "B", "C", 0 };
    static const char *const n_fell[] = { "C", "D", "E", 0 };           /* A, B fell off */
    static const char *const n_dup[] = { "A", "B", "C", "B", 0 };       /* N14: older rerun */
    static const char *const n_erased[] = { "X", 0 };                   /* RECALL/ERASE, then X */
    hs_lines a, b;
    hs_lines_init(&a); hs_lines_init(&b);
    fill(&a, p); fill(&b, n_add);
    expect_int("snapshot: one added", hs_snapshot_new(&a, &b), 3);
    fill(&b, n_same);
    expect_int("snapshot: nothing new", hs_snapshot_new(&a, &b), 3);
    fill(&b, n_fell);
    expect_int("snapshot: oldest fell off, two added", hs_snapshot_new(&a, &b), 1);
    fill(&b, n_dup);
    expect_int("snapshot: older command rerun", hs_snapshot_new(&a, &b), 3);
    fill(&b, n_erased);
    expect_int("snapshot: buffer erased", hs_snapshot_new(&a, &b), 0);
    {
        static const char *const n_alien[] = { "D", "E", "F", 0 };
        fill(&b, n_alien);
        expect_int("snapshot: no overlap, only the newest", hs_snapshot_new(&a, &b), 2);
    }
    fill(&a, p0); fill(&b, n_add);
    expect_int("snapshot: from empty", hs_snapshot_new(&a, &b), 0);
    hs_lines_free(&a); hs_lines_free(&b);
}

static void test_hist(void)
{
    hs_hist h;
    hs_hist_init(&h, 3);
    expect_int("add", hs_hist_add(&h, "ONE"), 1);
    expect_int("add empty ignored", hs_hist_add(&h, ""), 0);
    expect_int("add repeat ignored", hs_hist_add(&h, "ONE"), 0);
    hs_hist_add(&h, "TWO");
    hs_hist_add(&h, "ONE");
    hs_hist_add(&h, "THREE");
    expect_int("capped at max", h.l.n, 3);
    expect_str("oldest dropped", h.l.e[0], "TWO");
    /* up: THREE, ONE, TWO, then nothing */
    expect_str("prev 1", hs_hist_prev(&h, 0), "THREE");
    expect_str("prev 2", hs_hist_prev(&h, "THREE"), "ONE");
    expect_str("prev 3", hs_hist_prev(&h, "ONE"), "TWO");
    expect_str("prev at oldest", hs_hist_prev(&h, "TWO"), 0);
    expect_str("next 1", hs_hist_next(&h, "TWO"), "ONE");
    expect_str("next 2", hs_hist_next(&h, "ONE"), "THREE");
    expect_str("next past newest", hs_hist_next(&h, "THREE"), "");
    expect_str("prev again after the end", hs_hist_prev(&h, 0), "THREE");
    hs_hist_reset(&h);
    hs_hist_free(&h);

    hs_hist_init(&h, 10);                      /* skipping an equal entry */
    hs_hist_add(&h, "DIR");
    hs_hist_add(&h, "SHOW TIME");
    hs_hist_add(&h, "DIR");
    expect_str("prev from nothing shown", hs_hist_prev(&h, 0), "DIR");
    expect_str("prev skips the same text", hs_hist_prev(&h, "DIR"), "SHOW TIME");
    hs_hist_free(&h);
}

int main(void)
{
    test_snapshot();
    test_hist();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
