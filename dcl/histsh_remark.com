$! HISTSH_REMARK.COM - put HISTSH's marker on both ends of DCL's prompt.
$!
$! HISTSH finds DCL's prompt by the marker (docs/C1.md). HISTSH_CHILD.COM runs this
$! after LOGIN.COM; HISTSH types it, hidden, after a SET PROMPT has removed the
$! marker (docs/C2.md, NOTES N18). A prompt that already has it is left alone.
$!
$ set noon
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
$ set prompt="''histsh_m'''histsh_q'''histsh_m'"     ! marker at both ends (N12)
$ histsh_done:
$ exit 1 + 0*f$verify(histsh_v)
