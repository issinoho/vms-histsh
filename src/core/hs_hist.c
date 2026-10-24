/* hs_hist.c - C2's history list and DCL snapshot comparison. Pure C99, no I/O. */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "hs_hist.h"

void hs_lines_init(hs_lines *l) { l->e = 0; l->n = l->cap = 0; }

void hs_lines_free(hs_lines *l)
{
    unsigned int i;
    for (i = 0; i < l->n; i++) free(l->e[i]);
    free(l->e);
    hs_lines_init(l);
}

int hs_lines_push(hs_lines *l, const char *s)
{
    size_t len = strlen(s);
    char *copy;
    if (l->n == l->cap) {
        unsigned int cap = l->cap ? l->cap * 2 : 64;
        char **e = realloc(l->e, cap * sizeof *e);
        if (!e) return 0;
        l->e = e;
        l->cap = cap;
    }
    copy = malloc(len + 1);
    if (!copy) return 0;
    memcpy(copy, s, len + 1);
    l->e[l->n++] = copy;
    return 1;
}

void hs_lines_drop_oldest(hs_lines *l, unsigned int k)
{
    unsigned int i;
    if (k > l->n) k = l->n;
    for (i = 0; i < k; i++) free(l->e[i]);
    memmove(l->e, l->e + k, (l->n - k) * sizeof *l->e);
    l->n -= k;
}

void hs_lines_move(hs_lines *to, hs_lines *from)
{
    hs_lines_free(to);
    *to = *from;
    hs_lines_init(from);
}

unsigned int hs_snapshot_new(const hs_lines *p, const hs_lines *n)
{
    unsigned int k;
    for (k = 0; k < p->n; k++) {
        unsigned int m = p->n - k, i;          /* overlap length tried */
        if (m > n->n) continue;
        for (i = 0; i < m; i++)
            if (strcmp(p->e[k + i], n->e[i]) != 0) break;
        if (i == m) return m;
    }
    /* No overlap at all. From an empty snapshot everything is new; otherwise the
       buffer was erased (RECALL/ERASE) or the snapshot was misread, and only the
       newest entry is taken, so a bad snapshot can't flood the history. */
    if (p->n == 0 || n->n == 0) return 0;
    return n->n - 1;
}

void hs_hist_init(hs_hist *h, unsigned int max)
{
    hs_lines_init(&h->l);
    h->max = max ? max : 1;
    h->pos = 0;
}

void hs_hist_free(hs_hist *h) { hs_lines_free(&h->l); h->pos = 0; }

void hs_hist_reset(hs_hist *h) { h->pos = h->l.n; }

int hs_hist_add(hs_hist *h, const char *s)
{
    if (s[0] == '\0') return 0;
    if (h->l.n && strcmp(h->l.e[h->l.n - 1], s) == 0) { hs_hist_reset(h); return 0; }
    if (!hs_lines_push(&h->l, s)) return 0;
    if (h->l.n > h->max) hs_lines_drop_oldest(&h->l, h->l.n - h->max);
    hs_hist_reset(h);
    return 1;
}

const char *hs_hist_prev(hs_hist *h, const char *shown)
{
    unsigned int i = h->pos > h->l.n ? h->l.n : h->pos;
    while (i > 0) {
        i--;
        if (!shown || strcmp(h->l.e[i], shown) != 0) { h->pos = i; return h->l.e[i]; }
    }
    return 0;                                   /* nothing older: stay where we are */
}

const char *hs_hist_next(hs_hist *h, const char *shown)
{
    unsigned int i = h->pos;
    while (i + 1 < h->l.n) {
        i++;
        if (!shown || strcmp(h->l.e[i], shown) != 0) { h->pos = i; return h->l.e[i]; }
    }
    h->pos = h->l.n;                            /* past the newest: an empty line */
    return "";
}

static int contains_nocase(const char *s, const char *q)
{
    size_t n = strlen(q);
    if (n == 0) return 1;
    for (; *s; s++) {
        size_t i;
        for (i = 0; i < n && s[i]; i++)
            if (toupper((unsigned char) s[i]) != toupper((unsigned char) q[i])) break;
        if (i == n) return 1;
    }
    return 0;
}

int hs_hist_search(const hs_hist *h, const char *q, int from)
{
    int i;
    if (from >= (int) h->l.n) from = (int) h->l.n - 1;
    for (i = from; i >= 0; i--)
        if (contains_nocase(h->l.e[i], q)) return i;
    return -1;
}
