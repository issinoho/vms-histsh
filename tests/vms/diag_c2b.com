$ set noon
$ here = f$environment("DEFAULT")
$ drive := $'here'PTYDRIVE.EXE
$ drive diag_c2b.txt
