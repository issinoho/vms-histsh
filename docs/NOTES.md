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

In that interactive child, `RECALL/OUTPUT=f` writes the buffer newest first, one command
per record. `RECALL/ERASE/INPUT=f` restores it in the same order: RECALL/ALL matches
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
