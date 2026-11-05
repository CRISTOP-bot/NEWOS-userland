/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/openrc/rc-update/rc_update_main.c
 * Description: /sbin/rc-update: manage OpenRC runlevel membership.
 * Added: 2026-10-29
 */
/* rc-update - runlevel membership editor, NEWOS port.
 *
 *   rc-update add <service> <runlevel>
 *   rc-update del <service> <runlevel>
 *   rc-update show [<runlevel>]
 *
 * Upstream stores membership as a symlink /etc/runlevels/<level>/<svc> ->
 * /etc/init.d/<svc>. The NEWOS VFS has no symlinks, so the entry is an
 * indirection file holding the target path: same resolution contract,
 * readable with `cat`, honoured by openrc/rc-status through
 * rc_resolve_target(). */

#include "../librc/librc.h"

/* Upstream right-justifies the runlevel in an 18-column field:
 * `          boot | bootmisc`. nputf has no width flags, so the field is
 * padded here. */
static void print_entry(const char *level, const char *svc)
{
    for (int i = (int)nstrlen(level); i < 18; i++)
        nputc(' ');
    nputs(level);
    nputs(" | ");
    nputs(svc);
    nputc('\n');
}

static int show_level(const char *level)
{
    static struct nsh_dirent ents[64];
    char dir[RC_MAX_PATH];
    int n = 0;

    nstrcpy(dir, RC_RUNLEVEL_DIR "/");
    nstrcat(dir, level);
    if (sys_readdir(dir, ents, (u64)sizeof(ents)) < 0)
        return 0;
    for (int i = 0; i < 64 && ents[i].name[0]; i++) {
        char entry[RC_MAX_PATH];
        char target[RC_MAX_PATH];

        if (ents[i].name[0] == '.')
            continue;
        nstrcpy(entry, dir);
        nstrcat(entry, "/");
        nstrcat(entry, ents[i].name);
        print_entry(level, ents[i].name);
        if (rc_resolve_target(entry, ents[i].name, target,
                              (long)sizeof(target)) != 0)
            nputs("  -> missing init script\n");
        n++;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *cmd = argc > 1 ? argv[1] : "show";

    if (nstrcmp(cmd, "--help") == 0 || nstrcmp(cmd, "-h") == 0) {
        nputs("Usage: rc-update add <service> <runlevel>\n");
        nputs("       rc-update del <service> <runlevel>\n");
        nputs("       rc-update show [<runlevel>]\n");
        return 0;
    }
    if (nstrcmp(cmd, "show") == 0) {
        if (argc > 2)
            return show_level(argv[2]) ? 0 : 1;
        static struct nsh_dirent ents[32];
        if (sys_readdir(RC_RUNLEVEL_DIR, ents, (u64)sizeof(ents)) < 0)
            return fail("rc-update", "no runlevels installed");
        for (int i = 0; i < 32 && ents[i].name[0]; i++) {
            if (ents[i].name[0] == '.')
                continue;
            show_level(ents[i].name);
        }
        return 0;
    }
    if (nstrcmp(cmd, "add") != 0 && nstrcmp(cmd, "delete") != 0 &&
        nstrcmp(cmd, "del") != 0) {
        nputs("rc-update: unknown command ");
        nputs(cmd);
        nputc('\n');
        return 1;
    }
    if (argc < 4) {
        nputs("Usage: rc-update ");
        nputs(cmd);
        nputs(" <service> <runlevel>\n");
        return 1;
    }

    {
        const char *svc = argv[2];
        const char *level = argv[3];
        char script[RC_MAX_PATH];
        char entry[RC_MAX_PATH];
        char dir[RC_MAX_PATH];

        nstrcpy(script, RC_INITD_DIR "/");
        nstrcat(script, svc);
        if (!rc_file_exists(script)) {
            nputs("rc-update: no such init script: ");
            nputs(script);
            nputc('\n');
            return 1;
        }
        nstrcpy(dir, RC_RUNLEVEL_DIR "/");
        nstrcat(dir, level);
        nstrcpy(entry, dir);
        nstrcat(entry, "/");
        nstrcat(entry, svc);

        if (nstrcmp(cmd, "add") == 0) {
            if (rc_file_exists(entry)) {
                nputs("rc-update: ");
                nputs(svc);
                nputs(" is already in runlevel ");
                nputs(level);
                nputc('\n');
                return 0;
            }
            if (rc_mkdir_p(dir) != 0)
                return fail("rc-update", "cannot create the runlevel");
            {
                char body[RC_MAX_PATH + 1];
                nstrcpy(body, script);
                nstrcat(body, "\n");
                if (rc_write_file(entry, body) != 0)
                    return fail("rc-update", "cannot write the runlevel entry");
            }
            putf("rc-update: `%s' added to runlevel `%s'\n",
                 (u64)svc, (u64)level);
            return 0;
        }
        if (!rc_file_exists(entry)) {
            nputs("rc-update: ");
            nputs(svc);
            nputs(" is not in runlevel ");
            nputs(level);
            nputc('\n');
            return 1;
        }
        if (sys_unlink(entry) != 0)
            return fail("rc-update", "cannot remove the runlevel entry");
        putf("rc-update: `%s' removed from runlevel `%s'\n",
             (u64)svc, (u64)level);
        return 0;
    }
}
