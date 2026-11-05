/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/newpkg/newpkg_pkg.c
 * Description: newpkg package/database core (load, DB, deps, payload).
 * Added: 2026-10-30
 */
/* newpkg package/database core: everything that touches `.new` bytes and
 * the installed database at DB_DIR (loading + manifest parse into the
 * shared tables, DB path/lookup helpers, dependency and cycle checks, the
 * streaming payload verifier/installer and the install rollback). No
 * command-line policy lives here: the commands call in through the
 * newpkg_internal.h prototypes.
 */

#include "../../lib/nshlib.h"
#include "newpkg_format.h"
#include "newpkg_internal.h"

/* File-scope state private to this module: entry offsets into g_man, the
 * database read scratch and the decoded package header. */
static u32 g_off[NEWPKG_FILES_MAX];
static u8 g_db[NEWPKG_META_MAX + 256];
static struct newpkg_header g_hdr;

/* --- package loading ---------------------------------------------------------
 * Opens `path`, buffers header+metadata+manifest, verifies section CRCs,
 * parses the manifest into g_off/g_size/g_crc/g_mode (rejecting unsorted
 * or duplicate entries). Leaves the fd positioned at the payload start
 * (strictly sequential: NEWOS has no seek). Returns the fd or -1.
 */

int load_package(const char *path)
{
    static const char *required[] = {
        "name", "version", "arch", "desc", "license",
        "maintainer", "build", "newos-min", "depends", 0
    };
    long fd;
    u8 hdr[NEWPKG_HEADER_SIZE];
    u32 i;
    int first = 1;
    char probe[NEWPKG_PATH_MAX];

    fd = sys_open(path, O_READ);
    if (fd < 0) {
        fputf(1, "newpkg: cannot open '%s'\n", path);
        return -1;
    }
    if (read_full((int)fd, hdr, sizeof(hdr)) != 0) {
        fputf(1, "newpkg: '%s': truncated header\n", path);
        sys_close((int)fd);
        return -1;
    }
    if (newpkg_header_decode(hdr, &g_hdr) != 0) {
        fputf(1, "newpkg: '%s': bad header (magic/version/flags/crc)\n",
              path);
        sys_close((int)fd);
        return -1;
    }
    g_meta_len = g_hdr.meta_len;
    g_man_len = g_hdr.manifest_len;
    if (read_full((int)fd, g_meta, g_meta_len) != 0 ||
        read_full((int)fd, g_man, g_man_len) != 0) {
        fputf(1, "newpkg: '%s': truncated sections\n", path);
        sys_close((int)fd);
        return -1;
    }
    if (newpkg_crc32(g_meta, g_meta_len) != g_hdr.meta_crc ||
        newpkg_crc32(g_man, g_man_len) != g_hdr.manifest_crc) {
        fputf(1, "newpkg: '%s': section checksum mismatch\n", path);
        sys_close((int)fd);
        return -1;
    }
    if (g_man[g_man_len - 1] != '\n') {
        fputf(1, "newpkg: '%s': malformed manifest\n", path);
        sys_close((int)fd);
        return -1;
    }
    for (i = 0; i < 9; i++) {
        if (newpkg_meta_get(g_meta, g_meta_len, required[i], g_line,
                            sizeof(g_line)) != 0) {
            fputf(1, "newpkg: '%s': metadata lacks '%s'\n", path,
                  required[i]);
            sys_close((int)fd);
            return -1;
        }
    }
    if (newpkg_meta_get(g_meta, g_meta_len, "name", g_line,
                        sizeof(g_line)) != 0 ||
        valid_name(g_line) != 0) {
        fputf(1, "newpkg: '%s': bad package name\n", path);
        sys_close((int)fd);
        return -1;
    }
    /* Split the manifest into entries. */
    g_nfiles = 0;
    {
        u32 pos = 0, ls = 0;
        while (pos < g_man_len) {
            u32 len = 0;
            if (g_man[pos] == '\n') {
                fputf(1, "newpkg: '%s': empty manifest line\n", path);
                sys_close((int)fd);
                return -1;
            }
            while (pos + len < g_man_len && g_man[pos + len] != '\n')
                len++;
            if (len == 0 || len >= sizeof(g_line) - 1) {
                fputf(1, "newpkg: '%s': bad manifest line\n", path);
                sys_close((int)fd);
                return -1;
            }
            if (g_nfiles >= NEWPKG_FILES_MAX) {
                fputf(1, "newpkg: '%s': too many files\n", path);
                sys_close((int)fd);
                return -1;
            }
            nmemcpy((u8 *)g_line, g_man + pos, len);
            g_line[len] = '\0';
            g_off[g_nfiles] = ls;
            if (newpkg_manifest_parse_line(g_line, probe, &g_size[g_nfiles],
                                           &g_crc[g_nfiles],
                                           &g_mode[g_nfiles]) != 0) {
                fputf(1, "newpkg: '%s': bad manifest entry\n", path);
                sys_close((int)fd);
                return -1;
            }
            /* Canonical order: strictly ascending, no duplicates. */
            if (!first && nstrcmp(probe, g_path) <= 0) {
                fputf(1, "newpkg: '%s': manifest not sorted\n", path);
                sys_close((int)fd);
                return -1;
            }
            nstrcpy(g_path, probe);
            first = 0;
            g_nfiles++;
            pos += len + 1;
            ls = pos;
        }
    }
    if (g_nfiles != g_hdr.file_count) {
        fputf(1, "newpkg: '%s': file count mismatch\n", path);
        sys_close((int)fd);
        return -1;
    }
    return (int)fd;
}

/* Copy the entry's path (index k) into g_path. Re-parses the manifest line
 * at g_off[k] (single source of truth: the buffered manifest). */
void entry_path(u32 k)
{
    u32 pos = g_off[k], len = 0;
    while (pos + len < g_man_len && g_man[pos + len] != '\n')
        len++;
    nmemcpy((u8 *)g_line, g_man + pos, len);
    g_line[len] = '\0';
    /* Parsing cannot fail here: load_package already accepted the line. */
    (void)newpkg_manifest_parse_line(g_line, g_path, &g_size[k], &g_crc[k],
                                     &g_mode[k]);
}

/* --- installed database ------------------------------------------------------
 * DB_DIR/<name>.info  = metadata copy + "installed-at: ..." + "files: N"
 * DB_DIR/<name>.files = manifest copy
 */

void db_paths(const char *name, char *info, char *files)
{
    nstrcpy(info, DB_DIR);
    nstrcat(info, "/");
    nstrcat(info, name);
    nstrcat(info, ".info");
    nstrcpy(files, DB_DIR);
    nstrcat(files, "/");
    nstrcat(files, name);
    nstrcat(files, ".files");
}

/* Read `key` from an installed package's .info file. */
int db_read_key(const char *name, const char *key, char *out, u64 cap)
{
    char info[128], files[128];
    long fd;
    u64 total = 0;
    long n;
    db_paths(name, info, files);
    fd = sys_open(info, O_READ);
    if (fd < 0)
        return -1;
    for (;;) {
        if (total >= sizeof(g_db))
            break;
        n = sys_read((int)fd, g_db + total, sizeof(g_db) - total);
        if (n < 0) {
            sys_close((int)fd);
            return -1;
        }
        if (n == 0)
            break;
        total += (u64)n;
    }
    sys_close((int)fd);
    if (total == 0 || total >= sizeof(g_db))
        return -1;
    return newpkg_meta_get(g_db, (u32)total, key, out, (newpkg_usize)cap);
}

/* Find which installed package owns `path` ("" when unowned). */
int db_find_owner(const char *path, char *owner, u64 cap)
{
    static struct nsh_dirent dents[64];
    long n, i;
    owner[0] = '\0';
    n = sys_readdir(DB_DIR, dents, sizeof(dents));
    if (n < 0)
        return 0;               /* no database yet: everything unowned */
    for (i = 0; i < n; i++) {
        u64 L = nstrlen(dents[i].name);
        char full[128];
        long fd, r;
        u64 blen = 0;
        /* Only "*.files" entries. */
        if (L < 7 || nstrcmp(dents[i].name + L - 6, ".files") != 0)
            continue;
        nstrcpy(full, DB_DIR);
        nstrcat(full, "/");
        nstrcat(full, dents[i].name);
        fd = sys_open(full, O_READ);
        if (fd < 0)
            continue;
        /* Stream lines; each is a manifest line whose 4th field is path. */
        blen = 0;
        for (;;) {
            if (blen >= sizeof(g_line) - 1)
                break;
            r = sys_read((int)fd, g_io, 1);
            if (r <= 0)
                break;
            if (g_io[0] == '\n') {
                char ep[NEWPKG_PATH_MAX];
                u32 a, b, c;
                g_line[blen] = '\0';
                if (newpkg_manifest_parse_line(g_line, ep, &a, &b, &c) ==
                        0 &&
                    nstrcmp(ep, path) == 0) {
                    u64 k;
                    for (k = 0; k + 6 < (u64)L && k + 1 < cap; k++)
                        owner[k] = dents[i].name[k];
                    owner[k] = '\0';
                    sys_close((int)fd);
                    return 1;
                }
                blen = 0;
                continue;
            }
            g_line[blen++] = (char)g_io[0];
        }
        sys_close((int)fd);
    }
    return owner[0] ? 1 : 0;
}

/* --- dependency checks ------------------------------------------------------- */

struct dep_ctx {
    char target[NEWPKG_NAME_MAX];
    char stack[16][NEWPKG_NAME_MAX];
    int depth;
};

/* Depth-first search from `name` through installed depends edges; reports
 * 1 if `target` is reachable (a cycle once the new package is added). */
static int dep_reaches(struct dep_ctx *ctx, const char *name)
{
    char deps[512], dn[NEWPKG_NAME_MAX], op[8], ver[NEWPKG_VER_MAX];
    int idx = 0, i;
    if (nstrcmp(name, ctx->target) == 0)
        return 1;
    if (ctx->depth >= 16)
        return 0;
    for (i = 0; i < ctx->depth; i++) {
        if (nstrcmp(ctx->stack[i], name) == 0)
            return 0;           /* already on this path: no new info */
    }
    if (db_read_key(name, "depends", deps, sizeof(deps)) != 0)
        return 0;               /* not installed or no deps: leaf */
    nstrcpy(ctx->stack[ctx->depth], name);
    ctx->depth++;
    while (newpkg_dep_parse(deps, idx, dn, op, ver) == 0) {
        if (dep_reaches(ctx, dn)) {
            ctx->depth--;
            return 1;
        }
        idx++;
    }
    ctx->depth--;
    return 0;
}

int check_deps(const char *pkg_name)
{
    char deps[512], dn[NEWPKG_NAME_MAX], op[8], ver[NEWPKG_VER_MAX];
    int idx = 0;
    struct dep_ctx ctx;
    if (newpkg_meta_get(g_meta, g_meta_len, "depends", deps,
                        sizeof(deps)) != 0)
        return -1;
    nstrcpy(ctx.target, pkg_name);
    ctx.depth = 0;
    while (newpkg_dep_parse(deps, idx, dn, op, ver) == 0) {
        char inst[NEWPKG_VER_MAX];
        if (nstrcmp(dn, pkg_name) == 0) {
            fputf(1, "newpkg: circular dependency on '%s'\n", dn);
            return -1;
        }
        if (dep_reaches(&ctx, dn)) {
            fputf(1, "newpkg: circular dependency via '%s'\n", dn);
            return -1;
        }
        if (db_read_key(dn, "version", inst, sizeof(inst)) != 0) {
            fputf(1, "newpkg: missing dependency '%s'\n", dn);
            return -1;
        }
        {
            int m = newpkg_dep_match(inst, op, ver);
            if (m != 1) {
                if (m < 0)
                    fputf(1, "newpkg: malformed constraint on '%s'\n",
                          dn);
                else
                    fputf(1,
                          "newpkg: incompatible dependency '%s' "
                          "(have %s)\n",
                          dn, inst);
                return -1;
            }
        }
        idx++;
    }
    return 0;
}

/* Stream the payload: write_mode 0 = verify only, 1 = install to VFS.
 * In install mode each file is conflict-checked by the caller first; the
 * per-file CRC decides commit/rollback per file. Returns 0 when every
 * byte matches, -1 otherwise. */
int stream_payload(int pkg_fd, int write_mode, u32 *done_count)
{
    u32 total_crc = newpkg_crc32_begin();
    u32 k;
    if (done_count)
        *done_count = 0;
    for (k = 0; k < g_nfiles; k++) {
        u32 left = g_size[k];
        u32 fcrc = newpkg_crc32_begin();
        int out = -1;
        entry_path(k);
        if (write_mode) {
            if (mkdir_parent(g_path) != 0) {
                fputf(1, "newpkg: cannot create parent of '%s'\n",
                      g_path);
                return -1;
            }
            /* No O_TRUNC in NEWOS: unlink a replaceable target first so
             * no stale tail survives a shorter rewrite. */
            if (path_exists(g_path) && sys_unlink(g_path) != 0) {
                fputf(1, "newpkg: cannot replace '%s'\n", g_path);
                return -1;
            }
            {
                long f = sys_open(g_path, O_WRITE | O_CREATE);
                if (f < 0) {
                    fputf(1, "newpkg: cannot create '%s'\n", g_path);
                    return -1;
                }
                out = (int)f;
                /* Touched from here on: any later failure (short
                 * payload, bad CRC) must roll this file back. */
                if (done_count)
                    *done_count = k + 1;
            }
        }
        while (left > 0) {
            u64 want = left > (u32)CHUNK ? (u64)CHUNK : (u64)left;
            long n = sys_read(pkg_fd, g_io, want);
            if (n <= 0) {
                fputf(1, "newpkg: truncated payload at '%s'\n", g_path);
                if (out >= 0)
                    sys_close(out);
                return -1;
            }
            fcrc = newpkg_crc32_update(fcrc, g_io, (newpkg_usize)n);
            total_crc = newpkg_crc32_update(total_crc, g_io,
                                            (newpkg_usize)n);
            if (write_mode && write_full(out, g_io, (u64)n) != 0) {
                fputf(1, "newpkg: write failed at '%s'\n", g_path);
                sys_close(out);
                return -1;
            }
            left -= (u32)n;
        }
        if (out >= 0)
            sys_close(out);
        if (newpkg_crc32_end(fcrc) != g_crc[k]) {
            fputf(1, "newpkg: checksum mismatch in '%s'\n", g_path);
            return -1;
        }
    }
    if (newpkg_crc32_end(total_crc) != g_hdr.payload_crc) {
        fputf(1, "newpkg: payload checksum mismatch\n");
        return -1;
    }
    return 0;
}

/* Remove the first `count` manifest entries (rollback after a failed
 * install) plus any half-written DB files for `pkg`. */
void rollback(const char *pkg, u32 count)
{
    u32 k;
    char info[128], files[128];
    for (k = 0; k < count && k < g_nfiles; k++) {
        entry_path(k);
        sys_unlink(g_path);
    }
    db_paths(pkg, info, files);
    sys_unlink(files);
    sys_unlink(info);
}
