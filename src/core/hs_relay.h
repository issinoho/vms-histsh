/* hs_relay.h - the C1 relay between the user's terminal and the child DCL.

   Pure: no I/O. The platform feeds it the child's output and the user's keys, and
   it answers through the sink callbacks (docs/C1.md has the state table). */
#ifndef HS_RELAY_H
#define HS_RELAY_H

#include "hs_hist.h"

#define HS_MARKER "\033[?7777h"     /* both ends of the child's prompt; never shown (N9, N12) */

enum hs_state { HS_BOOT, HS_BOOT_ECHO, HS_RUNNING, HS_SETTLE, HS_IDLE, HS_HIDDEN, HS_REPAIR };

#define HS_HIDDEN_MS 5000   /* a hidden command that gets no prompt in this time is given up */
#define HS_SETTLE_MS 300    /* quiet time that proves typeahead has all been read */
enum hs_hidden { HS_HIDE_SNAP = 1, HS_HIDE_LOAD, HS_HIDE_SAVE, HS_HIDE_REMARK };
enum hs_event {
    HS_EV_SNAPPED = 1,      /* the start-up snapshot finished: build the load file now */
    HS_EV_LOADED,           /* the hidden load finished */
    HS_EV_SAVED,            /* the hidden save finished: check the file and rename it */
    HS_EV_TIMEOUT           /* a hidden command got no prompt in time */
};

typedef struct hs_sink {
    void (*screen)(void *ctx, const char *p, unsigned int n);   /* to the user */
    void (*child)(void *ctx, const char *p, unsigned int n);    /* to the child DCL */
    void (*event)(void *ctx, int ev);
    void (*timer)(void *ctx, unsigned int ms);   /* (re)start the one timer; 0 cancels */
    void *ctx;
} hs_sink;

#define HS_HOLD_MAX 1024
#define HS_LINE_MAX 4100     /* a DCL command line is at most about 4096 bytes */

typedef struct hs_relay {
    hs_sink sink;
    const char *boot_cmd;    /* typed at the first prompt, e.g. "@HISTSH:HISTSH_CHILD" */
    const char *load_cmd;    /* "RECALL/ERASE/INPUT=HISTSH$LOAD" */
    const char *save_cmd;    /* "RECALL/OUTPUT=HISTSH$NEW", also the start-up snapshot */
    int state;
    int hidden;              /* enum hs_hidden while HS_HIDDEN */
    int loaded;              /* the hidden load has been typed */
    int dirty;               /* a command was entered since the last save */
    int pending;             /* the user typed while RUNNING */
    int at_fresh;            /* SETTLE: the last thing DCL sent was a whole fresh prompt */
    int settle_start;        /* SETTLE: a fresh prompt's start marker seen, not its end */
    int redraw;              /* this hidden command was typed at a prompt on the screen */
    int pending_redraw;      /* so draw the next prompt over it */
    unsigned int typed;      /* user bytes sent at the current prompt */
    unsigned int mpos;       /* bytes of HS_MARKER matched and held back */
    char prev, prev2, prev3; /* the last three output bytes before any held ones */
    char hold[HS_HOLD_MAX];  /* user keys held during BOOT and HIDDEN */
    unsigned int nhold;
    unsigned int lost;       /* keys dropped because hold[] was full */
    /* C2 (docs/C2.md): history keys at a fresh prompt. */
    hs_hist *hist;           /* NULL: C1 behaviour, every key to DCL */
    int keys_ok;             /* IDLE at a fresh quiet prompt: history keys are ours */
    char esc[3];             /* an escape sequence being read (ESC, [ or O) */
    unsigned int nesc;
    char shown[HS_LINE_MAX]; /* the entry HISTSH last put on the line */
    int recall_pending;      /* shown[] still to be sent: a burst sends only the last */
    /* C2b: Ctrl/R reverse incremental search. */
    int searching;
    char query[256];
    unsigned int nquery;
    int match;               /* index in hist of the match shown, -1 for none */
    /* C2c: putting the marker back after SET PROMPT (NOTES N18). */
    const char *remark_cmd;  /* "@HISTSH:HISTSH_REMARK"; NULL: off */
    char line[64];           /* the start of the line typed at the prompt */
    unsigned int nline;
    int line_known;          /* line[] is what DCL has (no editing keys used) */
    int prompt_change;       /* the last line run looked like SET PROMPT */
} hs_relay;

void hs_relay_init(hs_relay *r, const hs_sink *sink, const char *boot_cmd,
                   const char *load_cmd, const char *save_cmd);
void hs_relay_child_output(hs_relay *r, const char *p, unsigned int n);
void hs_relay_user_input(hs_relay *r, const char *p, unsigned int n);
void hs_relay_timeout(hs_relay *r);
/* C2: give the relay a history to recall from (NULL for C1 behaviour). */
void hs_relay_set_history(hs_relay *r, hs_hist *h);
/* C2c: the command that puts the marker back on a changed prompt (NULL: off). */
void hs_relay_set_remark(hs_relay *r, const char *cmd);

#endif
