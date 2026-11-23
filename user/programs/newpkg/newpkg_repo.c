/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/newpkg/newpkg_repo.c
 * Description: Debian-like repository client for NEWOS `.new` packages.
 *             Manages /var/lib/newpkg/repos.conf, fetches the Release and
 *             Packages index over HTTP, verifies the index CRC32, searches
 *             available packages and installs by name from the pool.
 * Added: 2026-11-23
 */
/* newpkg_repo.c: on-board half of the Debian-like repository.
 *
 * Layout (produced by tools/newpkg/newpkg-repo.py, served over HTTP):
 *   <url>/Release
 *   <url>/dists/<suite>/main/binary-<arch>/Packages
 *   <url>/<Filename>                 pool packages
 *
 * Local state under /var/lib/newpkg:
 *   repos.conf      one repo URL per line ('#' comments ignored)
 *   repo-Release    last fetched Release (cached)
 *   repo-Packages   last fetched Packages index (cached)
 * Downloaded packages land in /var/cache/newpkg/ before verification.
 */
#include "../../lib/nshlib.h"
#include "newpkg_format.h"
#include "newpkg_internal.h"

#define REPO_CONF   "/var/lib/newpkg/repos.conf"
#define REPO_REL    "/var/lib/newpkg/repo-Release"
#define REPO_IDX    "/var/lib/newpkg/repo-Packages"
#define CACHE_DIR   "/var/cache/newpkg"
#define REPO_URL_MAX 192
#define IDX_MAX     (96u * 1024u)
#define REPO_SUITE  "stable"
#define REPO_ARCH   "x86_64"

/* Shared scratch: the Packages index is read whole into this buffer so
 * stanza parsing can walk it multiple times without re-reading the file. */
static u8 g_idx[IDX_MAX];

/* --- small string helpers (no libc) ------------------------------------- */

static int c_eq(const char *a, const char *b)
{
    return nstrcmp(a, b) == 0;
}

static int c_prefix(const char *s, const char *p)
{
    return nstrncmp(s, p, nstrlen(p)) == 0;
}

/* Substring search (case-sensitive), used for index queries. */
static int str_contains_local(const char *hay, const char *needle)
{
    u64 nl = nstrlen(needle);

    if (!nl)
        return 1;
    while (*hay) {
        if (nstrncmp(hay, needle, nl) == 0)
            return 1;
        hay++;
    }
    return 0;
}

/* Copy at most cap-1 bytes; always NUL-terminate. */
static void c_copy(char *d, const char *s, u64 cap)
{
    u64 i = 0;

    if (!cap)
        return;
    while (s[i] && i + 1 < cap) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}

static int c_append(char *d, u64 cap, const char *s)
{
    u64 dl = nstrlen(d);

    while (*s) {
        if (dl + 1 >= cap)
            return -1;
        d[dl++] = *s++;
    }
    d[dl] = 0;
    return 0;
}

/* --- file helpers -------------------------------------------------------- */

/* Read a whole file into buf (up to cap). Returns byte count or -1. */
static long read_whole(const char *path, u8 *buf, u64 cap)
{
    long fd = sys_open(path, O_READ);
    u64 total = 0;
    long n;

    if (fd < 0)
        return -1;
    for (;;) {
        if (total >= cap)
            break;
        n = sys_read((int)fd, buf + total, cap - total);
        if (n < 0) {
            sys_close((int)fd);
            return -1;
        }
        if (n == 0)
            break;
        total += (u64)n;
    }
    sys_close((int)fd);
    return (long)total;
}

/* Write buf to path, replacing any old copy (no O_TRUNC: unlink first). */
static int write_whole(const char *path, const u8 *buf, u64 len)
{
    long fd;

    sys_unlink(path);
    fd = sys_open(path, O_WRITE | O_CREATE);
    if (fd < 0)
        return -1;
    if (write_full((int)fd, buf, len) != 0) {
        sys_close((int)fd);
        return -1;
    }
    sys_close((int)fd);
    return 0;
}

/* Fetch a URL to a local file via the streaming HTTP client. Returns 0/-1. */
/* Copy a local file to path (used by file: repositories). */
static int copy_local(const char *src, const char *dst)
{
    long sfd = sys_open(src, O_READ);
    long dfd;
    u8 buf[512];
    long n;

    if (sfd < 0)
        return -1;
    sys_unlink(dst);
    dfd = sys_open(dst, O_WRITE | O_CREATE);
    if (dfd < 0) {
        sys_close((int)sfd);
        return -1;
    }
    for (;;) {
        n = sys_read((int)sfd, buf, sizeof(buf));
        if (n < 0) {
            sys_close((int)sfd);
            sys_close((int)dfd);
            return -1;
        }
        if (n == 0)
            break;
        if (write_full((int)dfd, buf, (u64)n) != 0) {
            sys_close((int)sfd);
            sys_close((int)dfd);
            return -1;
        }
    }
    sys_close((int)sfd);
    sys_close((int)dfd);
    return 0;
}

static int fetch_to(const char *url, const char *path)
{
    long fd;

    /* Local repositories: a leading "file:" or "/" is a path on the VFS. */
    if (c_prefix(url, "file:"))
        return copy_local(url + 5, path);
    if (url[0] == '/')
        return copy_local(url, path);

    sys_unlink(path);
    fd = sys_open(path, O_WRITE | O_CREATE);
    if (fd < 0) {
        fputf(1, "newpkg: cannot create %s\n", path);
        return -1;
    }
    if (nsh_http_download(url, (int)fd) < 0) {
        sys_close((int)fd);
        sys_unlink(path);
        return -1;
    }
    sys_close((int)fd);
    return 0;
}

/* First configured repo URL, or "" if none. */
static void first_repo(char *out, u64 cap)
{
    long fd = sys_open(REPO_CONF, O_READ);
    u8 buf[1024];
    u64 total = 0;
    long n;

    out[0] = 0;
    if (fd < 0)
        return;
    for (;;) {
        if (total >= sizeof(buf))
            break;
        n = sys_read((int)fd, buf + total, sizeof(buf) - total);
        if (n <= 0)
            break;
        total += (u64)n;
    }
    sys_close((int)fd);
    {
        u64 i = 0;

        while (i < total) {
            u64 j = i;
            char line[REPO_URL_MAX];

            while (j < total && buf[j] != '\n')
                j++;
            if (j > i) {
                u64 len = j - i;

                if (len >= sizeof(line))
                    len = sizeof(line) - 1;
                for (u64 k = 0; k < len; k++)
                    line[k] = (char)buf[i + k];
                line[len] = 0;
                if (line[0] && line[0] != '#') {
                    c_copy(out, line, cap);
                    return;
                }
            }
            i = j + 1;
        }
    }
}

/* --- repository management ---------------------------------------------- */

int cmd_repo_add(const char *url)
{
    char conf[1024];
    long len;
    char line[REPO_URL_MAX + 32];

    if (!url || !*url)
        return 1;
    if (mkdir_p("/var/lib/newpkg") != 0) {
        fputf(1, "newpkg: cannot create repo dir\n");
        return 1;
    }
    len = read_whole(REPO_CONF, (u8 *)conf, sizeof(conf) - 1);
    if (len < 0)
        len = 0;
    conf[len] = 0;
    /* Reject duplicates. */
    {
        u64 i = 0;

        while (conf[i]) {
            u64 j = i;

            while (conf[j] && conf[j] != '\n')
                j++;
            if (j > i) {
                char existing[REPO_URL_MAX];
                u64 l = j - i;

                if (l >= sizeof(existing))
                    l = sizeof(existing) - 1;
                for (u64 k = 0; k < l; k++)
                    existing[k] = conf[i + k];
                existing[l] = 0;
                if (c_eq(existing, url)) {
                    putf("newpkg: repo already configured: %s\n", url);
                    return 0;
                }
            }
            i = j + (conf[j] ? 1 : 0);
        }
    }
    line[0] = 0;
    if (len > 0 && conf[len - 1] != '\n')
        c_append(line, sizeof(line), "\n");
    c_append(line, sizeof(line), url);
    c_append(line, sizeof(line), "\n");
    if (write_whole(REPO_CONF, (u8 *)line, nstrlen(line)) != 0) {
        fputf(1, "newpkg: cannot write repo config\n");
        return 1;
    }
    putf("newpkg: added repository: %s\n", url);
    return 0;
}

int cmd_repo_list(void)
{
    char conf[1024];
    long len = read_whole(REPO_CONF, (u8 *)conf, sizeof(conf) - 1);
    int count = 0;

    if (len < 0)
        len = 0;
    conf[len] = 0;
    nputs("Configured repositories:\n");
    {
        u64 i = 0;

        while (conf[i]) {
            u64 j = i;

            while (conf[j] && conf[j] != '\n')
                j++;
            if (j > i && conf[i] != '#') {
                for (u64 k = i; k < j; k++)
                    nputc(conf[k]);
                nputc('\n');
                count++;
            }
            i = j + (conf[j] ? 1 : 0);
        }
    }
    if (!count)
        nputs("  (none; use 'newpkg repo add <url>')\n");
    return 0;
}

int cmd_repo_remove(const char *url)
{
    char conf[1024];
    long len = read_whole(REPO_CONF, (u8 *)conf, sizeof(conf) - 1);
    char out[1024];
    u64 olen = 0;
    int removed = 0;

    if (len < 0)
        return 1;
    conf[len] = 0;
    out[0] = 0;
    {
        u64 i = 0;

        while (conf[i]) {
            u64 j = i;

            while (conf[j] && conf[j] != '\n')
                j++;
            if (j > i) {
                char line[REPO_URL_MAX];
                u64 l = j - i;

                if (l >= sizeof(line))
                    l = sizeof(line) - 1;
                for (u64 k = 0; k < l; k++)
                    line[k] = conf[i + k];
                line[l] = 0;
                if (!c_eq(line, url)) {
                    u64 k = 0;

                    while (line[k]) {
                        if (olen + 1 >= sizeof(out))
                            return 1;
                        out[olen++] = line[k++];
                    }
                    if (olen + 1 >= sizeof(out))
                        return 1;
                    out[olen++] = '\n';
                } else {
                    removed = 1;
                }
            }
            i = j + (conf[j] ? 1 : 0);
        }
    }
    out[olen] = 0;
    if (write_whole(REPO_CONF, (u8 *)out, olen) != 0)
        return 1;
    if (removed)
        putf("newpkg: removed repository: %s\n", url);
    else
        fputf(1, "newpkg: repository not configured: %s\n", url);
    return removed ? 0 : 1;
}

/* --- index fetch + verification ----------------------------------------- */

/* Parse the CRC32 line out of a Release file: "CRC32: <hex8> <size> <path>".
 * Returns 0 and fills *crc_out on success. */
static int release_crc(const char *release, long len, u32 *crc_out)
{
    const char *p = release;
    const char *end = release + len;

    while (p < end) {
        if (c_prefix(p, "CRC32:")) {
            const char *q = p + 6;
            u32 v = 0;
            int digits = 0;

            while (q < end && *q == ' ')
                q++;
            while (q < end) {
                u32 d;

                if (*q >= '0' && *q <= '9')
                    d = (u32)(*q - '0');
                else if (*q >= 'a' && *q <= 'f')
                    d = (u32)(*q - 'a' + 10);
                else if (*q >= 'A' && *q <= 'F')
                    d = (u32)(*q - 'A' + 10);
                else
                    break;
                v = (v << 4) | d;
                digits++;
                q++;
            }
            if (digits > 0 && digits <= 8) {
                *crc_out = v;
                return 0;
            }
            return -1;
        }
        while (p < end && *p != '\n')
            p++;
        p++;
    }
    return -1;
}

int cmd_update(void)
{
    char repo[REPO_URL_MAX];
    char url[REPO_URL_MAX + 128];
    char rel[512];
    long rlen;
    u32 want = 0, got;

    first_repo(repo, sizeof(repo));
    if (!*repo) {
        fputf(1, "newpkg: no repositories configured\n");
        return 1;
    }
    if (mkdir_p("/var/lib/newpkg") != 0)
        return 1;

    c_copy(url, repo, sizeof(url));
    c_append(url, sizeof(url), "/Release");
    nputs("newpkg: fetching Release...\n");
    if (fetch_to(url, REPO_REL) != 0) {
        fputf(1, "newpkg: failed to fetch %s\n", url);
        return 1;
    }

    c_copy(url, repo, sizeof(url));
    c_append(url, sizeof(url), "/dists/");
    c_append(url, sizeof(url), REPO_SUITE);
    c_append(url, sizeof(url), "/main/binary-");
    c_append(url, sizeof(url), REPO_ARCH);
    c_append(url, sizeof(url), "/Packages");
    nputs("newpkg: fetching Packages...\n");
    if (fetch_to(url, REPO_IDX) != 0) {
        fputf(1, "newpkg: failed to fetch %s\n", url);
        return 1;
    }

    rlen = read_whole(REPO_REL, (u8 *)rel, sizeof(rel) - 1);
    if (rlen < 0 || release_crc(rel, rlen, &want) != 0) {
        fputf(1, "newpkg: Release has no CRC32 field\n");
        return 1;
    }
    {
        long ilen = read_whole(REPO_IDX, g_idx, IDX_MAX);

        if (ilen <= 0) {
            fputf(1, "newpkg: empty Packages index\n");
            return 1;
        }
        got = newpkg_crc32(g_idx, (newpkg_usize)ilen);
    }
    if (got != want) {
        fputf(1, "newpkg: Packages CRC mismatch (want %08x, got %08x)\n",
              want, got);
        return 1;
    }
    putf("newpkg: updated package index (CRC %08x)\n", got);
    return 0;
}

/* --- index stanza walking ------------------------------------------------ */

/* Field lookup inside one stanza (a NUL-terminated block of "Key: value\n"
 * lines). Returns pointer to value (static scratch) or 0. */
static const char *stanza_field(const char *block, const char *key)
{
    static char val[REPO_URL_MAX];
    const char *p = block;
    u64 klen = nstrlen(key);

    while (*p) {
        const char *e = p;

        while (*e && *e != '\n')
            e++;
        if ((u64)(e - p) > klen + 2 &&
            nstrncmp(p, key, (u64)klen) == 0 && p[klen] == ':') {
            const char *v = p + klen + 1;
            u64 n = 0;

            while (*v == ' ')
                v++;
            while (*v && *v != '\n' && n + 1 < sizeof(val)) {
                val[n++] = *v++;
            }
            val[n] = 0;
            return val;
        }
        p = *e ? e + 1 : e;
    }
    return 0;
}

/* Parse a hex CRC32 field. */
static int parse_hex32(const char *s, u32 *out)
{
    u32 v = 0;
    int digits = 0;

    if (!s)
        return -1;
    while (*s) {
        u32 d;

        if (*s >= '0' && *s <= '9')
            d = (u32)(*s - '0');
        else if (*s >= 'a' && *s <= 'f')
            d = (u32)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F')
            d = (u32)(*s - 'A' + 10);
        else
            return -1;
        v = (v << 4) | d;
        digits++;
        s++;
    }
    if (digits == 0 || digits > 8)
        return -1;
    *out = v;
    return 0;
}

/* Ensure the index is cached; fetch it if missing. Returns index length. */
static long ensure_index(void)
{
    long n = read_whole(REPO_IDX, g_idx, IDX_MAX);

    if (n > 0)
        return n;
    if (cmd_update() != 0)
        return -1;
    return read_whole(REPO_IDX, g_idx, IDX_MAX);
}

int cmd_repo_search(const char *query)
{
    long ilen = ensure_index();
    long pos = 0;
    int found = 0;

    if (ilen <= 0) {
        fputf(1, "newpkg: no package index (run 'newpkg update')\n");
        return 1;
    }
    g_idx[ilen] = 0;
    while (pos < ilen) {
        long start = pos;
        long end = pos;
        char name[REPO_URL_MAX], ver[REPO_URL_MAX], desc[REPO_URL_MAX];
        char arch[REPO_URL_MAX];
        const char *f;

        while (end < ilen && !(g_idx[end] == '\n' && g_idx[end + 1] == '\n'))
            end++;
        g_idx[end] = 0;
        f = stanza_field((const char *)g_idx + start, "Package");
        c_copy(name, f ? f : "", sizeof(name));
        f = stanza_field((const char *)g_idx + start, "Version");
        c_copy(ver, f ? f : "?", sizeof(ver));
        f = stanza_field((const char *)g_idx + start, "Description");
        c_copy(desc, f ? f : "", sizeof(desc));
        f = stanza_field((const char *)g_idx + start, "Architecture");
        c_copy(arch, f ? f : "?", sizeof(arch));
        if (name[0] && (!query || !*query ||
                        str_contains_local(name, query) ||
                        str_contains_local(desc, query))) {
            putf("%s/%s %s %s\n", name, ver, arch, desc);
            found++;
        }
        pos = end + 2;
    }
    if (!found)
        fputf(1, "newpkg: no packages match '%s'\n", query ? query : "");
    return 0;
}

/* --- install by name ----------------------------------------------------- */

int cmd_install_name(const char *name)
{
    char repo[REPO_URL_MAX];
    long ilen = ensure_index();
    long pos = 0;
    long best = -1;
    char best_ver[REPO_URL_MAX] = "";
    char ver[REPO_URL_MAX], fn[REPO_URL_MAX], crc_str[REPO_URL_MAX], arch[REPO_URL_MAX];
    const char *f;
    u32 want = 0;
    char url[REPO_URL_MAX * 2];
    char dst[160];
    long fd;
    u8 blob[4096];
    u64 total = 0;
    u32 got;

    if (ilen <= 0) {
        fputf(1, "newpkg: no package index (run 'newpkg update')\n");
        return 1;
    }
    g_idx[ilen] = 0;
    while (pos < ilen) {
        long start = pos;
        long end = pos;
        const char *pname;

        while (end < ilen && !(g_idx[end] == '\n' && g_idx[end + 1] == '\n'))
            end++;
        g_idx[end] = 0;
        pname = stanza_field((const char *)g_idx + start, "Package");
        if (pname && c_eq(pname, name)) {
            char cand[REPO_URL_MAX];
            const char *vf = stanza_field((const char *)g_idx + start, "Version");

            c_copy(cand, vf ? vf : "", sizeof(cand));
            if (best < 0 || newpkg_vercmp(cand, best_ver) > 0) {
                best = start;
                c_copy(best_ver, cand, sizeof(best_ver));
            }
        }
        pos = end + 2;
    }
    if (best < 0) {
        fputf(1, "newpkg: package '%s' not in index\n", name);
        return 1;
    }
    pos = best;
    f = stanza_field((const char *)g_idx + pos, "Version"); c_copy(ver, f ? f : "?", sizeof(ver));
    f = stanza_field((const char *)g_idx + pos, "Filename"); c_copy(fn, f ? f : "", sizeof(fn));
    f = stanza_field((const char *)g_idx + pos, "NEWOS-CRC32"); c_copy(crc_str, f ? f : "", sizeof(crc_str));
    f = stanza_field((const char *)g_idx + pos, "Architecture"); c_copy(arch, f ? f : "?", sizeof(arch));
    if (!fn[0] || !crc_str[0] || parse_hex32(crc_str, &want) != 0 ||
        (nstrcmp(arch, REPO_ARCH) != 0 && nstrcmp(arch, "any") != 0)) {
        fputf(1, "newpkg: bad index entry for '%s'\n", name);
        return 1;
    }
    first_repo(repo, sizeof(repo));
    if (!*repo) {
        fputf(1, "newpkg: no repositories configured\n");
        return 1;
    }
    if (mkdir_p(CACHE_DIR) != 0)
        return 1;

    c_copy(dst, CACHE_DIR, sizeof(dst));
    c_append(dst, sizeof(dst), "/");
    c_append(dst, sizeof(dst), fn);
    /* Filename from the index is repo-relative (pool/main/...); flatten it
     * for the local cache by using only the last path component. */
    {
        const char *slash = dst + nstrlen(CACHE_DIR) + 1;
        const char *last = slash;
        char flat[128];

        while (*slash) {
            if (*slash == '/')
                last = slash + 1;
            slash++;
        }
        c_copy(flat, last, sizeof(flat));
        c_copy(dst, CACHE_DIR, sizeof(dst));
        c_append(dst, sizeof(dst), "/");
        c_append(dst, sizeof(dst), flat);
    }

    c_copy(url, repo, sizeof(url));
    c_append(url, sizeof(url), "/");
    c_append(url, sizeof(url), fn);
    putf("newpkg: downloading %s %s...\n", name, ver);
    if (fetch_to(url, dst) != 0) {
        fputf(1, "newpkg: download failed: %s\n", url);
        return 1;
    }

    /* Verify the whole .new CRC32 before handing it to the installer. */
    got = newpkg_crc32_begin();
    fd = sys_open(dst, O_READ);
    if (fd < 0)
        return 1;
    for (;;) {
        long n = sys_read((int)fd, blob, sizeof(blob));

        if (n < 0) {
            sys_close((int)fd);
            return 1;
        }
        if (n == 0)
            break;
        got = newpkg_crc32_update(got, blob, (newpkg_usize)n);
        total += (u64)n;
    }
    sys_close((int)fd);
    got = newpkg_crc32_end(got);
    if (got != want) {
        fputf(1, "newpkg: package CRC mismatch (want %08x, got %08x)\n",
              want, got);
        sys_unlink(dst);
        return 1;
    }
    putf("newpkg: verified %u bytes (CRC %08x)\n", (u32)total, got);
    return cmd_install(dst, 0);
}

/* --- upgrade ------------------------------------------------------------- */

/* Find a package's newest version in the cached index. Returns 0 on hit. */
static int index_best_version(const char *name, char *ver_out, u64 cap)
{
    long ilen = read_whole(REPO_IDX, g_idx, IDX_MAX);
    long pos = 0;
    int found = 0;

    if (ilen <= 0)
        return -1;
    g_idx[ilen] = 0;
    while (pos < ilen) {
        long start = pos;
        long end = pos;
        char pname[REPO_URL_MAX], ver[REPO_URL_MAX];
        const char *f;

        while (end < ilen && !(g_idx[end] == '\n' && g_idx[end + 1] == '\n'))
            end++;
        g_idx[end] = 0;
        f = stanza_field((const char *)g_idx + start, "Package");
        c_copy(pname, f ? f : "", sizeof(pname));
        if (c_eq(pname, name)) {
            f = stanza_field((const char *)g_idx + start, "Version");
            c_copy(ver, f ? f : "", sizeof(ver));
            if (!found || newpkg_vercmp(ver, ver_out) > 0) {
                c_copy(ver_out, ver, cap);
                found = 1;
            }
        }
        pos = end + 2;
    }
    return found ? 0 : -1;
}

/* Upgrade all installed packages that have a newer version in the index. */
int cmd_upgrade_repo(void)
{
    static struct nsh_dirent dents[64];
    long n, i;
    int upgraded = 0, checked = 0;

    if (ensure_index() <= 0) {
        fputf(1, "newpkg: no package index (run 'newpkg update')\n");
        return 1;
    }
    n = sys_readdir(DB_DIR, dents, sizeof(dents));
    if (n < 0) {
        nputs("no packages installed\n");
        return 0;
    }
    for (i = 0; i < n; i++) {
        u64 L = nstrlen(dents[i].name);
        char name[NEWPKG_NAME_MAX], cur[NEWPKG_VER_MAX], avail[NEWPKG_VER_MAX];

        if (L < 6 || nstrcmp(dents[i].name + L - 5, ".info") != 0)
            continue;
        {
            u64 k;

            for (k = 0; k + 5 < L && k + 1 < sizeof(name); k++)
                name[k] = dents[i].name[k];
            name[k] = 0;
        }
        if (db_read_key(name, "version", cur, sizeof(cur)) != 0)
            continue;
        checked++;
        if (index_best_version(name, avail, sizeof(avail)) != 0)
            continue;
        if (newpkg_vercmp(avail, cur) > 0) {
            putf("upgrade: %s %s -> %s\n", name, cur, avail);
            if (cmd_install_name(name) == 0)
                upgraded++;
            else
                fputf(1, "upgrade: failed for %s\n", name);
        }
    }
    if (!checked)
        nputs("no packages installed\n");
    else if (!upgraded)
        nputs("upgrade: all packages up to date\n");
    return 0;
}
