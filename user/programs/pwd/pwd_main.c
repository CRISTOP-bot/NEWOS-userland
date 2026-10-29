/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/pwd/pwd_main.c
 * Description: userland program entry for pwd
 * Added: 2026-10-17
 */
#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    static char buf[256];

    (void)argc;
    (void)argv;
    if (sys_getcwd(buf, sizeof(buf)) < 0)
        return fail("pwd", "cannot get working directory");
    nputs(buf);
    nputc('\n');
    return 0;
}
