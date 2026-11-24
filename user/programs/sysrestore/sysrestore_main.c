/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: user/programs/sysrestore/sysrestore_main.c
 * Description: restore the persisted NEWOS system snapshot at boot.
 *
 * /bin/newinstall writes /fat/NEWOS.SYS. This program is an OpenRC `sysinit`
 * entry, so it runs before the runlevel chain and before the supervised
 * console: services and the shell see the installed configuration.
 * No image (or a corrupt one) is not fatal — the live ISO defaults stay in
 * place. Format: see ../newinstall/newinstall_main.c.
 */
#include "../../lib/nshlib.h"

#define IMAGE_PATH    "/fat/NEWOS.SYS"
#define IMAGE_MAGIC   "NEWSYS1\n"
#define IMAGE_VERSION 1u
#define HEADER_SIZE   32u
#define ENTRY_HDR_SIZE 12u
#define MAX_PATH      256u
#define MAX_ENTRIES   2048u
#define BUF_SIZE      512u

static u32 g_crc_table[256];

static void crc32_init(void)
{
    u32 i, j, c;

    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++)
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crc_table[i] = c;
    }
}

static u32 crc32_up(u32 crc, const u8 *p, u64 n)
{
    u64 i;

    for (i = 0; i < n; i++)
        crc = g_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

static int mem_eq(const u8 *a, const u8 *b, u64 n)
{
    u64 i;

    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u16 le16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static int read_all(int fd, void *buf, u64 len)
{
    u8 *p = buf;
    u64 done = 0;

    while (done < len) {
        long n = sys_read(fd, p + done, len - done);
        if (n < 0)
            return -1;
        if (!n)
            return -1;
        done += (u64)n;
    }
    return 0;
}

static int write_all(int fd, const void *buf, u64 len)
{
    const u8 *p = buf;
    u64 done = 0;

    while (done < len) {
        long n = sys_write(fd, p + done, len - done);
        if (n <= 0)
            return -1;
        done += (u64)n;
    }
    return 0;
}

/* The VFS does not synthesize parents. Restore paths are absolute. */
static void mkdir_p(const char *path)
{
    char buf[MAX_PATH];
    char *p;
    u64 n = nstrlen(path);

    if (n >= sizeof(buf) || path[0] != '/')
        return;
    nmemcpy(buf, path, n + 1);
    for (p = buf + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        sys_mkdir(buf);
        *p = '/';
    }
}

int main(void)
{
    static u8 hdr[HEADER_SIZE];
    static u8 ebuf[ENTRY_HDR_SIZE];
    static u8 path[MAX_PATH];
    static u8 data[BUF_SIZE];
    u32 count, i, restored = 0, failed = 0;
    long fd;

    crc32_init();
    fd = sys_open(IMAGE_PATH, O_READ);
    if (fd < 0) {
        nputs("sysrestore: no saved system (/fat/NEWOS.SYS); "
              "using live ISO defaults\n");
        return 0;
    }
    if (read_all((int)fd, hdr, sizeof(hdr)) != 0 ||
        !mem_eq(hdr, (const u8 *)IMAGE_MAGIC, 8) ||
        le32(hdr + 8) != IMAGE_VERSION) {
        nputs("sysrestore: bad snapshot header; using live ISO defaults\n");
        sys_close((int)fd);
        return 0;
    }
    if (~crc32_up(0xFFFFFFFFu, hdr, 20) != le32(hdr + 20)) {
        nputs("sysrestore: snapshot CRC mismatch; using live ISO defaults\n");
        sys_close((int)fd);
        return 0;
    }

    count = le32(hdr + 12);
    if (count == 0 || count > MAX_ENTRIES) {
        nputs("sysrestore: invalid file count; using live ISO defaults\n");
        sys_close((int)fd);
        return 0;
    }
    nputs("sysrestore: restoring installed system from /fat/NEWOS.SYS\n");

    for (i = 0; i < count; i++) {
        u16 plen, mode;
        u32 size, want_crc, crc = 0xFFFFFFFFu;
        u64 done = 0;
        long dst;

        if (read_all((int)fd, ebuf, sizeof(ebuf)) != 0)
            break;
        plen = le16(ebuf);
        size = le32(ebuf + 2);
        want_crc = le32(ebuf + 6);
        mode = le16(ebuf + 10);
        (void)mode;               /* permissions are informational in NEWOS */
        if (!plen || plen >= sizeof(path) ||
            read_all((int)fd, path, plen) != 0)
            break;
        path[plen] = '\0';

        mkdir_p((char *)path);
        sys_unlink((char *)path);
        dst = sys_open((char *)path, O_WRITE | O_CREATE);
        if (dst < 0) {
            failed++;
            continue;
        }
        while (done < size) {
            u64 chunk = size - done;
            long r;
            if (chunk > sizeof(data))
                chunk = sizeof(data);
            r = sys_read((int)fd, data, chunk);
            if (r <= 0)
                break;
            if (write_all((int)dst, data, (u64)r) != 0)
                break;
            crc = crc32_up(crc, data, (u64)r);
            done += (u64)r;
        }
        sys_close((int)dst);
        if (done != size || ~crc != want_crc)
            failed++;
        else
            restored++;
    }
    sys_close((int)fd);
    putf("sysrestore: %u files restored, %u failed\n", restored, failed);
    return 0;                     /* never block the boot on a bad snapshot */
}
