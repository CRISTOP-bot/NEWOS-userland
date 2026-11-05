/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/stat.c
 * Description: NEWOS libc: stat/fstat/mkdir over the kernel's Linux-numbered
 *              filesystem syscalls. The records cross the boundary in the
 *              kernel's own layout (struct nsh_stat), which is byte-for-byte
 *              the Linux x86_64 struct stat, so no translation is needed.
 * Added: 2026-10-30
 */
#include "../include/sys/stat.h"
#include "../../abi/syscall_abi.h"
#include "syscall.h"
#include "../include/errno.h"

static int stat_call(long nr, long a, void *buf)
{
    long r = libc_sys2(nr, a, (long)buf);

    if (r != 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int stat(const char *path, struct stat *buf)
{
    return stat_call(SYS_STAT, (long)path, buf);
}

int lstat(const char *path, struct stat *buf)
{
    /* The VFS is a static tree with no symlinks, so the kernel resolves
     * SYS_LSTAT exactly like SYS_STAT. */
    return stat_call(SYS_LSTAT, (long)path, buf);
}

int fstat(int fd, struct stat *buf)
{
    return stat_call(SYS_FSTAT, fd, buf);
}

int mkdir(const char *path, mode_t mode)
{
    long r;

    /* NEWOS has no permission model, so the mode is accepted and ignored;
     * the VFS records directories with a fixed mode. */
    (void)mode;
    r = libc_sys2(SYS_MKDIR, (long)path, (long)mode);
    if (r != 0) {
        errno = EEXIST;
        return -1;
    }
    return 0;
}
