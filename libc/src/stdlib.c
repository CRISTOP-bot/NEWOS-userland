/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/stdlib.c
 * Description: xorshift32: no division, no FPU, decent spread for games/demos.
 * Added: 2026-10-17
 */
#include "../include/stdlib.h"
#include "../include/string.h"
#include "../include/errno.h"
#include "../../abi/syscall_abi.h"
#include "syscall.h"

long libc_write(int fd, const void *buf, unsigned long len)
{
    return libc_sys3(SYS_WRITE, fd, (long)buf, (long)len);
}

void exit(int code)
{
    extern atexit_fn atexit_table[];
    extern int atexit_count;
    while (atexit_count > 0)
        atexit_table[--atexit_count]();
    libc_sys1(SYS_EXIT, code);
    for (;;)
        ;
}

#define ATEXIT_MAX 8
atexit_fn atexit_table[ATEXIT_MAX];
int atexit_count;

int atexit(atexit_fn fn)
{
    if (!fn || atexit_count >= ATEXIT_MAX)
        return -1;
    atexit_table[atexit_count++] = fn;
    return 0;
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

long labs(long v)
{
    return v < 0 ? -v : v;
}

static unsigned long rand_next = 1;

void srand(unsigned seed)
{
    rand_next = seed ? seed : 1;
}

int rand(void)
{
    /* xorshift32: no division, no FPU, decent spread for games/demos. */
    unsigned long x = rand_next;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rand_next = x;
    return (int)((x >> 8) & RAND_MAX);
}

/* --- integer conversions ------------------------------------------------ */

/* Shared scanner for the integer family. Accumulates in unsigned long long
 * and reports overflow; base 0 means auto-detect (0x/0b/0 prefixes). */
/* GCC predefines __LONG_MAX__ only; there is no MIN or LLONG variant.
 * This target is LP64, so long and long long have the same width and one
 * pair of bounds covers both. */
#define LIBC_LONG_MAX  __LONG_MAX__
#define LIBC_LONG_MIN  (-LIBC_LONG_MAX - 1)
#define LIBC_LLONG_MAX LIBC_LONG_MAX
#define LIBC_LLONG_MIN LIBC_LONG_MIN

static unsigned long long scan_uint(const char *s, char **end, int base,
                                   int *neg, int *ovf)
{
    unsigned long long v = 0;
    int c, digit, saw = 0;
    *neg = 0;
    *ovf = 0;
    while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
        s++;
    if (*s == '+')
        s++;
    else if (*s == '-') {
        *neg = 1;
        s++;
    }
    if (base == 0) {
        base = 10;
        if (s[0] == '0') {
            if (s[1] == 'x' || s[1] == 'X') {
                base = 16;
                s += 2;
            } else if (s[1] == 'b' || s[1] == 'B') {
                base = 2;
                s += 2;
            } else
                base = 8;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    for (;;) {
        c = (unsigned char)*s;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'z')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z')
            digit = c - 'A' + 10;
        else
            break;
        if (digit >= base)
            break;
        if (v > (~0ULL / (unsigned long long)base) ||
            v * (unsigned long long)base > ~0ULL - (unsigned long long)digit)
            *ovf = 1;
        v = v * (unsigned long long)base + (unsigned long long)digit;
        saw = 1;
        s++;
    }
    if (end)
        *end = saw ? (char *)s : (char *)0;
    return v;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    int neg, ovf;
    unsigned long long v;
    if (!s) {
        if (end)
            *end = (char *)s;
        return 0;
    }
    v = scan_uint(s, end, base, &neg, &ovf);
    if (ovf || (neg && v > 1) || (!neg && v > ~0UL)) {
        errno = ERANGE;
        return neg ? 0 : ~0UL;
    }
    if (neg)
        return 1UL - (unsigned long)v;
    return (unsigned long)v;
}

long strtol(const char *s, char **end, int base)
{
    int neg, ovf;
    unsigned long long v;
    if (!s) {
        if (end)
            *end = (char *)s;
        return 0;
    }
    v = scan_uint(s, end, base, &neg, &ovf);
    if (!neg && v > (unsigned long long)LIBC_LONG_MAX) {
        errno = ERANGE;
        return LIBC_LONG_MAX;
    }
    if (neg && v > (unsigned long long)(-(LIBC_LONG_MIN + 1)) + 1ULL) {
        errno = ERANGE;
        return LIBC_LONG_MIN;
    }
    if (ovf) {
        errno = ERANGE;
        return neg ? LIBC_LONG_MIN : LIBC_LONG_MAX;
    }
    return neg ? -(long)v : (long)v;
}

unsigned long long strtoull(const char *s, char **end, int base)
{
    int neg, ovf;
    unsigned long long v;
    if (!s) {
        if (end)
            *end = (char *)s;
        return 0;
    }
    v = scan_uint(s, end, base, &neg, &ovf);
    if (ovf || (neg && v > 1)) {
        errno = ERANGE;
        return neg ? 0ULL : ~0ULL;
    }
    if (neg)
        return 1ULL - v;
    return v;
}

long long strtoll(const char *s, char **end, int base)
{
    int neg, ovf;
    unsigned long long v;
    if (!s) {
        if (end)
            *end = (char *)s;
        return 0;
    }
    v = scan_uint(s, end, base, &neg, &ovf);
    if (ovf || (!neg && v > (unsigned long long)LIBC_LLONG_MAX) ||
        (neg && v > (unsigned long long)(-(LIBC_LLONG_MIN + 1)) + 1ULL)) {
        errno = ERANGE;
        return neg ? LIBC_LLONG_MIN : LIBC_LLONG_MAX;
    }
    return neg ? -(long long)v : (long long)v;
}

div_t div(int a, int b)
{
    div_t r;
    r.quot = (b == 0) ? 0 : a / b;
    r.rem = (b == 0) ? 0 : a % b;
    return r;
}

ldiv_t ldiv(long a, long b)
{
    ldiv_t r;
    r.quot = (b == 0) ? 0 : a / b;
    r.rem = (b == 0) ? 0 : a % b;
    return r;
}

/* --- environment -------------------------------------------------------- */

#define ENV_MAX 16
static struct {
    const char *name;
    char *value;
} env_table[ENV_MAX];

char *getenv(const char *name)
{
    int i;
    if (!name)
        return NULL;
    for (i = 0; i < ENV_MAX; i++)
        if (env_table[i].name && strcmp(env_table[i].name, name) == 0)
            return env_table[i].value;
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite)
{
    int i, free_slot = -1;
    if (!name || !*name || !value) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < ENV_MAX; i++) {
        if (env_table[i].name && strcmp(env_table[i].name, name) == 0) {
            char *copy;
            if (!overwrite)
                return 0;
            copy = strdup(value);
            if (!copy) {
                errno = ENOMEM;
                return -1;
            }
            free(env_table[i].value);
            env_table[i].value = copy;
            return 0;
        }
        if (!env_table[i].name && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0) {
        errno = ENOMEM;
        return -1;
    }
    env_table[free_slot].value = strdup(value);
    if (!env_table[free_slot].value) {
        errno = ENOMEM;
        return -1;
    }
    env_table[free_slot].name = env_table[free_slot].value;
    /* keep the name address stable: store a separate copy of the name */
    env_table[free_slot].name = name;   /* caller-owned, like POSIX expects */
    return 0;
}

int unsetenv(const char *name)
{
    int i;
    if (!name || !*name) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0; i < ENV_MAX; i++) {
        if (env_table[i].name && strcmp(env_table[i].name, name) == 0) {
            free(env_table[i].value);
            env_table[i].name = NULL;
            env_table[i].value = NULL;
        }
    }
    return 0;
}

/* --- process termination / non-existent services ------------------------ */

void abort(void)
{
    libc_write(2, "abort\n", 6);
    libc_sys1(SYS_EXIT, 134);
    for (;;)
        ;
}

int system(const char *cmd)
{
    (void)cmd;
    /* NEWOS has no fork() and no execve(): the call cannot be honoured. */
    errno = ENOSYS;
    return -1;
}
