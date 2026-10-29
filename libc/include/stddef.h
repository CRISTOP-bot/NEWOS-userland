/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/stddef.h
 * Description: Shared base types for the NEWOS libc (freestanding, no host headers).
 * Added: 2026-10-17
 */
#ifndef NEWOS_LIBC_STDDEF_H
#define NEWOS_LIBC_STDDEF_H

/* Shared base types for the NEWOS libc (freestanding, no host headers). */

typedef unsigned long size_t;
typedef long ssize_t;
typedef long ptrdiff_t;
typedef int sig_atomic_t;

#ifndef _WINT_T
#define _WINT_T
typedef __WINT_TYPE__ wint_t;
#endif

#define NULL ((void *)0)
#define EOF (-1)

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif
