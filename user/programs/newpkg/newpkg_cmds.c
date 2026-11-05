/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/newpkg/newpkg_cmds.c
 * Description: newpkg install-side commands (info/verify/install/remove/...).
 * Added: 2026-10-30
 */
/* newpkg install-side commands: the verbs that act on a package file or on
 * the installed database (info, verify, install, remove, list, files,
 * upgrade). Every command is a thin policy layer over newpkg_pkg.c (load,
 * dependency/conflict checks, streamed extract + CRC verify, rollback).
 * The pure query verbs (search, depends, conflicts, clean, repo,
 * changelog) stay next to main() in newpkg_main.c.
 */

#include "../../lib/nshlib.h"
#include "newpkg_format.h"
#include "newpkg_internal.h"

/* --- commands ---------------------------------------------------------------- */

int cmd_info(const char *path)
{
    char v[512];
    u32 k;
    static const char *keys[] = {
        "name", "version", "arch", "desc", "license",
        "maintainer", "build", "newos-min", 0
    };
    int i;
    long fd = load_package(path);
    if (fd < 0)
        return 1;
    sys_close((int)fd);         /* payload not needed for info */
    for (i = 0; keys[i]; i++) {
        if (newpkg_meta_get(g_meta, g_meta_len, keys[i], v, sizeof(v)) ==
            0)
            putf("%s: %s\n", keys[i], v);
    }
    if (newpkg_meta_get(g_meta, g_meta_len, "depends", v, sizeof(v)) == 0 &&
        nstrlen(v) > 0)
        putf("depends: %s\n", v);
    putf("files: %u\n", (unsigned long)g_nfiles);
    for (k = 0; k < g_nfiles; k++) {
        entry_path(k);
        nputs("  ");
        print_mode(g_mode[k]);
        putf(" %u ", (unsigned long)g_size[k]);
        print_hex8(g_crc[k]);
        putf(" %s\n", g_path);
    }
    putf("integrity: OK (header+metadata+manifest CRC)\n");
    return 0;
}

int cmd_verify(const char *path)
{
    long fd = load_package(path);
    int rc;
    if (fd < 0)
        return 1;
    rc = stream_payload((int)fd, 0, 0);
    sys_close((int)fd);
    if (rc != 0)
        return 1;
    putf("verify: %s: OK (%u files)\n", path, (unsigned long)g_nfiles);
    return 0;
}

int cmd_install(const char *path, int force)
{
    char name[NEWPKG_NAME_MAX], ver[NEWPKG_VER_MAX];
    char arch[32], min[NEWPKG_VER_MAX];
    struct nsh_uname u;
    long fd;
    u32 k, done = 0;
    char info[128], files[128];
    long db;

    fd = load_package(path);
    if (fd < 0)
        return 1;
    if (newpkg_meta_get(g_meta, g_meta_len, "name", name, sizeof(name)) !=
            0 ||
        newpkg_meta_get(g_meta, g_meta_len, "version", ver,
                        sizeof(ver)) != 0 ||
        newpkg_meta_get(g_meta, g_meta_len, "arch", arch,
                        sizeof(arch)) != 0 ||
        newpkg_meta_get(g_meta, g_meta_len, "newos-min", min,
                        sizeof(min)) != 0) {
        sys_close((int)fd);
        return 1;
    }
    /* Architecture + OS version gates (real uname data). */
    if (sys_uname(&u) != 0) {
        fputf(1, "newpkg: cannot read system version\n");
        sys_close((int)fd);
        return 1;
    }
    if (newpkg_arch_ok(arch, u.machine) != 0) {
        fputf(1, "newpkg: wrong architecture '%s' (system %s)\n", arch,
              u.machine);
        sys_close((int)fd);
        return 1;
    }
    if (newpkg_vercmp(u.release, min) < 0) {
        fputf(1, "newpkg: needs NEWOS >= %s (system %s)\n", min,
              u.release);
        sys_close((int)fd);
        return 1;
    }
    if (check_deps(name) != 0) {
        sys_close((int)fd);
        return 1;
    }
    /* /etc/nsh.rc runs on every shell.  Reinstalling an unchanged package
     * would rewrite dozens of files and make every new terminal sluggish;
     * --force remains the explicit repair/overwrite path. */
    if (!force) {
        char installed[NEWPKG_VER_MAX];
        if (db_read_key(name, "version", installed, sizeof(installed)) == 0 &&
            nstrcmp(installed, ver) == 0) {
            putf("install: %s %s: already installed\n", name, ver);
            sys_close((int)fd);
            return 0;
        }
    }
    /* Conflict scan before touching anything. */
    for (k = 0; k < g_nfiles; k++) {
        char owner[NEWPKG_NAME_MAX];
        entry_path(k);
        if (!path_exists(g_path))
            continue;
        if (!db_find_owner(g_path, owner, sizeof(owner)))
            owner[0] = '\0';
        if (owner[0] && nstrcmp(owner, name) == 0)
            continue;           /* reinstall/upgrade of our own file */
        if (!force) {
            if (owner[0])
                fputf(1, "newpkg: conflict: '%s' owned by '%s'\n",
                      g_path, owner);
            else
                fputf(1, "newpkg: conflict: '%s' exists (unowned)\n",
                      g_path);
            fputf(1, "newpkg: use --force to overwrite\n");
            sys_close((int)fd);
            return 1;
        }
    }
    /* Extract + verify in one streaming pass. */
    if (stream_payload((int)fd, 1, &done) != 0) {
        rollback(name, done);
        sys_close((int)fd);
        return 1;
    }
    sys_close((int)fd);
    /* Register: DB_DIR/<name>.{info,files}. The .info is the metadata
     * plus install stamp; .files is the manifest verbatim. */
    if (mkdir_p(DB_DIR) != 0) {
        fputf(1, "newpkg: cannot create %s\n", DB_DIR);
        rollback(name, done);
        return 1;
    }
    db_paths(name, info, files);
    /* Replace stale registrations (no O_TRUNC: unlink first). */
    sys_unlink(info);
    sys_unlink(files);
    db = sys_open(info, O_WRITE | O_CREATE);
    if (db < 0) {
        fputf(1, "newpkg: cannot register '%s'\n", name);
        rollback(name, done);
        return 1;
    }
    if (write_full((int)db, g_meta, g_meta_len) != 0) {
        sys_close((int)db);
        fputf(1, "newpkg: cannot register '%s'\n", name);
        rollback(name, done);
        return 1;
    }
    {
        struct nsh_time t;
        char stamp[96];
        u64 p = 0, q;
        if (sys_gettime(&t) == 0) {
            /* YYYY-MM-DD HH:MM:SS (decimal, zero-padded). */
            stamp[0] = '\0';
            nstrcpy(stamp, "installed-at: ");
            p = nstrlen(stamp);
            q = (u64)t.year;
            stamp[p++] = (char)('0' + (q / 1000) % 10);
            stamp[p++] = (char)('0' + (q / 100) % 10);
            stamp[p++] = (char)('0' + (q / 10) % 10);
            stamp[p++] = (char)('0' + q % 10);
            stamp[p++] = '-';
            stamp[p++] = (char)('0' + (t.mon / 10) % 10);
            stamp[p++] = (char)('0' + t.mon % 10);
            stamp[p++] = '-';
            stamp[p++] = (char)('0' + (t.day / 10) % 10);
            stamp[p++] = (char)('0' + t.day % 10);
            stamp[p++] = ' ';
            stamp[p++] = (char)('0' + (t.hour / 10) % 10);
            stamp[p++] = (char)('0' + t.hour % 10);
            stamp[p++] = ':';
            stamp[p++] = (char)('0' + (t.min / 10) % 10);
            stamp[p++] = (char)('0' + t.min % 10);
            stamp[p++] = ':';
            stamp[p++] = (char)('0' + (t.sec / 10) % 10);
            stamp[p++] = (char)('0' + t.sec % 10);
            stamp[p++] = '\n';
            stamp[p] = '\0';
            if (write_full((int)db, (u8 *)stamp, p) != 0) {
                sys_close((int)db);
                fputf(1, "newpkg: cannot register '%s'\n", name);
                rollback(name, done);
                return 1;
            }
        }
    }
    sys_close((int)db);
    db = sys_open(files, O_WRITE | O_CREATE);
    if (db < 0) {
        fputf(1, "newpkg: cannot register '%s'\n", name);
        rollback(name, done);
        return 1;
    }
    if (write_full((int)db, g_man, g_man_len) != 0) {
        sys_close((int)db);
        fputf(1, "newpkg: cannot register '%s'\n", name);
        rollback(name, done);
        return 1;
    }
    sys_close((int)db);
    putf("install: %s %s: OK (%u files)\n", name, ver, (unsigned long)g_nfiles);
    return 0;
}

int cmd_remove(const char *name)
{
    char info[128], files[128];
    long fd, r;
    u64 blen = 0;
    u32 removed = 0, warned = 0;
    if (valid_name(name) != 0)
        return fail("newpkg", "bad package name");
    db_paths(name, info, files);
    if (!path_exists(info) || !path_exists(files)) {
        fputf(1, "newpkg: '%s' is not installed\n", name);
        return 1;
    }
    fd = sys_open(files, O_READ);
    if (fd < 0) {
        fputf(1, "newpkg: cannot read database for '%s'\n", name);
        return 1;
    }
    blen = 0;
    for (;;) {
        if (blen >= sizeof(g_line) - 1)
            break;
        r = sys_read((int)fd, g_io, 1);
        if (r < 0) {
            sys_close((int)fd);
            return fail("newpkg", "database read error");
        }
        if (r == 0)
            break;
        if (g_io[0] == '\n') {
            char ep[NEWPKG_PATH_MAX];
            u32 a, b, c;
            g_line[blen] = '\0';
            blen = 0;
            if (newpkg_manifest_parse_line(g_line, ep, &a, &b, &c) != 0)
                continue;
            if (sys_unlink(ep) != 0) {
                if (!warned)
                    fputf(1, "newpkg: warning: '%s' already gone\n",
                          ep);
                warned = 1;
                continue;
            }
            removed++;
            continue;
        }
        g_line[blen++] = (char)g_io[0];
    }
    sys_close((int)fd);
    sys_unlink(files);
    sys_unlink(info);
    putf("remove: %s: OK (%u files removed)\n", name, (unsigned long)removed);
    return 0;
}

int cmd_list(void)
{
    static struct nsh_dirent dents[64];
    long n, i;
    int count = 0;
    n = sys_readdir(DB_DIR, dents, sizeof(dents));
    if (n < 0) {
        nputs("no packages installed\n");
        return 0;
    }
    for (i = 0; i < n; i++) {
        u64 L = nstrlen(dents[i].name);
        char name[NEWPKG_NAME_MAX], ver[NEWPKG_VER_MAX];
        u64 k;
        if (L < 6 || nstrcmp(dents[i].name + L - 5, ".info") != 0)
            continue;
        for (k = 0; k + 5 < L && k + 1 < sizeof(name); k++)
            name[k] = dents[i].name[k];
        name[k] = '\0';
        if (db_read_key(name, "version", ver, sizeof(ver)) != 0)
            nstrcpy(ver, "?");
        putf("%s %s\n", name, ver);
        count++;
    }
    if (!count)
        nputs("no packages installed\n");
    return 0;
}

int cmd_files(const char *name)
{
    char info[128], files[128];
    long fd, r;
    u64 blen = 0;
    if (valid_name(name) != 0)
        return fail("newpkg", "bad package name");
    db_paths(name, info, files);
    if (!path_exists(files)) {
        fputf(1, "newpkg: '%s' is not installed\n", name);
        return 1;
    }
    fd = sys_open(files, O_READ);
    if (fd < 0)
        return fail("newpkg", "database read error");
    blen = 0;
    for (;;) {
        if (blen >= sizeof(g_line) - 1)
            break;
        r = sys_read((int)fd, g_io, 1);
        if (r < 0) {
            sys_close((int)fd);
            return fail("newpkg", "database read error");
        }
        if (r == 0)
            break;
        if (g_io[0] == '\n') {
            char ep[NEWPKG_PATH_MAX];
            u32 a, b, c;
            g_line[blen] = '\0';
            blen = 0;
            if (newpkg_manifest_parse_line(g_line, ep, &a, &b, &c) != 0)
                continue;
            putf("%s\n", ep);
            continue;
        }
        g_line[blen++] = (char)g_io[0];
    }
    sys_close((int)fd);
    return 0;
}

int cmd_upgrade(void)
{
    static struct nsh_dirent dents[64];
    long n, i;
    int count = 0;
    n = sys_readdir(DB_DIR, dents, sizeof(dents));
    if (n < 0) {
        nputs("no packages installed\n");
        return 0;
    }
    for (i = 0; i < n; i++) {
        u64 L = nstrlen(dents[i].name);
        char name[NEWPKG_NAME_MAX], ver[NEWPKG_VER_MAX];
        if (L < 6 || nstrcmp(dents[i].name + L - 5, ".info") != 0)
            continue;
        { u64 k; for (k = 0; k + 5 < L && k + 1 < sizeof(name); k++)
            name[k] = dents[i].name[k];
          name[k] = '\0'; }
        if (db_read_key(name, "version", ver, sizeof(ver)) != 0)
            nstrcpy(ver, "?");
        /* Check if an update is available (placeholder: would
         * query the repo in a real implementation). */
        putf("upgrade: %s %s: checking...\n", name, ver);
        count++;
    }
    if (!count)
        nputs("nothing to upgrade\n");
    else
        nputs("upgrade: run 'newpkg install <pkg>.new' for updates\n");
    return 0;
}
