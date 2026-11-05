/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/openrc/openrc-init/openrc_init_main.c
 * Description: /sbin/openrc-init: the OpenRC-aware init for NEWOS.
 * Added: 2026-10-29
 */
/* /sbin/openrc-init - PID 1 for NEWOS when the image ships OpenRC.
 *
 * Responsibilities, in order (the upstream openrc-init contract):
 *   1. create the runtime tree (/run/openrc) and read /etc/rc.conf;
 *   2. run inittab entries with action `sysinit`;
 *   3. exec /sbin/openrc for the boot runlevel chain from /etc/rc.conf;
 *   4. supervise the console inittab entries (`respawn`, `wait`, `once`)
 *      and, on exit of every supervised child, hand the console back to a
 *      plain shell instead of leaving the machine stranded.
 *
 * The configuration tree itself (/etc/rc.conf, /etc/inittab, /etc/init.d,
 * /etc/runlevels) is installed by the kernel's initramfs from the blob
 * scripts/pack-openrc.py builds: PID 1 must not be the one materializing
 * the files its own services read.
 */

#include "../librc/librc.h"

/* ---- inittab ---------------------------------------------------------- */

typedef struct {
    char id[16];
    char levels[32];
    char action[16];
    char command[RC_MAX_PATH];
} ite_t;

static ite_t s_tab[8];
static int s_tab_n;

static void ite_field(char *dst, long cap, const char **cur)
{
    long n = 0;

    while (**cur && **cur != ':' && n < cap - 1)
        dst[n++] = *((*cur)++);
    dst[n] = '\0';
    if (**cur == ':')
        (*cur)++;
}

static int inittab_load(void)
{
    static char buf[4096];
    long n = rc_read_file("/etc/inittab", buf, (long)sizeof(buf));
    char *p;

    s_tab_n = 0;
    if (n <= 0)
        return 0;
    p = buf;
    while (*p && s_tab_n < 8) {
        char *eol = p;
        while (*eol && *eol != '\n')
            eol++;
        if (*eol == '\n')
            *eol++ = '\0';
        if (*p != '#' && *p) {
            ite_t *e = &s_tab[s_tab_n];
            const char *c = p;
            nstrcpy(e->id, "");
            ite_field(e->id, (long)sizeof(e->id), &c);
            ite_field(e->levels, (long)sizeof(e->levels), &c);
            ite_field(e->action, (long)sizeof(e->action), &c);
            ite_field(e->command, (long)sizeof(e->command), &c);
            if (*e->command)
                s_tab_n++;
        }
        p = eol;
    }
    return s_tab_n;
}

/* Split the inittab command into argv (max 16 tokens). */
static int ite_argv(const char *cmd, char **argv, int max)
{
    static char storage[8][128];
    int n = 0;
    const char *p = cmd;

    while (*p && n < max && n < 8) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        if (*p == '"' || *p == '\'') {
            char q = *p++;
            long k = 0;
            while (*p && *p != q && k < 127)
                storage[n][k++] = *p++;
            if (*p == q)
                p++;
            storage[n][k] = '\0';
        } else {
            long k = 0;
            while (*p && *p != ' ' && *p != '\t' && k < 127)
                storage[n][k++] = *p++;
            storage[n][k] = '\0';
        }
        argv[n] = storage[n];
        n++;
    }
    return n;
}

static long spawn_entry(const char *cmd)
{
    char *argv[NSH_MAX_ARGS];
    int argc = ite_argv(cmd, argv, NSH_MAX_ARGS);

    if (argc == 0)
        return -1;
    return sys_spawn2(argv[0], argv, argc);
}

static int entry_applies(const ite_t *e, const char *level)
{
    if (!*e->levels)
        return 1;
    if (!*level)
        return 0;
    return rc_has(e->levels, level);
}

int main(int argc, char **argv)
{
    char level[RC_MAX_NAME] = "";
    long pid;

    for (int i = 1; i < argc; i++)
        if (argv[i][0] != '-' && !*level)
            rc_trim_copy(level, argv[i], (long)sizeof(level));

    nputs("openrc-init: NEWOS init (OpenRC service manager)\n");

    /* 1. runtime tree (/etc comes from the kernel initramfs). */
    if (!rc_file_exists(RC_CONF_PATH))
        nputs("openrc-init: no /etc/rc.conf; using the built-in defaults\n");
    rc_mkdir_p(RC_STATE_DIR);
    rc_mkdir_p("/run/lock");
    rc_mkdir_p("/var/log");
    rc_conf_load(RC_CONF_PATH);

    inittab_load();

    /* 2. sysinit entries */
    for (int i = 0; i < s_tab_n; i++) {
        if (nstrcmp(s_tab[i].action, "sysinit") != 0)
            continue;
        pid = spawn_entry(s_tab[i].command);
        if (pid > 0) {
            sys_waitpid((pid_t)pid);
        } else {
            nputs("openrc-init: sysinit entry failed: ");
            nputs(s_tab[i].command);
            nputc('\n');
        }
    }

    /* 3. the runlevel chain. With no level on its own command line, plain
     * `openrc` walks rc_bootlevel + rc_defaultlevel from /etc/rc.conf; a
     * level argument (from the kernel or `init 3`) starts exactly that one. */
    {
        char *oargv[3];
        int oargc = 1;

        oargv[0] = (char *)"/sbin/openrc";
        if (*level) {
            oargv[1] = level;
            oargc = 2;
        }
        pid = sys_spawn2("/sbin/openrc", oargv, oargc);
        if (pid < 0) {
            nputs("openrc-init: cannot exec /sbin/openrc; "
                  "falling back to the shell\n");
        } else {
            long rc = sys_waitpid((pid_t)pid);
            if (rc != 0)
                putf("openrc-init: openrc reported %d service failure(s)\n",
                     (u64)rc);
        }
    }
    /* /run/openrc/current belongs to openrc: it writes it as it enters each
     * runlevel, so no one else has to guess which level is now active. */

    /* 4. console supervision. Non-respawn entries run first, in file order:
     * SYS_WAITPID only takes an exact pid, so the blocking wait on a respawn
     * entry *is* the supervision, and anything placed after it would never
     * run. `wait`, `once` and `askfirst` all mean "run this once and wait":
     * NEWOS boots serially anyway, and a child nobody reaps would leak a
     * process slot. */
    {
        int supervised = 0;

        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < s_tab_n; i++) {
                ite_t *e = &s_tab[i];
                int respawn = nstrcmp(e->action, "respawn") == 0;

                if (!respawn && nstrcmp(e->action, "wait") != 0 &&
                    nstrcmp(e->action, "once") != 0 &&
                    nstrcmp(e->action, "askfirst") != 0)
                    continue;
                if (pass == 0 ? respawn : !respawn)
                    continue;
                if (!entry_applies(e, *level ? level
                                             : rc_conf_get("rc_bootlevel")))
                    continue;
                if (!respawn) {
                    pid = spawn_entry(e->command);
                    if (pid > 0) {
                        sys_waitpid((pid_t)pid);
                        continue;
                    }
                    nputs("openrc-init: console command failed to start: ");
                    nputs(e->command);
                    nputc('\n');
                    continue;
                }
                if (supervised++) {
                    nputs("openrc-init: only one respawn entry can be "
                          "supervised; ignoring ");
                    nputs(e->command);
                    nputc('\n');
                    continue;
                }
                for (;;) {
                    pid = spawn_entry(e->command);
                    if (pid < 0) {
                        nputs("openrc-init: console command failed to start: ");
                        nputs(e->command);
                        nputc('\n');
                        break;
                    }
                    sys_waitpid((pid_t)pid);
                    sys_sleep(200);
                }
            }
        }
    }

    /* Nothing supervised (or the console died): keep the machine usable. */
    {
        char *sargv[2];

        sargv[0] = (char *)"/bin/sh";
        sargv[1] = (char *)0;
        for (;;) {
            pid = sys_spawn2("/bin/sh", sargv, 1);
            if (pid < 0)
                break;
            sys_waitpid((pid_t)pid);
        }
    }
    return 0;
}
