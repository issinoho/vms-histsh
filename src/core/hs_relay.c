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

static void timer(hs_relay *r, unsigned int ms)
{
    if (r->sink.timer) r->sink.timer(r->sink.ctx, ms);
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
    if (kind == HS_HIDE_SNAP) r->loaded = 1;
    if (kind == HS_HIDE_SAVE) r->dirty = 0;
    type_cmd(r, kind == HS_HIDE_LOAD ? r->load_cmd : r->save_cmd);
    timer(r, HS_HIDDEN_MS);
}

static void user_byte(hs_relay *r, char c);
static void flush_recall(hs_relay *r);

/* At a prompt, ready for the user: keys held meanwhile go to DCL now. */
static void enter_idle(hs_relay *r)
{
    unsigned int i, n = r->nhold;
    char tmp[HS_HOLD_MAX];
    r->state = HS_IDLE;
    r->typed = 0;
    r->pending = 0;
    r->keys_ok = 1;                        /* a fresh, quiet prompt */
    r->nesc = 0;
    r->shown[0] = '\0';
    memcpy(tmp, r->hold, n);
    r->nhold = 0;
    for (i = 0; i < n; i++) user_byte(r, tmp[i]);
    flush_recall(r);
}

/* DCL is at an empty top-level prompt with nothing typed ahead: the moment for a
   hidden command (the snapshot before the load, or a save), else just the prompt. */
static void at_quiet_prompt(hs_relay *r)
{
    if (!r->loaded) start_hidden(r, HS_HIDE_SNAP);
    else if (r->dirty) start_hidden(r, HS_HIDE_SAVE);
    else enter_idle(r);
}

/* A marker has been seen. The child's prompt is marker + prompt + marker (NOTES N12),
   and what came straight before a marker says what it is (NOTES N9, N10):
   - a start marker follows NUL (top level) or "_" (continuation);
     a fresh top-level prompt has LF NUL or LF CR NUL before it, while NUL NUL NUL
     or ESC [ K CR NUL is DCL redrawing the line (up-arrow, Ctrl/U, Ctrl/R);
   - an end marker follows the prompt's last character: the prompt is complete. */
static void on_marker(hs_relay *r)
{
    int fresh = r->prev == '\0' &&
                (r->prev2 == '\n' || (r->prev2 == '\r' && r->prev3 == '\n'));
    if (r->prev != '\0' && r->prev != '_') {         /* end marker */
        if (r->state == HS_SETTLE && r->settle_start) {
            r->settle_start = 0;
            r->at_fresh = 1;               /* nothing after the prompt, so far */
            timer(r, HS_SETTLE_MS);
        }
        return;
    }
    if (r->prev == '_') {                  /* continuation prompt: the command goes on */
        r->at_fresh = 0;
        r->settle_start = 0;
        if (r->state == HS_RUNNING && !r->pending) {
            r->state = HS_IDLE;
            r->typed = 0;
            r->keys_ok = 0;                /* mid-command: arrows stay DCL's */
        }
        return;
    }
    if (!fresh) return;                    /* a redraw: ignore */
    switch (r->state) {
    case HS_BOOT:
        type_cmd(r, r->boot_cmd);
        r->state = HS_BOOT_ECHO;
        timer(r, HS_HIDDEN_MS);
        break;
    case HS_RUNNING:
        if (!r->pending) { at_quiet_prompt(r); break; }
        /* The user typed while the command ran. That may be typeahead DCL is about
           to read, or input a program already took: wait for DCL to go quiet at
           a fresh prompt before typing anything (NOTES N12). */
        r->state = HS_SETTLE;
        r->dirty = 1;
        r->at_fresh = 0;
        r->settle_start = 1;               /* at_fresh once the end marker comes */
        timer(r, HS_SETTLE_MS);
        break;
    case HS_SETTLE:                        /* a typed-ahead line ran: wait again */
        r->at_fresh = 0;
        r->settle_start = 1;
        timer(r, HS_SETTLE_MS);
        break;
    case HS_HIDDEN:
        timer(r, 0);
        if (r->hidden == HS_HIDE_SNAP) {
            /* The buffer so far (the boot command, and any commands typed while
               LOGIN.COM ran) is saved; the platform merges it after the history
               file, and the load replaces the buffer with the result. */
            r->sink.event(r->sink.ctx, HS_EV_SNAPPED);
            start_hidden(r, HS_HIDE_LOAD);
            break;
        }
        r->sink.event(r->sink.ctx, r->hidden == HS_HIDE_LOAD ? HS_EV_LOADED : HS_EV_SAVED);
        r->hidden = 0;
        r->pending_redraw = r->redraw;     /* the prompt after this one is shown */
        r->redraw = 0;
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
            r->pending = 0;
            timer(r, 0);
        }
        break;
    case HS_SETTLE:
        if (!r->settle_start) {            /* after the prompt: DCL echoing typeahead */
            r->at_fresh = 0;
            timer(r, HS_SETTLE_MS);
        }
        out_byte(r, o, c);
        break;
    default:
        if (r->pending_redraw) {
            /* After a hidden command typed at a prompt already on the screen:
               draw the new prompt over the old one, not after it. */
            r->pending_redraw = 0;
            out_byte(r, o, '\r');
            out_byte(r, o, '\033'); out_byte(r, o, '['); out_byte(r, o, 'K');
        }
        out_byte(r, o, c);
    }
    r->prev3 = r->prev2;
    r->prev2 = r->prev;
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
    r->prev = r->prev2 = r->prev3 = '\n';
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
                on_marker(r);
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

/* Send the line recalled last, if not yet sent. Up-arrows in one burst (a key held
   down) send only the final line: one Ctrl/E Ctrl/U and text per key could overrun
   the terminal's typeahead buffer. */
static void flush_recall(hs_relay *r)
{
    size_t n;
    if (!r->recall_pending) return;
    r->recall_pending = 0;
    n = strlen(r->shown);
    r->sink.child(r->sink.ctx, "\005\025", 2);
    if (n) r->sink.child(r->sink.ctx, r->shown, (unsigned int) n);
}

/* A key at the prompt that goes to DCL as it is. */
static void idle_plain(hs_relay *r, char c)
{
    flush_recall(r);
    r->sink.child(r->sink.ctx, &c, 1);
    if (c == '\r') {
        if (r->typed) r->dirty = 1;
        r->state = HS_RUNNING;
        r->pending = 0;
        r->typed = 0;
        r->shown[0] = '\0';
        if (r->hist) hs_hist_reset(r->hist);
    } else r->typed++;
}

/* Put a history entry on DCL's line: Ctrl/E and Ctrl/U clear it from wherever the
   cursor is (NOTES N15), then the text (sent by flush_recall); DCL redraws and the
   user edits it as usual. */
static void recall_line(hs_relay *r, const char *text)
{
    size_t n = strlen(text);
    if (n >= sizeof r->shown) n = sizeof r->shown - 1;
    memcpy(r->shown, text, n);
    r->shown[n] = '\0';
    r->typed = (unsigned int) n;
    r->recall_pending = 1;                 /* sent by flush_recall */
}

static void history_up(hs_relay *r)
{
    const char *t = hs_hist_prev(r->hist, r->shown[0] ? r->shown : 0);
    if (t) recall_line(r, t);
    else r->sink.screen(r->sink.ctx, "\007", 1);         /* nothing older: bell */
}

static void history_down(hs_relay *r)
{
    if (r->hist->pos >= r->hist->l.n) return;            /* not moving: nothing newer */
    recall_line(r, hs_hist_next(r->hist, r->shown[0] ? r->shown : 0));
}

static void flush_esc(hs_relay *r)
{
    unsigned int i, n = r->nesc;
    r->nesc = 0;
    for (i = 0; i < n; i++) idle_plain(r, r->esc[i]);
}

/* A key at a fresh prompt (C2): up-arrow, down-arrow and Ctrl/B are history keys;
   ESC [ x and ESC O x are read whole, and any sequence but A or B goes to DCL. */
static void idle_key(hs_relay *r, char c)
{
    if (r->nesc == 0) {
        if (c == '\033') { r->esc[0] = c; r->nesc = 1; return; }
        if (c == '\002') { history_up(r); return; }    /* Ctrl/B, DCL's "recall" */
        idle_plain(r, c);
        return;
    }
    if (r->nesc == 1) {
        if (c == '[' || c == 'O') { r->esc[1] = c; r->nesc = 2; return; }
        flush_esc(r);
        idle_key(r, c);
        return;
    }
    r->nesc = 0;
    if (c == 'A') { history_up(r); return; }
    if (c == 'B') { history_down(r); return; }
    r->nesc = 2;
    flush_esc(r);
    idle_plain(r, c);
}

void hs_relay_set_history(hs_relay *r, hs_hist *h) { r->hist = h; }

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
        if (r->hist && r->keys_ok) idle_key(r, c);
        else idle_plain(r, c);
        return;
    case HS_SETTLE:
        r->sink.child(r->sink.ctx, &c, 1);
        r->at_fresh = 0;                   /* the user is typing: not quiet yet */
        timer(r, HS_SETTLE_MS);
        return;
    default:                               /* HS_RUNNING: typeahead or input to a program */
        r->sink.child(r->sink.ctx, &c, 1);
        r->pending = 1;
    }
}

void hs_relay_user_input(hs_relay *r, const char *p, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) user_byte(r, p[i]);
    flush_recall(r);
}

void hs_relay_timeout(hs_relay *r)
{
    switch (r->state) {
    case HS_SETTLE:
        /* Quiet for HS_SETTLE_MS. At an empty fresh prompt: go ahead, redrawing
           the prompt that is already on the screen. Otherwise DCL (or a program)
           is waiting in mid-line: the user is at the keyboard. */
        if (r->at_fresh) {
            r->redraw = 1;
            at_quiet_prompt(r);
            if (r->state == HS_IDLE) r->redraw = 0;
        } else {
            r->state = HS_IDLE;            /* DCL or a program is reading mid-line */
            r->typed = 1;
            r->pending = 0;
            r->keys_ok = 0;
        }
        return;
    case HS_HIDDEN:
    case HS_BOOT_ECHO:
        r->sink.event(r->sink.ctx, HS_EV_TIMEOUT);
        r->hidden = 0;
        r->redraw = 0;
        enter_idle(r);
        return;
    default:
        return;
    }
}
