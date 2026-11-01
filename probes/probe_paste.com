$! probe_paste.com - build and run probe_paste.c; P2 is the option (N, H, R, HR, U).
$ set noon
$ cc/warnings=disable=(questcompare) probe_paste.c
$ if $severity .ne. 1 then exit
$ link probe_paste
$ if $severity .ne. 1 then exit
$ probe_paste := $'f$environment("default")'probe_paste
$ probe_paste 'p2'
$ write sys$output "probe exit status ", $status
