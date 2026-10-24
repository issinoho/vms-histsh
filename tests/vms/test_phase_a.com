$! test_phase_a.com - Phase A end to end in spawned interactive sessions.
$! Uses a test history file and NETWORK as a stand-in identifier, so the user's
$! own SYS$LOGIN and the real HISTSH$USER are never touched.
$ set noon
$ here = f$environment("DEFAULT")
$ cc ptydrive.c
$ if $severity .ne. 1 then exit
$ link ptydrive
$ if $severity .ne. 1 then exit
$ define histsh 'here'
$ define histsh$identifier NETWORK
$ define histsh$history 'here'TEST_HISTORY.LIS
$ if f$search("TEST_HISTORY.LIS") .nes. "" then delete/nolog TEST_HISTORY.LIS;*
$ drive := $'here'PTYDRIVE.EXE
$ drive phase_a_1.txt
$ write sys$output "==== history file after session 1"
$ directory/protection/size/versions TEST_HISTORY.LIS
$ type TEST_HISTORY.LIS
$ drive phase_a_2.txt
$ write sys$output "==== history file after session 2"
$ directory/versions TEST_HISTORY.LIS
$ type TEST_HISTORY.LIS
$ drive phase_a_3.txt
$ write sys$output "==== history file after session 3 (expect unchanged)"
$ directory/versions TEST_HISTORY.LIS
$ delete/nolog TEST_HISTORY.LIS;*
