/* t_docmd.c - does LIB$DO_COMMAND run RECALL/OUTPUT at prompt level? */
#include <descrip.h>
#include <lib$routines.h>
int main(void)
{
    $DESCRIPTOR(cmd, "RECALL/OUTPUT=T_DOCMD.LIS");
    return lib$do_command(&cmd);    /* does not return if it works */
}
