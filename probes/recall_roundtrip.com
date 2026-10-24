$! recall_roundtrip.com - does RECALL /INPUT and /OUTPUT work in this mode?
$! No loops: a failed step just reports its status (2026-10-24 runaway, NOTES.md).
$ set noon
$ write sys$output "mode: ", f$mode()
$ open/write f rr_in.lis
$ write f "SHOW TIME ! one"
$ write f "WRITE SYS$OUTPUT ""quoted ''F$TIME()'"""
$ write f "SHOW DEFAULT ! three"
$ close f
$ recall/erase/input=rr_in.lis
$ write sys$output "input status ", $status
$ recall/output=rr_out.lis
$ write sys$output "output status ", $status
$ if f$search("rr_out.lis") .nes. "" then type rr_out.lis
$ delete rr_in.lis;*
$ if f$search("rr_out.lis") .nes. "" then delete rr_out.lis;*
