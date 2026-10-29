/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/sleep/sleep_main.c
 * Description: userland program entry for sleep
 * Added: 2026-10-17
 */
#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    int secs;

    if (argc != 2)
        return fail("sleep", "usage: sleep <seconds>");
    secs = natoi(argv[1]);
    if (secs < 0)
        return fail("sleep", "invalid interval");
    sys_sleep((u64)secs * 1000);
    return 0;
}
