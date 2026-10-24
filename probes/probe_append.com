$ set noon
$ cc/prefix=all probe_append.c
$ if .not. $status then exit
$ link probe_append
$ if .not. $status then exit
$ run probe_append
