/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/math.h
 * Description: Small freestanding math surface used by TinyGL/glxgears
 *              and the native Lua interpreter.
 * Added: 2026-10-22
 */
#ifndef NEWOS_LIBC_MATH_H
#define NEWOS_LIBC_MATH_H

/* Small freestanding math surface used by TinyGL/glxgears and Lua.
 *
 * NEWOS has no libm in the base image.  These functions are implemented in
 * libc/src/math.c without pulling in a host libm or newlib, so a host-built
 * static user program stays self-contained.  math.c is one of the FP-flag
 * files (compiled without -mgeneral-regs-only), like the glxgears libm path.
 * Domain errors (log of a negative, asin outside [-1,1], ...) follow the
 * file's historic convention and return -HUGE_VAL rather than NaN, with
 * sign chosen so the failure is visible; Lua only requires no crash. */

double fabs(double x);
float  fabsf(float x);
double floor(double x);
float  floorf(float x);
double ceil(double x);
float  ceilf(float x);
double trunc(double x);
float  truncf(float x);
double round(double x);              /* halves away from zero */
float  roundf(float x);
double sqrt(double x);
float  sqrtf(float x);
double sin(double x);
float  sinf(float x);
double cos(double x);
float  cosf(float x);
double tan(double x);
float  tanf(float x);
double asin(double x);
double acos(double x);
double atan(double x);
float  atanf(float x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);
double exp(double x);
float  expf(float x);
double log(double x);                /* natural log */
float  logf(float x);
double log2(double x);
double log10(double x);
double pow(double x, double y);
float  powf(float x, float y);
double fmod(double x, double y);     /* y == 0 returns x (Lua tolerance) */
float  fmodf(float x, float y);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double modf(double x, double *iptr);
double copysign(double x, double y);
float  copysignf(float x, float y);

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif
#ifndef M_PI_2
#define M_PI_2 1.5707963267948966192313216916398
#endif
#ifndef HUGE_VAL
#define HUGE_VAL (__builtin_huge_val())
#endif

#define isfinite(x) __builtin_isfinite(x)
#define isnan(x)    __builtin_isnan(x)
#define isinf(x)    __builtin_isinf(x)

#endif /* NEWOS_LIBC_MATH_H */
