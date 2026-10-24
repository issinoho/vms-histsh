$! HISTSH_CHILD.COM - first command of the child DCL that HISTSH runs.
$!
$! HISTSH types it at the child's first prompt. It runs the user's LOGIN.COM (a
$! spawned DCL doesn't), then puts HISTSH's marker back on whatever prompt
$! LOGIN.COM chose (HISTSH_REMARK.COM), because HISTSH finds DCL's prompt by it.
$!
$ set noon
$ if f$search("SYS$LOGIN:LOGIN.COM") .nes. "" then @SYS$LOGIN:LOGIN.COM
$ @HISTSH:HISTSH_REMARK                     ! the marker on whatever prompt it chose
