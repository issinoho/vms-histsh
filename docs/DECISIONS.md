# Decisions

## D1. Not an AUTHORIZE /CLI= image (2026-10-24)

A CLI image runs in supervisor mode and *is* the command interpreter: symbols,
command procedures, ON, SPAWN, Ctrl/Y. The only CLIs are DCL and MCR. A proxy that hands
commands to DCL can't sit in that slot, so DCL stays the CLI. A user is switched on
through AUTHORIZE another way (D3).

## D2. Two stages: native recall persisted (A), then a front end (C) (2026-10-24)

Considered:
- **A**: DCL's own recall buffer, loaded at login with `RECALL/ERASE/INPUT` and saved at
  LOGOUT with `RECALL/OUTPUT`. Days of work and DCL is untouched. Limits: 254 commands,
  a dropped connection loses its session, the last session to log out wins.
- **B**: a line-editor image in a DCL loop procedure (`$ 'cmd'`). Rejected: commands run
  at procedure depth 1, so SYS$INPUT, prompting for missing parameters, Ctrl/Y and
  apostrophe substitution all behave differently from plain DCL.
- **C**: an rlwrap-style front end. It owns the terminal and runs DCL on a
  pseudo-terminal (PTD$). DCL is untouched, every line is appended as it's entered, and
  it adds Ctrl/R search. Costs: two processes per session, and the most work.

Chosen by the user: A first as a quick win, then C. A stays as C's fallback.

## D3. How a user is switched on (open, Phase 0 probe 2)

Proposed: a rights identifier `HISTSH$USER`, granted with AUTHORIZE `GRANT/IDENTIFIER`
and checked at login as in NOTES N3. This leaves each user's `/LGICMD` and LOGIN.COM
alone, can be revoked with one command, and lets SYLOGIN decide in one place.
Rejected: `/LGICMD`, which replaces the user's own login procedure. To be confirmed
with the user before the identifier is created.

## D4. Phase A dropped; C is built in two steps (2026-10-24, user's choice)

NOTES N7: RECALL /INPUT and /OUTPUT have no effect inside a command procedure, so
neither SYLOGIN nor a LOGOUT wrapper can load or save the recall buffer. Considered:
- **C1 then C2** (chosen). C1: HISTSH passes keystrokes through and leaves DCL's own
  line editing and recall alone. At the first prompt it types a hidden
  `RECALL/ERASE/INPUT`, and after every command a hidden `RECALL/OUTPUT`, swallowing
  their echo. History survives dropped connections; the limit stays 254. C2: HISTSH's
  own line editor, with unlimited history, Ctrl/R and one shared append file.
- Straight to C2: nothing usable until the editor is done.
- Manual symbols or keys (HSAVE/HLOAD, DEFINE/KEY): works today, not automatic.

## D5. C2 keeps DCL's line editor and replaces only its history (2026-10-24)

Considered:
- **A whole line editor in HISTSH** (the plan's C2). HISTSH would draw and edit the
  line itself and send DCL the finished line. That means swallowing DCL's echo of every
  line (wrapping at the terminal width, tabs), and re-implementing every DCL editing
  key users already know.
- **Only the history keys (chosen).** At a fresh top-level prompt HISTSH intercepts
  up-arrow, down-arrow, Ctrl/B and Ctrl/R. Recalling an entry sends DCL Ctrl/E Ctrl/U
  and the entry's text (N15): DCL redraws the line and the user edits it with DCL's own
  keys. What was actually run comes from C1's hidden `RECALL/OUTPUT` after each command,
  compared with the previous snapshot (N14). New entries are appended at once to one
  history file shared by all sessions (N16), with no limit but `HISTSH$SIZE`.
  Everywhere else (continuation prompts, programs, typeahead) keys pass through, and
  DCL's own recall still works there with the session's buffer.

Ctrl/R stops being DCL's "redisplay line" at a fresh prompt (bash users expect search
there). HISTSH uses DCL's Ctrl/R itself, to repaint the line when a search is
cancelled.
