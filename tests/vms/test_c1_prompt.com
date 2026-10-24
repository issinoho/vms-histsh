$! test_c1.com - HISTSH C1 end to end, nested in PTYDRIVE (the "user's terminal").
$! Uses a test history file; the user's own SYS$LOGIN history is never touched.
$ set noon
$ here = f$environment("DEFAULT")
$ build = here - "]" + ".BUILD]"
$ define histsh 'here','build'
$ define histsh$history 'here'C1_HISTORY.LIS
$ if f$search("C1_HISTORY.LIS") .nes. "" then delete/nolog C1_HISTORY.LIS;*
$ drive := $'here'PTYDRIVE.EXE
$ drive c1_prompt.txt
$ write sys$output "==== left over"
$ directory C1_HISTORY.*
