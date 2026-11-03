$! HISTSH_CHILD.COM - first command of the child DCL that HISTSH runs.
$!
$! HISTSH types it at the child's first prompt. It runs the user's LOGIN.COM (a
$! spawned DCL doesn't), then puts HISTSH's marker back on whatever prompt
$! LOGIN.COM chose (HISTSH_REMARK.COM), because HISTSH finds DCL's prompt by it.
$!
$ set noon
$! HISTSH turned Ctrl/Y and Ctrl/T off for itself before spawning us, and we
$! inherited that: put back what the session had (NOTES N20).
$ histsh_ctl = f$trnlnm("HISTSH$CONTROL")
$ if histsh_ctl .nes. "" then set control=('histsh_ctl')
$ delete/symbol/local histsh_ctl
$ if f$search("SYS$LOGIN:LOGIN.COM") .nes. "" then @SYS$LOGIN:LOGIN.COM
$ @HISTSH:HISTSH_REMARK                     ! the marker on whatever prompt it chose
