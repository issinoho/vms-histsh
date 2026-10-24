$ recall/erase/input=SEED.LIS
$ write sys$output "loaded in procedure, status ", $status
