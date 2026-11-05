/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/openrc/rc-service/rc_service_main.c
 * Description: /sbin/rc-service: run one OpenRC service action.
 * Added: 2026-10-29
 */
/* rc-service <service> <action...> - the upstream frontend for /sbin/openrc.
 * It resolves the service name against /etc/init.d and execs the manager
 * with the single-service form, so the solver, state files and output stay
 * in one place. */

#include "../librc/librc.h"

int main(int argc, char **argv)
{
    char *fwd[NSH_MAX_ARGS];
    int n = 0;
    long pid;

    if (argc < 2 || nstrcmp(argv[1], "--help") == 0) {
        nputs("Usage: rc-service <service> <action>\n");
        nputs("       rc-service --list\n");
        nputs("Actions: start stop restart status zap\n");
        return argc < 2 ? 1 : 0;
    }
    if (nstrcmp(argv[1], "--list") == 0) {
        static struct nsh_dirent ents[64];
        if (sys_readdir(RC_INITD_DIR, ents, (u64)sizeof(ents)) < 0)
            return fail("rc-service", "no init scripts installed");
        for (int i = 0; i < 64 && ents[i].name[0]; i++) {
            if (ents[i].name[0] == '.')
                continue;
            nputs(ents[i].name);
            nputc('\n');
        }
        return 0;
    }
    if (nstrlen(argv[1]) > RC_MAX_NAME - 1)
        return fail("rc-service", "service name too long");

    fwd[n++] = (char *)"/sbin/openrc";
    for (int i = 1; i < argc && n < NSH_MAX_ARGS; i++)
        fwd[n++] = argv[i];
    fwd[n] = (char *)0;

    pid = sys_spawn2("/sbin/openrc", fwd, n);
    if (pid < 0)
        return fail("rc-service", "cannot exec /sbin/openrc");
    return (int)sys_waitpid((pid_t)pid);
}
