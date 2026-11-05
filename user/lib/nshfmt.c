/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/lib/nshfmt.c
 * Description: nshlib output and printf-style formatter.
 * Added: 2026-10-30
 */
/* nshlib output layer: character/string writers and the tiny printf engine.
 *
 * Split out of nshlib.c so the syscall/string core and the formatter stay
 * under the 500-line house rule. Everything here is public nshlib API
 * (declared in nshlib.h) except emit_padded/vemits, which are private to
 * this translation unit.
 */

#include "nshlib.h"

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
