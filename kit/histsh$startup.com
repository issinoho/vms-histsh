$! HISTSH$STARTUP.COM - system startup for HISTSH (command history across sessions)
$!
$! Installed into SYS$STARTUP. Defines the system logical names HISTSH$ROOT
$! (the installed [HISTSH] directory) and HISTSH (its top level, where
$! HISTSH.EXE and the procedures are). To run it at every boot, add it to
$! SYS$MANAGER:SYSTARTUP_VMS.COM:
$!
$!     $ @SYS$STARTUP:HISTSH$STARTUP.COM
$!
$! P1 = "INSTALL": also print the post-installation tasks (PCSI runs it so).
$! P1 = "REMOVE":  deassign the logical names instead (PCSI runs it so at removal).
$!
$! Then, as the LAST line of SYS$MANAGER:SYLOGIN.COM:
$!     $ IF F$SEARCH("HISTSH:HISTSH_START.COM") .NES. "" THEN @HISTSH:HISTSH_START
$! and switch users on with AUTHORIZE:
$!     UAF> ADD/IDENTIFIER HISTSH$USER
$!     UAF> GRANT/IDENTIFIER HISTSH$USER username
$!
$ set noon
$ mode = f$edit(p1, "UPCASE")
$ if mode .eqs. "REMOVE"
$ then
$   if f$trnlnm("HISTSH", "LNM$SYSTEM_TABLE") .nes. "" then -
        deassign/system/executive_mode HISTSH
$   if f$trnlnm("HISTSH$ROOT", "LNM$SYSTEM_TABLE") .nes. "" then -
        deassign/system/executive_mode HISTSH$ROOT
$   exit 1
$ endif
$!
$! This procedure sits in <destination>[SYS$STARTUP]; the product is in
$! <destination>[HISTSH]. Rooted logicals need the physical form (as GREP$STARTUP).
$ proc = f$environment("PROCEDURE")
$ dev = f$parse(proc,,,"DEVICE","NO_CONCEAL")
$ dir = f$edit(f$parse(proc,,,"DIRECTORY","NO_CONCEAL"), "UPCASE") - "]["
$ root = dir - "SYS$STARTUP]" + "HISTSH.]"
$ if root .eqs. dir + "HISTSH.]"
$ then
$   write sys$error "HISTSH$STARTUP: expected to be in a [SYS$STARTUP] directory, not ''dir'"
$   exit 44
$ endif
$ root = root - ".000000"
$ define/system/executive_mode/translation_attributes=concealed HISTSH$ROOT 'dev''root'
$ define/system/executive_mode HISTSH HISTSH$ROOT:[000000]
$ if f$search("HISTSH:HISTSH.EXE") .eqs. ""
$ then
$   write sys$error "HISTSH$STARTUP: HISTSH.EXE not found under ''dev'''root'"
$   exit 44
$ endif
$ if mode .nes. "INSTALL" then exit 1
$ say = "write sys$output"
$ say ""
$ say "    Post-installation tasks for HISTSH"
$ say ""
$ say "    At system startup: to define HISTSH$ROOT and HISTSH at every boot, add to"
$ say "    SYS$MANAGER:SYSTARTUP_VMS.COM:"
$ say "    $ @SYS$STARTUP:HISTSH$STARTUP.COM"
$ say "    At login: add as the LAST line of SYS$MANAGER:SYLOGIN.COM:"
$ say "    $ IF F$SEARCH(""HISTSH:HISTSH_START.COM"") .NES. """" THEN @HISTSH:HISTSH_START"
$ say "    For each user who wants it, with AUTHORIZE:"
$ say "    UAF> ADD/IDENTIFIER HISTSH$USER              ! once"
$ say "    UAF> GRANT/IDENTIFIER HISTSH$USER username"
$ say "    It takes effect at the user's next login."
$ say ""
$ say "    PRODUCT REMOVE HISTSH removes the product and deassigns the logical names."
$ say "    Users' history files (SYS$LOGIN:HISTSH_HISTORY.LIS) are kept."
$ say ""
$ exit 1
