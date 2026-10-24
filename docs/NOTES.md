# Notes: platform behaviour observed

## N1. RECALL /OUTPUT writes nothing outside an interactive DCL (2026-10-24, x86)

In a NETWORK-mode procedure (`vms.sh run`), `RECALL/ERASE/INPUT=f` and
`RECALL/OUTPUT=g` both return %X00030001, but no `g` is created: the process has no
recall buffer. Recall has to be probed in an interactive DCL, either a real session or
one spawned on a pseudo-terminal (`probes/probe_ptd.c`).

## N2. A probe loop must never depend on an earlier step having worked (2026-10-24)

The first `recall_roundtrip.com` had `SET NOON` and a READ loop on the file RECALL
should have written. With no file, every OPEN and READ failed and the loop went round
for ever, writing errors: 10,690 log blocks in three minutes. Ending `vms.sh` (timeout)
ended ssh, **not** the VMS process. The user stopped PID 745 by hand. Probes now have no
unbounded loops: they check each step's status and give up.

## N3. Holding an identifier can be checked from DCL (2026-10-24, x86)

`SHOW PROCESS/RIGHTS/OUTPUT=scratch` followed by `SEARCH/NOOUTPUT/EXACT scratch " NAME "`
(the name padded with spaces) reports a held identifier correctly (tested with
NETWORK, INTERACTIVE and BATCH). It takes about 0.1 s and needs no privilege
(`probes/rights_check.com`).

## N4. DCL spawned on a pseudo-terminal is fully interactive (2026-10-24, x86 and IA64)

`probes/probe_ptd.c`: `PTD$CREATE` (VT100, ANSI, line editing) and `LIB$SPAWN /NOWAIT`
with input and output on the FTA device. The child reports `F$MODE() = INTERACTIVE`
and `TT_EDITING = TRUE`, even though the parent was a NETWORK job. Up-arrow and Ctrl/B
recall work, Ctrl/Y gives the usual reverse-video "Interrupt", and LOGOUT ends the child
(its completion AST fires). No privileges needed.

DCL redraws a recalled line as `<CR><ESC>[K<NUL><NUL><NUL>$ text`. Its own `$` prompt
arrives as `<CR><NUL>$ `; a parameter prompt (`_From: `) has no NUL. Interesting, but
not something to rely on (N6).

## N5. RECALL round trip keeps order (2026-10-24, x86 and IA64)

In that interactive child, `RECALL/OUTPUT=f` writes the buffer oldest first, one command
per record (corrected 2026-10-24: first read as newest first). `RECALL/ERASE/INPUT=f` restores it in the same order: RECALL/ALL matches
before and after, and up-arrow then gives the newest line. RECALL commands themselves
are never stored. Phase A works as planned.

## N6. PTD$ read-event ASTs never fire (2026-10-24, x86 and IA64)

`PTD$SET_EVENT_NOTIFICATION` accepts types 7–11 (START/MIDDLE/END_READ, ENABLE/
DISABLE_READ), but none was delivered: not at DCL's prompt, not at `_From:`.
CHAR_CHANGED (5) and RESUME_OUTPUT (4) do fire (at startup, on `SET TERMINAL/WIDTH`,
at logout). BELL (1) doesn't; BEL arrives as data. So Phase C detects DCL's prompt with
a marker in the child's prompt, which HISTSH strips before output reaches the real
terminal, and uses CHAR_CHANGED to copy characteristic changes back to it.

## N7. RECALL works only at the prompt, never inside a procedure (2026-10-24, x86)

In an interactive DCL (`tests/vms/diag_a.txt`, `diag_b.txt` via PTYDRIVE):
- `@T_RECALL` running `RECALL/OUTPUT=f`: status %X00030001, **no file**. The same
  command typed at the `$` prompt writes it.
- `@T_LOAD` running `RECALL/ERASE/INPUT=seed`: status %X00030001, **buffer
  unchanged** (RECALL/ALL shows only the session's own commands; the seeded lines
  aren't there, and neither /ERASE nor /INPUT took effect).
- An image run at the prompt calling `LIB$DO_COMMAND("RECALL/OUTPUT=f")` **does**
  write the file: the command runs at prompt level after the image exits. But DCL then
  returns to the prompt, so nothing can follow it (no chaining into LOGOUT).
So neither SYLOGIN (load) nor a LOGOUT wrapper procedure (save) can reach the recall
buffer. Phase A as planned (D2) is not possible. Only something typing at the prompt,
the user or a front end on a pseudo-terminal, can load or save it.

## N8. A global symbol does replace the LOGOUT verb, with qualifiers (2026-10-24, x86)

`LO*GOUT :== ...` is substituted for `LOGOUT`, `LOGOUT/BRIEF` and `LO`, and
`DELETE/SYMBOL/GLOBAL LOGOUT` removes the abbreviated symbol.

## N9. A prompt can carry an escape-sequence marker (2026-10-24, x86)

`tests/vms/diag_prompt.txt`: `SET PROMPT="<ESC>[?7777h$ "` is accepted, and so are
prompts of at least 42 characters (F$ENVIRONMENT("PROMPT") reported 42). The marker
arrives in DCL's output as:
- top-level prompt: `<CR><NUL><ESC>[?7777h$ ` (a NUL straight before the marker);
- continuation prompt (after `-`): `<CR>_<ESC>[?7777h$ ` (an underscore before it);
- parameter prompt (`COPY` → `_From: `): no marker.
So the byte before the marker tells a top-level prompt from a continuation.
DCL redraws the prompt (marker included) on up-arrow, Ctrl/U and Ctrl/R, so a
marker seen while the user is already at the prompt is a redraw, not a new prompt.

## N10. A fresh prompt follows a line feed; redraws don't (2026-10-24, x86)

In the first HISTSH end-to-end run, up-arrows and Return sent in one burst made DCL
redraw the line (`<NUL><NUL><NUL>` + marker) *after* the Return, and HISTSH took the
redraw for the next prompt: the hidden save ran early and showed on the screen, and its
temporary file was left behind. Every fresh top-level prompt seen so far follows a line
feed: `<LF><CR><NUL>` + marker, or `<LF><NUL>` + marker (first prompt, after
Ctrl/Y "Interrupt"). Redraws have `<NUL><NUL><NUL>` or `<ESC>[K<CR><NUL>` before the
marker. hs_relay now requires the line feed (unit test `burst_redraw_while_running`).

## N11. A spawned DCL doesn't inherit the parent's prompt (2026-10-24, user report)

Trying HISTSH, the user saw the prompt fall back to `$ `: both nodes set it in SYLOGIN
(`X86VMS::`, `Myi64::`), and the child was spawned with marker + `$ `. HISTSH_START.COM
now sets the local symbol `HISTSH_PROMPT = F$ENVIRONMENT("PROMPT")`, and HISTSH reads it
with LIB$GET_SYMBOL (an exact string, so no quoting) and spawns with marker + that.
Verified on x86 and IA64 with tests/vms/c1_prompt.txt (`TESTNODE::`, and
`_TESTNODE::` on a continuation line).
