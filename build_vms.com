$! build_vms.com - build HISTSH.EXE on OpenVMS (x86-64 or IA64), from clean.
$ set noon
$ set default 'f$parse(f$environment("PROCEDURE"),,,"DEVICE")''f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")'
$ if f$search("[.build]*.*") .nes. "" then delete/nolog [.build]*.*;*
$ if f$search("build.dir") .eqs. "" then create/directory [.build]
$! Under /STANDARD=C99 only ANSI names get the DECC$ prefix: ALL_ENTRIES maps
$! getpid, stat and the rest too (as in vms-steward and vmsguard).
$ cflags = "/STANDARD=C99/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/INCLUDE=([.src.core])"
$ cc'cflags'/OBJECT=[.build]hs_relay.obj [.src.core]hs_relay.c
$ if .not. $status then exit 44
$ cc'cflags'/OBJECT=[.build]histsh.obj [.src.plat.vms]histsh.c
$ if .not. $status then exit 44
$ cc'cflags'/OBJECT=[.build]hs_hist.obj [.src.core]hs_hist.c
$ if .not. $status then exit 44
$ link/executable=[.build]histsh.exe [.build]histsh.obj, hs_relay.obj, hs_hist.obj
$ if .not. $status then exit 44
$ write sys$output "build: [.build]histsh.exe"
