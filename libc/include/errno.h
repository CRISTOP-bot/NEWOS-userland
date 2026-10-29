/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/errno.h
 * Description: Minimal errno: single global (no threads yet), common codes.
 * Added: 2026-10-17
 */
#ifndef NEWOS_LIBC_ERRNO_H
#define NEWOS_LIBC_ERRNO_H

/* Minimal errno: single global (no threads yet), common codes. */

extern int errno;

/* Newlib libm compatibility; currently returns the process-wide errno. */
int *__errno(void);

#define EPERM 1
#define ENOENT 2
#define EIO 5
#define ENXIO 6
#define EBADF 9
#define EAGAIN 11
#define ENOMEM 12
#define EACCES 13
#define EFAULT 14
#define EBUSY 16
#define EEXIST 17
#define EXDEV 18
#define ENODEV 19
#define ENOTDIR 20
#define EISDIR 21
#define EINVAL 22
#define ENFILE 23
#define EMFILE 24
#define ENOTTY 25
#define ETXTBSY 26
#define EFBIG 27
#define ENOSPC 28
#define ESPIPE 29
#define EROFS 30
#define EMLINK 31
#define ERANGE 34
#define ENAMETOOLONG 36
#define ENOSYS 38
#define ENOTEMPTY 39
#define EILSEQ 84

const char *strerror(int e);

#endif
