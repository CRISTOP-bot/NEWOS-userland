/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/openrc/librc/librc.c
 * Description: librc implementation for the NEWOS OpenRC service manager.
 * Added: 2026-10-29
 */
/* librc implementation. Freestanding like all NEWOS userland: integer
 * only, nshlib syscall wrappers, no allocation. */

#include "librc.h"

static struct {
    char key[64];
    char val[192];
} s_conf[RC_MAX_VARS];
static int s_conf_n;

int rc_file_exists(const char *path)
{
    struct nsh_stat st;
    return sys_stat(path, &st) == 0;
}

long rc_read_file(const char *path, char *buf, long max)
{
    long fd = sys_open(path, O_READ);
    long total = 0;

    if (fd < 0)
        return -1;
    while (total < max - 1) {
        long n = sys_read((int)fd, buf + total, (u64)(max - 1 - total));
        if (n <= 0)
            break;
        total += n;
    }
    buf[total] = '\0';
    sys_close((int)fd);
    return total;
}

int rc_write_file(const char *path, const char *data)
{
    int fd = (int)sys_open(path, O_WRITE | O_CREATE | NSH_O_TRUNC);
    long n;

    if (fd < 0)
        return -1;
    n = sys_write(fd, data, nstrlen(data));
    sys_close(fd);
    return n == (long)nstrlen(data) ? 0 : -1;
}

int rc_mkdir_path(const char *path)
{
    long r = sys_mkdir(path);
    return r == 0 || r == -1;      /* -1 covers "already exists" */
}

int rc_mkdir_p(const char *path)
{
    char cur[RC_MAX_PATH];
    long n = (long)nstrlen(path);

    if (n == 0 || n >= (long)sizeof(cur))
        return -1;
    for (long i = 1; i <= n; i++) {
        if (i == n || path[i] == '/') {
            for (long j = 0; j < i; j++)
                cur[j] = path[j];
            cur[i] = '\0';
            if (sys_mkdir(cur) != 0 && !rc_file_exists(cur))
                return -1;
        }
    }
    return 0;
}

void rc_trim_copy(char *dst, const char *src, long cap)
{
    long n = 0;

    if (cap <= 0)
        return;
    while (src[n] && n < cap - 1) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = '\0';
    while (n > 0 && (dst[n - 1] == '\n' || dst[n - 1] == '\r' ||
                     dst[n - 1] == ' ' || dst[n - 1] == '\t'))
        dst[--n] = '\0';
}

/* Value of one "<key>=" field of a state record ("" when absent). */
void rc_state_field(const char *body, const char *key, char *out, long cap)
{
    long kl = nstrlen(key);
    const char *p = body;
    long n = 0;

    out[0] = '\0';
    if (cap <= 0)
        return;
    while (*p) {
        if (nstrncmp(p, key, (u64)kl) == 0) {
            p += kl;
            while (*p && *p != ' ' && *p != '\n' && *p != '\r' &&
                   n < cap - 1)
                out[n++] = *p++;
            out[n] = '\0';
            return;
        }
        p++;
    }
}

int rc_has(const char *hay, const char *needle)
{
    u64 n = nstrlen(needle);

    if (!n)
        return 1;
    for (; *hay; hay++)
        if (nstrncmp(hay, needle, n) == 0)
            return 1;
    return 0;
}

int rc_resolve_target(const char *entry_path, const char *name,
                      char *out, long outlen)
{
    char body[RC_MAX_PATH];
    char guess[RC_MAX_PATH];

    /* A runlevel entry placed directly on an init script is valid too. */
    if (rc_has(entry_path, RC_INITD_DIR "/") &&
        rc_file_exists(entry_path)) {
        rc_trim_copy(out, entry_path, outlen);
        return 0;
    }
    if (rc_read_file(entry_path, body, (long)sizeof(body)) > 0) {
        char *p = body;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '/' && rc_file_exists(p)) {
            rc_trim_copy(out, p, outlen);
            return 0;
        }
    }
    /* Marker file (or garbage): fall back to the conventional location. */
    nstrcpy(guess, RC_INITD_DIR "/");
    nstrcat(guess, name);
    if (rc_file_exists(guess)) {
        rc_trim_copy(out, guess, outlen);
        return 0;
    }
    return -1;
}

/* ---- rc.conf ---------------------------------------------------------- */

void rc_conf_load(const char *path)
{
    static char buf[8192];
    char *line;

    s_conf_n = 0;
    if (rc_read_file(path, buf, (long)sizeof(buf)) <= 0)
        return;
    line = buf;
    while (*line) {
        char *eol = line;
        while (*eol && *eol != '\n')
            eol++;
        if (*eol == '\n')
            *eol++ = '\0';
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == '#' || !*line || *line == '\n') {
            line = eol;
            continue;
        }
        char *eq = line;
        while (*eq && *eq != '=')
            eq++;
        if (*eq == '=' && s_conf_n < RC_MAX_VARS) {
            char *k = s_conf[s_conf_n].key;
            char *v = s_conf[s_conf_n].val;
            long kl = eq - line;
            const char *vs = eq + 1;
            long n = 0;

            if (kl > 0 && kl < 64) {
                while (n < kl && line[n] && line[n] != ' ' && line[n] != '\t')
                    n++;
                for (long i = 0; i < n; i++)
                    k[i] = line[i];
                k[n] = '\0';
                while (*vs == ' ')
                    vs++;
                if (*vs == '"' || *vs == '\'')
                    vs++;
                rc_trim_copy(v, vs, (long)sizeof(s_conf[0].val));
                n = (long)nstrlen(v);
                while (n > 0 && (v[n - 1] == '"' || v[n - 1] == '\''))
                    v[--n] = '\0';
                s_conf_n++;
            }
        }
        line = eol;
    }
}

const char *rc_conf_get(const char *key)
{
    for (int i = 0; i < s_conf_n; i++)
        if (nstrcmp(s_conf[i].key, key) == 0)
            return s_conf[i].val;
    return "";
}

/* ---- depend() parsing -------------------------------------------------- */

/* Is the given position inside the depend() block of the script? We scan
 * line-wise: the block starts at "depend() {" and ends at a lone "}". */
static const char *depend_block(const char *script, long *len)
{
    const char *p = script;

    while (*p) {
        if (nstrncmp(p, "depend()", 8) == 0) {
            const char *brace = p;
            while (*brace && *brace != '{')
                brace++;
            if (!*brace)
                break;
            p = brace + 1;
            const char *end = p;
            while (*end && *end != '}')
                end++;
            *len = end - p;
            return p;
        }
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
    *len = 0;
    return 0;
}

int rc_dep_list(const char *script, const char *kw, char *out, long cap)
{
    long blen;
    const char *blk = depend_block(script, &blen);
    u64 kl = nstrlen(kw);
    int found = 0;

    out[0] = '\0';
    if (!blk)
        return 0;
    for (long i = 0; i < blen; ) {
        /* One line of the block. */
        long start = i;
        while (i < blen && blk[i] != '\n')
            i++;
        long end = i;
        if (i < blen)
            i++;                        /* step over '\n' */

        const char *ln = blk + start;
        long ll = end - start;
        long pos = 0;
        while (pos < ll && (ln[pos] == ' ' || ln[pos] == '\t' ||
                            ln[pos] == '\r'))
            pos++;
        if (pos < ll && ln[pos] == '#')
            continue;
        /* Match "kw " at the start of any block line (and after other
         * tokens on the same line, e.g. "after GETTY provide X"). */
        int at = (int)pos;
        while (at < ll) {
            if (ll - at >= (long)kl + 1 && nstrncmp(ln + at, kw, kl) == 0 &&
                (ln[at + kl] == ' ' || ln[at + kl] == '\t')) {
                /* Collect ';'-separated tokens following the keyword until
                 * the next keyword or end of line. */
                int j = at + (int)kl;
                long n = 0;

                for (;;) {
                    while (j < ll && (ln[j] == ' ' || ln[j] == '\t'))
                        j++;
                    if (j >= ll)
                        break;
                    if (ll - j >= (long)kl + 1 &&
                        nstrncmp(ln + j, kw, kl) == 0 &&
                        (ln[j + kl] == ' ' || ln[j + kl] == '\t'))
                        break;          /* another keyword starts */
                    long s = j;
                    while (j < ll && ln[j] != ' ' && ln[j] != '\t' &&
                           ln[j] != '\r')
                        j++;
                    if (n + (j - s) + 1 >= cap)
                        break;
                    if (n)
                        out[n++] = ';';
                    for (long k = s; k < j; k++)
                        out[n++] = ln[k];
                }
                out[n] = '\0';
                if (n)
                    found = 1;
            }
            while (at < ll && ln[at] != ' ' && ln[at] != '\t')
                at++;
            while (at < ll && (ln[at] == ' ' || ln[at] == '\t'))
                at++;
        }
    }
    return found;
}

int rc_list_has(const char *list, const char *name)
{
    const char *c = list;
    char tok[RC_MAX_NAME];

    while (rc_list_next(&c, tok, (long)sizeof(tok)))
        if (nstrcmp(tok, name) == 0)
            return 1;
    return 0;
}

void rc_list_add(char *list, const char *name, long cap)
{
    long n = (long)nstrlen(list);
    long l = (long)nstrlen(name);

    if (n + l + 2 > cap || rc_list_has(list, name))
        return;
    for (long i = 0; i < l; i++)
        list[n++] = name[i];
    list[n++] = ';';
    list[n] = '\0';
}

int rc_list_next(const char **cursor, char *tok, long cap)
{
    const char *c = *cursor;
    long n = 0;

    while (*c == ';')
        c++;
    if (!*c) {
        *cursor = c;
        return 0;
    }
    while (*c && *c != ';' && n < cap - 1)
        tok[n++] = *c++;
    tok[n] = '\0';
    *cursor = c;
    return 1;
}

/* Does the script define an action block like "start() {"? */
static int has_action(const char *script, const char *action)
{
    u64 al = nstrlen(action);

    for (const char *p = script; *p; p++) {
        if (nstrncmp(p, action, al) == 0 && p[al] == '(' &&
            p[al + 1] == ')') {
            const char *b = p + al + 2;
            while (*b == ' ' || *b == '\t')
                b++;
            if (*b == '{')
                return 1;
        }
        while (*p && *p != '\n')
            p++;
        if (!*p)
            break;
    }
    return 0;
}

void rc_parse_service(rc_service_t *svc, const char *path)
{
    static char buf[16384];
    long n = rc_read_file(path, buf, (long)sizeof(buf));

    nstrcpy(svc->path, path);
    svc->need[0] = svc->use[0] = svc->before[0] = '\0';
    svc->after[0] = svc->provide[0] = svc->keyword[0] = '\0';
    svc->has_start = svc->has_stop = 0;
    if (n <= 0)
        return;
    rc_dep_list(buf, "need", svc->need, (long)sizeof(svc->need));
    rc_dep_list(buf, "use", svc->use, (long)sizeof(svc->use));
    rc_dep_list(buf, "before", svc->before, (long)sizeof(svc->before));
    rc_dep_list(buf, "after", svc->after, (long)sizeof(svc->after));
    rc_dep_list(buf, "provide", svc->provide, (long)sizeof(svc->provide));
    rc_dep_list(buf, "keyword", svc->keyword, (long)sizeof(svc->keyword));
    svc->has_start = has_action(buf, "start");
    svc->has_stop = has_action(buf, "stop");
}

int rc_scan_runlevel(const char *level, rc_service_t *svcs, int max)
{
    static char dir[RC_MAX_PATH];
    static struct nsh_dirent ents[64];
    int count = 0;

    nstrcpy(dir, RC_RUNLEVEL_DIR "/");
    nstrcat(dir, level);
    if (sys_readdir(dir, ents, (u64)sizeof(ents)) < 0)
        return 0;
    for (int i = 0; i < 64 && i < max; i++) {
        const char *name = ents[i].name;
        char entry[RC_MAX_PATH];
        long l = (long)nstrlen(name);

        if (!l || name[0] == '.')
            continue;
        if (l > RC_MAX_NAME - 1)
            continue;
        nstrcpy(entry, dir);
        nstrcat(entry, "/");
        nstrcat(entry, name);
        nstrcpy(svcs[count].name, name);
        if (rc_resolve_target(entry, name, svcs[count].path,
                              (long)sizeof(svcs[count].path)) != 0)
            continue;
        rc_parse_service(&svcs[count], svcs[count].path);
        count++;
    }
    return count;
}

/* ---- einfo style output ------------------------------------------------ */

static void emit_mark(const char *color)
{
    if (color) {
        nputs("\x1B[");
        nputs(color);
        nputc('m');
        nputc('*');
        nputs("\x1B[0m");
    } else {
        nputc('*');
    }
}

static void eout(const char *color, const char *what)
{
    nputc(' ');
    emit_mark(color);
    nputc(' ');
    nputs(what);
    nputc('\n');
}

void rc_einfo(const char *what)
{
    eout("1;32", what);
}

void rc_ewarn(const char *what)
{
    eout("1;33", what);
}

void rc_eerror(const char *what)
{
    eout("1;31", what);
}
