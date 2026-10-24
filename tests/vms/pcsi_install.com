$! pcsi_install.com - replace the hand install with the PCSI kit, and verify.
$! Approved by the user 2026-10-24 for both nodes. Leaves HISTSH installed by
$! PCSI; the identifier, SYLOGIN and SYSTARTUP lines and users' history are
$! untouched. Run from the work directory after tools/kit.sh.
$ set noon
$ old_privs = f$setprv("SYSPRV,SYSNAM,CMKRNL,SYSLCK,BYPASS,WORLD")
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ base = "I64VMS"
$ if arch .eqs. "X86_64" then base = "X86VMS"
$ kitdir = f$environment("DEFAULT") - "]" + ".KIT_''arch']"
$!
$ write sys$output "=== remove the hand-installed copies (not PCSI's)"
$ if f$search("SYS$COMMON:[HISTSH]*.*;*") .nes. "" then delete/log SYS$COMMON:[HISTSH]*.*;*
$ if f$search("SYS$COMMON:[SYS$STARTUP]HISTSH$STARTUP.COM;*") .nes. "" then -
     delete/log SYS$COMMON:[SYS$STARTUP]HISTSH$STARTUP.COM;*
$!
$ write sys$output "=== INSTALL from ", kitdir
$ product install HISTSH /producer=ISSINOHO /base_system='base' /source='kitdir' /options=noconfirm /log
$ write sys$output "=== install status ", $status
$ product show product HISTSH /producer=ISSINOHO
$ write sys$output "=== VERIFY"
$ show logical/system HISTSH*
$ directory/protection/owner/size HISTSH:,SYS$STARTUP:HISTSH$STARTUP.COM
$ histsh_v := $HISTSH:HISTSH.EXE
$ histsh_v "-v"
$ dummy = f$setprv(old_privs)
