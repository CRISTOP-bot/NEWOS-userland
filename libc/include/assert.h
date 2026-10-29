/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/assert.h
 * Description: Assertion macro. Active unless NDEBUG; the failure path reports
 * Added: 2026-10-27
 */
#ifndef NEWOS_LIBC_ASSERT_H
#define NEWOS_LIBC_ASSERT_H

/* Assertion macro. Active unless NDEBUG; the failure path reports on stderr and
 * aborts, so it never silently returns. */

#include "stdio.h"
#include "stdlib.h"

#ifdef assert
#undef assert
#endif

#define __assert_fail(expr, file, line) \
    do { \
        fprintf(stderr, "%s:%d: assertion failed: %s\n", \
                (file), (line), (expr)); \
        abort(); \
    } while (0)

#ifdef NDEBUG
# define assert(expr) ((void)0)
#else
# define assert(expr) ((expr) ? (void)0 : __assert_fail(#expr, __FILE__, __LINE__))
#endif

#endif
