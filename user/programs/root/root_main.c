/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/root/root_main.c
 * Description: root: report the root account and home directory.
 * Added: 2026-11-24
 */
#include "../../lib/nshlib.h"

/* NEWOS boots single-user as uid 0, so "root" is the only account. This
 * tool prints the real root entry from /etc/passwd (never a hardcoded
 * string) and, with -h, changes the shell's view of HOME via sys_chdir.
 * The shell itself is a separate process, so chdir here affects only the
 * child; use `cd /root` in the shell to change its cwd. */

static int show_root(void)
{
    char buf[256];
    long n, fd = sys_open("/etc/passwd", O_READ);
    long i = 0;

    if (fd < 0)
        return fail("root", "cannot read /etc/passwd");
    n = sys_read((int)fd, buf, sizeof(buf) - 1);
    sys_close((int)fd);
    if (n <= 0)
        return fail("root", "empty /etc/passwd");
    buf[n] = '\0';

    /* First line is the root entry in NEWOS's passwd. */
    while (i < n && buf[i] != ':' && buf[i] != '\n') {
        nputc(buf[i]);
        i++;
    }
    nputc('\n');
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 1)
        return show_root();
    if (argc == 2 && nstrcmp(argv[1], "-h") == 0) {
        putf("root home: /root\n");
        return 0;
    }
    return fail("root", "usage: root [-h]");
}
