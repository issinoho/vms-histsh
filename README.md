# vms-histsh

Command history that lasts across sessions for OpenVMS, the way bash keeps
`~/.bash_history`: log in, press up-arrow, and your commands from earlier sessions are
there. Everything else is plain DCL, unchanged.

## Why

DCL's up-arrow and `RECALL` only cover the current session. `RECALL /OUTPUT` and
`RECALL /INPUT` can save and load the buffer, but only when typed at the `$` prompt.
They have no effect inside a command procedure, so neither SYLOGIN nor a LOGOUT wrapper
can do it ([docs/NOTES.md](docs/NOTES.md) N7).

## How it works

HISTSH runs your DCL session on a pseudo-terminal and passes every keystroke through,
so DCL's own line editing, Ctrl/Y and Ctrl/T behave as always. It finds DCL's prompt by
an invisible marker at both ends of it. After each command it quietly types
`RECALL/OUTPUT` at the prompt, sees what DCL ran, and appends it at once to one history
file shared by all your sessions. You never see the hidden command, and a dropped
connection loses nothing.

At the `$` prompt, HISTSH answers the history keys from that file:

| Key | Does |
| --- | --- |
| up-arrow, Ctrl/B | the previous command (from every session, not just this one); edit it with DCL's keys |
| down-arrow | the next one; past the newest, an empty line |
| Ctrl/R | search, as in bash: type to narrow, Ctrl/R for older, Return runs it, Escape or an arrow edits it, Ctrl/G cancels |

Everywhere else (a continuation line, a program such as EDT, typing ahead) keys go to
DCL or the program untouched. The details are in [docs/C1.md](docs/C1.md) and
[docs/C2.md](docs/C2.md), and the decisions in [docs/DECISIONS.md](docs/DECISIONS.md).

A user is switched on with a rights identifier:

    UAF> ADD/IDENTIFIER HISTSH$USER
    UAF> GRANT/IDENTIFIER HISTSH$USER username

`HISTSH_START.COM`, called as the last line of SYLOGIN, starts HISTSH only for
interactive logins by users who hold `HISTSH$USER`. Everyone else's login is
untouched. (AUTHORIZE's `/CLI=` can't be used: a CLI image *is* the command interpreter,
and HISTSH hands every command to DCL.)

## Status

**0.2.0, an early test release, working on OpenVMS x86-64 (E9.2-4) and IA64
(V8.4-2L3)**, tested on both nodes with the on-node tests in [tests/vms](tests/vms) and
installed from its PCSI kits. 0.1.0 used DCL's own recall buffer (a few kilobytes, about
190 short commands, [docs/NOTES.md](docs/NOTES.md) N13), and the last session to log out
won. 0.2.0 keeps one shared file, with no limit but `HISTSH$SIZE` (default 2000).
Limits:

- A `SET PROMPT` typed at the prompt is handled (HISTSH puts its marker back); a prompt
  changed inside a command procedure isn't, and saving stops for that session.
- LOGIN.COM is taken to be `SYS$LOGIN:LOGIN.COM` (a UAF `/LGICMD` isn't read).
- Each session uses two processes: the spawned DCL counts against PRCLM, and you see
  two "logged out" lines.
- At the prompt Ctrl/R is HISTSH's search, not DCL's "redisplay line".
- As with bash, anything typed on a command line is recorded, a password given as a
  parameter included. Passwords typed at a prompt are not.

## Installing

From the PCSI kit for your architecture (x86-64 or IA64):

    $ PRODUCT INSTALL HISTSH /SOURCE=dev:[dir]

This puts the files in `SYS$COMMON:[HISTSH]` and `SYS$STARTUP:HISTSH$STARTUP.COM`, and
runs it once (it defines `HISTSH$ROOT` and `HISTSH`). Then:

- add `$ @SYS$STARTUP:HISTSH$STARTUP.COM` to SYSTARTUP_VMS.COM;
- add this as the last line of SYLOGIN.COM:

      $ IF F$SEARCH("HISTSH:HISTSH_START.COM") .NES. "" THEN @HISTSH:HISTSH_START

- grant `HISTSH$USER` to each user who wants it (above).

`[HISTSH]README.VMS` covers trying it without these steps, the history file, and
removing it. `PRODUCT REMOVE HISTSH` deassigns the logical names; users' history
files are kept.

## Building and testing

- Linux: `make test` builds the pure relay core (`src/core/`) with
  `-std=c99 -pedantic -Wall -Wextra -Werror` and runs its unit tests.
- OpenVMS: `@BUILD_VMS` builds `HISTSH.EXE` with VSI C. The on-node tests use PTYDRIVE,
  which plays the user's terminal on a pseudo-terminal ([tests/vms/README.md](tests/vms/README.md)).

## Part of the OpenVMS ports family

See [openvms.issinoho.com](https://openvms.issinoho.com) for the other projects.

## Licence

MIT, see [LICENSE](LICENSE).
