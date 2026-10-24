/* probe_append.c - can two sessions append to one history file at once?
   "PROBE_APPEND" runs two copies of itself as subprocesses ("PROBE_APPEND A",
   "... B"), each appending 300 lines by open-append-close, retrying while the file
   is locked. Then it counts the lines: 600 intact ones means it works. Bounded:
   each writer gives up after 300 lines or 200 failed opens in a row, and the
   parent waits at most 60 s. */
#define __NEW_STARLET 1
#include <stdio.h>
#include <string.h>
#include <descrip.h>
#include <lib$routines.h>
#include <starlet.h>

#define FILE_NAME "PROBE_APPEND.LIS"
#define LINES 300

static void wait_ms(unsigned int ms)
{
    float s = (float) ms / 1000.0f;
    int type = 4;                       /* LIB$K_IEEE_S (steward NOTES) */
    lib$wait(&s, 0, &type);
}

static int writer(const char *who)
{
    int i, fails = 0, retries = 0;
    for (i = 0; i < LINES; i++) {
        FILE *f;
        while ((f = fopen(FILE_NAME, "a")) == NULL) {
            if (++fails > 200) { printf("%s: gave up at line %d\n", who, i); return 0; }
            retries++;
            wait_ms(10);
        }
        fails = 0;
        fprintf(f, "%s %04d SOME COMMAND TEXT TO MAKE THE RECORD LONGER\n", who, i);
        fclose(f);
    }
    printf("%s: done, %d retries\n", who, retries);
    return 1;
}

int main(int argc, char **argv)
{
    static char ca[64], cb[64];
    struct dsc$descriptor_s da, db;
    unsigned int flags = 1, sa = 0, sb = 0, pa, pb, st;
    int waited = 0, a = 0, b = 0, other = 0, bad = 0;
    char line[256];
    FILE *f;

    if (argc == 2) return writer(argv[1]);

    remove(FILE_NAME);
    f = fopen(FILE_NAME, "w"); if (f) fclose(f);
    strcpy(ca, "MCR SYS$DISK:[]PROBE_APPEND A"); strcpy(cb, "MCR SYS$DISK:[]PROBE_APPEND B");
    da.dsc$w_length = (unsigned short) strlen(ca); da.dsc$b_dtype = DSC$K_DTYPE_T;
    da.dsc$b_class = DSC$K_CLASS_S; da.dsc$a_pointer = ca;
    db = da; db.dsc$w_length = (unsigned short) strlen(cb); db.dsc$a_pointer = cb;
    st = lib$spawn(&da, 0, 0, &flags, 0, &pa, &sa, 0, 0, 0, 0, 0, 0);
    if (!(st & 1)) { printf("spawn A failed %08X\n", st); return st; }
    st = lib$spawn(&db, 0, 0, &flags, 0, &pb, &sb, 0, 0, 0, 0, 0, 0);
    if (!(st & 1)) { printf("spawn B failed %08X\n", st); return st; }
    while ((sa == 0 || sb == 0) && waited < 600) { wait_ms(100); waited++; }
    printf("writers ended: A %08X  B %08X  after %d ms\n", sa, sb, waited * 100);

    f = fopen(FILE_NAME, "r");
    if (!f) { printf("cannot read back\n"); return 1; }
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, "SOME COMMAND TEXT TO MAKE THE RECORD LONGER") == NULL) bad++;
        else if (line[0] == 'A') a++;
        else if (line[0] == 'B') b++;
        else other++;
    }
    fclose(f);
    printf("lines: A %d, B %d, other %d, damaged %d (want %d, %d, 0, 0)\n", a, b, other, bad, LINES, LINES);
    remove(FILE_NAME);
    return 1;
}
