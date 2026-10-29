/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/clear/clear_main.c
 * Description: userland program entry for clear
 * Added: 2026-10-17
 */
#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < 25; i++)
        nputc('\n');
    nputs("\x1B[2J\x1B[H");
    return 0;
}
