$! HISTSH_CHILD.COM - first command of the child DCL that HISTSH runs.
$!
$! HISTSH types it at the child's first prompt. It runs the user's LOGIN.COM (a
$! spawned DCL doesn't), then puts HISTSH's marker back on whatever prompt
$! LOGIN.COM chose, because HISTSH finds DCL's prompt by that marker (docs/C1.md).
$!
$ set noon
$ if f$search("SYS$LOGIN:LOGIN.COM") .nes. "" then @SYS$LOGIN:LOGIN.COM
$ histsh_v = f$verify(0)
$ histsh_m = f$trnlnm("HISTSH$MARKER")
$ histsh_p = f$environment("PROMPT")
$ if histsh_m .eqs. "" .or. f$locate(histsh_m, histsh_p) .lt. f$length(histsh_p) -
     then goto histsh_done
$! Double any quotes so the prompt survives being put back inside quotes.
$ histsh_q = ""
$ histsh_i = 0
$ histsh_loop:
$   histsh_c = f$extract(histsh_i, 1, histsh_p)
$   if histsh_c .eqs. "" then goto histsh_set
$   if histsh_c .eqs. """" then histsh_c = """"""
$   histsh_q = histsh_q + histsh_c
$   histsh_i = histsh_i + 1
$   if histsh_i .lt. 255 then goto histsh_loop
$ histsh_set:
$ set prompt="''histsh_m'''histsh_q'"
$ histsh_done:
$ exit 1 + 0*f$verify(histsh_v)
