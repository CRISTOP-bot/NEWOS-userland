/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/kill/kill_main.c
 * Description: -9 accepted (every kill here is SIGKILL-strength); @<file>
 *              reads the pid from a pidfile (OpenRC services record theirs
 *              at /run/<service>.pid)
 * Added: 2026-10-17
 */
#include "../../lib/nshlib.h"

/* First decimal number of a pidfile, or -1. */
static int pid_from_file(const char *path)
{
    char buf[32];
    long fd = sys_open(path, O_READ);
    long n;

    if (fd < 0)
        return -1;
    n = sys_read((int)fd, buf, (long)sizeof(buf) - 1);
    sys_close((int)fd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    {
        long i = 0;

        while (i < n && (buf[i] == ' ' || buf[i] == '\t'))
            i++;
        if (i >= n || buf[i] < '0' || buf[i] > '9')
            return -1;
        return natoi(buf + i);
    }
}

int main(int argc, char **argv)
{
    int rc = 0, i = 1;

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == '9' &&
        argv[1][2] == '\0')
        i = 2;   /* -9 accepted (every kill here is SIGKILL-strength) */
    if (i >= argc)
        return fail("kill", "missing pid");
    for (; i < argc; i++) {
        int pid;

        if (argv[i][0] == '@') {
            pid = pid_from_file(argv[i] + 1);
            if (pid <= 0) {
                fputf(1, "kill: %s: no pid in file\n", argv[i] + 1);
                rc = 1;
                continue;
            }
        } else {
            pid = natoi(argv[i]);
        }
        if (pid <= 0 || sys_kill(pid) != 0) {
            fputf(1, "kill: (%s) no such process\n", argv[i]);
            rc = 1;
        }
    }
    return rc;
}
