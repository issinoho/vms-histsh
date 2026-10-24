$ set noon
$ here = f$environment("DEFAULT")
$ define histsh$history 'here'REHEARSE_HISTORY.LIS
$ if f$search("REHEARSE_HISTORY.LIS") .nes. "" then delete/nolog REHEARSE_HISTORY.LIS;*
$ drive := $'here'PTYDRIVE.EXE
$ drive rehearse.txt
$ write sys$output "==== saved"
$ type REHEARSE_HISTORY.LIS
$ delete/nolog REHEARSE_HISTORY.LIS;*
