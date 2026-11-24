/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/sudo/sudo_main.c
 * Description: sudo: run a command as root (NEWOS is single-user: uid 0).
 * Added: 2026-11-24
 */
#include "../../lib/nshlib.h"

/* NEWOS has no user accounts to elevate from: every process already runs
 * as uid 0. `sudo` therefore resolves the command the same way nsh does
 * (/bin, /usr/bin, /sbin, then the BusyBox multi-call binary) and spawns it.
 * It is a compatibility convenience, not a privilege boundary. */

#define SUDO_MAX_ARGS 16
#define SUDO_PATH_LEN 256

static int exists(const char *path)
{
    long fd = sys_open(path, O_READ);
    if (fd < 0)
        return 0;
    sys_close((int)fd);
    return 1;
}

static int run_path(const char *path, char **child_argv, int n)
{
    long pid = sys_spawn2(path, child_argv, n);

    if (pid < 0)
        return -1;
    return (int)sys_waitpid((pid_t)pid);
}

int main(int argc, char **argv)
{
    char path[SUDO_PATH_LEN];
    char *cargv[SUDO_MAX_ARGS + 2];
    const char *name;
    int n = 0, i;

    if (argc < 2)
        return fail("sudo", "usage: sudo <command> [args...]");
    if (argc > SUDO_MAX_ARGS)
        return fail("sudo", "too many arguments");

    name = argv[1];
    /* Build child argv: argv[0] becomes the resolved path for the program,
     * but we keep the original command name as argv[0] to match nsh. */
    cargv[n++] = (char *)name;
    for (i = 2; i < argc; i++)
        cargv[n++] = argv[i];
    cargv[n] = (char *)0;

    /* Absolute or relative path: use it directly. */
    for (const char *p = name; *p; p++) {
        if (*p == '/')
            return run_path(name, cargv, n) >= 0 ? 0 : fail("sudo", "cannot run command");
    }

    /* Same search order as nsh: native /bin, installed /usr/bin, /sbin. */
    nstrcpy(path, "/bin/");
    nstrcat(path, name);
    if (exists(path))
        return run_path(path, cargv, n) >= 0 ? 0 : fail("sudo", "command failed");

    nstrcpy(path, "/usr/bin/");
    nstrcat(path, name);
    if (exists(path))
        return run_path(path, cargv, n) >= 0 ? 0 : fail("sudo", "command failed");

    nstrcpy(path, "/sbin/");
    nstrcat(path, name);
    if (exists(path))
        return run_path(path, cargv, n) >= 0 ? 0 : fail("sudo", "command failed");

    /* Last resort: BusyBox multi-call binary (NEWOS has no symlinks). */
    if (n < SUDO_MAX_ARGS + 1) {
        char *bb_argv[SUDO_MAX_ARGS + 3];
        int bn = 0;
        bb_argv[bn++] = (char *)"busybox";
        for (i = 0; i < n; i++)
            bb_argv[bn++] = cargv[i];
        bb_argv[bn] = (char *)0;
        long pid = sys_spawn2("/bin/busybox", bb_argv, bn);
        if (pid >= 0)
            return (int)sys_waitpid((pid_t)pid);
    }

    return fail("sudo", "command not found");
}
