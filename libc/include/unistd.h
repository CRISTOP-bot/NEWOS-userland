/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/unistd.h
 * Description: POSIX-ish facade over NEWOS syscalls. Values match the kernel VFS
 * Added: 2026-10-17
 */
#ifndef NEWOS_LIBC_UNISTD_H
#define NEWOS_LIBC_UNISTD_H

/* POSIX-ish facade over NEWOS syscalls. Values match the kernel VFS
 * flags (O_*), so no translation bugs: compare abi/syscall_abi.h. */

#include "stddef.h"

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

/* Permission bits accepted by access(). The NEWOS kernel has no permission
 * model, so SYS_ACCESS answers "does this path resolve?" and the mode is
 * carried for source compatibility only. */
#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 1
#endif

long read(int fd, void *buf, unsigned long len);
long write(int fd, const void *buf, unsigned long len);
int close(int fd);
long lseek(int fd, long off, int whence);
int access(const char *path, int mode);
/* NEWOS exposes the Linux-compatible brk syscall; malloc uses this to grow
 * the process heap beyond its initial image instead of a fixed arena. */
void *sbrk(long increment);

/* stdin is always the console here. */
static inline int isatty(int fd)
{
    (void)fd;
    return 1;
}

#endif
