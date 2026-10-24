$! test_c2.com - C2a on the node: shared history, nested in PTYDRIVE.
$! Uses its own history file (C2_HISTORY.LIS), seeded with 300 old commands.
$ set noon
$ here = f$environment("DEFAULT")
$ dcl_dir = here - "]" + ".DCL]"
$ build_dir = here - "]" + ".BUILD]"
$ define histsh 'dcl_dir','build_dir'           ! this build, not the installed kit
$ show logical histsh
$ define histsh$history 'here'C2_HISTORY.LIS
$ define histsh$size 5000
$ if f$search("C2_HISTORY.LIS") .nes. "" then delete/nolog C2_HISTORY.LIS;*
$ open/write h C2_HISTORY.LIS
$ i = 1
$ seed: write h "SHOW SYMBOL Z" + f$fao("!3ZL", i)
$   i = i + 1
$   if i .le. 300 then goto seed
$ close h
$ drive := $'here'PTYDRIVE.EXE
$ drive c2_single.txt
$ write sys$output "==== the file afterwards: last lines, count, protection"
$ open/read/error=no_file h C2_HISTORY.LIS
$ n = 0
$ cnt: read/end=cnt_done h line
$   n = n + 1
$   if n .gt. 300 then write sys$output "  ", line
$   if n .lt. 1000 then goto cnt
$ cnt_done: close h
$ write sys$output "lines: ", n
$ goto listed
$ no_file:
$ write sys$output "C2_HISTORY.LIS could not be opened: ", f$message($status)
$ listed:
$ directory/protection C2_HISTORY.*;*
