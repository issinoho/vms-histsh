$! test_c2_shared.com P1 - one PTYDRIVE session on the shared file C2_SHARED.LIS.
$! P1 = 1 or 2 (run both at once), CHECK (afterwards), or RESET (first).
$ set noon
$ here = f$environment("DEFAULT")
$ dcl_dir = here - "]" + ".DCL]"
$ build_dir = here - "]" + ".BUILD]"
$ define histsh 'dcl_dir','build_dir'
$ define histsh$history 'here'C2_SHARED.LIS
$ if p1 .eqs. "RESET"
$ then
$   if f$search("C2_SHARED.LIS") .nes. "" then delete/nolog C2_SHARED.LIS;*
$   exit
$ endif
$ drive := $'here'PTYDRIVE.EXE
$ if p1 .nes. "CHECK" then drive c2_sess'p1'.txt
$ if p1 .nes. "CHECK" then exit
$ define/user sys$output nl:
$ drive c2_check.txt
$ if f$search("C2_SHARED.LIS") .eqs. ""
$ then
$   write sys$output "no C2_SHARED.LIS: nothing was saved"
$   exit
$ endif
$ open/read h C2_SHARED.LIS
$ n = 0
$ na = 0
$ nb = 0
$ rd: read/end=rd_done h line
$   n = n + 1
$   if f$extract(0, 13, line) .eqs. "SHOW SYMBOL A" then na = na + 1
$   if f$extract(0, 13, line) .eqs. "SHOW SYMBOL B" then nb = nb + 1
$   if n .lt. 1000 then goto rd
$ rd_done: close h
$ write sys$output "lines ", n, ": A ", na, ", B ", nb, " (want 30: 15 and 15)"
$ directory/protection C2_SHARED.LIS;*
