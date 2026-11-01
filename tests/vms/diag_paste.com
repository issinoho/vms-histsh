$! diag_paste.com - a 1.4 KB paste into CREATE inside HISTSH (NOTES N19).
$ set noon
$ here = f$environment("DEFAULT")
$ dcl_dir = here - "]" + ".DCL]"
$ build_dir = here - "]" + ".BUILD]"
$ define histsh 'dcl_dir','build_dir'
$ define histsh$history 'here'DIAG_PASTE.LIS
$ if f$search("DIAG_PASTE.LIS") .nes. "" then delete/nolog DIAG_PASTE.LIS;*
$ if f$search("DIAG_PASTE_OUT.TXT") .nes. "" then delete/nolog DIAG_PASTE_OUT.TXT;*
$ drive := $'here'PTYDRIVE.EXE
$ define histsh$trace 'here'DIAG_PASTE.TRACE
$ drive diag_paste.txt
$ deassign histsh$trace
$ type DIAG_PASTE.TRACE
$ delete/nolog DIAG_PASTE.TRACE;*,DIAG_PASTE_OUT.TXT;*
