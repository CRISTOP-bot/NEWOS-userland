/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/stdlib.h
 * Description: NEWOS libc: numbers, pseudo-random, heap (libc/src/stdlib.c,
 * Added: 2026-10-17
 */
#ifndef NEWOS_LIBC_STDLIB_H
#define NEWOS_LIBC_STDLIB_H

/* NEWOS libc: numbers, pseudo-random, heap (libc/src/stdlib.c,
 * libc/src/malloc.c). Integer only. */

#include "stddef.h"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

int abs(int v);
long labs(long v);
int atoi(const char *s);
long atol(const char *s);

/* Float parsing (libc/src/strtod.c, compiled with the FP flags like
 * libc/src/math.c; exact for round-trips of "%.14g", within 1 ulp). */
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
double atof(const char *s);

/* Bases 2..36 for the integer family; strtod/strtof/atof live in
 * libc/src/strtod.c, which is the FP-allowed part of libc. */
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
double atof(const char *s);

typedef struct { long quot; long rem; } div_t;
typedef struct { long long quot; long long rem; } ldiv_t;
div_t div(int a, int b);
ldiv_t ldiv(long a, long b);

void srand(unsigned seed);
int rand(void);
#define RAND_MAX 32767

void *malloc(size_t n);
void free(void *p);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);

/* One small internal table: NEWOS gives a program no environment block, so
 * the table starts empty and setenv() is the only way to populate it. */
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

typedef void (*atexit_fn)(void);
int atexit(atexit_fn fn);

void exit(int code);
void abort(void) __attribute__((__noreturn__));

/* No process creation exists yet, so os.execute() and system() fail honestly
 * instead of pretending to run a command. */
int system(const char *cmd);

#endif
