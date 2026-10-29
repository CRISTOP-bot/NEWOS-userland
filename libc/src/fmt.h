/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/fmt.h
 * Description: Shared internals of the printf engine: conversion flags and the
 * Added: 2026-10-27
 */
#ifndef NEWOS_LIBC_SRC_FMT_H
#define NEWOS_LIBC_SRC_FMT_H

#include <stdarg.h>

/* Shared internals of the printf engine: conversion flags and the float
 * renderer. Not installed; stdio.c and printf_flo.c must agree on these. */

#define F_MINUS 1  /* left adjust */
#define F_PLUS  2  /* force a sign on numbers */
#define F_SPACE 4  /* blank before a non-negative number */
#define F_ALT   8  /* 0x/0 prefixes, keep trailing decimal point */
#define F_ZERO  16 /* zero pad instead of space pad */
#define F_UPPER 32 /* %E/%G/%A/%F: uppercase exponent, hex digits, INF/NAN */

/* Double to text, lowercase. conv is one of 'e' 'f' 'g' 'a'; prec < 0 means
 * "use the conversion default". Returns the number of characters written
 * (NUL included is NOT counted) or -1 when cap is too small. */
int __fcvt(char *out, int cap, int conv, long prec, int flags, double v);

/* Fetch the floating argument and render it. This lives in the FP object on
 * purpose: pulling a `double`/`long double` out of a va_list needs FP return
 * registers, and stdio.c is compiled integer-only. lenmod is the printf
 * length modifier (8 == 'L'). Same return contract as __fcvt.
 *
 * `ap` is passed with the ordinary va_list spelling: here (and in every NEWOS
 * target) va_list is an array type, so the parameter decays to a pointer to
 * the caller's own object and the conversion advances the caller's list. */
int __fvafmt(char *out, int cap, int conv, long prec, int flags, int lenmod,
             va_list ap);

#endif
