/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/stdio.c
 * Description: The header's va_list is the same builtin type, so these mix freely.
 * Added: 2026-10-17
 */
#include "../include/stdio.h"
#include "../include/string.h"
#include "../include/stdlib.h"
#include "../include/errno.h"
#include "../include/unistd.h"
#include "../include/fcntl.h"
#include "syscall.h"
#include "../../abi/syscall_abi.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

/* The header's va_list is the same builtin type, so these mix freely. */

long libc_write(int fd, const void *buf, unsigned long len);

static void outc(char c)
{
    libc_write(1, &c, 1);
}

static void outs(const char *s)
{
    size_t n = 0;
    if (!s)
        s = "(null)";
    while (s[n])
        n++;
    if (n)
        libc_write(1, s, n);
}


int putchar(int c)
{
    char ch = (char)c;
    libc_write(1, &ch, 1);
    return c;
}
int puts(const char *s)
{
    outs(s);
    outc('\n');
    return 0;
}

/* Every printf-family entry point goes through the one engine below, so the
 * conversions cannot drift apart per stream. */
int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int r;

    va_start(ap, fmt);
    r = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return r;
}


/* --- FILE streams (thin fd wrappers, unbuffered) ----------------------- */

static FILE file_stdin = { 0, 0, 0, -1 };
static FILE file_stdout = { 1, 0, 0, -1 };
static FILE file_stderr = { 2, 0, 0, -1 };

FILE *stdin = &file_stdin;
FILE *stdout = &file_stdout;
FILE *stderr = &file_stderr;

FILE *fopen(const char *path, const char *mode)
{
    int flags = O_RDONLY;
    int plus = 0;
    int fd;
    FILE *f;
    const char *m;

    if (!path || !mode || !mode[0])
        return NULL;
    for (m = mode + 1; *m; m++) {
        if (*m == '+')
            plus = 1;
        else if (*m != 'b')
            return NULL;
    }
    if (mode[0] == 'r')
        flags = plus ? O_RDWR : O_RDONLY;
    else if (mode[0] == 'w')
        flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a')
        flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
    else
        return NULL;

    fd = open(path, flags);
    if (fd < 0) {
        errno = ENOENT;
        return NULL;
    }
    f = (FILE *)malloc(sizeof(FILE));
    if (!f) {
        libc_sys1(SYS_CLOSE, fd);
        errno = ENOMEM;
        return NULL;
    }
    f->fd = fd;
    f->err = 0;
    f->eof = 0;
    f->ungc = -1;
    return f;
}

int fclose(FILE *f)
{
    int r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (f == stdin || f == stdout || f == stderr)
        return 0;
    r = (int)libc_sys1(SYS_CLOSE, f->fd);
    free(f);
    return r;
}

size_t fread(void *p, size_t s, size_t n, FILE *f)
{
    size_t want, got = 0;
    if (!f || !p || !s) {
        if (f)
            f->err = 1;
        return 0;
    }
    want = s * n;
    if (f->ungc >= 0) {
        ((char *)p)[got++] = (char)f->ungc;
        f->ungc = -1;
    }
    while (got < want) {
        long r = libc_sys3(SYS_READ, f->fd, (long)((char *)p + got),
                           (long)(want - got));
        if (r < 0) {
            f->err = 1;
            break;
        }
        if (r == 0) {
            f->eof = 1;
            break;
        }
        got += (size_t)r;
    }
    return s ? got / s : 0;
}

size_t fwrite(const void *p, size_t s, size_t n, FILE *f)
{
    size_t want, got = 0;
    if (!f || !p || !s) {
        if (f)
            f->err = 1;
        return 0;
    }
    want = s * n;
    while (got < want) {
        long r = libc_sys3(SYS_WRITE, f->fd, (long)((const char *)p + got),
                           (long)(want - got));
        if (r < 0) {
            f->err = 1;
            break;
        }
        if (r == 0) {
            f->err = 1;
            break;
        }
        got += (size_t)r;
    }
    return s ? got / s : 0;
}

int fseek(FILE *f, long off, int whence)
{
    long r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    r = libc_sys3(SYS_LSEEK, f->fd, off, whence);
    if (r < 0) {
        f->err = 1;
        return -1;
    }
    f->eof = 0;
    f->ungc = -1;
    return 0;
}

long ftell(FILE *f)
{
    long r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    r = libc_sys3(SYS_LSEEK, f->fd, 0, SEEK_CUR);
    if (r < 0)
        f->err = 1;
    return r;
}

void rewind(FILE *f)
{
    if (!f)
        return;
    if (libc_sys3(SYS_LSEEK, f->fd, 0, SEEK_SET) < 0)
        f->err = 1;
    else
        f->eof = 0;
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0;
    if (!s || n <= 0 || !f) {
        if (f)
            f->err = 1;
        return NULL;
    }
    if (f->ungc >= 0) {
        s[i++] = (char)f->ungc;
        f->ungc = -1;
    }
    while (i + 1 < n) {
        long r = libc_sys3(SYS_READ, f->fd, (long)(s + i), 1);
        if (r < 0) {
            f->err = 1;
            return i ? s : NULL;
        }
        if (r == 0) {
            f->eof = 1;
            return i ? s : NULL;
        }
        if (s[i++] == '\n')
            break;
    }
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *f)
{
    char ch = (char)c;
    long r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    r = libc_sys3(SYS_WRITE, f->fd, (long)&ch, 1);
    if (r != 1) {
        f->err = 1;
        return -1;
    }
    return c;
}

int fputs(const char *s, FILE *f)
{
    size_t n = 0;
    if (!s || !f) {
        if (f)
            f->err = 1;
        return -1;
    }
    while (s[n])
        n++;
    if (n && (size_t)fwrite(s, 1, n, f) != n)
        return -1;
    return 0;
}

int getc(FILE *f)
{
    unsigned char ch;
    long r;
    if (!f) {
        errno = EINVAL;
        return -1;
    }
    if (f->ungc >= 0) {
        int c = f->ungc;
        f->ungc = -1;
        return c;
    }
    r = libc_sys3(SYS_READ, f->fd, (long)&ch, 1);
    if (r < 0) {
        f->err = 1;
        return -1;
    }
    if (r == 0) {
        f->eof = 1;
        return -1;
    }
    return (int)ch;
}

int ungetc(int c, FILE *f)
{
    if (!f || c == EOF || f->ungc >= 0)
        return EOF;
    f->ungc = (int)(unsigned char)c;
    f->eof = 0;
    return c;
}

int putc(int c, FILE *f)
{
    return fputc(c, f);
}

int fgetc(FILE *f)
{
    return getc(f);
}

int getchar(void)
{
    return getc(stdin);
}

int fflush(FILE *f)
{
    (void)f;
    return 0;   /* unbuffered: nothing to flush */
}

int ferror(FILE *f)
{
    return f ? f->err : 1;
}

int feof(FILE *f)
{
    return f ? f->eof : 1;
}

void clearerr(FILE *f)
{
    if (f) {
        f->err = 0;
        f->eof = 0;
    }
}

void perror(const char *s)
{
    if (s && *s) {
        libc_sys3(SYS_WRITE, 2, (long)s, (long)strlen(s));
        libc_sys3(SYS_WRITE, 2, (long)": ", 2);
    }
    {
        const char *e = strerror(errno);
        size_t n = strlen(e);
        libc_sys3(SYS_WRITE, 2, (long)e, (long)n);
        libc_sys3(SYS_WRITE, 2, (long)"\n", 1);
    }
}

ssize_t getline(char **lp, size_t *cap, FILE *f)
{
    size_t len = 0;
    if (!lp || !cap || !f) {
        if (f)
            f->err = 1;
        return -1;
    }
    if (!*lp) {
        *cap = 128;
        *lp = (char *)malloc(*cap);
        if (!*lp)
            return -1;
    }
    for (;;) {
        long r;
        if (len + 1 >= *cap) {
            size_t ncap = *cap * 2;
            char *np = (char *)realloc(*lp, ncap);
            if (!np)
                return -1;
            *lp = np;
            *cap = ncap;
        }
        r = libc_sys3(SYS_READ, f->fd, (long)(*lp + len), 1);
        if (r < 0) {
            f->err = 1;
            return -1;
        }
        if (r == 0) {
            f->eof = 1;
            return len ? (ssize_t)len : -1;
        }
        if ((*lp)[len++] == '\n')
            return (ssize_t)len;
    }
}

/* --- bounded formatting ------------------------------------------------ */

struct snk {
    char *dst;
    size_t cap;     /* usable bytes excluding the NUL */
    size_t len;
};

static void sn_emit(struct snk *s, char c)
{
    if (s->len < s->cap)
        s->dst[s->len] = c;
    s->len++;
}

/* --- the one formatting engine -----------------------------------------
 *
 * Conversion flags and the float renderer contract live in fmt.h. */
#include "fmt.h"

/* Integer digits in reverse order; returns how many were written. */
static int digits_rev(unsigned long long v, int base, char *buf)
{
    static const char d[] = "0123456789abcdef";
    int n = 0;
    if (!v)
        buf[n++] = '0';
    while (v) {
        buf[n++] = d[v % (unsigned)base];
        v /= (unsigned)base;
    }
    return n;
}

static char upper_ch(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - ('a' - 'A')) : c;
}

/* Double to text. Lives in libc/src/printf_flo.c, which is the only part of
 * the formatter allowed to use the FPU; conv is the lowercase conversion
 * character, prec < 0 means "use the default". Returns the length written, or
 * -1 if it could not fit in cap. */
int __fcvt(char *out, int cap, int conv, long prec, int flags, double v);

static int vsnprintf_inner(char *dst, size_t n, const char *fmt, va_list ap)
{
    struct snk s = { dst, n ? n - 1 : 0, 0 };
    char tmp[400];   /* one rendered conversion, before width padding */
    char rev[32];    /* integer digits, reversed */
    int i;

    if (!dst && n)
        return -1;

    while (*fmt) {
        long width = 0, prec = -1, pad;
        int flags = 0, lenmod = 0, out = 0, zero_ok = 1, upcase;
        char conv;

        if (*fmt != '%') {
            sn_emit(&s, *fmt++);
            continue;
        }
        fmt++;
        for (;;) {
            if (*fmt == '-')
                flags |= F_MINUS;
            else if (*fmt == '+')
                flags |= F_PLUS;
            else if (*fmt == ' ')
                flags |= F_SPACE;
            else if (*fmt == '#')
                flags |= F_ALT;
            else if (*fmt == '0')
                flags |= F_ZERO;
            else
                break;
            fmt++;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                flags |= F_MINUS;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                if (prec < 0)
                    prec = -1;
                fmt++;
            } else {
                prec = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    prec = prec * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }
        for (;;) {
            char lm = *fmt;
            int next = lenmod;
            if (lm == 'h')
                next = (lenmod == 2) ? 1 : 2;      /* hh then h */
            else if (lm == 'l')
                next = (lenmod == 3) ? 4 : 3;      /* ll then l */
            else if (lm == 'z' || lm == 'j' || lm == 't')
                next = 5;                          /* all 64-bit here */
            else if (lm == 'L')
                next = 8;                          /* long double */
            else
                break;
            lenmod = next;
            fmt++;
        }
        conv = *fmt;
        if (!conv)
            break;      /* format ended right after the modifiers */
        fmt++;
        upcase = (conv == 'X' || conv == 'F' || conv == 'E' ||
                  conv == 'G' || conv == 'A');

        switch (conv) {
        case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
            int base = (conv == 'o') ? 8 :
                       (conv == 'x' || conv == 'X') ? 16 : 10;
            int sgn = (conv == 'd' || conv == 'i');
            unsigned long long uv = 0;
            int neg = 0, dn, t = 0;
            long min_digits;
            if (sgn) {
                long long v;
                if (lenmod == 1)
                    v = (signed char)va_arg(ap, int);
                else if (lenmod == 2)
                    v = (short)va_arg(ap, int);
                else if (lenmod == 0)
                    v = va_arg(ap, int);
                else
                    v = va_arg(ap, long long);
                neg = (v < 0);
                /* -LLONG_MIN overflows; negate in unsigned space instead. */
                uv = neg ? (0ULL - (unsigned long long)v)
                         : (unsigned long long)v;
            } else if (lenmod == 1) {
                uv = (unsigned)va_arg(ap, int) & 0xffu;
            } else if (lenmod == 2) {
                uv = (unsigned)va_arg(ap, int) & 0xffffu;
            } else if (lenmod == 0) {
                uv = va_arg(ap, unsigned int);
            } else {
                uv = va_arg(ap, unsigned long long);
            }
            dn = digits_rev(uv, base, rev);
            min_digits = (prec > dn) ? prec : dn;
            if (prec == 0 && !uv)
                min_digits = 0;          /* "%.0d" of zero prints nothing */
            if (neg)
                tmp[t++] = '-';
            else if (flags & F_PLUS)
                tmp[t++] = '+';
            else if ((flags & F_SPACE) && sgn)
                tmp[t++] = ' ';
            if (min_digits) {
                if ((flags & F_ALT) && base == 16) {
                    tmp[t++] = '0';
                    tmp[t++] = upcase ? 'X' : 'x';
                } else if ((flags & F_ALT) && base == 8 &&
                           (min_digits > 1 || rev[0] != '0')) {
                    tmp[t++] = '0';
                }
                while (min_digits > dn) {
                    tmp[t++] = '0';
                    min_digits--;
                }
                while (dn--)
                    tmp[t++] = rev[dn];
            }
            out = t;
            break;
        }
        case 's': {
            const char *v = va_arg(ap, const char *);
            long len = 0;
            zero_ok = 0;
            if (!v) {
                if (prec != 0) {
                    v = "(null)";
                    len = 7;
                }
            } else {
                while (v[len])
                    len++;
                if (prec >= 0 && len > prec)
                    len = prec;
            }
            pad = width - len;
            if (!(flags & F_MINUS))
                for (i = 0; i < (int)pad; i++)
                    sn_emit(&s, ' ');
            for (i = 0; i < (int)len; i++)
                sn_emit(&s, v[i]);
            if (flags & F_MINUS)
                for (i = 0; i < (int)pad; i++)
                    sn_emit(&s, ' ');
            out = -1;               /* already emitted */
            break;
        }
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            out = 1;
            zero_ok = 0;
            break;
        case 'p': {
            unsigned long v = va_arg(ap, unsigned long);
            int dn2 = digits_rev(v, 16, rev);
            int t2 = 0;
            tmp[t2++] = '0';
            tmp[t2++] = 'x';
            while (dn2--)
                tmp[t2++] = rev[dn2];
            out = t2;
            zero_ok = 0;
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
        case 'a': case 'A': {
            int cv = conv | 32;         /* lowercase for the converter */
            int fl = flags | ((conv & 32) ? 0 : F_UPPER);
            /* The FP argument is fetched by printf_flo.c: reading a double
             * out of a va_list needs FP registers this file may not use. */
            out = __fvafmt(tmp, (int)sizeof(tmp), cv, prec, fl, lenmod, ap);
            if (out < 0)
                out = 0;               /* nothing fitted: emit as empty */
            break;
        }
        case 'n':
            /* Position write-back is not supported (streams are unbuffered
             * and this would be a silent memory write for ports). */
            (void)va_arg(ap, int *);
            out = -1;
            break;
        case '%':
            tmp[0] = '%';
            out = 1;
            zero_ok = 0;
            break;
        default:
            /* Unknown conversion: echo it, consuming no argument, so a port
             * that hits one still sees the offending text. */
            tmp[0] = '%';
            tmp[1] = conv;
            out = 2;
            zero_ok = 0;
            break;
        }

        if (out < 0)
            continue;
        if (upcase)
            for (i = 0; i < out; i++)
                tmp[i] = upper_ch(tmp[i]);
        pad = width - out;
        if (pad > 0 && (flags & F_ZERO) && !(flags & F_MINUS) &&
            prec < 0 && zero_ok) {
            int skip = 0;
            if (tmp[0] == '-' || tmp[0] == '+' || tmp[0] == ' ')
                skip = 1;
            if (out > skip + 1 && tmp[skip] == '0' &&
                (tmp[skip + 1] == 'x' || tmp[skip + 1] == 'X'))
                skip += 2;
            for (i = 0; i < skip; i++)
                sn_emit(&s, tmp[i]);
            for (i = 0; i < (int)pad; i++)
                sn_emit(&s, '0');
            for (i = skip; i < out; i++)
                sn_emit(&s, tmp[i]);
        } else {
            if (!(flags & F_MINUS))
                for (i = 0; i < (int)pad; i++)
                    sn_emit(&s, ' ');
            for (i = 0; i < out; i++)
                sn_emit(&s, tmp[i]);
            if (flags & F_MINUS)
                for (i = 0; i < (int)pad; i++)
                    sn_emit(&s, ' ');
        }
    }
    if (n)
        dst[s.len < s.cap ? s.len : s.cap] = '\0';
    return (int)s.len;
}

int vsnprintf(char *dst, size_t n, const char *fmt, va_list ap)
{
    return vsnprintf_inner(dst, n, fmt, ap);
}

int snprintf(char *dst, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf_inner(dst, n, fmt, ap);
    va_end(ap);
    return r;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    /* Bounded single pass: 4 KiB staging, then one write. Long lines
     * truncate (editors format short status lines; the longest is well
     * under a hundred bytes). */
    static char stage[4096];
    int n;
    size_t w;
    if (!f || !fmt)
        return -1;
    n = vsnprintf_inner(stage, sizeof(stage), fmt, ap);
    if (n < 0)
        return -1;
    w = (size_t)n < sizeof(stage) ? (size_t)n : sizeof(stage) - 1;
    if (w && fwrite(stage, 1, w, f) != w)
        return -1;
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vfprintf(f, fmt, ap);
    va_end(ap);
    return r;
}

int vsprintf(char *dst, const char *fmt, va_list ap)
{
    /* Unbounded by contract: the caller owns a buffer big enough. */
    return vsnprintf_inner(dst, (size_t)-1, fmt, ap);
}

int sprintf(char *dst, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsprintf(dst, fmt, ap);
    va_end(ap);
    return r;
}

/* Streams carry no user-space buffer, so the buffering request is recorded on
 * the FILE and otherwise has no effect: it must not fail, or ports that set
 * line buffering on stdout would treat the console as broken. */
static int stream_bufmode = _IOFBF;

int setvbuf(FILE *f, char *buf, int mode, size_t size)
{
    (void)f;
    (void)buf;
    (void)size;
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) {
        errno = EINVAL;
        return -1;
    }
    stream_bufmode = mode;
    return 0;
}

void setbuf(FILE *f, char *buf)
{
    (void)setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

FILE *freopen(const char *path, const char *mode, FILE *f)
{
    FILE *nf;
    if (!f) {
        errno = EINVAL;
        return NULL;
    }
    /* Reopening a stream by descriptor (path == NULL) needs a kernel that can
     * re-open a live fd; NEWOS opens by name only. */
    if (!path) {
        errno = ENOENT;
        return NULL;
    }
    nf = fopen(path, mode);
    if (!nf)
        return NULL;
    if (f != stdin && f != stdout && f != stderr)
        libc_sys1(SYS_CLOSE, f->fd);
    else
        libc_sys2(SYS_DUP2, nf->fd, f->fd);
    f->fd = nf->fd;
    f->err = 0;
    f->eof = 0;
    f->ungc = -1;
    free(nf);
    return f;
}

/* Temporary names share one counter and one shape, "/tmpNNN", so tmpnam and
 * tmpfile agree on what is already taken. Existence is probed with access()
 * and creation uses O_EXCL, so two programs in one session cannot collide.
 * The counter never rewinds, which bounds a name to one hand-out per session;
 * the VFS is RAM-backed, so a name freed by unlink() is not re-issued. */
#define TMP_SLOTS 1000

static unsigned tmp_next;

static int tmp_slot_name(char *buf, unsigned slot)
{
    if (slot >= TMP_SLOTS)
        return -1;
    buf[0] = '/';
    buf[1] = 't';
    buf[2] = 'm';
    buf[3] = 'p';
    buf[4] = (char)('0' + (slot / 100) % 10);
    buf[5] = (char)('0' + (slot / 10) % 10);
    buf[6] = (char)('0' + slot % 10);
    buf[7] = '\0';
    return 0;
}

/* Next unused slot name, or -1 when the space is exhausted. */
static int tmp_free_name(char *buf)
{
    while (tmp_next < TMP_SLOTS) {
        unsigned slot = tmp_next++;
        if (tmp_slot_name(buf, slot) != 0)
            return -1;
        if (libc_sys2(SYS_ACCESS, (long)buf, 0) != 0)
            return 0;
    }
    return -1;
}

char *tmpnam(char *s)
{
    static char reserved[L_tmpnam];
    char *dst = s ? s : reserved;

    if (tmp_free_name(dst) != 0) {
        errno = EEXIST;
        return NULL;
    }
    return dst;
}

FILE *tmpfile(void)
{
    char name[L_tmpnam];
    FILE *f;

    while (tmp_free_name(name) == 0) {
        int fd = open(name, O_RDWR | O_CREAT | O_EXCL);
        if (fd >= 0) {
            f = (FILE *)malloc(sizeof(FILE));
            if (!f) {
                libc_sys1(SYS_CLOSE, fd);
                errno = ENOMEM;
                return NULL;
            }
            f->fd = fd;
            f->err = 0;
            f->eof = 0;
            f->ungc = -1;
            return f;
        }
        /* Someone won the race for this slot: try the next one. */
    }
    errno = ENOSPC;
    return NULL;
}

int rename(const char *from, const char *to)
{
    long r = libc_sys2(SYS_RENAME, (long)from, (long)to);
    return r < 0 ? -1 : 0;
}

int remove(const char *path)
{
    /* Directories are not unlinked here: the kernel has no SYS_RMDIR handler,
     * so asking for one would only log an unknown syscall. */
    long r = libc_sys1(SYS_UNLINK, (long)path);
    return r < 0 ? -1 : 0;
}
