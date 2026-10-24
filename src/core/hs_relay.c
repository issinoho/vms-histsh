/* hs_relay.c - the C1 relay state machine (docs/C1.md). Pure C99, no I/O. */
#include <string.h>
#include "hs_relay.h"

#define MLEN (sizeof HS_MARKER - 1)

/* Output to the screen is gathered per call and written in one piece. */
typedef struct { char buf[512]; unsigned int n; } outbuf;

static void out_flush(hs_relay *r, outbuf *o)
{
    if (o->n) r->sink.screen(r->sink.ctx, o->buf, o->n);
    o->n = 0;
}

static void out_byte(hs_relay *r, outbuf *o, char c)
{
    if (o->n == sizeof o->buf) out_flush(r, o);
    o->buf[o->n++] = c;
}

static void type_cmd(hs_relay *r, const char *cmd)
{
    r->sink.child(r->sink.ctx, cmd, (unsigned int) strlen(cmd));
    r->sink.child(r->sink.ctx, "\r", 1);
}

static void start_hidden(hs_relay *r, int kind)
{
    r->state = HS_HIDDEN;
    r->hidden = kind;
    if (kind == HS_HIDE_LOAD) r->loaded = 1;
    if (kind == HS_HIDE_SAVE) r->dirty = 0;
    type_cmd(r, kind == HS_HIDE_LOAD ? r->load_cmd : r->save_cmd);
    if (r->sink.timer) r->sink.timer(r->sink.ctx, 1);
}

static void user_byte(hs_relay *r, char c);

/* Back at a prompt after BOOT or HIDDEN: keys typed meanwhile go to DCL now. */
static void enter_idle(hs_relay *r)
{
    unsigned int i, n = r->nhold;
    char tmp[HS_HOLD_MAX];
    r->state = HS_IDLE;
    r->typed = 0;
    r->ahead = 0;
    memcpy(tmp, r->hold, n);
    r->nhold = 0;
    for (i = 0; i < n; i++) user_byte(r, tmp[i]);
}

/* A marker has been seen; `before` is the output byte straight before it. */
static void on_marker(hs_relay *r, char before)
{
    if (before == '_') {                   /* continuation prompt: the command goes on */
        if (r->state == HS_RUNNING) { r->state = HS_IDLE; r->typed = r->ahead; r->ahead = 0; }
        return;
    }
    if (before != '\0') return;            /* not a prompt DCL issued: ignore */
    switch (r->state) {
    case HS_BOOT:
        type_cmd(r, r->boot_cmd);
        r->state = HS_BOOT_ECHO;
        if (r->sink.timer) r->sink.timer(r->sink.ctx, 1);
        break;
    case HS_RUNNING:
        if (r->ahead != 0) { r->state = HS_IDLE; r->typed = r->ahead; r->ahead = 0; }
        else if (!r->loaded) start_hidden(r, HS_HIDE_LOAD);
        else if (r->dirty) start_hidden(r, HS_HIDE_SAVE);
        else enter_idle(r);
        break;
    case HS_HIDDEN:
        if (r->sink.timer) r->sink.timer(r->sink.ctx, 0);
        r->sink.event(r->sink.ctx, r->hidden == HS_HIDE_LOAD ? HS_EV_LOADED : HS_EV_SAVED);
        r->hidden = 0;
        enter_idle(r);
        break;
    default:                               /* HS_IDLE: a redraw of the prompt */
        break;
    }
}

/* One output byte that isn't part of a marker. */
static void plain_byte(hs_relay *r, outbuf *o, char c)
{
    switch (r->state) {
    case HS_HIDDEN:
        break;
    case HS_BOOT_ECHO:
        if (c == '\n') {
            r->state = HS_RUNNING;
            r->ahead = 0;
            if (r->sink.timer) r->sink.timer(r->sink.ctx, 0);
        }
        break;
    default:
        out_byte(r, o, c);
    }
    r->prev = c;
}

void hs_relay_init(hs_relay *r, const hs_sink *sink, const char *boot_cmd,
                   const char *load_cmd, const char *save_cmd)
{
    memset(r, 0, sizeof *r);
    r->sink = *sink;
    r->boot_cmd = boot_cmd;
    r->load_cmd = load_cmd;
    r->save_cmd = save_cmd;
    r->state = HS_BOOT;
    r->prev = '\n';
}

void hs_relay_child_output(hs_relay *r, const char *p, unsigned int n)
{
    static const char marker[] = HS_MARKER;
    outbuf o;
    unsigned int i;
    o.n = 0;
    for (i = 0; i < n; i++) {
        char c = p[i];
        if (c == marker[r->mpos]) {
            if (++r->mpos == MLEN) {
                r->mpos = 0;
                out_flush(r, &o);          /* screen order: text before the prompt first */
                on_marker(r, r->prev);
            }
            continue;
        }
        if (r->mpos) {                     /* a false start: the held bytes were text */
            unsigned int k, held = r->mpos;
            r->mpos = 0;
            for (k = 0; k < held; k++) plain_byte(r, &o, marker[k]);
            if (c == marker[0]) { r->mpos = 1; continue; }   /* ESC only starts the marker */
        }
        plain_byte(r, &o, c);
    }
    out_flush(r, &o);
}

static void user_byte(hs_relay *r, char c)
{
    switch (r->state) {
    case HS_BOOT:
    case HS_BOOT_ECHO:
    case HS_HIDDEN:
        if (r->nhold < HS_HOLD_MAX) r->hold[r->nhold++] = c;
        else r->lost++;
        return;
    case HS_IDLE:
        r->sink.child(r->sink.ctx, &c, 1);
        if (c == '\r') {
            if (r->typed) r->dirty = 1;
            r->state = HS_RUNNING;
            r->ahead = 0;
            r->typed = 0;
        } else r->typed++;
        return;
    default:                               /* HS_RUNNING: typeahead or input to a program */
        r->sink.child(r->sink.ctx, &c, 1);
        r->ahead++;
    }
}

void hs_relay_user_input(hs_relay *r, const char *p, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) user_byte(r, p[i]);
}

void hs_relay_timeout(hs_relay *r)
{
    if (r->state != HS_HIDDEN && r->state != HS_BOOT_ECHO) return;
    r->sink.event(r->sink.ctx, HS_EV_TIMEOUT);
    r->hidden = 0;
    enter_idle(r);
}
