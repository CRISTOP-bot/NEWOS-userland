/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/string.c
 * Description: NEWOS libc module
 * Added: 2026-10-17
 */
#include "../include/string.h"
#include "../include/stdlib.h"

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    while (n--)
        *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    if (dd < ss) {
        while (n--)
            *dd++ = *ss++;
    } else if (dd > ss) {
        dd += n;
        ss += n;
        while (n--)
            *--dd = *--ss;
    }
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = (unsigned char *)d;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (n--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    while (n--) {
        if (*p == (unsigned char)c)
            return (void *)(unsigned long)p;
        p++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    char *r = d;
    while (n--) {
        if ((*d++ = *s++) == 0) {
            while (n--)
                *d++ = 0;
            break;
        }
    }
    return r;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strncat(char *d, const char *s, size_t n)
{
    size_t off = strlen(d);
    size_t i = 0;
    while (i < n && s[i]) {
        d[off + i] = s[i];
        i++;
    }
    d[off + i] = 0;
    return d;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n--) {
        if (*a != *b || *a == 0)
            return (int)(unsigned char)*a - (int)(unsigned char)*b;
        a++;
        b++;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c)
            return (char *)(unsigned long)s;
        s++;
    }
    return c == 0 ? (char *)(unsigned long)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c)
            last = s;
        s++;
    }
    if (c == 0)
        return (char *)(unsigned long)s;
    return (char *)(unsigned long)last;
}

int atoi(const char *s)
{
    return (int)atol(s);
}

long atol(const char *s)
{
    int neg = 0;
    long v = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

char *strstr(const char *h, const char *n)
{
    size_t nl;
    if (!h || !n)
        return NULL;
    nl = strlen(n);
    if (!nl)
        return (char *)(unsigned long)h;
    while (*h) {
        if (*h == *n && memcmp(h, n, nl) == 0)
            return (char *)(unsigned long)h;
        h++;
    }
    return NULL;
}

char *strdup(const char *s)
{
    size_t n;
    char *d;
    if (!s)
        return NULL;
    n = strlen(s) + 1;
    d = (char *)malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

char *strndup(const char *s, size_t max)
{
    size_t n = strnlen(s, max);
    char *d = (char *)malloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = '\0';
    }
    return d;
}

char *strpbrk(const char *s, const char *set)
{
    if (!s || !set)
        return NULL;
    while (*s) {
        const char *p = set;
        while (*p) {
            if (*s == *p)
                return (char *)(unsigned long)s;
            p++;
        }
        s++;
    }
    return NULL;
}

size_t strspn(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n]) {
        const char *p = set;
        int hit = 0;
        while (*p) {
            if (s[n] == *p) {
                hit = 1;
                break;
            }
            p++;
        }
        if (!hit)
            break;
        n++;
    }
    return n;
}

size_t strcspn(const char *s, const char *set)
{
    size_t n = 0;
    while (s[n]) {
        const char *p = set;
        while (*p && *p != s[n])
            p++;
        if (*p)
            break;
        n++;
    }
    return n;
}

/* Single-token strtok: the POSIX state lives in one static pointer, which is
 * all a single-threaded process needs. */
static char *strtok_last;

char *strtok(char *s, const char *set)
{
    char *start = s ? s : strtok_last;
    char *end;
    if (!start)
        return NULL;
    start += strspn(start, set);
    if (!*start) {
        strtok_last = NULL;
        return NULL;
    }
    end = start + strcspn(start, set);
    strtok_last = *end ? end + 1 : NULL;
    *end = '\0';
    return start;
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + ('a' - 'A') : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + ('a' - 'A') : *b;
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* The C locale has no collation table: comparison is by byte value. */
int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}
