/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/sys/stat.h
 * Description: NEWOS libc: struct stat and the file/metadata queries the
 *              kernel already answers (SYS_STAT/SYS_FSTAT/SYS_MKDIR).
 * Added: 2026-10-30
 */
#ifndef NEWOS_LIBC_SYS_STAT_H
#define NEWOS_LIBC_SYS_STAT_H

#include <sys/types.h>

typedef unsigned int mode_t;
typedef unsigned long ino_t;
typedef unsigned long dev_t;
typedef unsigned long nlink_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long blkcnt_t;
typedef long blksize_t;

/* Layout is the Linux x86_64 struct stat, byte for byte: the kernel fills
 * the record straight out of struct nsh_stat (see abi/syscall_abi.h, which
 * statically asserts its 144-byte size and st_size offset). */
struct stat {
    dev_t      st_dev;
    ino_t      st_ino;
    nlink_t    st_nlink;
    mode_t     st_mode;
    uid_t      st_uid;
    gid_t      st_gid;
    unsigned int __pad0;
    dev_t      st_rdev;
    off_t      st_size;
    blksize_t  st_blksize;
    blkcnt_t   st_blocks;
    time_t     st_atime;
    unsigned long st_atime_nsec;
    time_t     st_mtime;
    unsigned long st_mtime_nsec;
    time_t     st_ctime;
    unsigned long st_ctime_nsec;
    long       __unused[3];
};

#define S_IFMT   00170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000

#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* NEWOS has no permission model: the VFS grants every mode, so these bits
 * are recorded and reported but never enforced. */
#define S_ISUID 0004000
#define S_ISGID 0002000
#define S_ISVTX 0001000

#define S_IRWXU 0700
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXG 0070
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXO 0007
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001

int stat(const char *path, struct stat *buf);
int lstat(const char *path, struct stat *buf);
int fstat(int fd, struct stat *buf);
int mkdir(const char *path, mode_t mode);

#endif
