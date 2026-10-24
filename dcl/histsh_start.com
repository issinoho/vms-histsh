$! HISTSH_START.COM - start HISTSH for users who hold HISTSH$USER.
$!
$! Call it as the LAST thing in SYLOGIN.COM, so the child DCL that HISTSH runs
$! inherits everything SYLOGIN set up:
$!     $ IF F$SEARCH("HISTSH:HISTSH_START.COM") .NES. "" THEN @HISTSH:HISTSH_START
$!
$! When the user's session (the child) logs out, this logs the parent out too. If
$! HISTSH can't start, the login carries on as plain DCL.
$!
$! Logicals (optional): HISTSH$IDENTIFIER (default HISTSH$USER),
$! HISTSH$HISTORY (default SYS$LOGIN:HISTSH_HISTORY.LIS).
$!
$ verify = 'f$verify(0)'
$ set noon
$ if f$mode() .nes. "INTERACTIVE" then goto done
$ if f$trnlnm("HISTSH$CHILD") .nes. "" then goto done       ! already inside HISTSH
$ if .not. f$getdvi("SYS$COMMAND", "TRM") then goto done
$ if f$search("HISTSH:HISTSH.EXE") .eqs. "" then goto done
$ id = f$trnlnm("HISTSH$IDENTIFIER")
$ if id .eqs. "" then id = "HISTSH$USER"
$ id = f$edit(id, "UPCASE,TRIM")
$!
$! Does this process hold the identifier? (NOTES N3) SHOW PROCESS/RIGHTS pads
$! each name with spaces, so " NAME " never matches a longer name.
$ tmp = "SYS$SCRATCH:HISTSH_" + f$getjpi("", "PID") + ".TMP"
$ show process/rights/output='tmp'
$ define/user sys$output nl:
$ define/user sys$error nl:
$ search/nooutput/exact 'tmp' " ''id' "
$ held = $severity .eq. 1
$ if f$search(tmp) .nes. "" then delete/nolog 'tmp';*
$ if .not. held then goto done
$!
$ histsh_image := $HISTSH:HISTSH.EXE
$ histsh_image
$ status = $status
$ if status then logout/brief
$ write sys$output "%HISTSH-W-NOTSTARTED, command history is off for this session"
$ write sys$output f$message(status)
$ done:
$ exit 1 + 0*f$verify(verify)
