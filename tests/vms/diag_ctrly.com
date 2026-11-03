$! diag_ctrly.com - Ctrl/Y into an ON CONTROL_Y loop, with and without HISTSH.
$ set noon
$ here = f$environment("DEFAULT")
$ dcl_dir = here - "]" + ".DCL]"
$ build_dir = here - "]" + ".BUILD]"
$ define histsh 'dcl_dir','build_dir'
$ define histsh$history 'here'DIAG_CTRLY.LIS
$ if f$search("DIAG_CTRLY.LIS") .nes. "" then delete/nolog DIAG_CTRLY.LIS;*
$ define histsh$trace 'here'DIAG_CTRLY.TRACE
$ drive := $'here'PTYDRIVE.EXE
$ drive diag_ctrly.txt
$ deassign histsh$trace
$ if f$search("DIAG_CTRLY.TRACE") .nes. "" then type DIAG_CTRLY.TRACE
$ if f$search("DIAG_CTRLY.TRACE") .nes. "" then delete/nolog DIAG_CTRLY.TRACE;*
$ if f$search("DIAG_CTRLY.LIS") .nes. "" then delete/nolog DIAG_CTRLY.LIS;*
