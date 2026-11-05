/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/openrc/rc-status/rc_status_main.c
 * Description: /sbin/rc-status: report the state of OpenRC runlevels.
 * Added: 2026-10-29
 */
/* rc-status - the OpenRC status reader, NEWOS port.
 *
 *   rc-status                 current runlevel
 *   rc-status <runlevel>      any installed runlevel
 *   rc-status --all           every runlevel and its services
 *   rc-status -a              same as --all
 *
 * State comes from /run/openrc/<level>/<svc>, written by /sbin/openrc when
 * a service starts (runlevel + CMOS stamp, plus the pid of a detached
 * service); nothing here is canned. Services that are started but are not
 * members of the reported runlevel are listed separately, as upstream does.
 */

#include "../librc/librc.h"

static char s_level[RC_MAX_NAME];
static rc_service_t s_list[RC_MAX_SERVICES];
static int s_cnt;

static void print_state(const rc_service_t *svc, const char *state_dir)
{
    char path[RC_MAX_PATH];
    char body[128];
    const char *state = "inactive";
    const char *color = "\x1B[1;33m";

    body[0] = '\0';
    nstrcpy(path, state_dir);
    nstrcat(path, "/");
    nstrcat(path, svc->name);
    if (rc_read_file(path, body, (long)sizeof(body)) > 0) {
        state = "started";
        color = "\x1B[1;32m";
    } else if (svc->has_start) {
        state = "stopped";
        color = "\x1B[1;31m";
    }

    nputs(" ");
    nputs(svc->name);
    /* 28 columns of name field: the state record printed after the bracket
     * has to stay inside an 80-column console. */
    for (int i = (int)nstrlen(svc->name) + 1; i < 28; i++)
        nputc(' ');
    nputs("[ ");
    nputs(color);
    nputs(state);
    nputs("\x1B[0m ]");
    if (nstrcmp(state, "started") == 0) {
        char pid[32];

        /* The full record (pid, runlevel, CMOS start stamp) stays in the
         * state file; one line of status output has to fit 80 columns. A
         * synchronous service has no daemon, so it has no pid to show. */
        rc_state_field(body, "pid=", pid, (long)sizeof(pid));
        if (*pid) {
            nputs(" (pid ");
            nputs(pid);
            nputs(")");
        }
    }
    nputc('\n');
}

static int report_level(const char *level)
{
    char state_dir[RC_MAX_PATH];
    int cnt;

    rc_trim_copy(s_level, level, (long)sizeof(s_level));
    cnt = rc_scan_runlevel(s_level, s_list, RC_MAX_SERVICES);
    s_cnt = cnt;
    nstrcpy(state_dir, RC_STATE_DIR "/");
    nstrcat(state_dir, s_level);
    nputs("Runlevel: ");
    nputs(s_level);
    nputc('\n');
    if (cnt == 0) {
        nputs("  (no services)\n");
        return 0;
    }
    for (int i = 0; i < cnt; i++)
        print_state(&s_list[i], state_dir);
    return cnt;
}

/* Classify one started record: -1 when it belongs to the runlevel being
 * reported, otherwise 0 and `level` receives the runlevel that owns it. */
static int started_owner(const char *name, char *level, long cap)
{
    char path[RC_MAX_PATH];
    char body[128];

    level[0] = '\0';
    for (int j = 0; j < s_cnt; j++)
        if (nstrcmp(s_list[j].name, name) == 0)
            return -1;
    nstrcpy(path, RC_STARTED_DIR "/");
    nstrcat(path, name);
    if (rc_read_file(path, body, (long)sizeof(body)) <= 0)
        return -1;
    rc_state_field(body, "runlevel=", level, cap);
    return 0;
}

/* Records outside the reported runlevel, split the way upstream splits
 * them: what the boot level started stays under "Boot Runlevel", anything
 * else the manager is holding (started by hand, or by another level of the
 * chain) is reported as dynamic/manual. */
static void report_manual(void)
{
    static struct nsh_dirent ents[32];
    int shown[2] = { 0, 0 };

    if (sys_readdir(RC_STARTED_DIR, ents, (u64)sizeof(ents)) < 0)
        return;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 32 && ents[i].name[0]; i++) {
            char level[RC_MAX_NAME];
            int is_boot;

            if (ents[i].name[0] == '.')
                continue;
            if (started_owner(ents[i].name, level, (long)sizeof(level)) != 0)
                continue;
            is_boot = nstrcmp(level, "boot") == 0;
            if (is_boot == (pass == 1))
                continue;
            if (!shown[pass]++) {
                nputc('\n');
                nputs(is_boot ? "Boot Runlevel\n"
                              : "Dynamic Runlevel: manual\n");
            }
            {
                rc_service_t svc;

                nstrcpy(svc.name, ents[i].name);
                print_state(&svc, RC_STARTED_DIR);
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *want = 0;
    int all = 0;

    for (int i = 1; i < argc; i++) {
        if (nstrcmp(argv[i], "--all") == 0 || nstrcmp(argv[i], "-a") == 0)
            all = 1;
        else if (nstrcmp(argv[i], "--help") == 0 ||
                 nstrcmp(argv[i], "-h") == 0) {
            nputs("Usage: rc-status [-a|--all] [runlevel]\n");
            return 0;
        } else if (argv[i][0] != '-')
            want = argv[i];
    }

    if (all) {
        static struct nsh_dirent ents[32];
        if (sys_readdir(RC_RUNLEVEL_DIR, ents, (u64)sizeof(ents)) < 0)
            return fail("rc-status", "no runlevels installed");
        for (int i = 0; i < 32 && ents[i].name[0]; i++) {
            if (ents[i].name[0] == '.')
                continue;
            report_level(ents[i].name);
            nputc('\n');
        }
    } else {
        char cur[RC_MAX_NAME] = "";
        const char *level;

        if (want) {
            level = want;
        } else if (rc_read_file(RC_CURRENT, cur, (long)sizeof(cur)) <= 0 ||
                   !*cur) {
            rc_conf_load(RC_CONF_PATH);
            const char *b = rc_conf_get("rc_bootlevel");
            level = *b ? b : "boot";
        } else {
            for (char *p = cur; *p; p++)
                if (*p == '\n' || *p == ' ' || *p == '\t') {
                    *p = '\0';
                    break;
                }
            level = cur;
        }
        if (!rc_file_exists(RC_RUNLEVEL_DIR))
            return fail("rc-status", "OpenRC is not installed (/etc/runlevels)");
        report_level(level);
        report_manual();
    }

    {
        struct nsh_sysinfo si;
        if (sys_sysinfo(&si) == 0) {
            u64 up = si.uptime_sec;
            putf("Uptime: %uh %um %us\n", (u64)(up / 3600),
                 (u64)((up % 3600) / 60), (u64)(up % 60));
        }
    }
    return 0;
}
