/* hs_hist.h - C2's history list and DCL snapshot comparison (docs/C2.md).

   Pure: no I/O. Strings are copied in, oldest first. */
#ifndef HS_HIST_H
#define HS_HIST_H

/* A list of lines, oldest first. Used both for the history and for a snapshot of
   DCL's recall buffer (RECALL/OUTPUT). */
typedef struct hs_lines {
    char **e;
    unsigned int n, cap;
} hs_lines;

void hs_lines_init(hs_lines *l);
void hs_lines_free(hs_lines *l);
int hs_lines_push(hs_lines *l, const char *s);         /* 0 if out of memory */
void hs_lines_drop_oldest(hs_lines *l, unsigned int k);
void hs_lines_move(hs_lines *to, hs_lines *from);       /* to takes from's lines */

/* The entries DCL added between snapshot p and snapshot n: n's lines after the
   longest overlap, where the smallest k with p[k..] a prefix of n gives n[len(p)-k..]
   (N13: the oldest fall off; N14: DCL only appends). Returns the index in n where
   the new entries start (n->n when there are none). With no overlap at all, only
   n's newest entry counts as new (the buffer was erased, or the snapshot is wrong). */
unsigned int hs_snapshot_new(const hs_lines *p, const hs_lines *n);

/* The history: lines plus a position while moving through it with up/down. */
typedef struct hs_hist {
    hs_lines l;
    unsigned int max;        /* keep at most this many (HISTSH$SIZE) */
    unsigned int pos;        /* while moving: index of the entry shown; l.n = not moving */
} hs_hist;

void hs_hist_init(hs_hist *h, unsigned int max);
void hs_hist_free(hs_hist *h);
/* Add a command run: ignored if empty or equal to the newest. Returns 1 if added. */
int hs_hist_add(hs_hist *h, const char *s);
/* Older / newer than the one shown, skipping entries equal to `shown` (may be NULL).
   prev returns NULL when there is nothing older; next returns "" past the newest. */
const char *hs_hist_prev(hs_hist *h, const char *shown);
const char *hs_hist_next(hs_hist *h, const char *shown);
void hs_hist_reset(hs_hist *h);                          /* stop moving (Return) */
/* Ctrl/R: the newest entry at or before index `from` that contains q, ignoring case
   (DCL does). -1 if none. */
int hs_hist_search(const hs_hist *h, const char *q, int from);

#endif
