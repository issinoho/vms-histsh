$! install_node.com - put HISTSH in place by hand (until the PCSI kit), as the
$! user approved on 2026-10-24 (x86, then IA64): files in SYS$COMMON:[HISTSH], the startup in
$! SYS$STARTUP, the logicals defined, and the identifier granted to IAIN.
$! Run from the work directory after build_vms.com.
$ set noon
$ old_privs = f$setprv("SYSPRV,SYSNAM")
$ if .not. f$privilege("SYSPRV,SYSNAM") then exit 36    ! SS$_NOPRIV
$ if f$search("SYS$COMMON:[000000]HISTSH.DIR") .eqs. "" then -
     create/directory/owner=[1,4]/protection=(S:RWE,O:RWE,G:RE,W:RE) SYS$COMMON:[HISTSH]
$ copy/log [.build]histsh.exe SYS$COMMON:[HISTSH]
$ copy/log histsh_start.com, histsh_child.com SYS$COMMON:[HISTSH]   ! own line: COPY carries [.build] over
$ set file/protection=(S:RWED,O:RWED,G:RE,W:RE)/owner=[1,4] SYS$COMMON:[HISTSH]*.*;
$ purge/nolog SYS$COMMON:[HISTSH]
$ copy/log histsh$startup.com SYS$COMMON:[SYS$STARTUP]
$ set file/protection=(S:RWED,O:RWED,G:RE,W:RE)/owner=[1,4] SYS$COMMON:[SYS$STARTUP]HISTSH$STARTUP.COM;
$ @SYS$STARTUP:HISTSH$STARTUP
$ show logical/system HISTSH*
$ directory/protection/owner/size HISTSH:
$!
$ define/user sysuaf SYS$SYSTEM:SYSUAF.DAT
$ mcr authorize
show/identifier HISTSH$USER
add/identifier HISTSH$USER
grant/identifier HISTSH$USER IAIN
show/identifier HISTSH$USER
show/rights IAIN
$ dummy = f$setprv(old_privs)
