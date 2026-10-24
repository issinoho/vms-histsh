$! Generate a PTYDRIVE script that types 260 distinct commands, then RECALL/OUTPUT.
$ open/write s diag_c2_fill.txt
$! Two commands per line keeps it under PTYDRIVE's 200-step cap and within typeahead.
$ i = 1
$ loop:
$   j = i + 1
$   write s "0.2 cmds ''i' | SHOW SYMBOL C''i'\rSHOW SYMBOL C''j'\r"
$   i = i + 2
$   if i .le. 260 then goto loop
$ write s "2 save | RECALL/OUTPUT=C2_FILL.LIS\r"
$ write s "2 logout | LOGOUT\r"
$ close s
$ here = f$environment("DEFAULT")
$ drive := $'here'PTYDRIVE.EXE
$ define/user sys$output nl:
$ drive diag_c2_fill.txt
$ open/read f C2_FILL.LIS
$ n = 0
$ rd: read/end=rd_done f line
$   n = n + 1
$   if n .eq. 1 then write sys$output "first kept: ", line
$   last = line
$   if n .lt. 400 then goto rd
$ rd_done: close f
$ write sys$output "lines kept: ", n, "  last: ", last
$ delete/nolog C2_FILL.LIS;*, diag_c2_fill.txt;*
