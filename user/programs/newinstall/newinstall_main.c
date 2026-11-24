/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: user/programs/newinstall/newinstall_main.c
 * Description: archinstall-style guided installer for NEWOS.
 *
 * Runs from the live ISO, asks for a small set of installation choices,
 * applies them to the RAM root, installs the selected .new packages and then
 * snapshots the resulting system into /fat/NEWOS.SYS. That image is restored
 * by /sbin/sysrestore (an OpenRC `sysinit` entry) on every later boot.
 *
 * There is no target-root/chroot facility in NEWOS: newpkg writes absolute
 * paths into the live VFS and keeps its database on /fat/NEWPKG. The snapshot
 * is therefore a userspace image, not a disk partition with its own kernel:
 * the kernel and built-in /bin toolbox continue to come from the boot medium.
 *
 * Format of /fat/NEWOS.SYS (keep in sync with ../sysrestore/):
 *   header (32 B): "NEWSYS1\n", u32 version, u32 entries, u32 bytes, u32 crc
 *   entries: u16 path_len, u32 data_len, u32 data_crc, u16 mode,
 *            path_len bytes (no NUL), data_len bytes
 * All integers little-endian; header crc is CRC32 over bytes 0..19.
 */
#include "../../lib/nshlib.h"

#define IMAGE_PATH     "/fat/NEWOS.SYS"
#define IMAGE_MAGIC    "NEWSYS1\n"
#define IMAGE_VERSION  1u
#define HEADER_SIZE    32u
#define ENTRY_HDR_SIZE 12u
#define MAX_PATH       256u
#define MAX_ENTRIES    2048u
#define MAX_PACKAGES   32u
#define MAX_NAME       64u
#define BUF_SIZE       512u

struct snapshot_entry {
    char path[MAX_PATH];
    u32 size;
    u32 crc;
    u16 mode;
};

static struct snapshot_entry g_entries[MAX_ENTRIES];
static u32 g_count;
static u64 g_bytes;
static u32 g_crc_table[256];
static int g_index_updated;
static char g_index_names[16][MAX_NAME];
static u32 g_index_count;

static int g_db_is_ram(void);

struct choices {
    char hostname[MAX_NAME];
    char username[MAX_NAME];
    char packages[256];
    u32 profile;                   /* 1 = minimal, 2 = desktop, 3 = full */
    u32 session_gui;
    u32 copy_base;
    u32 assets;
};

static struct choices g_choice = {
    "newos", "user", "", 2u, 0u, 1u, 0u
};

/* --- small helpers ------------------------------------------------------ */

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

/* Running CRC32 (IEEE). Start with 0xFFFFFFFFu, complement the result. */
static u32 crc32_up(u32 crc, const u8 *p, u64 n)
{
    u64 i;

    for (i = 0; i < n; i++)
        crc = g_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

static void put32(u8 *p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

static void put16(u8 *p, u16 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
}

static u64 g_written;

static int write_all(int fd, const void *buf, u64 len)
{
    const u8 *p = buf;
    u64 done = 0;

    while (done < len) {
        long n = sys_write(fd, p + done, len - done);
        if (n <= 0)
            return -1;
        done += (u64)n;
        g_written += (u64)n;
    }
    return 0;
}

/* mkdir_p: the VFS does not synthesize parents, so create a component at a
 * time. The buffer is restored (except for added trailing NULs) on return. */
static void mkdir_p(char *path)
{
    char *p;

    if (!path[0] || path[0] != '/')
        return;
    for (p = path + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        sys_mkdir(path);
        *p = '/';
    }
    sys_mkdir(path);
}

/* Raw console line reader: NEWOS has no kernel line discipline, so echo and
 * backspace are handled here. Also works on a pipe or redirected file. */
static int read_line(char *buf, u64 cap)
{
    u64 n = 0;
    u8 c;

    buf[0] = '\0';
    for (;;) {
        long r = sys_read(0, &c, 1);
        if (r < 0)
            return -1;
        if (r == 0) {
            if (!n)
                return -1;
            break;
        }
        if (c == '\r' || c == '\n')
            break;
        if (c == 0x7f || c == 0x08) {
            if (n) {
                n--;
                nputc('\b');
                nputc(' ');
                nputc('\b');
            }
            continue;
        }
        if (c == 0x03 || c == 0x04) {  /* Ctrl+C / Ctrl+D */
            nputc('\n');
            return -1;
        }
        if (c < 0x20 || c >= 0x7f)
            continue;
        nputc((char)c);
        if (n + 1 < cap) {
            buf[n++] = (char)c;
            buf[n] = '\0';
        }
    }
    nputc('\n');
    return 0;
}

static int prompt(const char *label, const char *def, char *out, u64 cap)
{
    u64 i;

    putf("%s [%s]: ", label, def);
    if (read_line(out, cap) != 0)
        return -1;
    for (i = 0; out[i]; i++) {
        if (out[i] != ' ' && out[i] != '\t')
            break;
    }
    if (!out[i])
        nstrcpy(out, def);
    return 0;
}

static int prompt_yn(const char *label, int def)
{
    char ans[16];

    putf("%s [%s]: ", label, def ? "y" : "n");
    if (read_line(ans, sizeof(ans)) != 0)
        return def;
    return (ans[0] == 'y' || ans[0] == 'Y') ? 1 : 0;
}

/* Tiny buffer formatter (nshlib's putf writes to stdout, not to a buffer). */
static void bcat(char *d, const char *s)
{
    nstrcat(d, s);
}

static void bcat_u32(char *d, u32 v)
{
    char tmp[16];
    u32 i = 0, j;

    if (!v)
        tmp[i++] = '0';
    while (v) {
        tmp[i++] = (char)('0' + v % 10u);
        v /= 10u;
    }
    for (j = 0; j < i; j++) {
        u64 len = nstrlen(d);
        d[len] = tmp[i - 1 - j];
        d[len + 1] = '\0';
    }
}

/* --- answer file (also used by the automated installer test) ------------ */

static int set_choice(const char *key, const char *val)
{
    if (nstrcmp(key, "hostname") == 0) {
        nstrcpy(g_choice.hostname, val);
        return 0;
    }
    if (nstrcmp(key, "user") == 0) {
        nstrcpy(g_choice.username, val);
        return 0;
    }
    if (nstrcmp(key, "packages") == 0) {
        nstrcpy(g_choice.packages, val);
        return 0;
    }
    if (nstrcmp(key, "profile") == 0) {
        if (nstrcmp(val, "minimal") == 0)
            g_choice.profile = 1;
        else if (nstrcmp(val, "desktop") == 0)
            g_choice.profile = 2;
        else if (nstrcmp(val, "full") == 0)
            g_choice.profile = 3;
        else
            g_choice.profile = (u32)natoi(val);
        /* Later `copy_base`/`assets` keys override these defaults. */
        if (g_choice.profile == 1)
            g_choice.copy_base = 0;
        if (g_choice.profile == 3)
            g_choice.assets = 1;
        return 0;
    }
    if (nstrcmp(key, "session") == 0) {
        g_choice.session_gui = (nstrcmp(val, "gui") == 0);
        return 0;
    }
    if (nstrcmp(key, "copy_base") == 0) {
        g_choice.copy_base = (nstrcmp(val, "yes") == 0);
        return 0;
    }
    if (nstrcmp(key, "assets") == 0) {
        g_choice.assets = (nstrcmp(val, "yes") == 0);
        return 0;
    }
    return -1;
}

static int load_answers(const char *path)
{
    static u8 buf[4096];
    long fd, n;
    u64 total = 0, start = 0, i;

    fd = sys_open(path, O_READ);
    if (fd < 0) {
        putf("newinstall: cannot open %s\n", path);
        return -1;
    }
    for (;;) {
        n = sys_read((int)fd, buf + total, sizeof(buf) - total);
        if (n < 0) {
            sys_close((int)fd);
            return -1;
        }
        if (!n)
            break;
        total += (u64)n;
        if (total == sizeof(buf))
            break;
    }
    sys_close((int)fd);

    for (i = 0; i <= total; i++) {
        u64 end, j, eq = 0;
        char key[32], val[256];

        if (i != total && buf[i] != '\n' && buf[i] != '\r')
            continue;
        end = i;
        if (end > start && buf[start] != '#') {
            for (j = start; j < end; j++) {
                if (buf[j] == '=') {
                    eq = j;
                    break;
                }
            }
            if (eq) {
                u64 klen = eq - start, vlen = end - eq - 1;
                if (klen && klen < sizeof(key) && vlen < sizeof(val)) {
                    for (j = 0; j < klen; j++)
                        key[j] = (char)buf[start + j];
                    key[klen] = '\0';
                    for (j = 0; j < vlen; j++)
                        val[j] = (char)buf[eq + 1 + j];
                    val[vlen] = '\0';
                    set_choice(key, val);
                }
            }
        }
        start = i + 1;
    }
    return 0;
}

static void cputs(const char *color, const char *text)
{
    nputs(color);
    nputs(text);
    nputs("\033[0m");
}

static void update_index(void)
{
    char *argv[2];

    if (g_index_updated)
        return;
    argv[0] = (char *)"newpkg";
    argv[1] = (char *)"update";
    sys_waitpid(sys_spawn2("/bin/newpkg", argv, 2));
    g_index_updated = 1;
}

/* Print a compact numbered list from the on-board Packages index so the
 * user can pick names instead of guessing them. */
static void show_packages(void)
{
    static u8 buf[96 * 1024];
    long fd, n;
    u64 total = 0, start = 0, shown = 0;

    fd = sys_open("/var/lib/newpkg/repo-Packages", O_READ);
    if (fd < 0)
        return;
    for (;;) {
        n = sys_read((int)fd, buf + total, sizeof(buf) - total);
        if (n <= 0)
            break;
        total += (u64)n;
        if (total == sizeof(buf))
            break;
    }
    sys_close((int)fd);
    cputs("\033[1;36m", "Available packages (from /etc/newpkg-repo):\n");
    for (;;) {
        u64 end = start;
        while (end < total && buf[end] != '\n')
            end++;
        if (end - start > 9 && nstrncmp((char *)buf + start, "Package: ", 9) == 0) {
            u64 k, len = end - start - 9;
            if (len >= MAX_NAME)
                len = MAX_NAME - 1;
            for (k = 0; k < len; k++)
                g_index_names[shown][k] = (char)buf[start + 9 + k];
            g_index_names[shown][len] = '\0';
            putf("  %u) %s\n", (u32)(shown + 1), g_index_names[shown]);
            shown++;
            if (shown >= 16)
                break;
        }
        if (end == total)
            break;
        start = end + 1;
    }
    g_index_count = (u32)shown;
    if (!shown)
        nputs("  (index unavailable; type names manually)\n");
}

static void show_menu(void)
{
    nputs("\n");
    cputs("\033[1;36m", "  ==================================================\n");
    cputs("\033[1;36m", "    NEWOS INSTALLER  -  guided setup for the live ISO\n");
    cputs("\033[1;36m", "  ==================================================\n");
    nputs("  The live ISO boots from RAM. This writes a snapshot to\n");
    nputs("  /fat/NEWOS.SYS; /sbin/sysrestore replays it at boot.\n");
    nputs("  The kernel and built-in /bin toolbox still come from the ISO.\n\n");
}

static int show_summary(void)
{
    cputs("\033[1;33m", "\n--- Installation summary ---\n");
    putf("  profile : %u (%s)\n", g_choice.profile,
         g_choice.profile == 1 ? "minimal" :
         g_choice.profile == 3 ? "full" : "desktop");
    putf("  hostname: %s\n", g_choice.hostname);
    putf("  user    : %s\n", g_choice.username);
    putf("  session : %s\n",
         g_choice.session_gui ? "graphical (VOXEL)" : "text console");
    putf("  base/games: %s / %s\n",
         g_choice.copy_base ? "yes" : "no", g_choice.assets ? "yes" : "no");
    putf("  packages: %s\n",
         g_choice.packages[0] ? g_choice.packages : "(none)");
    return prompt_yn("Write /fat/NEWOS.SYS now?", 1);
}

static int interactive(void)
{
    char ans[MAX_NAME];

    show_menu();
    nputs("1) minimal   (config + selected packages)\n");
    nputs("2) desktop   (base system + desktop services)\n");
    nputs("3) full      (base system + games and assets)\n");
    if (prompt("Profile", "2", ans, sizeof(ans)) != 0)
        return -1;
    g_choice.profile = (u32)natoi(ans);
    if (g_choice.profile < 1 || g_choice.profile > 3)
        g_choice.profile = 2;

    if (prompt("Hostname", g_choice.hostname, ans, sizeof(ans)) != 0)
        return -1;
    nstrcpy(g_choice.hostname, ans);
    if (prompt("User name", g_choice.username, ans, sizeof(ans)) != 0)
        return -1;
    nstrcpy(g_choice.username, ans);

    g_choice.session_gui = prompt_yn("Boot into graphical session?", 0);
    g_choice.copy_base = prompt_yn("Copy base system to disk?", 1);
    g_choice.assets = prompt_yn("Include games/assets (large)?", 0);

    update_index();
    show_packages();
    if (prompt("Packages (numbers or names, comma separated; blank = none)",
               g_choice.packages, g_choice.packages,
               sizeof(g_choice.packages)) != 0)
        return -1;
    return show_summary() ? 0 : -1;
}

/* --- apply the chosen configuration ------------------------------------ */

static int write_text(const char *path, const char *text)
{
    char parent[MAX_PATH];
    u64 i, len = nstrlen(text);
    char *last = (char *)path;
    long fd;

    for (i = 0; path[i]; i++)
        if (path[i] == '/')
            last = (char *)&path[i];
    i = (u64)(last - path);
    if (i > 1) {
        for (u64 k = 0; k < i; k++)
            parent[k] = path[k];
        parent[i] = '\0';
        mkdir_p(parent);
    }
    sys_unlink(path);
    fd = sys_open(path, O_WRITE | O_CREATE);
    if (fd < 0) {
        putf("newinstall: cannot write %s\n", path);
        return -1;
    }
    if (write_all((int)fd, text, len) != 0) {
        sys_close((int)fd);
        putf("newinstall: short write to %s\n", path);
        return -1;
    }
    sys_close((int)fd);
    return 0;
}

static int apply_config(void)
{
    static char buf[2048];
    static char conf[512];
    static char passwd[1024];
    static char group[512];

    putf("newinstall: applying configuration for %s\n", g_choice.hostname);

    nstrcpy(conf, "# Managed by /bin/newinstall.\nsession=");
    bcat(conf, g_choice.session_gui ? "gui\nhostname=" : "text\nhostname=");
    bcat(conf, g_choice.hostname);
    bcat(conf, "\nuser=");
    bcat(conf, g_choice.username);
    bcat(conf, "\nprofile=");
    bcat_u32(conf, g_choice.profile);
    bcat(conf, "\n");
    if (write_text("/etc/newos.conf", conf) != 0)
        return -1;

    if (write_text("/etc/hostname", g_choice.hostname) != 0)
        return -1;

    nstrcpy(passwd, "root:x:0:0:root:/root:/bin/sh\n");
    bcat(passwd, g_choice.username);
    bcat(passwd, ":x:1000:1000:");
    bcat(passwd, g_choice.username);
    bcat(passwd, ":/home/");
    bcat(passwd, g_choice.username);
    bcat(passwd, ":/bin/sh\n");
    if (write_text("/etc/passwd", passwd) != 0)
        return -1;

    nstrcpy(group, "root:x:0:\nusers:x:100:\n");
    bcat(group, g_choice.username);
    bcat(group, ":x:1000:");
    bcat(group, g_choice.username);
    bcat(group, "\n");
    if (write_text("/etc/group", group) != 0)
        return -1;

    nstrcpy(buf, "# /etc/rc.conf - managed by /bin/newinstall.\n"
                "rc_bootlevel=\"sysinit boot\"\n"
                "rc_defaultlevel=\"default\"\n");
    if (write_text("/etc/rc.conf", buf) != 0)
        return -1;

    /* VOXEL is the only graphical session in the image. */
    if (g_choice.session_gui) {
        nstrcpy(buf, "# /etc/conf.d/voxel - managed by /bin/newinstall.\n"
                     "session_program=\"/bin/voxel\"\n");
        if (write_text("/etc/conf.d/voxel", buf) != 0)
            return -1;
        if (write_text("/etc/runlevels/default/voxel", "") != 0)
            return -1;
    } else {
        sys_unlink("/etc/runlevels/default/voxel");
    }
    /* Drop stale links from images that still shipped the old desktop. */
    sys_unlink("/etc/runlevels/default/desktop");
    return 0;
}

static int install_packages(void)
{
    char names[MAX_PACKAGES][MAX_NAME];
    u32 count = 0, i, start = 0;
    u64 len = nstrlen(g_choice.packages);
    char *argv[4];

    if (!len)
        return 0;

    if (!g_index_updated) {
        nputs("newinstall: refreshing package index...\n");
        update_index();
    }

    for (i = 0; i <= len && count < MAX_PACKAGES; i++) {
        if (i != len && g_choice.packages[i] != ',' &&
            g_choice.packages[i] != ' ' && g_choice.packages[i] != '\t')
            continue;
        if (i > start && count < MAX_PACKAGES) {
            u64 k, n = i - start;
            if (n >= MAX_NAME)
                n = MAX_NAME - 1;
            for (k = 0; k < n; k++)
                names[count][k] = g_choice.packages[start + k];
            names[count][n] = '\0';
            {
                u32 j, all_digits = 1;
                for (j = 0; j < n; j++)
                    if (names[count][j] < '0' || names[count][j] > '9')
                        all_digits = 0;
                if (all_digits && n) {
                    u32 pick = (u32)natoi(names[count]);
                    if (pick >= 1 && pick <= g_index_count)
                        nstrcpy(names[count], g_index_names[pick - 1]);
                }
            }
            count++;
        }
        start = i + 1;
    }
    for (i = 0; i < count; i++) {
        putf("newinstall: installing %s...\n", names[i]);
        argv[0] = (char *)"newpkg";
        argv[1] = (char *)"install";
        argv[2] = names[i];
        sys_waitpid(sys_spawn2("/bin/newpkg", argv, 3));
    }
    return 0;
}

/* --- snapshot collection ------------------------------------------------ */

static int skipped(const char *path)
{
    static const char *const skip[] = {
        "/fat", "/proc", "/sys", "/dev", "/run", "/tmp", "/baseq2",
        "/usr/share/doom",
        /* Boot-time visual assets: the ISO provides them on every boot. */
        "/etc/splash.bmp", "/etc/wallpaper.bmp", "/etc/test.ppm", 0
    };
    u32 i;

    for (i = 0; skip[i]; i++) {
        if (nstrcmp(path, skip[i]) == 0 ||
            (nstrncmp(path, skip[i], nstrlen(skip[i])) == 0 &&
             path[nstrlen(skip[i])] == '/'))
            return 1;
    }
    return 0;
}

static int add_file(const char *path, u32 size, u16 mode)
{
    static u8 data[BUF_SIZE];
    struct snapshot_entry *e;
    long fd;
    u64 done = 0;
    u32 i, crc = 0xFFFFFFFFu;

    if (g_count >= MAX_ENTRIES) {
        nputs("newinstall: too many files for one image\n");
        return -1;
    }
    for (i = 0; i < g_count; i++) {
        if (nstrcmp(g_entries[i].path, path) == 0)
            return 0;             /* already snapshotted */
    }
    e = &g_entries[g_count];
    nstrcpy(e->path, path);
    e->size = size;
    e->mode = mode;
    fd = sys_open(path, O_READ);
    if (fd >= 0) {
        while (done < size) {
            u64 want = size - done;
            long r;
            if (want > sizeof(data))
                want = sizeof(data);
            r = sys_read((int)fd, data, want);
            if (r <= 0)
                break;
            crc = crc32_up(crc, data, (u64)r);
            done += (u64)r;
        }
        sys_close((int)fd);
    }
    e->crc = ~crc;
    g_count++;
    g_bytes += size;
    if (size > 8192)
        putf("newinstall: captured %s (%u KiB)\n", path, (u32)(size / 1024));
    return 0;
}

static int collect_dir(const char *path, u32 depth)
{
    static struct nsh_dirent dents[128];
    char child[MAX_PATH];
    long n, i;

    if (depth > 16 || skipped(path))
        return 0;
    n = sys_readdir(path, dents, sizeof(dents));
    if (n <= 0)
        return 0;
    for (i = 0; i < n; i++) {
        u64 base = nstrlen(path), name = nstrlen(dents[i].name);
        if (base + name + 2 >= MAX_PATH)
            continue;
        nstrcpy(child, path);
        if (base > 1)
            nstrcat(child, "/");
        nstrcat(child, dents[i].name);
        if (skipped(child))
            continue;
        if ((dents[i].mode & 0040000u) == 0040000u) {
            collect_dir(child, depth + 1);
        } else if ((dents[i].mode & 0100000u) == 0100000u) {
            if (add_file(child, (u32)dents[i].size,
                         (u16)(dents[i].mode & 0777u)) != 0)
                return -1;
        }
    }
    return 0;
}

/* Installed packages register their manifest at /fat/NEWPKG/<token>.FIL.
 * Include those files regardless of copy_base: a package that is listed as
 * installed after a reboot must also be present on disk. */
static int collect_package_files(void)
{
    static struct nsh_dirent dents[64];
    static u8 manifest[65536];
    long n, i;

    if (g_db_is_ram()) {
        nputs("newinstall: package DB is RAM-only (no /fat disk)\n");
        return 0;
    }
    n = sys_readdir("/fat/NEWPKG", dents, sizeof(dents));
    if (n <= 0)
        return 0;
    for (i = 0; i < n; i++) {
        u64 len = nstrlen(dents[i].name), total = 0, start = 0;
        long fd;
        char path[160];

        if (len < 4 || nstrcmp(dents[i].name + len - 4, ".FIL") != 0)
            continue;
        nstrcpy(path, "/fat/NEWPKG/");
        nstrcat(path, dents[i].name);
        fd = sys_open(path, O_READ);
        if (fd < 0)
            continue;
        for (;;) {
            long r = sys_read((int)fd, manifest + total,
                              sizeof(manifest) - total);
            if (r <= 0)
                break;
            total += (u64)r;
            if (total == sizeof(manifest))
                break;
        }
        sys_close((int)fd);

        for (;;) {
            u64 end = start, a, b, c;
            while (end < total && manifest[end] != '\n')
                end++;
            if (end > start) {
                char file[MAX_PATH];
                u64 size = 0, k;
                u16 mode = 0644;

                /* <octmode> <size> <crc32> <abspath> */
                a = start;
                while (a < end && manifest[a] != ' ')
                    a++;
                if (a < end) {
                    u32 m = 0;
                    for (k = start; k < a; k++)
                        if (manifest[k] >= '0' && manifest[k] <= '7')
                            m = m * 8u + (u32)(manifest[k] - '0');
                    mode = (u16)(m & 0777u);
                }
                b = a + 1;
                while (b < end && manifest[b] != ' ')
                    b++;
                for (k = a + 1; k < b; k++)
                    if (manifest[k] >= '0' && manifest[k] <= '9')
                        size = size * 10u + (u64)(manifest[k] - '0');
                c = b + 1;
                while (c < end && manifest[c] != ' ')
                    c++;
                if (c < end && end - c - 1 < sizeof(file)) {
                    for (k = 0; k < end - c - 1; k++)
                        file[k] = (char)manifest[c + 1 + k];
                    file[end - c - 1] = '\0';
                    if (file[0] == '/' && size)
                        add_file(file, (u32)size, mode);
                }
            }
            if (end == total)
                break;
            start = end + 1;
        }
    }
    return 0;
}

/* True when newpkg moved its DB onto the FAT32 disk. */
static int g_db_is_ram(void)
{
    long fd = sys_open("/fat/NEWPKG", O_READ);

    if (fd < 0)
        return 1;
    sys_close((int)fd);
    return 0;
}

static int collect(void)
{
    static const char *const roots[] = { "/etc", "/home", "/root", 0 };
    static const char *const base[] = {
        "/bin", "/sbin", "/usr/bin", "/usr/lib", "/lib", 0
    };
    u32 i;

    nputs("newinstall: collecting configuration and user files...\n");
    for (i = 0; roots[i]; i++)
        if (collect_dir(roots[i], 0) != 0)
            return -1;
    if (collect_package_files() != 0)
        return -1;

    if (g_choice.copy_base) {
        nputs("newinstall: collecting base system binaries...\n");
        for (i = 0; base[i]; i++)
            if (collect_dir(base[i], 0) != 0)
                return -1;
    }
    if (g_choice.assets) {
        nputs("newinstall: collecting games and assets...\n");
        if (collect_dir("/baseq2", 0) != 0)
            return -1;
        if (collect_dir("/usr/share/doom", 0) != 0)
            return -1;
    }
    return 0;
}

static int write_image(void)
{
    static u8 hdr[HEADER_SIZE];
    static u8 ebuf[ENTRY_HDR_SIZE];
    static u8 data[BUF_SIZE];
    u32 i;
    u64 offset = HEADER_SIZE, total;
    long fd;

    putf("newinstall: writing %s (%u files, %u KiB)...\n", IMAGE_PATH,
         g_count, (u32)(g_bytes / 1024));
    /* FAT32 cannot extend a file's cluster chain through write(): it can
     * only grow via ftruncate. Pre-size the image before streaming data. */
    total = HEADER_SIZE + g_bytes;
    for (i = 0; i < g_count; i++)
        total += ENTRY_HDR_SIZE + nstrlen(g_entries[i].path);
    sys_unlink(IMAGE_PATH);
    fd = sys_open(IMAGE_PATH, O_WRITE | O_CREATE);
    if (fd < 0) {
        nputs("newinstall: no FAT32 disk at /fat (boot with an ATA disk)\n");
        return -1;
    }
    if (sys_ftruncate((int)fd, total) != 0) {
        nputs("newinstall: cannot size image on /fat (disk full?)\n");
        sys_close((int)fd);
        return -1;
    }
    g_written = 0;
    nmemset(hdr, 0, sizeof(hdr));
    if (write_all((int)fd, hdr, sizeof(hdr)) != 0)
        goto fail;

    for (i = 0; i < g_count; i++) {
        struct snapshot_entry *e = &g_entries[i];
        u16 plen = (u16)nstrlen(e->path);
        long src = sys_open(e->path, O_READ);
        u64 done = 0;

        if (src < 0) {
            putf("newinstall: cannot read %s (skipped)\n", e->path);
            continue;
        }
        put16(ebuf, plen);
        put32(ebuf + 2, e->size);
        put32(ebuf + 6, e->crc);
        put16(ebuf + 10, e->mode);
        if (write_all((int)fd, ebuf, sizeof(ebuf)) != 0 ||
            write_all((int)fd, e->path, plen) != 0) {
            sys_close((int)src);
            goto fail;
        }
        offset += ENTRY_HDR_SIZE + plen;
        while (done < e->size) {
            u64 want = e->size - done;
            long r;
            if (want > sizeof(data))
                want = sizeof(data);
            r = sys_read((int)src, data, want);
            if (r <= 0)
                break;
            if (write_all((int)fd, data, (u64)r) != 0) {
                sys_close((int)src);
                goto fail;
            }
            done += (u64)r;
        }
        sys_close((int)src);
        offset += done;
    }

    nmemcpy(hdr, IMAGE_MAGIC, 8);
    put32(hdr + 8, IMAGE_VERSION);
    put32(hdr + 12, g_count);
    put32(hdr + 16, (u32)offset);
    put32(hdr + 20, ~crc32_up(0xFFFFFFFFu, hdr, 20));
    if (sys_lseek((int)fd, 0, SEEK_SET) < 0 ||
        write_all((int)fd, hdr, sizeof(hdr)) != 0)
        goto fail;
    sys_close((int)fd);
    putf("newinstall: OK (%u bytes). Reboot to start the installed system.\n",
         (u32)offset);
    return 0;
fail:
    sys_close((int)fd);
    putf("newinstall: failed to write image at byte %u (disk full?)\n",
         (u32)g_written);
    return -1;
}

/* --- main --------------------------------------------------------------- */

int main(int argc, char **argv)
{
    crc32_init();

    if (argc > 1 && argv[1][0] != '-') {
        if (load_answers(argv[1]) != 0)
            return 1;
        putf("newinstall: using answers from %s\n", argv[1]);
    } else if (interactive() != 0) {
        return 1;
    }

    if (apply_config() != 0)
        return 1;
    if (install_packages() != 0)
        return 1;
    /* Packages install new files after apply_config, so snapshot last. */
    if (collect() != 0)
        return 1;
    return write_image();
}
