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
