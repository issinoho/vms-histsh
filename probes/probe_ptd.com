$! probe_ptd.com - build and run probe_ptd.c (bounded: see the source).
$ set noon
$ cc/warnings=disable=(questcompare) 'p1' probe_ptd.c
$ if $severity .ne. 1 then exit
$ link probe_ptd
$ if $severity .ne. 1 then exit
$ run probe_ptd
$ write sys$output "probe exit status ", $status
