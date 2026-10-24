# vms-histsh: bash-style persistent command history for OpenVMS

## Context

DCL's up-arrow recall only covers the current session. The goal is bash-like behaviour:
up-arrow walks back through a user's commands from previous sessions, and everything
else goes to DCL unchanged. Users switch it on through AUTHORIZE.

What exploration found (read-only HELP on both nodes, 2026-10-24):
- `RECALL /INPUT=file` and `RECALL /OUTPUT=file` exist on x86 and IA64. The recall
  buffer holds up to 254 commands. So DCL can already save and load its own history;
  the missing piece is a hook at session end.
- AUTHORIZE `/CLI=` can't host a proxy. A CLI image runs in supervisor mode and has to
  *be* the command interpreter (only DCL and MCR exist), so a proxy can't fill that slot.
  AUTHORIZE turns it on another way: by granting a rights identifier (preferred) or by
  setting `/LGICMD`.

Decision (user, 2026-10-24): ship **A** (native recall, persisted) first as a quick win,
then build **C** (an rlwrap-style front end on a pseudo-terminal) as the full project.
New green-field repo `~/projects/vms-histsh`, following the vms-steward conventions
(C99, VSI C, `tools/vms.sh`, DECISIONS.md / NOTES.md, PCSI kit, family release process).

## Phase 0: scaffolding and probes (no system-wide changes)

Set up the repo by copying the vms-steward skeleton: `tools/vms.sh`,
`tools/nodes.conf.example` (git-ignored real file), `tools/kit.sh`, `CLAUDE.md` ground
rules, `docs/PLAN.md`, `docs/DECISIONS.md`, `docs/NOTES.md`, `probes/`.

Probes, each about 20 lines, run under my own account:
1. RECALL round trip: does `/OUTPUT` then `/ERASE/INPUT` keep order, keep the 254
   limit, and handle long lines and lines with quotes?
2. Turning it on: can a login procedure tell cheaply whether the user holds identifier
   `HISTSH$USER` (parse `SHOW PROCESS/RIGHTS`, or a tiny C check with `$FIND_HELD`)?
   Compare with `/LGICMD`, and record the choice in DECISIONS.md.
3. PTD$: `PTD$CREATE` an FTA device and `LIB$SPAWN /NOWAIT` with input and output on it.
   Is the child `F$MODE() = "INTERACTIVE"`? Do Ctrl/Y, Ctrl/T, EDT/TPU, `SET TERMINAL
   /INQUIRE` and broadcasts behave? Which privileges are needed (expected: none)?
4. Spotting the prompt: does `PTD$SET_EVENT_NOTIFICATION` report that a read has started?
   If not, use a marker in the child's `SET PROMPT` (a harmless SGR sequence) and check
   that it survives `SET PROMPT` changes made by the user's own LOGIN.COM.

## Phase A: native recall, persisted (DCL only)

- `HISTSH_LOGIN.COM` (called from a new SYLOGIN `sy_n` entry, interactive only): if the
  user holds the identifier, run `RECALL/ERASE/INPUT=SYS$LOGIN:HISTSH_HISTORY.LIS`
  (when the file exists), then `LO*GOUT :== @HISTSH:HISTSH_LOGOUT`.
- `HISTSH_LOGOUT.COM`: `RECALL/OUTPUT` to a temporary version, rename it over the
  history file (file protection O:RWED only), then the real `LOGOUT` with any
  qualifiers passed through (`'P1'`…).
- Limits, stated in the README: 254 commands; a dropped connection loses that session's
  commands; with concurrent sessions the last to log out wins; no Ctrl/R search.
- Deliver as a 0.1.0 kit (DCL only), installed and tested on x86 first, then IA64.

## Phase C: HISTSH front end (C)

The `HISTSH.EXE` loop:
1. Load the history file (shared read). Create a pseudo-terminal and copy the real
   terminal's characteristics to it (width, page, ANSI/DEC_CRT, EDIT, 8-bit).
2. Spawn DCL on it (`LIB$SPAWN /NOWAIT`, logical `HISTSH$CHILD` defined so the child
   never starts HISTSH again).
3. Pump bytes both ways with asynchronous QIOs and ASTs. **Passthrough mode** (an app
   or a DCL prompt for missing parameters is reading): send keystrokes raw, Ctrl/Y and
   Ctrl/T included.
4. **Prompt mode** (DCL is at its `$` prompt, per probe 4): edit the line locally.
   Left/right, Home/End (Ctrl/H, Ctrl/E), delete, insert/overstrike (Ctrl/A),
   up/down through history, Ctrl/R incremental search, Ctrl/U. On Return, send the
   line plus CR to the pseudo-terminal, swallow DCL's echo of that exact text, and append
   the line to the history file.
5. When the child exits (LOGOUT), HISTSH exits and the login procedure logs the parent
   out. If HISTSH fails to start, log a warning and fall back to Phase A.

Source layout (pure core with platform shims, as in vms-steward):
- `src/core/`: line editor state machine, history ring (dedupe consecutive lines, skip
  lines that start with a space, cap from logical `HISTSH$SIZE`, default 2000), prompt
  marker scanner, echo matcher. No I/O, unit-tested on Linux with
  `-std=c99 -pedantic -Wall -Wextra`.
- `src/plat/vms/`: PTD$, QIO/AST pump, RMS shared-append history file, spawn.
- `src/plat/stub/`: a fake for Linux tests (`openpty`, which lets the editor be tested
  against a real bash too).

History file: `SYS$LOGIN:HISTSH_HISTORY.DAT`. Every line is appended as it's entered,
opened for shared write, so sessions interleave like bash with `histappend`. Protection
O:RWED. The README warns that, as in bash, passwords typed on a command line get
recorded.

Known costs, documented in the README: each session uses two processes (the spawned DCL
shares the parent's quotas and counts against PRCLM), and SHOW PROCESS inside shows the
subprocess.

## Release

Follow [vms-family-release-process]: PCSI kit per architecture, producer ISSINOHO, built
on the node by `MAKE_KIT.COM`. Install-test on x86 first; **ask before every PRODUCT
INSTALL, every SYLOGIN edit (and commit it to vms-site-config), and before creating
`HISTSH$USER` or granting it**. Then a GitHub release in the sibling format, plus a
projects.json entry and a README row in vms-grep/site.

## Verification

- Linux: unit tests for the editor, history and echo matcher; an `openpty` harness
  scripting keystrokes against bash.
- Phase A on x86, with a test account the user names: log in, run commands, LOGOUT,
  log in again; up-arrow shows the earlier session. Repeat with two concurrent sessions
  and with a dropped SSH connection (expect the documented loss).
- Phase C on x86, then IA64, over SSH (port 2222) and TELNET: up-arrow across sessions;
  Ctrl/R; Ctrl/Y aborting `DIR/SIZE [...]`; EDT and TPU full-screen; `COPY` prompting
  for missing parameters (passthrough, not recorded); `SET PASSWORD` (not recorded);
  `SPAWN`; `@` procedures; quotes and apostrophe substitution exactly as in plain DCL;
  LOGOUT ending the session cleanly with no process left behind; two concurrent sessions
  appending to one file.
