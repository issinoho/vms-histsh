$! pcsi_upgrade.com - install a newer HISTSH kit over the installed one, and verify.
$! Approved by the user 2026-10-24 (0.1.0 -> 0.2.0, both nodes). Run from the work
$! directory after tools/kit.sh. Sessions already running keep their old image.
$ set noon
$ old_privs = f$setprv("SYSPRV,SYSNAM,CMKRNL,SYSLCK,BYPASS,WORLD")
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ base = "I64VMS"
$ if arch .eqs. "X86_64" then base = "X86VMS"
$ kitdir = f$environment("DEFAULT") - "]" + ".KIT_''arch']"
$ write sys$output "=== before"
$ product show product HISTSH /producer=ISSINOHO
$ write sys$output "=== INSTALL from ", kitdir
$ product install HISTSH /producer=ISSINOHO /base_system='base' /source='kitdir' /options=noconfirm
$ write sys$output "=== install status ", $status
$ product show product HISTSH /producer=ISSINOHO
$ write sys$output "=== VERIFY"
$ show logical/system HISTSH*
$ directory/protection/owner/size HISTSH:,SYS$STARTUP:HISTSH$STARTUP.COM
$ histsh_v := $HISTSH:HISTSH.EXE
$ histsh_v "-v"
$ dummy = f$setprv(old_privs)
