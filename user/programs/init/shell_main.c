/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/init/shell_main.c
 * Description: nsh: the NEWOS shell (/init, /bin/sh).
 * Added: 2026-10-17
 */
/* nsh: the NEWOS shell (/init, /bin/sh).
 *
 * A real interactive shell, not a demo: UTF-8 line editing (arrows,
 * Home/End, Delete, history, Ctrl+A/C/D/E/K/L/U/V/W/Y), quoting, builtins (cd, exit,
 * help, history, clear) and external /bin programs spawned with real
 * argc/argv. Input arrives from serial and the PS/2 keyboard alike;
 * output goes to the multiplexed console (serial + VGA text).
 */

#include "../../lib/nshlib.h"

/* Injected by the build from the repository VERSION file. */
#ifndef NEWOS_VERSION
#define NEWOS_VERSION "0.0.0"
#endif

#define LINE_MAX 256
#define HIST_MAX 16
#define MAX_ARGS 16
#define MAX_STAGES 8
#define O_TRUNC NSH_O_TRUNC
#define O_APPEND NSH_O_APPEND

/* --- history ----------------------------------------------------------- */

static char hist[HIST_MAX][LINE_MAX];
static int hcount;      /* stored entries */
static int hnav;        /* -1 = current draft, else index into hist */
static char draft[LINE_MAX];

/* --- line editor ------------------------------------------------------- */

static char line[LINE_MAX];
static int len;         /* bytes used */
static int pos;         /* cursor (byte index) */
static int maxdraw;     /* widest redraw so far (for stale-char cleanup) */
static char prompt[300];

/* OpenRC service-script state: set when this shell is invoked as
 * `/bin/sh /etc/init.d/<svc> <action>` (see run_service_script). */
static char svc_name[64];           /* "" outside service mode */

static void redraw(void);
static int exec_line(char *ln);   /* one command line -> shell exit code */

/* Kill-ring clipboard for Ctrl+K/U/W (cut) and Ctrl+Y/V (paste). */
static char clip[LINE_MAX];
static int cliplen;

/* Delete line[from..to), stashing the removed bytes in the clipboard. */
static void kill_range(int from, int to)
{
    int n;
    if (from < 0)
        from = 0;
    if (to > len)
        to = len;
    if (from >= to)
        return;
    n = to - from;
    if (n >= (int)sizeof(clip))
        n = (int)sizeof(clip) - 1;
    for (int i = 0; i < n; i++)
        clip[i] = line[from + i];
    clip[n] = '\0';
    cliplen = n;
    n = len - to;
    for (int i = 0; i < n; i++)
        line[from + i] = line[to + i];
    len -= to - from;
    pos = from;
    hnav = -1;
    redraw();
}

/* Display width: continuation bytes (10xxxxxx) don't advance the cell. */
static int dwidth(const char *s, int n)
{
    int w = 0;
    for (int i = 0; i < n; i++) {
        if (((u8)s[i] & 0xC0) != 0x80)
            w++;
    }
    return w;
}

static void redraw(void)
{
    int w = dwidth(prompt, (int)nstrlen(prompt)) + dwidth(line, len);
    nputc('\r');
    nputs(prompt);
    nputsn(line, (u64)len);
    for (int i = w; i < maxdraw; i++)
        nputc(' ');
    if (maxdraw < w)
        maxdraw = w;
    nputc('\r');
    nputs(prompt);
    /* The pen already sits after the second prompt copy above; moving
     * only the line-width remainder lands the caret on the cursor cell.
     * Counting the prompt here as well pushed the caret a full prompt
     * width into empty space on every console (VGA, serial, fbcon). */
    int target = dwidth(line, pos);
    for (int i = 0; i < target; i++)
        nputs("\x1B[C");
}

static void set_prompt(void)
{
    static char cwd[256];
    nstrcpy(prompt, "root@newos:");
    if (sys_getcwd(cwd, sizeof(cwd)) > 0)
        nstrcat(prompt, cwd);
    else
        nstrcat(prompt, "?");
    nstrcat(prompt, "# ");
}

/* Step one char back over UTF-8 continuation bytes. */
static int prev_char(int p)
{
    if (p <= 0)
        return 0;
    p--;
    while (p > 0 && (((u8)line[p] & 0xC0) == 0x80))
        p--;
    return p;
}

static int next_char(int p)
{
    if (p >= len)
        return len;
    p++;
    while (p < len && (((u8)line[p] & 0xC0) == 0x80))
        p++;
    return p;
}

static void hist_show(int idx)
{
    nstrcpy(line, hist[idx]);
    len = (int)nstrlen(line);
    pos = len;
}

/* Returns 1 when the line is submitted, 0 to keep editing, -1 to exit. */
static int edit_key(char c)
{
    /* Pending ANSI escape state lives in the reader below; here we get
     * fully decoded actions through codes >= 0x80. */
    if (c == '\r' || c == '\n') {
        nputc('\n');
        maxdraw = 0;
        return 1;
    }
    if (c == 0x03) {                    /* Ctrl+C: cancel line */
        nputs("^C\n");
        len = pos = 0;
        hnav = -1;
        maxdraw = 0;
        return 0;
    }
    if (c == 0x04) {                    /* Ctrl+D: exit on empty line */
        if (len == 0) {
            nputs("exit\n");
            return -1;
        }
        return 0;
    }
    if (c == 0x0C) {                    /* Ctrl+L: clear screen */
        for (int i = 0; i < 25; i++)
            nputc('\n');
        nputs("\x1B[2J\x1B[H");
        maxdraw = 0;
        redraw();
        return 0;
    }
    if (c == 0x01) {                    /* Ctrl+A: start of line */
        pos = 0;
        redraw();
        return 0;
    }
    if (c == 0x05) {                    /* Ctrl+E: end of line */
        pos = len;
        redraw();
        return 0;
    }
    if (c == 0x15) {                    /* Ctrl+U: cut whole line */
        kill_range(0, len);
        return 0;
    }
    if (c == 0x0B) {                    /* Ctrl+K: cut to end of line */
        kill_range(pos, len);
        return 0;
    }
    if (c == 0x17) {                    /* Ctrl+W: cut word behind cursor */
        int p = pos;
        while (p > 0 && (line[p - 1] == ' ' || line[p - 1] == '\t'))
            p = prev_char(p);
        while (p > 0 && line[p - 1] != ' ' && line[p - 1] != '\t')
            p = prev_char(p);
        kill_range(p, pos);
        return 0;
    }
    if (c == 0x19 || c == 0x16) {        /* Ctrl+Y / Ctrl+V: paste */
        if (cliplen > 0 && len < (int)sizeof(line) - 1) {
            int n = cliplen;
            if (n > (int)sizeof(line) - 1 - len)
                n = (int)sizeof(line) - 1 - len;
            for (int i = len; i > pos; i--)
                line[i + n - 1] = line[i - 1];
            for (int i = 0; i < n; i++)
                line[pos + i] = clip[i];
            len += n;
            pos += n;
            hnav = -1;
            redraw();
        }
        return 0;
    }
    if (c == 0x7F || c == 0x08) {        /* backspace: erase one char */
        if (pos > 0) {
            int p = prev_char(pos);
            int n = len - pos;
            for (int i = 0; i < n; i++)
                line[p + i] = line[pos + i];
            len -= pos - p;
            pos = p;
            hnav = -1;
            redraw();
        }
        return 0;
    }
    if (c == (char)0x80) {              /* left */
        pos = prev_char(pos);
        redraw();
        return 0;
    }
    if (c == (char)0x81) {              /* right */
        pos = next_char(pos);
        redraw();
        return 0;
    }
    if (c == (char)0x82) {              /* up: older history */
        if (hcount == 0)
            return 0;
        if (hnav == -1) {
            nstrcpy(draft, line);
            hnav = hcount - 1;
        } else if (hnav > 0) {
            hnav--;
        } else {
            return 0;
        }
        hist_show(hnav);
        redraw();
        return 0;
    }
    if (c == (char)0x83) {              /* down: newer history */
        if (hnav == -1)
            return 0;
        if (hnav < hcount - 1) {
            hnav++;
            hist_show(hnav);
        } else {
            hnav = -1;
            nstrcpy(line, draft);
            len = (int)nstrlen(line);
            pos = len;
        }
        redraw();
        return 0;
    }
    if (c == (char)0x84) {              /* home */
        pos = 0;
        redraw();
        return 0;
    }
    if (c == (char)0x85) {              /* end */
        pos = len;
        redraw();
        return 0;
    }
    if (c == (char)0x86) {              /* delete at cursor */
        if (pos < len) {
            int np = next_char(pos);
            int n = len - np;
            for (int i = 0; i < n; i++)
                line[pos + i] = line[np + i];
            len -= np - pos;
            hnav = -1;
            redraw();
        }
        return 0;
    }
    /* Printable byte (ASCII or UTF-8 piece): insert at the cursor. */
    if ((u8)c >= 0x20 && len < (int)sizeof(line) - 1) {
        for (int i = len; i > pos; i--)
            line[i] = line[i - 1];
        line[pos++] = c;
        len++;
        hnav = -1;
        redraw();
        return 0;
    }
    return 0;
}

/* Read one edited line. Returns 0 normally, -1 on Ctrl+D / EOF. */
static int read_line(void)
{
    int esc = 0, csi = 0, csi_num = 0;

    len = pos = 0;
    maxdraw = 0;
    hnav = -1;
    line[0] = '\0';
    set_prompt();
    redraw();

    for (;;) {
        char c;
        if (sys_read(0, &c, 1) != 1)
            return -1;

        if (esc) {
            if (!csi) {
                if (c == '[') {
                    csi = 1;
                    csi_num = 0;
                } else {
                    esc = 0;   /* bare ESC: ignore */
                }
                continue;
            }
            if (c >= '0' && c <= '9') {
                /* CSI input is untrusted; keep the parameter bounded. */
                if (csi_num < 1000)
                    csi_num = csi_num * 10 + (c - '0');
                continue;
            }
            esc = csi = 0;
            if (c == 'A')
                c = (char)0x82;   /* up */
            else if (c == 'B')
                c = (char)0x83;   /* down */
            else if (c == 'C')
                c = (char)0x81;   /* right */
            else if (c == 'D')
                c = (char)0x80;   /* left */
            else if (c == 'H')
                c = (char)0x84;   /* home */
            else if (c == 'F')
                c = (char)0x85;   /* end */
            else if (c == '~') {
                if (csi_num == 3)
                    c = (char)0x86;   /* delete */
                else
                    continue;         /* ins/pgup/pgdn: ignore */
            } else {
                continue;
            }
        } else if (c == 0x1B) {
            esc = 1;
            continue;
        }

        int r = edit_key(c);
        if (r != 0)
            return r;
    }
}

/* --- parsing ----------------------------------------------------------- */

/* Operator characters split into their own tokens when unquoted. */
static int is_op(char c)
{
    return c == '|' || c == '<' || c == '>';
}

/* tok_op[i] = 1 when args[i] came from an unquoted operator, so a quoted
 * "|" stays a plain word. Filled by tokenize(). */
static u8 tok_op[MAX_ARGS + 2];

/* Kind of an operator token: 1 "|", 2 "<", 3 ">", 4 ">>". */
static int op_kind(const char *s)
{
    if (s[0] == '|' && !s[1])
        return 1;
    if (s[0] == '<' && !s[1])
        return 2;
    if (s[0] == '>' && !s[1])
        return 3;
    if (s[0] == '>' && s[1] == '>' && !s[2])
        return 4;
    return 0;
}

static int tokenize(char *ln, char *argv[], int max)
{
    int n = 0;

    while (*ln && n < max) {
        while (*ln == ' ' || *ln == '\t')
            ln++;
        if (!*ln)
            break;
        if (n >= max)
            return -1;
        if (is_op(*ln)) {
            /* Operator tokens live in opsym: `>>` may be glued to its
             * path, and we must never overwrite the byte after them. */
            static char opsym[MAX_ARGS + 2][3];
            char *op = opsym[n];
            op[0] = *ln++;
            op[2] = '\0';
            if (op[0] == '>' && *ln == '>') {
                op[1] = '>';
                ln++;
            } else
                op[1] = '\0';
            tok_op[n] = 1;
            argv[n++] = op;
            continue;
        }
        char *word = ln;
        char *dst = ln, *src = ln;
        char quote = 0;
        while (*src) {
            if (quote) {
                if (*src == quote) {
                    quote = 0;
                    src++;
                } else {
                    *dst++ = *src++;
                }
            } else if (*src == '\'' || *src == '"') {
                quote = *src++;
            } else if (*src == '\\' && src[1]) {
                src++;
                *dst++ = *src++;
            } else if (*src == ' ' || *src == '\t') {
                break;
            } else if (is_op(*src)) {
                break;             /* word stops at an operator token */
            } else {
                *dst++ = *src++;
            }
        }
        if (quote)
            return -1;             /* unmatched single/double quote */
        if (dst < src)
            *dst = '\0';
        else if (*src == ' ' || *src == '\t')
            *src++ = '\0';
        /* Leave an operator in place for the next token iteration. */
        tok_op[n] = 0;
        argv[n++] = word;
        ln = src;
    }
    return n;
}

/* --- builtins ---------------------------------------------------------- */

static int builtin_cd(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/";
    if (argc > 2) {
        nputs("cd: too many arguments\n");
        return 1;
    }
    if (sys_chdir(dir) != 0) {
        putf("cd: %s: no such directory\n", dir);
        return 1;
    }
    return 0;
}

static int builtin_history(void)
{
    for (int i = 0; i < hcount; i++)
        putf("%d  %s\n", (u64)(i + 1), hist[i]);
    return 0;
}

static int builtin_help(void)
{
    nputs("nsh - NEWOS " NEWOS_VERSION " shell\n");
    nputs("builtins: cd, exit, help, history, clear\n");
    nputs("/bin: ls cat echo mkdir rmdir rm mv touch pwd clear uname\n");
    nputs("       hostname whoami id date uptime ps kill sleep free\n");
    nputs("       df wc head grep find sort hello img vid desktop\n");
    nputs("       about newpkg newfetch nano tcc busybox\n");
    nputs("GNU tools install under /usr/bin; use /usr/bin/<tool>\n");
    nputs("BusyBox: 'busybox --list'; unshadowed applet names run automatically\n");
    nputs("Images: img /etc/splash.bmp  |  img /etc/test.ppm\n");
    nputs("nano: GNU nano (Ctrl+O save, Ctrl+X exit)\n");
    nputs("tcc: Tiny C Compiler; 'tcc -c file.c -o file.o' needs no SDK\n");
    nputs("pipes: cmd1 | cmd2    redirect: cmd > f, cmd >> f, cmd < f\n");
    nputs("GUI: boot with gui=1, choose the GUI Limine entry, or run desktop\n");
    nputs("boot: init=<path> gui=0/1 headless=1 test_mode=1\n");
    nputs("keys: Ctrl+A/E line ends, Ctrl+U/W/K cut, Ctrl+Y/V paste,\n");
    nputs("      Ctrl+C cancel, Ctrl+D exit, Ctrl+L clear\n");
    return 0;
}

static int builtin_clear(void)
{
    for (int i = 0; i < 25; i++)
        nputc('\n');
    nputs("\x1B[2J\x1B[H");
    return 0;
}

/* --- main loop --------------------------------------------------------- */

static void hist_push(const char *ln)
{
    if (!*ln)
        return;
    if (hcount > 0 && nstrcmp(hist[hcount - 1], ln) == 0)
        return;   /* skip repeats */
    if (hcount < HIST_MAX) {
        nstrcpy(hist[hcount++], ln);
    } else {
        for (int i = 1; i < HIST_MAX; i++)
            nstrcpy(hist[i - 1], hist[i]);
        nstrcpy(hist[HIST_MAX - 1], ln);
    }
}

static void show_motd(void)
{
    long fd = sys_open("/etc/motd", O_READ);
    if (fd < 0)
        return;
    char buf[256];
    long n;
    while ((n = sys_read((int)fd, buf, sizeof(buf))) > 0)
        sys_write(1, buf, (u64)n);
    sys_close((int)fd);
}

static int text_has(const char *haystack, const char *needle)
{
    u64 n = nstrlen(needle);

    if (!n)
        return 1;
    for (; *haystack; haystack++) {
        if (nstrncmp(haystack, needle, n) == 0)
            return 1;
    }
    return 0;
}

static int boot_flag(const char *flag)
{
    char buf[512];
    long fd = sys_open("/proc/cmdline", O_READ);
    long n;

    if (fd < 0)
        return 0;
    n = sys_read((int)fd, buf, sizeof(buf) - 1);
    sys_close((int)fd);
    if (n <= 0)
        return 0;
    buf[n] = '\0';
    return text_has(buf, flag);
}

/* The kernel always enters this shell. On a framebuffer boot carrying
 * `gui=1`, the boot shell (argv[1] == "--session") opens the graphical
 * session as a child and returns to the prompt when the desktop closes
 * (Ctrl+Q or the last window). Launches made later from the shell have no
 * session flag, so nested `sh` instances stay plain shells. */
static void start_gui_session(void)
{
    struct nsh_fbinfo fi;
    char *argv[1];
    long pid;

    if (!boot_flag("gui=1") || boot_flag("test_mode=1") ||
        boot_flag("headless=1"))
        return;
    if (sys_fbinfo(&fi) != 0 || !fi.present) {
        nputs("gui: no framebuffer; staying in nsh\n");
        return;
    }
    argv[0] = (char *)"/bin/voxel";
    nputs("gui: starting /bin/voxel (Ctrl+Q returns to nsh)\n");
    pid = sys_spawn2("/bin/voxel", argv, 1);
    if (pid < 0) {
        nputs("gui: voxel unavailable; staying in nsh\n");
        return;
    }
    (void)sys_waitpid((pid_t)pid);
    /* fbcon and the compositor share the panel; start the text session on a
     * clean screen with a fresh cursor after the compositor's last frame. */
    nputs("\x1B[2J\x1B[H");
    nputs("gui: voxel completed; back to nsh\n");
}

static int is_builtin_name(const char *s)
{
    return nstrcmp(s, "cd") == 0 || nstrcmp(s, "exit") == 0 ||
           nstrcmp(s, "help") == 0 || nstrcmp(s, "history") == 0 ||
           nstrcmp(s, "clear") == 0;
}

/* Spawn an executable path while passing the original command argv. */
static long spawn_path(const char *path, char **argv, int argc,
                       int fdin, int fdout)
{
    if (fdin < 0 && fdout < 0)
        return sys_spawn2(path, argv, argc);
    return sys_spawn3(path, argv, argc, fdin, fdout);
}

/* Bare commands search native tools first, then installed GNU tools, then
 * BusyBox.  BusyBox's `--install` wants symlinks, which the VFS does not
 * provide, so nsh invokes the multi-call binary as `busybox <applet>` for
 * any name that is not a native or GNU command. */
static long spawn_bin(char **argv, int argc, int fdin, int fdout)
{
    char path[256];
    const char *name = argv[0];
    long pid;

    for (const char *p = name; *p; p++) {
        if (*p == '/')
            return spawn_path(name, argv, argc, fdin, fdout);
    }
    if (nstrlen(name) > sizeof(path) - 10) {
        putf("nsh: command too long\n");
        return -1;
    }
    nstrcpy(path, "/bin/");
    nstrcat(path, name);
    pid = spawn_path(path, argv, argc, fdin, fdout);
    if (pid >= 0)
        return pid;
    nstrcpy(path, "/usr/bin/");
    nstrcat(path, name);
    pid = spawn_path(path, argv, argc, fdin, fdout);
    if (pid >= 0)
        return pid;
    /* System administration tools ship in /sbin (OpenRC's home upstream). */
    nstrcpy(path, "/sbin/");
    nstrcat(path, name);
    pid = spawn_path(path, argv, argc, fdin, fdout);
    if (pid >= 0)
        return pid;

    /* Keep the fallback bounded: nsh has a small command-line limit, and a
     * fixed array avoids allocation in this path. */
    if (argc < 16) {
        char *bb_argv[18];
        int n = 0;
        bb_argv[n++] = (char *)"busybox";
        bb_argv[n++] = (char *)name;
        for (int i = 1; i < argc; i++)
            bb_argv[n++] = argv[i];
        bb_argv[n] = (char *)0;
        pid = spawn_path("/bin/busybox", bb_argv, n, fdin, fdout);
        if (pid >= 0)
            return pid;
    }
    putf("nsh: %s: command not found\n", name);
    return -1;
}

static int wait_rc(long pid)
{
    long rc = sys_waitpid((pid_t)pid);

    if (rc == 128 + 9)
        putf("^C\n");                       /* killed via the waitpid Ctrl+C path */
    else if (rc != 0)
        putf("[exit %d]\n", rc);
    return (int)rc;
}

static int run_external(char **argv, int argc)
{
    long pid = spawn_bin(argv, argc, -1, -1);

    if (pid < 0)
        return 127;
    return wait_rc(pid);
}

/* Run `args` when it contains operators: parse stages + redirections,
 * wire pipes, spawn every stage, then reap. */
static void run_pipeline(char *args[], int n)
{
    static char *st_av[MAX_STAGES][MAX_ARGS + 1];
    int st_n[MAX_STAGES];
    long pid[MAX_STAGES];
    int pfd[MAX_STAGES][2];
    const char *in_path = 0, *out_path = 0;
    int append = 0, in_stage = -1, out_stage = -1;
    int ns = 0, i, nstages, in_fd = -1, out_fd = -1;

    for (i = 0; i < MAX_STAGES; i++)
        pfd[i][0] = pfd[i][1] = -1;
    st_n[0] = 0;
    for (i = 0; i < n; i++) {
        int k = tok_op[i] ? op_kind(args[i]) : 0;
        if (!k) {
            if (st_n[ns] == MAX_ARGS) {
                putf("nsh: too many arguments (maximum %d)\n", MAX_ARGS);
                return;
            }
            st_av[ns][st_n[ns]++] = args[i];
            continue;
        }
        if (k == 1) {
            if (st_n[ns] == 0 || ns == MAX_STAGES - 1) {
                putf("nsh: syntax error near '|'\n");
                return;
            }
            ns++;
            st_n[ns] = 0;
            continue;
        }
        if (i + 1 >= n || tok_op[i + 1]) {
            putf("nsh: %s: missing file operand\n", args[i]);
            return;
        }
        if (k == 2) {
            if (in_path) {
                putf("nsh: only one '<' allowed\n");
                return;
            }
            in_path = args[++i];
            in_stage = ns;
        } else {
            out_path = args[++i];
            append = (k == 4);
            out_stage = ns;
        }
    }
    if (st_n[ns] == 0) {
        putf("nsh: syntax error: dangling operator\n");
        return;
    }
    nstages = ns + 1;
    if (in_stage != -1 && in_stage != 0) {
        putf("nsh: '<' must precede the first command\n");
        return;
    }
    if (out_stage != -1 && out_stage != ns) {
        putf("nsh: '>' must follow the last command\n");
        return;
    }
    for (i = 0; i < nstages; i++) {
        st_av[i][st_n[i]] = 0;
        if (nstages > 1 && is_builtin_name(st_av[i][0])) {
            putf("nsh: %s: builtin cannot run in a pipeline\n",
                 st_av[i][0]);
            return;
        }
    }

    if (in_path) {
        in_fd = (int)sys_open(in_path, O_READ);
        if (in_fd < 0) {
            putf("nsh: %s: cannot open\n", in_path);
            return;
        }
    }
    if (out_path) {
        /* `>>` must preserve the existing contents. Opening with O_TRUNC
         * (as `>` needs) emptied the file first, so the lseek below then
         * seeked to offset 0 and appended text replaced everything. */
        int oflags = O_WRITE | O_CREATE | (append ? O_APPEND : O_TRUNC);
        out_fd = (int)sys_open(out_path, oflags);
        if (out_fd < 0) {
            putf("nsh: %s: cannot open for writing\n", out_path);
            if (in_fd >= 0)
                sys_close(in_fd);
            return;
        }
        if (append)
            sys_lseek(out_fd, 0, SEEK_END);
    }
    for (i = 1; i < nstages; i++) {
        if (sys_pipe(pfd[i - 1]) < 0) {
            putf("nsh: out of pipes\n");
            goto fail;
        }
    }
    for (i = 0; i < nstages; i++) {
        int fdin = (i == 0) ? in_fd : pfd[i - 1][0];
        int fdout = (i == nstages - 1) ? out_fd : pfd[i][1];

        pid[i] = spawn_bin(st_av[i], st_n[i], fdin, fdout);
        if (pid[i] < 0) {
            /* Release the parent's pipe references before waiting. A child
             * may be blocked writing to a pipe whose reader was never
             * spawned; waiting first would deadlock the shell. */
            for (int j = 1; j < nstages; j++) {
                if (pfd[j - 1][0] >= 0) {
                    sys_close(pfd[j - 1][0]);
                    pfd[j - 1][0] = -1;
                }
                if (pfd[j - 1][1] >= 0) {
                    sys_close(pfd[j - 1][1]);
                    pfd[j - 1][1] = -1;
                }
            }
            if (in_fd >= 0) {
                sys_close(in_fd);
                in_fd = -1;
            }
            if (out_fd >= 0) {
                sys_close(out_fd);
                out_fd = -1;
            }
            while (--i >= 0)
                sys_waitpid((pid_t)pid[i]);
            goto fail;
        }
    }
    /* Our copies of the pipe ends must go: a held write end would stop
     * downstream EOF forever. */
    for (i = 1; i < nstages; i++) {
        sys_close(pfd[i - 1][0]);
        sys_close(pfd[i - 1][1]);
    }
    if (in_fd >= 0)
        sys_close(in_fd);
    if (out_fd >= 0)
        sys_close(out_fd);
    for (i = 0; i < nstages; i++)
        wait_rc(pid[i]);
    return;

fail:
    if (in_fd >= 0)
        sys_close(in_fd);
    if (out_fd >= 0)
        sys_close(out_fd);
    for (i = 1; i < nstages; i++) {
        if (pfd[i - 1][0] >= 0)
            sys_close(pfd[i - 1][0]);
        if (pfd[i - 1][1] >= 0)
            sys_close(pfd[i - 1][1]);
    }
}

/* One command line: builtins run here, everything else is spawned. Shared
 * by the interactive loop and the boot script (see run_script). */
/* Append `v` as decimal to `out` (no printf in the freestanding lib). */
static void append_dec(char *out, long v)
{
    char tmp[24];
    int n = 0;
    size_t k = nstrlen(out);

    if (v < 0)
        out[k++] = '-';
    if (v < 0)
        v = -v;
    if (v == 0)
        tmp[n++] = '0';
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0)
        out[k++] = tmp[--n];
    out[k] = '\0';
}

/* `cmd &` inside a service script: spawn detached and record the pid in
 * /run/<service>.pid, the NEWOS equivalent of --pidfile. */
static int run_detached(char **args, int n)
{
    long pid = spawn_bin(args, n, -1, -1);
    char path[96];
    char body[32];

    if (pid < 0)
        return 127;
    if (!*svc_name)
        return 0;
    nstrcpy(path, "/run/");
    nstrcat(path, svc_name);
    nstrcat(path, ".pid");
    nstrcpy(body, "");
    append_dec(body, pid);
    nstrcat(body, "\n");
    {
        int fd = (int)sys_open(path, O_WRITE | O_CREATE | O_TRUNC);

        if (fd >= 0) {
            sys_write(fd, body, nstrlen(body));
            sys_close(fd);
        }
    }
    return 0;
}

/* One command line: builtins run here, everything else is spawned. Shared
 * by the interactive loop, the boot script (run_script) and the OpenRC
 * service interpreter (run_service_script). Returns the shell's exit code
 * for that line (0 for builtins). */
static int exec_line(char *ln)
{
    static char *args[MAX_ARGS + 1];
    int n = tokenize(ln, args, MAX_ARGS);
    int detached = 0;

    if (n < 0) {
        nputs("nsh: syntax error: unmatched quote or too many tokens\n");
        return 2;
    }
    if (n == 0)
        return 0;
    if (n == 1 && !tok_op[0] && nstrcmp(args[0], "&") == 0)
        return 0;
    if (n > 1 && !tok_op[n - 1] && nstrcmp(args[n - 1], "&") == 0) {
        detached = 1;
        n--;
    }
    args[n] = 0;

    if (detached)
        return run_detached(args, n);

    int ops = 0;
    for (int i = 0; i < n; i++)
        if (tok_op[i])
            ops = 1;
    if (ops) {
        run_pipeline(args, n);
        return 0;
    } else if (nstrcmp(args[0], "exit") == 0) {
        nputs("Bye.\n");
        sys_exit(0);
    } else if (nstrcmp(args[0], "cd") == 0) {
        return builtin_cd(n, args);
    } else if (nstrcmp(args[0], "help") == 0) {
        builtin_help();
    } else if (nstrcmp(args[0], "history") == 0) {
        builtin_history();
    } else if (nstrcmp(args[0], "clear") == 0) {
        builtin_clear();
    } else {
        return run_external(args, n);
    }
    return 0;
}

/* Execute a file of commands. Returns 0 normally, -1 when `path` is absent
 * (not an error: an image without a script just boots interactively),
 * -2 on a syntax-free read failure. */
static int run_script(const char *path)
{
    static char buf[512];
    static char ln[256];
    long fd = sys_open(path, O_READ);
    long n;
    size_t pos = 0, have = 0, used = 0;
    int status = 0;
    int overflow = 0;

    if (fd < 0)
        return -1;

    for (;;) {
        if (used == have) {
            n = sys_read((int)fd, buf, sizeof(buf));
            if (n < 0) {
                status = -2;
                break;
            }
            if (n == 0)
                break;
            have = (size_t)n;
            used = 0;
        }
        while (used < have) {
            char c = buf[used++];
            if (c == '\n') {
                if (overflow) {
                    nputs("nsh: script line too long; skipped\n");
                } else {
                    ln[pos] = '\0';
                    if (pos && ln[0] != '#')
                        exec_line(ln);
                }
                pos = 0;
                overflow = 0;
                continue;
            }
            if (c == '\r')
                continue;
            if (pos < sizeof(ln) - 1)
                ln[pos++] = c;
            else
                overflow = 1;
        }
    }
    if (overflow) {
        nputs("nsh: script line too long; skipped\n");
    } else if (pos) {
        ln[pos] = '\0';
        if (ln[0] != '#')
            exec_line(ln);
    }
    sys_close((int)fd);
    return status;
}

/* ---- OpenRC service scripts -------------------------------------------
 * `/bin/sh /etc/init.d/<svc> <action>` runs one function block of an init
 * script. nsh is the interpreter OpenRC uses for those scripts, so the
 * supported vocabulary is deliberately the one the shipped scripts need and
 * nothing more: `name() { <one command per line> }` blocks, KEY=VALUE
 * option files under /etc/conf.d, `$NAME`/`${NAME}` substitution and the
 * trailing `&` detach handled by exec_line. Chaining, subshells and test
 * builtins are out of scope: a script needing them is a script bug here. */

#define SVC_MAX_VARS 32
#define SVC_MAX_KEY 48
#define SVC_MAX_VAL 128
#define SVC_MAX_SRC 16384
#define SVC_MAX_LINE 256
#define SVC_MAX_NAME 64

/* svc_name lives with the shell globals: run_detached writes the pidfile
 * through it. */
static char s_svc_key[SVC_MAX_VARS][SVC_MAX_KEY];
static char s_svc_val[SVC_MAX_VARS][SVC_MAX_VAL];
static int s_svc_nvars;

/* Whole-file read: service scripts and option files are small and the root
 * filesystem is a tmpfs, so one buffer holds them. */
static long read_all(const char *path, char *buf, long cap)
{
    long fd = sys_open(path, O_READ);
    long have = 0;

    if (fd < 0)
        return -1;
    while (have < cap - 1) {
        long n = sys_read((int)fd, buf + have, cap - have);
        if (n <= 0)
            break;
        have += n;
    }
    sys_close((int)fd);
    buf[have] = '\0';
    return have;
}

static void svc_set(const char *key, const char *val)
{
    int slot = -1;
    long klen = nstrlen(key);

    if (!klen)
        return;
    if (klen > SVC_MAX_KEY - 1)
        klen = SVC_MAX_KEY - 1;
    for (int i = 0; i < s_svc_nvars; i++)
        if (nstrncmp(s_svc_key[i], key, (u64)klen) == 0 &&
            s_svc_key[i][klen] == '\0') {
            slot = i;
            break;
        }
    if (slot < 0) {
        if (s_svc_nvars == SVC_MAX_VARS)
            return;
        slot = s_svc_nvars++;
        for (long i = 0; i < klen; i++)
            s_svc_key[slot][i] = key[i];
        s_svc_key[slot][klen] = '\0';
    }
    long vlen = nstrlen(val);

    if (vlen > SVC_MAX_VAL - 1)
        vlen = SVC_MAX_VAL - 1;
    for (long i = 0; i < vlen; i++)
        s_svc_val[slot][i] = val[i];
    s_svc_val[slot][vlen] = '\0';
}

static const char *svc_get(const char *key, long len)
{
    for (int i = 0; i < s_svc_nvars; i++)
        if (nstrncmp(s_svc_key[i], key, (u64)len) == 0 &&
            s_svc_key[i][len] == '\0')
            return s_svc_val[i];
    return 0;
}

/* KEY=VALUE lines of /etc/conf.d/<svc>; surrounding quotes are stripped.
 * A missing file is not an error: most services have no options. */
static void svc_load_conf(const char *svc)
{
    static char buf[4096];
    char path[SVC_MAX_NAME + 16];
    long n;

    nstrcpy(path, "/etc/conf.d/");
    nstrcat(path, svc);
    n = read_all(path, buf, (long)sizeof(buf));
    if (n <= 0)
        return;

    char *p = buf;
    while (*p) {
        char *eol = p;
        while (*eol && *eol != '\n')
            eol++;
        if (*eol == '\n')
            *eol++ = '\0';
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p != '#' && *p != '\0') {
            char *eq = p;
            while (*eq && *eq != '=')
                eq++;
            if (*eq == '=') {
                char key[SVC_MAX_KEY];
                long kl = eq - p;
                char *val = eq + 1;

                if (kl < (long)sizeof(key)) {
                    for (long i = 0; i < kl; i++)
                        key[i] = p[i];
                    key[kl] = '\0';
                    while (*val == ' ' || *val == '\t')
                        val++;
                    long vl = nstrlen(val);
                    while (vl > 0 && (val[vl - 1] == ' ' || val[vl - 1] == '\t'))
                        val[--vl] = '\0';
                    if (vl >= 2 && ((*val == '"' && val[vl - 1] == '"') ||
                                    (*val == '\'' && val[vl - 1] == '\''))) {
                        val[vl - 1] = '\0';
                        val++;
                    }
                    svc_set(key, val);
                }
            }
        }
        p = eol;
    }
}

/* Copy `in` to `out`, replacing $NAME and ${NAME}. An unset name becomes the
 * empty string, which is what a POSIX shell does for an unset variable. */
static void svc_expand(const char *in, char *out, long cap)
{
    long o = 0;
    const char *p = in;

#define SVC_PUT(c)                                    \
    do {                                              \
        if (o < cap - 1)                              \
            out[o++] = (c);                           \
    } while (0)

    while (*p) {
        if (*p != '$') {
            SVC_PUT(*p++);
            continue;
        }
        p++;
        long len = 0;
        if (*p == '{') {
            const char *s = ++p;
            while (*p && *p != '}')
                p++;
            len = p - s;
            if (*p == '}')
                p++;
            else
                len = 0;
            const char *v = len ? svc_get(s, len) : 0;
            while (v && *v)
                SVC_PUT(*v++);
            continue;
        }
        const char *s = p;
        while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
               (*p >= '0' && *p <= '9') || *p == '_')
            p++;
        len = p - s;
        const char *v = len ? svc_get(s, len) : 0;
        if (!v && !len)
            SVC_PUT('$');
        while (v && *v)
            SVC_PUT(*v++);
    }
#undef SVC_PUT
    out[o] = '\0';
}

/* Find `<name>() { ... }` and return its body; `*outlen` is the body length
 * up to the line whose first token is `}`. */
static const char *svc_find_block(const char *src, const char *name,
                                  long *outlen)
{
    long nl = nstrlen(name);
    const char *ls = src;

    while (*ls) {
        const char *le = ls;
        while (*le && *le != '\n')
            le++;
        const char *q = ls;
        while (*q == ' ' || *q == '\t')
            q++;
        if (nstrncmp(q, name, (u64)nl) == 0 && q[nl] == '(') {
            const char *b = q + nl;
            while (*b == '(' || *b == ')' || *b == ' ' || *b == '\t')
                b++;
            if (*b == '{') {
                const char *body = b + 1;
                const char *cl = (*le == '\n') ? le + 1 : le;

                while (*cl) {
                    const char *ce = cl;
                    while (*ce && *ce != '\n')
                        ce++;
                    q = cl;
                    while (*q == ' ' || *q == '\t')
                        q++;
                    if (*q == '}') {
                        *outlen = cl - body;
                        return body;
                    }
                    cl = (*ce == '\n') ? ce + 1 : ce;
                }
                return 0;
            }
        }
        ls = (*le == '\n') ? le + 1 : le;
    }
    return 0;
}

/* Run one action of a service script. Every line executes (a shell does not
 * abort a function on a failed command); the first non-zero exit code is the
 * block's result, so a broken start() is reported even when a later command
 * would have succeeded. */
static int run_service_script(const char *path, const char *action)
{
    static char src[SVC_MAX_SRC];
    static char ln[SVC_MAX_LINE];
    static char exp[SVC_MAX_LINE];
    long n = read_all(path, src, (long)sizeof(src));
    const char *body;
    long blen = 0;
    int rc = 0;

    if (n <= 0) {
        nputs("nsh: cannot read service script: ");
        nputs(path);
        nputc('\n');
        return 1;
    }

    /* The service name is the script's basename: `$SVCNAME` and the pidfile
     * path both derive from it. */
    {
        const char *base = path;
        const char *last = 0;

        for (const char *p = path; *p; p++)
            if (*p == '/')
                last = p;
        base = last ? last + 1 : base;
        long i = 0;
        while (base[i] && i < (long)sizeof(svc_name) - 1) {
            svc_name[i] = base[i];
            i++;
        }
        svc_name[i] = '\0';
    }

    svc_set("SVCNAME", svc_name);
    svc_load_conf(svc_name);

    body = svc_find_block(src, action, &blen);
    if (!body) {
        nputs("nsh: service ");
        nputs(svc_name);
        nputs(" does not support the action ");
        nputs(action);
        nputc('\n');
        return 1;
    }

    for (long i = 0; i < blen; ) {
        long k = 0;
        while (i < blen && body[i] != '\n') {
            if (k < (long)sizeof(ln) - 1 && body[i] != '\r')
                ln[k++] = body[i];
            i++;
        }
        ln[k] = '\0';
        i++;
        while (*ln == ' ' || *ln == '\t') {
            for (long j = 0; ln[j]; j++)
                ln[j] = ln[j + 1];
        }
        if (!*ln || *ln == '#')
            continue;
        svc_expand(ln, exp, (long)sizeof(exp));
        if (!*exp)
            continue;
        {
            int r = exec_line(exp);

            if (r && !rc)
                rc = r;
        }
    }
    return rc;
}

int main(int argc, char **argv)
{
    int boot_session = argc > 1 && nstrcmp(argv[1], "--session") == 0;
    int rc_console = argc > 2 && nstrcmp(argv[2], "--rc-console") == 0;

    /* Service-script mode owns the whole process: no boot rc, no motd, no
     * prompt. OpenRC calls it as `/bin/sh <script> <action> --rc`. */
    if (argc > 2 && argv[1][0] == '/')
        sys_exit(run_service_script(argv[1], argv[2]));

    /* A boot script configures the session and stays out of the way: end it
     * with `exit` to hand control back to the kernel (what a non-interactive
     * CI run wants), or let it fall through to the prompt. */
    if (run_script("/nsh.rc") < 0)
        run_script("/etc/nsh.rc");

    show_motd();
    nputs("Type 'help' for commands.\n");
    /* `--rc-console` is the console that /sbin/openrc-init supervises: the
     * graphical session belongs to the `desktop` service there, so this shell
     * must not start a second compositor. */
    if (boot_session && !rc_console)
        start_gui_session();

    for (;;) {
        int r = read_line();
        if (r < 0)
            sys_exit(0);
        line[len] = '\0';
        hist_push(line);
        exec_line(line);
    }
}
