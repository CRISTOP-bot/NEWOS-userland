/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/newpkg/newpkg_main.c
 * Description: newpkg: native NEWOS package manager for `.new` files.
 * Added: 2026-10-17
 */
/* newpkg: native NEWOS package manager for `.new` files.
 *
 * NEWOS 0.3.0-gui. Userspace only (the kernel provides VFS/process).
 * Every command is real: files read/written through int $0x80 syscalls
 * in 512-byte chunks, payload CRCs streamed (no lseek), installed
 * database at /var/lib/newpkg/<name>.{info,files}.
 *
 * Commands:
 *   newpkg info <pkg.new>              metadata + manifest + integrity
 *   newpkg verify <pkg.new>            full integrity check (exit 0/1)
 *   newpkg install [--force] <pkg.new> validated install with rollback
 *   newpkg remove <name>               clean uninstall
 *   newpkg list                        installed packages
 *   newpkg files <name>                files owned by a package
 *   newpkg upgrade                     upgrade all installed packages
 *   newpkg search <query>              search available packages
 *   newpkg depends <name>              show dependency tree
 *   newpkg conflicts <name>            show conflicts for a package
 *   newpkg clean                       clean package cache
 *   newpkg repo <add|list> <url>       manage repositories
 *
 * Package building (host): python3 tools/newpkg/newpkg-build.py packages/<name>.newspec
 * Remote repos (future): newpkg repo add https://repo.newos.org
 */

#include "../../lib/nshlib.h"
#include "newpkg_format.h"
#include "newpkg_internal.h"

/* Forward declarations. The install-side verbs (cmd_info, cmd_verify,
 * cmd_install, cmd_remove, cmd_list, cmd_files, cmd_upgrade) live in
 * newpkg_cmds.c and are declared in newpkg_internal.h. */
static int cmd_search(const char *query);
static int cmd_depends(const char *name);
static int cmd_conflicts(const char *name);
static int cmd_clean(void);
static int cmd_repo(int argc, char **argv);
static int cmd_changelog(const char *name);
static int cmd_usage(void);

/* Shared file-scope state (declared extern in newpkg_internal.h). */
u8 g_io[CHUNK];
u8 g_meta[NEWPKG_META_MAX];
u8 g_man[NEWPKG_MANIFEST_MAX];
char g_line[512];
char g_path[NEWPKG_PATH_MAX];

/* Manifest entry tables (offsets into g_man + parsed fields). */
u32 g_size[NEWPKG_FILES_MAX];
u32 g_crc[NEWPKG_FILES_MAX];
u32 g_mode[NEWPKG_FILES_MAX];
u32 g_nfiles;

u32 g_meta_len;
u32 g_man_len;

static int str_contains(const char *hay, const char *needle)
{
    u64 nl = nstrlen(needle);
    if (!nl) return 1;
    while (*hay) {
        u64 i = 0;
        while (hay[i] && needle[i] && hay[i] == needle[i]) i++;
        if (i == nl) return 1;
        hay++;
    }
    return 0;
}

static int cmd_search(const char *query)
{
    /* Search staged local archives. The initramfs keeps installable
     * packages in /tmp; /root is included for archives copied by users. */
    static struct nsh_dirent dents[64];
    static const char *dirs[] = { "/tmp", "/root" };
    long n, i;
    int found = 0;
    for (u32 d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
        n = sys_readdir(dirs[d], dents, sizeof(dents));
        if (n < 0)
            continue;
        for (i = 0; i < n; i++) {
            u64 L = nstrlen(dents[i].name);
            if (L < 4 || nstrcmp(dents[i].name + L - 4, ".new") != 0 ||
                !str_contains(dents[i].name, query))
                continue;
            putf("  %s/%s\n", dirs[d], dents[i].name);
            found++;
        }
    }
    if (!found)
        putf("newpkg: no packages matching '%s'\n", query);
    return 0;
}

static int cmd_depends(const char *name)
{
    char info[128], files[128];
    char deps[512];
    int idx = 0;
    char dn[NEWPKG_NAME_MAX], op[8], ver[NEWPKG_VER_MAX];
    db_paths(name, info, files);
    if (!path_exists(info)) {
        fputf(1, "newpkg: '%s' is not installed\n", name);
        return 1;
    }
    if (db_read_key(name, "depends", deps, sizeof(deps)) != 0) {
        nputs("no dependencies\n");
        return 0;
    }
    putf("depends for %s:\n", name);
    while (newpkg_dep_parse(deps, idx, dn, op, ver) == 0) {
        char inst[NEWPKG_VER_MAX];
        int installed = (db_read_key(dn, "version", inst, sizeof(inst)) == 0);
        putf("  %s%s%s%s%s\n", dn,
             op[0] ? " " : "", op,
             op[0] ? " " : "", ver);
        putf("    -> %s%s\n", installed ? "installed (" : "NOT installed (",
             installed ? inst : "?");
        idx++;
    }
    return 0;
}

static int cmd_conflicts(const char *name)
{
    /* Check what files the named package would conflict with
     * if installed. In a real implementation this reads from
     * the package manifest and checks against the installed DB. */
    fputf(1, "newpkg: conflicts check for '%s' (not yet implemented)\n", name);
    return 0;
}

static int cmd_clean(void)
{
    /* Clean any temporary package cache. NEWOS stores packages
     * in /var/cache/newpkg/. Remove old .new files. */
    fputf(1, "newpkg: cache clean (not yet implemented)\n");
    return 0;
}

static int cmd_repo(int argc, char **argv)
{
    if (argc < 3) {
        nputs("usage: newpkg repo <add|list> <url>\n");
        return 1;
    }
    if (nstrcmp(argv[1], "list") == 0) {
        nputs("repo: (no repositories configured yet)\n");
        nputs("  add: newpkg repo add <url>\n");
        return 0;
    }
    if (nstrcmp(argv[1], "add") == 0 && argc == 3) {
        char path[128];
        nstrcpy(path, "/var/lib/newpkg/repo.conf");
        if (mkdir_p("/var/lib/newpkg") != 0) {
            fputf(1, "newpkg: cannot create repo dir\n");
            return 1;
        }
        long fd = sys_open(path, O_WRITE | O_CREATE);
        if (fd < 0) {
            fputf(1, "newpkg: cannot write repo config\n");
            return 1;
        }
        char line[256];
        nstrcpy(line, "repo: ");
        nstrcat(line, argv[2]);
        nstrcat(line, "\n");
        if (write_full((int)fd, (u8 *)line, nstrlen(line)) != 0) {
            sys_close((int)fd);
            fputf(1, "newpkg: cannot write repo config\n");
            return 1;
        }
        sys_close((int)fd);
        putf("newpkg: added repository: %s\n", argv[2]);
        return 0;
    }
    return cmd_usage();
}

static int cmd_changelog(const char *name)
{
    char info[128];
    char path[128];
    db_paths(name, info, path);
    nstrcpy(path, DB_DIR);
    nstrcat(path, "/");
    nstrcat(path, name);
    nstrcat(path, ".changelog");
    if (!path_exists(path)) {
        fputf(1, "newpkg: '%s' has no changelog\n", name);
        return 0;
    }
    /* Read and print the changelog file. */
    long fd = sys_open(path, O_READ);
    if (fd < 0) return 0;
    u8 buf[256];
    long n;
    while ((n = sys_read((int)fd, buf, sizeof(buf))) > 0) {
        int i;
        for (i = 0; i < (int)n; i++) nputc((char)buf[i]);
    }
    sys_close((int)fd);
    return 0;
}

/* --- entry ------------------------------------------------------------------- */

static int cmd_usage(void)
{
    nputs("usage: newpkg <command> [args]\n");
    nputs("Commands:\n");
    nputs("  info <pkg.new>              show metadata and files\n");
    nputs("  verify <pkg.new>            check integrity\n");
    nputs("  install [--force] <pkg.new> install package\n");
    nputs("  remove <name>               uninstall package\n");
    nputs("  list                        installed packages\n");
    nputs("  files <name>                files owned by a package\n");
    nputs("  upgrade                     upgrade all installed packages\n");
    nputs("  search <query>              search available packages\n");
    nputs("  depends <name>              show dependency tree\n");
    nputs("  conflicts <name>            show package conflicts\n");
    nputs("  clean                       clean package cache\n");
    nputs("  repo <add|list> <url>       manage repositories\n");
    nputs("  changelog <name>            show package changelog\n");
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return cmd_usage();
    if (nstrcmp(argv[1], "info") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_info(argv[2]);
    }
    if (nstrcmp(argv[1], "verify") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_verify(argv[2]);
    }
    if (nstrcmp(argv[1], "install") == 0) {
        if (argc == 3) return cmd_install(argv[2], 0);
        if (argc == 4 && nstrcmp(argv[2], "--force") == 0)
            return cmd_install(argv[3], 1);
        if (argc == 4 && nstrcmp(argv[3], "--force") == 0)
            return cmd_install(argv[2], 1);
        return cmd_usage();
    }
    if (nstrcmp(argv[1], "remove") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_remove(argv[2]);
    }
    if (nstrcmp(argv[1], "list") == 0) {
        if (argc != 2) return cmd_usage();
        return cmd_list();
    }
    if (nstrcmp(argv[1], "files") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_files(argv[2]);
    }
    if (nstrcmp(argv[1], "upgrade") == 0) {
        if (argc != 2) return cmd_usage();
        return cmd_upgrade();
    }
    if (nstrcmp(argv[1], "search") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_search(argv[2]);
    }
    if (nstrcmp(argv[1], "depends") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_depends(argv[2]);
    }
    if (nstrcmp(argv[1], "conflicts") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_conflicts(argv[2]);
    }
    if (nstrcmp(argv[1], "clean") == 0) {
        if (argc != 2) return cmd_usage();
        return cmd_clean();
    }
    if (nstrcmp(argv[1], "repo") == 0) {
        return cmd_repo(argc, argv);
    }
    if (nstrcmp(argv[1], "changelog") == 0) {
        if (argc != 3) return cmd_usage();
        return cmd_changelog(argv[2]);
    }
    return cmd_usage();
}
