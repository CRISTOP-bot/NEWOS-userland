/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/openrc/openrc/openrc_main.c
 * Description: /sbin/openrc: the NEWOS OpenRC service manager.
 * Added: 2026-10-29
 */
/* /sbin/openrc - real OpenRC runlevel manager for NEWOS.
 *
 * Layout and semantics follow upstream OpenRC:
 *   /etc/init.d/<svc>          init scripts (depend(), start(){}, stop(){})
 *   /etc/runlevels/<level>     runlevel membership (indirection files;
 *                              NEWOS VFS has no symlinks, see librc.h)
 *   /etc/rc.conf               KEY=VALUE configuration
 *   /etc/conf.d/<svc>          per-service KEY=VALUE overrides
 *   /run/openrc/<level>/<svc>  started-service state (PID + timestamp)
 *
 * Usage:
 *   openrc [runlevel]            solve + start a runlevel
 *   openrc --stop [runlevel]     stop everything recorded for a runlevel
 *   openrc --status [runlevel]   brief started list
 *   openrc --list                list available runlevels
 *   openrc <svc> <action>        single service (used by rc-service)
 */

#include "../librc/librc.h"

typedef struct {
    rc_service_t svc;
    int in_level;                 /* member of the requested runlevel */
    int failed;                   /* this service failed to start */
} node_t;

static node_t s_nodes[RC_MAX_SERVICES];
static int s_nn;

/* Is `name` known to the transaction, directly or through provide()? */
static int provides(const char *name)
{
    for (int i = 0; i < s_nn; i++) {
        if (!s_nodes[i].in_level)
            continue;
        if (nstrcmp(s_nodes[i].svc.name, name) == 0)
            return 1;
        if (rc_list_has(s_nodes[i].svc.provide, name))
            return 1;
    }
    return 0;
}

/* Should a start before b? Upstream vocabulary, with `need`/`use` pointing
 * the other way than `before`: a dependency listed in need/use starts
 * FIRST, so those names create an edge into a, never out of it. Each of a's
 * statements is also read mirrored onto b, which is what lets a service
 * that only says `after bootmisc` still wait for it. */
static int edge_needed(int a, int b)
{
    const char *na = s_nodes[a].svc.name;
    const char *nb = s_nodes[b].svc.name;

    if (rc_list_has(s_nodes[a].svc.before, nb))
        return 1;                          /* a explicitly before b */
    if (rc_list_has(s_nodes[a].svc.after, nb))
        return 0;                          /* a explicitly after b */
    if (rc_list_has(s_nodes[a].svc.need, nb))
        return 0;                          /* b is a's dependency */
    if (rc_list_has(s_nodes[a].svc.use, nb) && provides(nb))
        return 0;                          /* soft dependency */
    if (rc_list_has(s_nodes[b].svc.need, na))
        return 1;                          /* a is b's dependency */
    if (rc_list_has(s_nodes[b].svc.use, na) && provides(na))
        return 1;                          /* soft, other direction */
    if (rc_list_has(s_nodes[b].svc.before, na))
        return 0;                          /* b claims to go first */
    if (rc_list_has(s_nodes[b].svc.after, na))
        return 1;                          /* b claims to come later */
    return 0;
}

/* Environment keywords (upstream `keyword` semantics): a bare name must be
 * present in this boot, a leading '-' means it must be absent. NEWOS knows
 * one synthetic keyword, `gui`, true when a framebuffer exists and the
 * cmdline asked for a graphical session; every other name is matched
 * against the boot cmdline. */
static int env_has(const char *kw)
{
    static char cmd[512];
    int neg = (kw[0] == '-');
    const char *name = neg ? kw + 1 : kw;
    int present = 0;

    if (rc_read_file("/proc/cmdline", cmd, (long)sizeof(cmd)) > 0)
        present = rc_has(cmd, name);
    if (nstrcmp(name, "gui") == 0) {
        struct nsh_fbinfo fi;
        int fb = (sys_fbinfo(&fi) == 0 && fi.present);
        present = fb && (rc_has(cmd, "gui=1") || present);
    }
    return neg ? !present : present;
}

static int keywords_ok(const char *list)
{
    const char *c = list;
    char tok[RC_MAX_NAME];

    while (rc_list_next(&c, tok, (long)sizeof(tok)))
        if (!env_has(tok))
            return 0;
    return 1;
}

/* Which runlevel(s) a plain `openrc` walks, per /etc/rc.conf. */
static char s_levels[4][RC_MAX_NAME];
static int s_nlevels;

static void boot_chain(void)
{
    const char *b = rc_conf_get("rc_bootlevel");
    const char *d = rc_conf_get("rc_defaultlevel");
    const char *c;

    s_nlevels = 0;
    if (!*b)
        b = "boot";
    c = b;
    while (*c && s_nlevels < 4) {
        char tok[RC_MAX_NAME];
        long n = 0;

        while (*c == ' ' || *c == '\t')
            c++;
        if (!*c)
            break;
        while (*c && *c != ' ' && *c != '\t' && n < RC_MAX_NAME - 1)
            tok[n++] = *c++;
        tok[n] = '\0';
        if (n > 0)
            nstrcpy(s_levels[s_nlevels++], tok);
    }
    /* The default runlevel joins the chain when configured, not already
     * present, and actually installed. */
    if (*d && s_nlevels < 4) {
        int dup = 0;

        for (int i = 0; i < s_nlevels; i++)
            if (nstrcmp(s_levels[i], d) == 0)
                dup = 1;
        if (!dup && rc_file_exists(RC_RUNLEVEL_DIR "/default"))
            nstrcpy(s_levels[s_nlevels++], d);
    }
    if (s_nlevels == 0)
        nstrcpy(s_levels[s_nlevels++], "boot");
}

static void append_num(char *body, unsigned long v)
{
    char tmp[24];
    int n = 0;
    int k = (int)nstrlen(body);

    if (v == 0)
        tmp[n++] = '0';
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        body[k++] = tmp[--n];
    body[k] = '\0';
}

/* A detached service records itself in /run/<svc>.pid (nsh writes it for a
 * trailing `&`). That is the process the state file must name: the script's
 * own pid is already gone when start() returns. Returns -1 for services that
 * do the work synchronously (no running daemon to track). */
static long pidfile_pid(const char *svc)
{
    char path[96];
    char body[32];
    long n;

    nstrcpy(path, "/run/");
    nstrcat(path, svc);
    nstrcat(path, ".pid");
    n = rc_read_file(path, body, (long)sizeof(body));
    if (n <= 0)
        return -1;
    return natoi(body);
}

/* Record a started service. Upstream keeps one state file per service in
 * /run/openrc/started; the per-runlevel directory is what `openrc --status
 * <level>` lists, so both are written with the same body. Only a service
 * that really detached has a pid worth recording: the transient /bin/sh
 * which ran start() is gone, and naming it would be a lie. */
static void state_write(const char *level, const char *svc)
{
    char path[RC_MAX_PATH];
    char body[128];
    struct nsh_time t;
    long tracked = pidfile_pid(svc);

    body[0] = '\0';
    if (tracked > 0) {
        nstrcat(body, "pid=");
        append_num(body, (unsigned long)tracked);
        nstrcat(body, " ");
    }
    nstrcat(body, "runlevel=");
    nstrcat(body, level);
    if (sys_gettime(&t) == 0) {
        nstrcat(body, " started=");
        append_num(body, t.year);
        nstrcat(body, "-");
        append_num(body, t.mon);
        nstrcat(body, "-");
        append_num(body, t.day);
        nstrcat(body, " ");
        append_num(body, t.hour);
        nstrcat(body, ":");
        append_num(body, t.min);
        nstrcat(body, ":");
        append_num(body, t.sec);
    }
    nstrcat(body, "\n");

    nstrcpy(path, RC_STATE_DIR "/");
    nstrcat(path, level);
    rc_mkdir_p(path);
    nstrcat(path, "/");
    nstrcat(path, svc);
    rc_write_file(path, body);

    nstrcpy(path, RC_STARTED_DIR "/");
    rc_mkdir_p(RC_STARTED_DIR);
    nstrcat(path, svc);
    rc_write_file(path, body);
}

static void state_remove(const char *level, const char *svc)
{
    char path[RC_MAX_PATH];

    nstrcpy(path, RC_STATE_DIR "/");
    nstrcat(path, level);
    nstrcat(path, "/");
    nstrcat(path, svc);
    sys_unlink(path);
    nstrcpy(path, RC_STARTED_DIR "/");
    nstrcat(path, svc);
    sys_unlink(path);
}

/* Run one action of one init script through /bin/sh: the NEWOS shell owns
 * the script syntax (name() { ... } blocks, one command per line). */
static int action_run(const char *script, const char *name,
                      const char *action, long *pid_out)
{
    char *argv[4];
    long pid;

    argv[0] = (char *)"/bin/sh";
    argv[1] = (char *)script;
    argv[2] = (char *)action;
    argv[3] = (char *)"--rc";
    pid = sys_spawn2("/bin/sh", argv, 4);
    if (pid < 0) {
        nputs("openrc: cannot spawn the init script for ");
        nputs(name);
        nputc('\n');
        return -1;
    }
    *pid_out = pid;
    return 0;
}

/* Is the service already recorded as running (in any runlevel)? Upstream
 * keeps one state file per service, and a second start() would double it. */
static int state_exists(const char *svc)
{
    char path[RC_MAX_PATH];

    nstrcpy(path, RC_STARTED_DIR "/");
    nstrcat(path, svc);
    return rc_file_exists(path);
}

/* The runlevel that owns a service's state record ("" when there is none).
 * Per-service actions use it, so `rc-service bootmisc restart` with
 * `default` current does not move bootmisc's record into default. */
static void state_owner(const char *svc, char *out, long cap)
{
    char path[RC_MAX_PATH];
    char body[128];

    out[0] = '\0';
    nstrcpy(path, RC_STARTED_DIR "/");
    nstrcat(path, svc);
    if (rc_read_file(path, body, (long)sizeof(body)) <= 0)
        return;
    rc_state_field(body, "runlevel=", out, cap);
}

static int service_action(const char *level, node_t *nd, const char *action)
{
    long pid;
    char msg[RC_MAX_NAME + 32];

    if (nstrcmp(action, "start") == 0 && state_exists(nd->svc.name)) {
        nstrcpy(msg, "already started ");
        nstrcat(msg, nd->svc.name);
        rc_einfo(msg);
        return 0;
    }
    if (action_run(nd->svc.path, nd->svc.name, action, &pid) != 0)
        return -1;
    long rc = sys_waitpid((pid_t)pid);

    if (nstrcmp(action, "start") == 0) {
        nstrcpy(msg, "started ");
        nstrcat(msg, nd->svc.name);
        if (rc == 0) {
            state_write(level, nd->svc.name);
            rc_einfo(msg);
            return 0;
        }
        rc_eerror(msg);
        putf("      openrc: %s exited with code %d\n",
             (u64)nd->svc.name, (u64)rc);
        return -1;
    }
    if (nstrcmp(action, "stop") == 0) {
        if (rc != 0) {
            nstrcpy(msg, "not stopped ");
            nstrcat(msg, nd->svc.name);
            rc_eerror(msg);
            putf("      openrc: %s stop exited with code %d\n",
                 (u64)nd->svc.name, (u64)rc);
            /* State stays: the service is still running from the manager's
             * point of view, exactly like upstream's failed stop. */
            return -1;
        }
        state_remove(level, nd->svc.name);
        {
            char pf[96];
            nstrcpy(pf, "/run/");
            nstrcat(pf, nd->svc.name);
            nstrcat(pf, ".pid");
            sys_unlink(pf);       /* the detached service is gone */
        }
        nstrcpy(msg, "stopped ");
        nstrcat(msg, nd->svc.name);
        rc_einfo(msg);
        return 0;
    }
    return (int)rc;
}

/* Kahn topological order over the in-level nodes using edge_needed().
 * Members left after the fixpoint form a dependency cycle: they are
 * appended in runlevel order so a cycle warns instead of stranding boot. */
static int topo_order(int *order)
{
    int placed[RC_MAX_SERVICES];
    int n = 0, changed = 1;

    for (int i = 0; i < s_nn; i++)
        placed[i] = 0;
    while (changed) {
        changed = 0;
        for (int i = 0; i < s_nn; i++) {
            if (!s_nodes[i].in_level || placed[i])
                continue;
            int ready = 1;
            for (int j = 0; j < s_nn && ready; j++) {
                if (!s_nodes[j].in_level || placed[j] || i == j)
                    continue;
                if (edge_needed(j, i))
                    ready = 0;
            }
            if (ready) {
                order[n++] = i;
                placed[i] = 1;
                changed = 1;
            }
        }
    }
    for (int i = 0; i < s_nn; i++) {
        if (s_nodes[i].in_level && !placed[i]) {
            nputs("openrc: dependency cycle involving ");
            nputs(s_nodes[i].svc.name);
            nputc('\n');
            order[n++] = i;
            placed[i] = 1;
        }
    }
    return n;
}

/* Pull `need` dependencies that live in another runlevel into the
 * transaction, the way upstream does instead of failing the dependent
 * service. Two passes cover a dependency of a service that was itself
 * pulled in. */
static void pull_needs(void)
{
    for (int pass = 0; pass < 2; pass++) {
        int n0 = s_nn;

        for (int i = 0; i < n0 && s_nn < RC_MAX_SERVICES; i++) {
            const char *c = s_nodes[i].svc.need;
            char tok[RC_MAX_NAME];

            while (rc_list_next(&c, tok, (long)sizeof(tok))) {
                int known = 0;
                char path[RC_MAX_PATH];

                for (int j = 0; j < s_nn; j++) {
                    if (nstrcmp(s_nodes[j].svc.name, tok) == 0 ||
                        rc_list_has(s_nodes[j].svc.provide, tok)) {
                        known = 1;
                        break;
                    }
                }
                if (known)
                    continue;
                nstrcpy(path, RC_INITD_DIR "/");
                nstrcat(path, tok);
                if (!rc_file_exists(path)) {
                    putf("openrc: %s: need %s is not installed\n",
                         (u64)s_nodes[i].svc.name, (u64)tok);
                    s_nodes[i].failed = 1;
                    continue;
                }
                nstrcpy(s_nodes[s_nn].svc.name, tok);
                rc_parse_service(&s_nodes[s_nn].svc, path);
                s_nodes[s_nn].in_level = 1;
                s_nodes[s_nn].failed = 0;
                s_nn++;
            }
        }
    }
}

static int run_level(const char *level, const char *action)
{
    static rc_service_t list[RC_MAX_SERVICES];
    int cnt = rc_scan_runlevel(level, list, RC_MAX_SERVICES);
    int order[RC_MAX_SERVICES];
    int fails = 0;

    s_nn = 0;
    for (int i = 0; i < cnt && s_nn < RC_MAX_SERVICES; i++) {
        /* Byte copy, not `=`: freestanding userland links no libc, and a
         * struct assignment of this size is lowered to memcpy() by the
         * compiler. */
        nmemcpy(&s_nodes[s_nn].svc, &list[i], sizeof(rc_service_t));
        s_nodes[s_nn].in_level = 1;
        s_nodes[s_nn].failed = 0;
        s_nn++;
    }
    if (cnt == 0) {
        nputs("openrc: runlevel '");
        nputs(level);
        nputs("' has no services\n");
        return 0;
    }
    if (nstrcmp(action, "stop") != 0)
        pull_needs();
    int n = topo_order(order);

    if (nstrcmp(action, "stop") == 0) {
        for (int i = n - 1; i >= 0; i--) {
            int idx = order[i];
            node_t *nd = &s_nodes[idx];

            if (nd->failed)
                continue;
            if (!nd->svc.has_stop) {
                /* Nothing to tear down: drop the state record so rc-status
                 * does not keep reporting a service that cannot stop. */
                state_remove(level, nd->svc.name);
                continue;
            }
            if (service_action(level, nd, "stop") != 0)
                fails++;
        }
        return fails;
    }
    for (int i = 0; i < n; i++) {
        int idx = order[i];
        node_t *nd = &s_nodes[idx];

        if (nd->failed)
            continue;
        if (*nd->svc.keyword && !keywords_ok(nd->svc.keyword)) {
            putf("openrc: %s: skipped (keyword '%s' not in this boot)\n",
                 (u64)nd->svc.name, (u64)nd->svc.keyword);
            continue;
        }
        if (nstrcmp(action, "start") == 0 && !nd->svc.has_start) {
            putf("openrc: %s: no start() action\n", (u64)nd->svc.name);
            continue;
        }
        if (service_action(level, nd, action) != 0) {
            nd->failed = 1;
            fails++;
        }
    }
    return fails;
}

static int usage(void)
{
    nputs("Usage: openrc [options] [runlevel | <svc> <action>]\n");
    nputs("  openrc [runlevel]          start a runlevel chain\n");
    nputs("  openrc --stop [runlevel]   stop a runlevel\n");
    nputs("  openrc --status [runlevel] list started services\n");
    nputs("  openrc --list              list installed runlevels\n");
    nputs("  openrc <svc> start|stop|restart|status|zap\n");
    nputs("  openrc --version           print version\n");
    return 0;
}

static int is_action(const char *s)
{
    return nstrcmp(s, "start") == 0 || nstrcmp(s, "stop") == 0 ||
           nstrcmp(s, "restart") == 0 || nstrcmp(s, "status") == 0 ||
           nstrcmp(s, "zap") == 0;
}

int main(int argc, char **argv)
{
    const char *level = "boot";
    int explicit_level = 0, stop_mode = 0, status_mode = 0, list_mode = 0;
    char single_svc[RC_MAX_NAME] = "";
    const char *single_act = 0;

    rc_conf_load(RC_CONF_PATH);
    {
        const char *c = rc_conf_get("rc_runlevel");
        if (*c)
            level = c;
    }

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (*a == '-') {
            if (a[1] != '-')
                continue;                   /* short modifiers: accepted */
            if (nstrcmp(a, "--stop") == 0)
                stop_mode = 1;
            else if (nstrcmp(a, "--status") == 0)
                status_mode = 1;
            else if (nstrcmp(a, "--list") == 0)
                list_mode = 1;
            else if (nstrcmp(a, "--version") == 0) {
                nputs("openrc " RC_VERSION " (NEWOS)\n");
                return 0;
            } else if (nstrcmp(a, "--help") == 0)
                return usage();
            else if (nstrcmp(a, "--branch") != 0 &&
                     nstrcmp(a, "--nondependent") != 0) {
                nputs("openrc: unknown option ");
                nputs(a);
                nputc('\n');
                return 1;
            }
            continue;                       /* upstream modifiers: no-ops */
        }
        if (is_action(a)) {
            if (*single_svc && !single_act)
                single_act = a;
            continue;
        }
        if (!*single_svc && i + 1 < argc && is_action(argv[i + 1])) {
            rc_trim_copy(single_svc, a, (long)sizeof(single_svc));
            continue;                       /* the next token is the action */
        }
        level = a;
        explicit_level = 1;
    }

    if (*single_svc) {
        char path[RC_MAX_PATH];
        char owner[RC_MAX_NAME];
        node_t nd;

        nstrcpy(path, RC_INITD_DIR "/");
        nstrcat(path, single_svc);
        if (!rc_file_exists(path)) {
            nputs("openrc: no such service: ");
            nputs(single_svc);
            nputc('\n');
            return 1;
        }
        nstrcpy(nd.svc.name, single_svc);
        rc_parse_service(&nd.svc, path);
        nd.in_level = 1;
        nd.failed = 0;
        if (!single_act) {
            nputs("openrc: missing action for service ");
            nputs(single_svc);
            nputc('\n');
            return 1;
        }
        state_owner(single_svc, owner, (long)sizeof(owner));
        if (*owner)
            level = owner;
        if (nstrcmp(single_act, "zap") == 0) {
            state_remove(level, single_svc);
            nputs("openrc: cleared state for ");
            nputs(single_svc);
            nputc('\n');
            return 0;
        }
        if (nstrcmp(single_act, "restart") == 0) {
            if (nd.svc.has_stop) {
                service_action(level, &nd, "stop");
            } else {
                state_remove(level, single_svc);
            }
            return service_action(level, &nd, "start") == 0 ? 0 : 1;
        }
        if (nstrcmp(single_act, "stop") == 0 && !nd.svc.has_stop) {
            char msg[RC_MAX_NAME + 16];

            state_remove(level, single_svc);
            nstrcpy(msg, "stopped ");
            nstrcat(msg, single_svc);
            rc_einfo(msg);
            return 0;
        }
        if (nstrcmp(single_act, "status") == 0) {
            long pid;
            if (action_run(path, single_svc, "status", &pid) != 0)
                return 1;
            return (int)sys_waitpid((pid_t)pid);
        }
        return service_action(level, &nd, single_act) == 0 ? 0 : 1;
    }

    if (list_mode) {
        static struct nsh_dirent ents[32];

        if (sys_readdir(RC_RUNLEVEL_DIR, ents, (u64)sizeof(ents)) < 0) {
            nputs("openrc: no runlevels installed\n");
            return 1;
        }
        nputs("Runlevels:\n");
        for (int i = 0; i < 32 && ents[i].name[0]; i++) {
            if (ents[i].name[0] == '.')
                continue;
            nputs("  ");
            nputs(ents[i].name);
            nputc('\n');
        }
        return 0;
    }

    if (stop_mode) {
        /* Without an explicit argument, stop the runlevel that is current. */
        if (!explicit_level) {
            static char cur[RC_MAX_NAME];
            if (rc_read_file(RC_CURRENT, cur, (long)sizeof(cur)) > 0 && *cur)
                level = cur;
        }
        return run_level(level, "stop") ? 1 : 0;
    }

    if (status_mode) {
        char path[RC_MAX_PATH];
        static struct nsh_dirent ents[32];

        nstrcpy(path, RC_STATE_DIR "/");
        nstrcat(path, level);
        if (sys_readdir(path, ents, (u64)sizeof(ents)) < 0) {
            nputs("openrc: nothing started in runlevel ");
            nputs(level);
            nputc('\n');
            return 0;
        }
        for (int i = 0; i < 32 && ents[i].name[0]; i++) {
            if (ents[i].name[0] == '.')
                continue;
            nputs("  ");
            nputs(ents[i].name);
            nputs("\tstarted\n");
        }
        return 0;
    }

    rc_mkdir_p(RC_STATE_DIR);

    if (explicit_level) {
        rc_write_file(RC_CURRENT, level);
        return run_level(level, "start") ? 1 : 0;
    }

    /* Plain `openrc`: the configured chain. A failing level stops it,
     * because everything downstream assumed that level came up. */
    boot_chain();
    for (int i = 0; i < s_nlevels; i++) {
        rc_write_file(RC_CURRENT, s_levels[i]);
        if (run_level(s_levels[i], "start") > 0) {
            nputs("openrc: boot chain stopped at runlevel '");
            nputs(s_levels[i]);
            nputs("'\n");
            return 1;
        }
    }
    return 0;
}
