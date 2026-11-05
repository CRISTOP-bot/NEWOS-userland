/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/newpkg/newpkg_util.c
 * Description: newpkg low-level helpers (I/O loops, mkdir -p, printers).
 * Added: 2026-10-30
 */
/* newpkg low-level helpers: package-independent plumbing shared by the
 * command and database layers (full-read/full-write loops over int $0x80,
 * path probing, mkdir -p of a directory or of a file's parent, the two
 * hex/permission printers and the package-name validator). Holds no
 * package logic; g_tmp is a private scratch buffer for the two directory
 * helpers.
 */

#include "../../lib/nshlib.h"
#include "newpkg_format.h"
#include "newpkg_internal.h"

/* File-scope state private to this module. */
static char g_tmp[512];

/* --- low-level I/O --------------------------------------------------------- */

int read_full(int fd, u8 *buf, u64 len)
{
    u64 done = 0;
    while (done < len) {
        long n = sys_read(fd, buf + done, len - done);
        if (n <= 0)
            return -1;
        done += (u64)n;
    }
    return 0;
}

int write_full(int fd, const u8 *buf, u64 len)
{
    u64 done = 0;
    while (done < len) {
        long n = sys_write(fd, buf + done, len - done);
        if (n <= 0)
            return -1;
        done += (u64)n;
    }
    return 0;
}

int path_exists(const char *path)
{
    long fd = sys_open(path, O_READ);
    if (fd < 0)
        return 0;
    sys_close((int)fd);
    return 1;
}

/* mkdir -p for a directory path (existing components are fine). */
int mkdir_p(const char *dir)
{
    u64 n = nstrlen(dir);
    u64 i;
    if (n == 0 || n >= sizeof(g_tmp))
        return -1;
    nstrcpy(g_tmp, dir);
    for (i = 1; i < n; i++) {
        if (g_tmp[i] == '/') {
            g_tmp[i] = '\0';
            if (sys_mkdir(g_tmp) != 0 && !path_exists(g_tmp))
                return -1;
            g_tmp[i] = '/';
        }
    }
    if (sys_mkdir(g_tmp) != 0 && !path_exists(g_tmp))
        return -1;
    return 0;
}

/* Ensure the parent directory of a file path exists. */
int mkdir_parent(const char *file)
{
    u64 n = nstrlen(file);
    u64 i = n;
    if (n == 0 || n >= sizeof(g_tmp))
        return -1;
    while (i > 0 && file[i - 1] != '/')
        i--;
    if (i <= 1)
        return 0;               /* "/x": parent is root, always there */
    if (i - 1 >= sizeof(g_tmp))
        return -1;
    nmemcpy(g_tmp, file, i - 1);
    g_tmp[i - 1] = '\0';
    return mkdir_p(g_tmp);
}

void print_hex8(u32 v)
{
    int i;
    for (i = 7; i >= 0; i--) {
        u32 d = (v >> (u32)(i * 4)) & 0xFu;
        nputc((char)(d < 10 ? '0' + d : 'a' + d - 10));
    }
}

void print_mode(u32 mode)
{
    char b[5];
    b[0] = (char)('0' + ((mode >> 9) & 7));
    b[1] = (char)('0' + ((mode >> 6) & 7));
    b[2] = (char)('0' + ((mode >> 3) & 7));
    b[3] = (char)('0' + (mode & 7));
    b[4] = '\0';
    nputs(b);
}

int valid_name(const char *name)
{
    u64 i = 0, n = nstrlen(name);
    if (n == 0 || n >= NEWPKG_NAME_MAX)
        return -1;
    for (; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '+'))
            return -1;
    }
    return 0;
}
