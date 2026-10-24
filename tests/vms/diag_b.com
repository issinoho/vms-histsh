$ set noon
$ here = f$environment("DEFAULT")
$ cc t_docmd.c
$ link t_docmd
$ drive := $'here'PTYDRIVE.EXE
$ drive diag_b.txt
