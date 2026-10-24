# CLAUDE.md

Working notes for vms-histsh: bash-style command history for OpenVMS that persists
across sessions. `docs/PLAN.md` is the phased plan; `docs/DECISIONS.md` records every
decision and the alternatives considered; `docs/NOTES.md` records platform surprises.
Read them before starting work. Work phase by phase, and stop and ask at each phase exit.

Green-field code, not a port.

## Ground rules (from the sibling vms-* repos)

- **Use `tools/vms.sh`** for remote work. Never raw `ssh host cmd`, never `WAIT` over
  ssh. Run long jobs with `run_in_background`.
- **Committed files must not contain real node details** (they live in the git-ignored
  `tools/nodes.conf`), credentials or passwords. Write `<x86-host>` instead.
- **Nothing destructive or system-wide on the nodes without asking**: no SYLOGIN edits,
  no PRODUCT INSTALL, no rights identifiers created or granted, no AUTHORIZE changes,
  no system logical names.
- Prefer a 20-line probe in `probes/` over speculation. Don't claim something works
  until a test on the node shows it.

## Hard constraints

- **C99, VSI C, 32-bit pointers.** No C11, no VLAs, no anonymous unions. The Linux
  build uses `-std=c99 -pedantic -Wall -Wextra`.
- **`src/core/` does no I/O**: the line editor, history ring, prompt-marker scanner and
  echo matcher are pure and unit-tested on Linux. `src/plat/vms/` holds the PTD$, QIO and
  RMS code; `src/plat/stub/` holds the Linux fake.
- **Never record what DCL didn't read at its own prompt.** Passwords and replies to
  application prompts must never reach the history file.

## Commits

Commit in logical steps with messages that explain why. Record what was verified and how.
Don't push unless the user asks.
