/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/signal.c
 * Description: accepted and ignored: no delivery exists yet
 * Added: 2026-10-17
 */
#include "../include/signal.h"

sighandler_t signal(int sig, sighandler_t fn)
{
    (void)sig;
    (void)fn;
    return 0;   /* accepted and ignored: no delivery exists yet */
}

int raise(int sig)
{
    (void)sig;
    return -1;
}
