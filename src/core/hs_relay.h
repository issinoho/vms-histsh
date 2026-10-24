/* hs_relay.h - the C1 relay between the user's terminal and the child DCL.

   Pure: no I/O. The platform feeds it the child's output and the user's keys, and
   it answers through the sink callbacks (docs/C1.md has the state table). */
#ifndef HS_RELAY_H
#define HS_RELAY_H

#define HS_MARKER "\033[?7777h"     /* in the child's prompt; never shown (NOTES N9) */

enum hs_state { HS_BOOT, HS_BOOT_ECHO, HS_RUNNING, HS_IDLE, HS_HIDDEN };
enum hs_hidden { HS_HIDE_LOAD = 1, HS_HIDE_SAVE = 2 };
enum hs_event {
    HS_EV_LOADED = 1,       /* the hidden load finished */
    HS_EV_SAVED,            /* the hidden save finished: check the file and rename it */
    HS_EV_TIMEOUT           /* a hidden command got no prompt in time */
};

typedef struct hs_sink {
    void (*screen)(void *ctx, const char *p, unsigned int n);   /* to the user */
    void (*child)(void *ctx, const char *p, unsigned int n);    /* to the child DCL */
    void (*event)(void *ctx, int ev);
    void (*timer)(void *ctx, int on);   /* start (1) or cancel (0) the hidden timeout */
    void *ctx;
} hs_sink;

#define HS_HOLD_MAX 1024

typedef struct hs_relay {
    hs_sink sink;
    const char *boot_cmd;    /* typed at the first prompt, e.g. "@HISTSH:HISTSH_CHILD" */
    const char *load_cmd;    /* "RECALL/ERASE/INPUT=HISTSH$FILE" or "RECALL/ERASE" */
    const char *save_cmd;    /* "RECALL/OUTPUT=HISTSH$NEW" */
    int state;
    int hidden;              /* enum hs_hidden while HS_HIDDEN */
    int loaded;              /* the hidden load has been typed */
    int dirty;               /* a command was entered since the last save */
    unsigned int ahead;      /* user bytes sent while RUNNING: typeahead */
    unsigned int typed;      /* user bytes sent at the current prompt */
    unsigned int mpos;       /* bytes of HS_MARKER matched and held back */
    char prev;               /* last output byte before the held bytes */
    char hold[HS_HOLD_MAX];  /* user keys held during BOOT and HIDDEN */
    unsigned int nhold;
    unsigned int lost;       /* keys dropped because hold[] was full */
} hs_relay;

void hs_relay_init(hs_relay *r, const hs_sink *sink, const char *boot_cmd,
                   const char *load_cmd, const char *save_cmd);
void hs_relay_child_output(hs_relay *r, const char *p, unsigned int n);
void hs_relay_user_input(hs_relay *r, const char *p, unsigned int n);
void hs_relay_timeout(hs_relay *r);

#endif
