$! try_histsh.com - try HISTSH in the current session, with no system changes.
$! Starts a HISTSH session inside this one; LOGOUT in it returns here.
$! History goes to SYS$LOGIN:HISTSH_HISTORY.LIS, as it would for real.
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + -
         f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ build = here - "]" + ".BUILD]"
$ define/nolog histsh 'here','build'
$ histsh_prompt = f$environment("PROMPT")     ! HISTSH gives the child this prompt
$ run histsh:histsh
$ deassign histsh
