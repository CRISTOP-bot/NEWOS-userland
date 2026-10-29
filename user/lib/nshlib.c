/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/lib/nshlib.c
 * Description: nshlib implementation: syscall stubs + string/output helpers.
 * Added: 2026-10-17
 */
/* nshlib implementation: syscall stubs + string/output helpers. */

#include "nshlib.h"

#define SYSC0(nr) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr) : "memory"); r; })
#define SYSC1(nr, a) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)) : "memory"); r; })
#define SYSC2(nr, a, b) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)) \
    : "memory"); r; })
#define SYSC3(nr, a, b, c) ({ long r; __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)), \
    "d"((long)(c)) : "memory"); r; })
#define SYSC4(nr, a, b, c, d) ({ long r; \
    register long _x10 __asm__("r10") = (long)(d); \
    __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)), \
    "d"((long)(c)), "r"(_x10) : "memory"); r; })
#define SYSC5(nr, a, b, c, d, e) ({ long r; \
    register long _x10 __asm__("r10") = (long)(d); \
    register long _x8 __asm__("r8") = (long)(e); \
    __asm__ __volatile__( \
    "int $0x80" : "=a"(r) : "a"(nr), "D"((long)(a)), "S"((long)(b)), \
    "d"((long)(c)), "r"(_x10), "r"(_x8) : "memory"); r; })

long sys_read(int fd, void *buf, u64 len)
{
    return SYSC3(SYS_READ, fd, buf, len);
}

long sys_write(int fd, const void *buf, u64 len)
{
    return SYSC3(SYS_WRITE, fd, buf, len);
}

long sys_open(const char *path, int flags)
{
    return SYSC2(SYS_OPEN, path, flags);
}

long sys_close(int fd)
{
    return SYSC1(SYS_CLOSE, fd);
}

long sys_mkdir(const char *path)
{
    return SYSC1(SYS_MKDIR, path);
}

long sys_unlink(const char *path)
{
    return SYSC1(SYS_UNLINK, path);
}

long sys_rename(const char *oldpath, const char *newpath)
{
    return SYSC2(SYS_RENAME, oldpath, newpath);
}

long sys_stat(const char *path, struct nsh_stat *st)
{
    return SYSC2(SYS_STAT, path, st);
}

long sys_readdir(const char *path, struct nsh_dirent *buf, u64 bytes)
{
    return SYSC3(SYS_READDIR, path, buf, bytes);
}

long sys_chdir(const char *path)
{
    return SYSC1(SYS_CHDIR, path);
}

long sys_getcwd(char *buf, u64 size)
{
    return SYSC2(SYS_GETCWD, buf, size);
}

long sys_spawn2(const char *path, char **argv, int argc)
{
    return SYSC3(SYS_SPAWN2, path, argv, argc);
}

long sys_waitpid(pid_t pid)
{
    return SYSC2(SYS_WAITPID, pid, 0);
}

long sys_getpid(void)
{
    return SYSC0(SYS_GETPID);
}

long sys_sleep(u64 ms)
{
    /* SYS_SLEEP is Linux nanosleep: the kernel reads a timespec. */
    struct nsh_timespec ts = { .tv_sec = (long)(ms / 1000),
                               .tv_nsec = (long)(ms % 1000) * 1000000 };
    return SYSC1(SYS_SLEEP, &ts);
}

long sys_ps(struct nsh_proc *buf, u64 bytes)
{
    return SYSC2(SYS_PS, buf, bytes);
}

long sys_kill(pid_t pid)
{
    return SYSC1(SYS_KILL, pid);
}

long sys_uname(struct nsh_uname *u)
{
    return SYSC1(SYS_UNAME, u);
}

long sys_gettime(struct nsh_time *t)
{
    return SYSC1(SYS_GETTIME, t);
}

long sys_sysinfo(struct nsh_sysinfo *si)
{
    return SYSC1(SYS_SYSINFO, si);
}

/* --- /proc helpers ----------------------------------------------------- */

static int proc_read_file(const char *path, char *buf, u64 size)
{
    int fd;
    u64 used = 0;

    if (!buf || size < 2)
        return -1;
    fd = (int)sys_open(path, O_READ);
    if (fd < 0)
        return -1;
    while (used + 1 < size) {
        long n = sys_read(fd, buf + used, size - 1 - used);
        if (n < 0) {
            sys_close(fd);
            return -1;
        }
        if (n == 0)
            break;
        used += (u64)n;
    }
    sys_close(fd);
    buf[used] = '\0';
    return used ? 0 : -1;
}

static int proc_kb_field(const char *buf, const char *key, u64 *out)
{
    const char *p = buf;
    u64 klen = nstrlen(key);

    while (*p) {
        if (nstrncmp(p, key, klen) == 0) {
            p += klen;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p < '0' || *p > '9')
                return -1;
            *out = natou(p);
            return 0;
        }
        while (*p && *p != '\n')
            p++;
        if (*p)
            p++;
    }
    return -1;
}

int nsh_proc_meminfo(u64 *total_kb, u64 *free_kb)
{
    char buf[512];

    if (!total_kb || !free_kb)
        return -1;
    if (proc_read_file("/proc/meminfo", buf, sizeof(buf)) != 0)
        return -1;
    if (proc_kb_field(buf, "MemTotal:", total_kb) != 0)
        return -1;
    if (proc_kb_field(buf, "MemFree:", free_kb) != 0)
        return -1;
    return 0;
}

int nsh_proc_uptime(u64 *sec, u64 *centi)
{
    char buf[64];
    const char *p = buf;

    if (!sec || !centi)
        return -1;
    if (proc_read_file("/proc/uptime", buf, sizeof(buf)) != 0)
        return -1;
    if (*p < '0' || *p > '9')
        return -1;

    *sec = natou(p);
    while (*p >= '0' && *p <= '9')
        p++;
    *centi = 0;
    if (*p == '.') {
        p++;
        if (*p >= '0' && *p <= '9')
            *centi = (u64)(*p++ - '0') * 10;
        if (*p >= '0' && *p <= '9')
            *centi += (u64)(*p - '0');
    }
    return 0;
}

long sys_fbinfo(struct nsh_fbinfo *fi)
{
    return SYSC1(SYS_FBINFO, fi);
}

long sys_fbwrite(const struct nsh_fbwrite *rq)
{
    return SYSC1(SYS_FBWRITE, rq);
}

long sys_lseek(int fd, long off, int whence)
{
    return SYSC3(SYS_LSEEK, fd, off, whence);
}

long sys_mouse_get(struct nsh_mouse *m)
{
    return SYSC1(SYS_MOUSE_GET, m);
}

long sys_kbd_grab(int on)
{
    return SYSC1(SYS_KBD_GRAB, on);
}

long sys_kbd_read(void *buf, u64 len)
{
    return SYSC2(SYS_KBD_READ, buf, len);
}

long sys_console_gui(int on)
{
    return SYSC1(SYS_CONSOLE_GUI, on);
}

long sys_pipe(int fds[2])
{
    return SYSC1(SYS_PIPE, fds);
}

long sys_spawn3(const char *path, char **argv, int argc, int fdin, int fdout)
{
    return SYSC5(SYS_SPAWN3, path, argv, argc, fdin, fdout);
}

void sys_exit(int code)
{
    SYSC1(SYS_EXIT, code);
    for (;;)
        ;
}

/* --- strings ----------------------------------------------------------- */

u64 nstrlen(const char *s)
{
    u64 n = 0;
    while (s[n])
        n++;
    return n;
}

int nstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int nstrncmp(const char *a, const char *b, u64 n)
{
    for (u64 i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i])
            return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
    }
    return 0;
}

char *nstrcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

char *nstrcat(char *d, const char *s)
{
    char *r = d;
    while (*d)
        d++;
    while ((*d++ = *s++))
        ;
    return r;
}

char *nstrchr(const char *s, char c)
{
    while (*s) {
        if (*s == c)
            return (char *)s;
        s++;
    }
    return c ? 0 : (char *)s;
}

void *nmemcpy(void *d, const void *s, u64 n)
{
    u8 *dd = d;
    const u8 *ss = s;
    for (u64 i = 0; i < n; i++)
        dd[i] = ss[i];
    return d;
}

void *nmemset(void *d, int c, u64 n)
{
    u8 *dd = d;
    for (u64 i = 0; i < n; i++)
        dd[i] = (u8)c;
    return d;
}

int natoi(const char *s)
{
    int neg = 0, v = 0;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

u64 natou(const char *s)
{
    u64 v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (u64)(*s++ - '0');
    return v;
}

/* --- output ------------------------------------------------------------ */

void nputc(char c)
{
    sys_write(1, &c, 1);
}

void nputs(const char *s)
{
    u64 n = nstrlen(s);
    if (n)
        sys_write(1, s, n);
}

void nputsn(const char *s, u64 n)
{
    if (n)
        sys_write(1, s, n);
}

void putdec(long v)
{
    char buf[24];
    int i = sizeof(buf);
    unsigned long u;
    if (v < 0) {
        nputc('-');
        u = (unsigned long)(-(v + 1)) + 1;
    } else {
        u = (unsigned long)v;
    }
    if (u == 0) {
        nputc('0');
        return;
    }
    while (u > 0) {
        buf[--i] = (char)('0' + (u % 10));
        u /= 10;
    }
    nputsn(&buf[i], sizeof(buf) - (u64)i);
}

void putu(u64 v)
{
    char buf[24];
    int i = sizeof(buf);
    if (v == 0) {
        nputc('0');
        return;
    }
    while (v > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    nputsn(&buf[i], sizeof(buf) - (u64)i);
}

void puthex(u64 v)
{
    char buf[16];
    int i = sizeof(buf);
    if (v == 0) {
        nputs("0x0");
        return;
    }
    while (v > 0) {
        u64 d = v & 0xF;
        buf[--i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v >>= 4;
    }
    nputs("0x");
    nputsn(&buf[i], sizeof(buf) - (u64)i);
}

void put2(u64 v)
{
    nputc((char)('0' + ((v / 10) % 10)));
    nputc((char)('0' + (v % 10)));
}

static void emit_padded(int fd, const char *s, int len, int width, int zero)
{
    int pad = width > len ? width - len : 0;

    if (zero && len > 0 && s[0] == '-') {
        sys_write(fd, s, 1);
        s++;
        len--;
        while (pad-- > 0)
            sys_write(fd, "0", 1);
        sys_write(fd, s, (u64)len);
        return;
    }
    if (!zero) {
        while (pad-- > 0)
            sys_write(fd, " ", 1);
    } else {
        while (pad-- > 0)
            sys_write(fd, "0", 1);
    }
    sys_write(fd, s, (u64)len);
}

static void vemits(int fd, const char *fmt, __builtin_va_list ap)
{
    char tmp[32];
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            sys_write(fd, fmt, 1);
            continue;
        }
        fmt++;
        int zero = 0;
        int width = 0;
        if (*fmt == '0') {
            zero = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            int n = (int)nstrlen(s);
            emit_padded(fd, s, n, width, zero);
            break;
        }
        case 'd': {
            long v = __builtin_va_arg(ap, long);
            int neg = v < 0;
            unsigned long u = neg ? (unsigned long)(-(v + 1)) + 1
                                  : (unsigned long)v;
            int i = sizeof(tmp);
            if (u == 0)
                tmp[--i] = '0';
            while (u > 0) {
                tmp[--i] = (char)('0' + (u % 10));
                u /= 10;
            }
            if (neg)
                tmp[--i] = '-';
            emit_padded(fd, &tmp[i], (int)(sizeof(tmp) - (u64)i), width,
                        zero);
            break;
        }
        case 'u': {
            unsigned long u = __builtin_va_arg(ap, unsigned long);
            int i = sizeof(tmp);
            if (u == 0)
                tmp[--i] = '0';
            while (u > 0) {
                tmp[--i] = (char)('0' + (u % 10));
                u /= 10;
            }
            emit_padded(fd, &tmp[i], (int)(sizeof(tmp) - (u64)i), width,
                        zero);
            break;
        }
        case 'x': {
            unsigned long u = __builtin_va_arg(ap, unsigned long);
            int i = sizeof(tmp);
            if (u == 0)
                tmp[--i] = '0';
            while (u > 0) {
                unsigned long d = u & 0xF;
                tmp[--i] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
                u >>= 4;
            }
            emit_padded(fd, &tmp[i], (int)(sizeof(tmp) - (u64)i), width,
                        zero);
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            emit_padded(fd, &c, 1, width, zero);
            break;
        }
        default:
            if (*fmt)
                sys_write(fd, fmt, 1);
            break;
        }
    }
}

void putf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vemits(1, fmt, ap);
    __builtin_va_end(ap);
}

void fputf(int fd, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vemits(fd, fmt, ap);
    __builtin_va_end(ap);
}

int fail(const char *prog, const char *msg)
{
    fputf(1, "%s: %s\n", prog, msg);
    return 1;
}
