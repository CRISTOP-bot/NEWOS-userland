/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/sys/time.h
 * Description: Present for portability; NEWOS has no gettimeofday yet. Ports must
 * Added: 2026-10-17
 */
#ifndef NEWOS_LIBC_SYS_TIME_H
#define NEWOS_LIBC_SYS_TIME_H

/* Present for portability; NEWOS has no gettimeofday yet. Ports must
 * use time() (uptime seconds) instead. */

struct timeval {
    long tv_sec;
    long tv_usec;
};

#endif
