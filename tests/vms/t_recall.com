$ recall/output=T_OUT.LIS
$ write sys$output "in procedure, status ", $status, " file: ", f$search("T_OUT.LIS")
