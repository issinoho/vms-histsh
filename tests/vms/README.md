# On-node tests

`PTYDRIVE` (`ptydrive.c`) plays the user's terminal: it spawns DCL on a
pseudo-terminal and types a script at it. `test_c1.com` runs HISTSH inside that DCL,
so the transcript is what a user's screen would show. It uses its own history file
(`HISTSH$HISTORY`), never the user's.

    tools/vms.sh x86 dcl '@build_vms' 'CC/PREFIX=ALL PTYDRIVE.C' 'LINK PTYDRIVE'
    tools/vms.sh x86 run tests/vms/test_c1.com

(Upload `src/`, `dcl/histsh_child.com`, `build_vms.com` and this directory's files to
the work directory first.) The `diag_*` and `phase_a_*` scripts are the probes behind
NOTES N7–N9.
