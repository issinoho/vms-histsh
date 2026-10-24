$! rights_check.com - can DCL tell whether this process holds an identifier?
$! Uses environmental identifiers (NETWORK/BATCH/INTERACTIVE) as stand-ins.
$ set noon
$ tmp = "SYS$SCRATCH:RC_" + f$getjpi("","PID") + ".TMP"
$ t0 = f$cvtime(,,"HUNDREDTH") + 100*f$cvtime(,,"SECOND")
$ show process/rights/output='tmp'
$ type 'tmp'
$ foreach_id = "NETWORK,INTERACTIVE,BATCH"
$ i = 0
$ loop: id = f$element(i, ",", foreach_id)
$   if id .eqs. "," then goto done
$   search/nooutput/exact 'tmp' " ''id' "
$   held = $severity .eq. 1
$   write sys$output id, " held: ", held
$   i = i + 1
$   goto loop
$ done:
$ t1 = f$cvtime(,,"HUNDREDTH") + 100*f$cvtime(,,"SECOND")
$ write sys$output "elapsed hundredths: ", t1 - t0
$ delete 'tmp';*
$ write sys$output "mode: ", f$mode()
