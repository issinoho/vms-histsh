$ set noon
$ here = f$environment("DEFAULT")
$ drive := $'here'PTYDRIVE.EXE
$ drive diag_prompt.txt
