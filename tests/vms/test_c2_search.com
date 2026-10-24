$! test_c2_search.com - C2b (Ctrl/R, SET PROMPT) on the node, nested in PTYDRIVE.
$ set noon
$ here = f$environment("DEFAULT")
$ dcl_dir = here - "]" + ".DCL]"
$ build_dir = here - "]" + ".BUILD]"
$ define histsh 'dcl_dir','build_dir'
$ define histsh$history 'here'C2_SEARCH.LIS
$ if f$search("C2_SEARCH.LIS") .nes. "" then delete/nolog C2_SEARCH.LIS;*
$ drive := $'here'PTYDRIVE.EXE
$ drive c2_search.txt
