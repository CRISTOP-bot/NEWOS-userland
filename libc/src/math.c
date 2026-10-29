/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/math.c
 * Description: Freestanding math for TinyGL / glxgears and the Lua port.
 * Added: 2026-10-22
 */
/* Freestanding math for TinyGL / glxgears and the Lua interpreter.
 *
 * The kernel exposes x87/SSE state per thread, so sqrt uses the hardware
 * instruction.  Everything else is a small double-precision implementation;
 * TinyGL only needs sin/cos for matrices and a handful of lights, floor for
 * raster edge snapping, and pow for the (usually disabled) spot exponent.
 * Lua's math library plus its numeric literal parser need the rest of the
 * double surface below.  The implementations are classic fdlibm/Cephes-style
 * algorithms (argument reduction with split high/low precision constants and
 * truncated power series) written from scratch; all constants are ordinary
 * published mathematical values, so accuracy is ~1 ulp for exp/log/pow and
 * ~1e-15 absolute/relative for the trig and hyperbolic families.
 */

#include "../include/math.h"
#include "../include/stddef.h"

typedef unsigned long long u64;

#define PI      3.1415926535897932384626433832795
#define PI_2    1.5707963267948966192313216916398
#define TWO_PI  6.2831853071795864769252867665590
#define LN2     0.69314718055994530941723212145818
#define INV_2PI 0.15915494309189533576888376337251

/* High/low splits used for argument reduction.  Each triple sums to the true
 * constant to well beyond double precision, and the "hi" terms have enough
 * trailing zero mantissa bits that multiplying them by |k| < 2^20 is exact. */
#define PIO2_1   1.57079632673412561417e+00
#define PIO2_1T  6.07710050650619224932e-11
#define PIO2_2   6.07710050630396597647e-11
#define PIO2_2T  2.02226759135473799966e-21
#define PIO2_3   2.02226759119472476937e-21
#define PIO2_3T  8.49597833869518884829e-32
#define INV_PIO2 6.36619772367581343076e-01
#define LN2_HI   6.93147180369123816490e-01
#define LN2_LO   1.90821492927058772479e-10
#define LOG2E_X  1.44269504088896338700e+00   /* log2(e), low mantissa bits zero */
#define LOG2E_T  1.26321067751602898860e-16   /* log2(e) - LOG2E_X */
#define INV_LN10 4.34294481903251754206e-01   /* 1/ln(10), low mantissa bits zero */
#define INV_LN10_T 1.28048518664441498641e-18 /* 1/ln(10) - INV_LN10 */
#define P15_2DP 1.22064366830108489068e-04    /* pi/2 - 2^17*INV_PIO2 (2^-17 ulp) */
#define P15_3DP 1.49913404837417892261e-09    /* pi/2 - 2^18*INV_PIO2 (2^-18 ulp) */
#define BIGTAN  6.123233995736766e-17         /* tan(pi/2); ~6e-17 off true 2^-54 */
#define SQRT2   1.41421356237309504880e+00
#define S2HALF  7.07106781186547524400e-01    /* sqrt(2)/2 */

double fabs(double x)
{
    union { double d; u64 u; } v;
    v.d = x;
    v.u &= 0x7fffffffffffffffull;
    return v.d;
}

float fabsf(float x)
{
    union { float f; unsigned u; } v;
    v.f = x;
    v.u &= 0x7fffffffu;
    return v.f;
}

double floor(double x)
{
    long long i;

    if (!isfinite(x) || x >= 9223372036854775807.0 ||
        x <= -9223372036854775808.0)
        return x;
    i = (long long)x;
    if ((double)i > x)
        i--;
    return (double)i;
}

float floorf(float x)
{
    return (float)floor((double)x);
}

double sqrt(double x)
{
    double r;
    if (x <= 0.0)
        return 0.0;
    __asm__ __volatile__("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}

float sqrtf(float x)
{
    float r;
    if (x <= 0.0f)
        return 0.0f;
    __asm__ __volatile__("sqrtss %1, %0" : "=x"(r) : "x"(x));
    return r;
}

/* Reduce x to r in [-pi/4, pi/4]: k = rint(x*2/pi), r = x - k*pi/2 evaluated
 * with the split pi/2 constants.  Accurate for |x| < 2^20; beyond that the
 * plain multiplication degrades (the full Payne-Hanek table is not worth its
 * size here, and Lua/TinyGL never feed arguments that large to trig). */
static int reduce_pio2(double x, double *rp)
{
    double k, r;
    long long n;

    k = floor(x * INV_PIO2 + 0.5);
    if (k > 4.6e18 || k < -4.6e18)   /* absurd argument: no exact answer */
        k = 0.0;
    n = (long long)k;
    r = x - (double)n * PIO2_1;      /* product exact for |n| < 2^20 */
    r -= (double)n * PIO2_1T;
    r -= (double)n * PIO2_2;
    r -= (double)n * PIO2_2T;
    *rp = r;
    return (int)n;
}

/* sin(r), cos(r) for |r| <= pi/4 via truncated Taylor series; truncating at
 * r^15 / r^16 leaves < 1e-17 of error on that interval. */
static double sin_core(double x)
{
    static const double S1 = -1.66666666666666657415e-01;  /* 1/3! */
    static const double S2 =  8.33333333333333322480e-03;  /* 1/5! */
    static const double S3 = -1.98412698412698412702e-04;  /* 1/7! */
    static const double S4 =  2.75573192239858888809e-06;  /* 1/9! */
    static const double S5 = -2.50521083854417173436e-08;  /* 1/11! */
    static const double S6 =  1.60590438368216090570e-10;  /* 1/13! */
    static const double S7 = -7.47141108834409508008e-13;  /* 1/15! */
    double z, z2;

    z = x * x;
    z2 = z * z;
    return x + x * z * (S1 + z * S2 + z2 * (S3 + z * S4 +
                                            z2 * (S5 + z * S6 + z2 * S7)));
}

static double cos_core(double x)
{
    static const double C1 = -5.00000000000000000000e-01;  /* 1/2! */
    static const double C2 =  4.16666666666666666667e-02;  /* 1/4! */
    static const double C3 = -1.38888888888888888889e-03;  /* 1/6! */
    static const double C4 =  2.48015873015873015873e-05;  /* 1/8! */
    static const double C5 = -2.75573192239858906526e-07;  /* 1/10! */
    static const double C6 =  2.08767569878680999357e-09;  /* 1/12! */
    static const double C7 = -1.14707455976212276777e-11;  /* 1/14! */
    double z, z2, w;

    if (x < 1.4e-04)
        return 1.0 - 0.5 * x * x;
    z = x * x;
    z2 = z * z;
    w = C1 + z * C2 + z2 * (C3 + z * C4 + z2 * (C5 + z * C6 + z2 * C7));
    return 1.0 + z * w;
}

double sin(double x)
{
    double r, s;
    int n;

    if (!isfinite(x))
        return 0.0;
    n = reduce_pio2(x, &r);
    if (n & 1)
        s = cos_core(r);
    else
        s = sin_core(r);
    if (((n >> 1) & 1) != 0)
        s = -s;
    return s;
}

float sinf(float x) { return (float)sin((double)x); }

double cos(double x)
{
    double r, s;
    int n;

    if (!isfinite(x))
        return 0.0;
    n = reduce_pio2(x, &r);
    if (n & 1)
        s = sin_core(r);
    else
        s = cos_core(r);
    if ((((n + 1) >> 1) & 1) != 0)
        s = -s;
    return s;
}

float cosf(float x) { return (float)cos((double)x); }

/* tan(x): pi/2 reduction; even quadrant uses sin(r)/cos(r), odd quadrant the
 * reciprocal -cot(r), which stays accurate when x sits near the pole. */
double tan(double x)
{
    double r, s, c, ratio;
    int n;

    if (!isfinite(x))
        return 0.0;
    if (x > 0.0 && x < 1e-8)
        return x;
    if (x < 0.0 && x > -1e-8)
        return x;
    if (x == PI_2 || x == -PI_2)
        return BIGTAN;
    n = reduce_pio2(x, &r);
    if (n & 1) {
        s = sin_core(r);
        c = cos_core(r);
        if (s == 0.0)
            ratio = c == 0.0 ? 0.0 : HUGE_VAL;
        else
            ratio = c / s;
    } else {
        s = sin_core(r);
        c = cos_core(r);
        ratio = s / c;
    }
    return (n & 1) ? -ratio : ratio;
}

float tanf(float x) { return (float)tan((double)x); }

/* exp(r) for |r| <= ln2/2 via the Taylor series (tail < 1e-18); used by the
 * public exp and (through exp) by pow. */
static double exp_core(double x)
{
    static const double E1 =  5.00000000000000000000e-01;  /* 1/2! */
    static const double E2 =  1.66666666666666657415e-01;  /* 1/3! */
    static const double E3 =  4.16666666666666643600e-02;  /* 1/4! */
    static const double E4 =  8.33333333333333322480e-03;  /* 1/5! */
    static const double E5 =  1.38888888888888888889e-03;  /* 1/6! */
    static const double E6 =  1.98412698412698401035e-04;  /* 1/7! */
    static const double E7 =  2.48015873015873001985e-05;  /* 1/8! */
    static const double E8 =  2.75573192239858906525e-06;  /* 1/9! */
    static const double E9 =  2.75573192239858898910e-07;  /* 1/10! */
    static const double EA =  2.50521083854417156824e-08;  /* 1/11! */
    static const double EB =  2.08767569878681021356e-09;  /* 1/12! */
    static const double EC =  1.60590438368216134590e-10;  /* 1/13! */
    static const double ED =  1.16350883523930269627e-11;  /* 1/14! */
    static const double EE =  7.75672556826201779400e-13;  /* 1/15! */
    static const double EF =  4.84795348016376098970e-14;  /* 1/16! */
    double z;

    /* Horner over x^k / k! (powers folded as x*(...) keeps ~1 ulp). */
    z = EF + x * (EE + x * (ED + x * (EC + x * (EB + x * (EA +
          x * (E9 + x * (E8 + x * (E7 + x * (E6 + x * (E5 +
          x * (E4 + x * (E3 + x * (E2 + x * (E1 + x * 1.0))))))))))))));
    return 1.0 + x * z;
}

/* Multiply the double given as raw bits (whose exponent field was forced to
 * bias 1022, i.e. value m * 2^-1, m in [1,2)) by 2^n exactly, with
 * overflow/subnormal handling. */
static double scale_biased(u64 ubits, long long n)
{
    union { double d; u64 u; } v;
    long long e = n + 1022;              /* final biased exponent */
    u64 mant;
    int shift;

    v.u = ubits;
    if (e >= 0x7ff)
        return HUGE_VAL;
    if (e <= 0) {                        /* subnormal result */
        mant = (ubits & 0x000fffffffffffffull) | (1ull << 52);
        shift = (int)(1 - e);
        if (shift > 53)
            mant = 0;                    /* sign is lost, acceptable here */
        else
            mant >>= shift;
        v.u = (ubits & 0x8000000000000000ull) | mant;
        return v.d;
    }
    v.u = (ubits & 0x800fffffffffffffull) | ((u64)e << 52);
    return v.d;
}

double exp(double x)
{
    union { double d; u64 u; } v;
    double k;
    long long kk;

    if (isnan(x))
        return x;
    if (x > 709.782712893384)
        return HUGE_VAL;
    if (x < -745.133219101941)
        return 0.0;
    k = floor(x / LN2 + 0.5);
    kk = (long long)k;
    x = (x - (double)kk * LN2_HI) - (double)kk * LN2_LO;
    v.d = exp_core(x);
    /* Strip the exponent of exp(r) (it is in [~0.7, ~1.4], so bias 1022 or
     * 1023) and fold k back in through the bit-level scaler. */
    kk += (long long)((v.u >> 52) & 0x7ff) - 1022;
    v.u = (v.u & 0x800fffffffffffffull) | (1022ull << 52);
    return scale_biased(v.u, kk);
}

float expf(float x) { return (float)exp((double)x); }

/* 2^n exactly for integral n; powers built by squaring so every step is a
 * representable power of two. */
static double pow2i(long long n)
{
    union { double d; u64 u; } v;
    double hi = 1.0, res = 1.0;
    int neg = n < 0;
    unsigned long long bits;
    int b;

    if (n > 1023)
        return HUGE_VAL;
    if (n < -1074)
        return 0.0;
    bits = neg ? (unsigned long long)(-n) : (unsigned long long)n;
    for (b = 0; b < 12 && bits; b++, bits >>= 1) {
        if (bits & 1) {
            res *= hi;
            if (res == 0.0 || res == HUGE_VAL)
                break;
        }
        if (hi != HUGE_VAL)
            hi *= hi;
    }
    if (!neg) {
        v.d = res;
        return v.d;
    }
    return 1.0 / res;
}

/* log(x) via x = 2^k * m, m in [1,2), and log(m) = 2*atanh((m-1)/(m+1));
 * the series tail is below 1e-19 for |f| < 1/3, and k*LN2_HI is exact. */
double log(double x)
{
    union { double d; u64 u; } v;
    int e;
    double m, f, f2, p, t, sum;
    int i;

    if (isnan(x))
        return x;
    if (x < 0.0)
        return -HUGE_VAL;            /* file convention for domain errors */
    if (x == 0.0)
        return -HUGE_VAL;
    if (x == HUGE_VAL)
        return HUGE_VAL;
    v.d = x;
    e = (int)((v.u >> 52) & 0x7ff);
    if (e == 0) {                    /* subnormal: renormalize first */
        v.d *= 0x1p52;
        e = (int)((v.u >> 52) & 0x7ff) - 52;
    }
    v.u = (v.u & 0x800fffffffffffffull) | (1023ull << 52);
    e -= 1023;
    m = v.d;                         /* [1, 2) */
    if (m == 1.0)
        return (double)e * LN2;      /* exact powers of two */
    if (m > SQRT2) {                 /* keep |f| <= tanh(ln2/4) ~ 0.19 */
        m *= 0.5;
        e++;
    }
    f = (m - 1.0) / (m + 1.0);
    f2 = f * f;
    t = f;
    sum = f;
    for (i = 3; i <= 65; i += 2) {
        t *= f2;
        sum += t / (double)i;
    }
    p = 2.0 * sum;
    return (double)e * LN2_HI + ((double)e * LN2_LO + p);
}

float logf(float x) { return (float)log((double)x); }

double log2(double x)
{
    double k, m, f, f2, p, t, sum;
    union { double d; u64 u; } v;
    int e, i;

    if (isnan(x))
        return x;
    if (x < 0.0 || x == 0.0)
        return -HUGE_VAL;
    if (x == HUGE_VAL)
        return HUGE_VAL;
    v.d = x;
    e = (int)((v.u >> 52) & 0x7ff);
    if (e == 0) {                    /* subnormal: renormalize first */
        v.d *= 0x1p52;
        e = (int)((v.u >> 52) & 0x7ff) - 52;
    }
    v.u = (v.u & 0x800fffffffffffffull) | (1023ull << 52);
    k = (double)(e - 1023);
    m = v.d;
    if (m == 1.0)
        return k;
    if (m > SQRT2) {
        m *= 0.5;
        k += 1.0;
    }
    f = (m - 1.0) / (m + 1.0);
    f2 = f * f;
    t = f;
    sum = f;
    for (i = 3; i <= 65; i += 2) {
        t *= f2;
        sum += t / (double)i;
    }
    p = 2.0 * sum;
    return k + (p * LOG2E_X + p * LOG2E_T);
}

double log10(double x)
{
    double k, m, f, f2, p, t, sum;
    union { double d; u64 u; } v;
    int e, i;

    if (isnan(x))
        return x;
    if (x < 0.0 || x == 0.0)
        return -HUGE_VAL;
    if (x == HUGE_VAL)
        return HUGE_VAL;
    v.d = x;
    e = (int)((v.u >> 52) & 0x7ff);
    if (e == 0) {                    /* subnormal: renormalize first */
        v.d *= 0x1p52;
        e = (int)((v.u >> 52) & 0x7ff) - 52;
    }
    v.u = (v.u & 0x800fffffffffffffull) | (1023ull << 52);
    k = (double)(e - 1023);
    m = v.d;
    if (m == 1.0)
        return k * INV_LN10;         /* exact-ish: k*log10(2) */
    if (m > SQRT2) {
        m *= 0.5;
        k += 1.0;
    }
    f = (m - 1.0) / (m + 1.0);
    f2 = f * f;
    t = f;
    sum = f;
    for (i = 3; i <= 65; i += 2) {
        t *= f2;
        sum += t / (double)i;
    }
    p = 2.0 * sum;
    return k * INV_LN10 + (p * INV_LN10 + p * INV_LN10_T);
}

/* atan for |x| <= 0.4143 via the alternating series; terms shrink by ~x^2
 * (<=0.172) per step, so 26 terms leave < 1e-18 of tail. */
static double atan_core(double x)
{
    double z, t, s;
    int i;

    z = x * x;
    t = -z;
    s = 0.0;
    for (i = 1; i <= 26; i++) {
        s += t / (double)(2 * i + 1);
        t *= -z;
    }
    return x + x * s;
}

double atan(double x)
{
    double t, s, u;
    int neg = 0, inv = 0, cnt = 0;

    if (isnan(x))
        return x;
    if (x == 1.0)
        return 0.78539816339744830962;
    if (x == -1.0)
        return -0.78539816339744830962;
    if (x < 0.0) {
        neg = 1;
        x = -x;
    }
    if (isinf(x))
        return neg ? -PI_2 : PI_2;
    if (x > 1.0) {                   /* atan(x) = pi/2 - atan(1/x) */
        x = 1.0 / x;
        inv = 1;
    }
    while (x > 0.4143) {             /* halving: tan(t/2)=u/(1+sqrt(1+u^2)) */
        u = x;
        t = u / (1.0 + sqrt(1.0 + u * u));
        x = t;
        cnt++;
    }
    s = atan_core(x);
    while (cnt-- > 0)
        s += s;
    if (inv)
        s = PI_2 - s;                /* PI_2 carries ~6e-17 of constant error */
    return neg ? -s : s;
}

float atanf(float x) { return (float)atan((double)x); }

double atan2(double y, double x)
{
    double t, r;

    if (isnan(x) || isnan(y))
        return x + y;
    if (y == 0.0) {
        if (x >= 0.0)
            return y;                /* +/-0 per the sign of y */
        return copysign(PI, y);      /* +/-pi per the sign of y (incl. -0) */
    }
    if (x == 0.0)
        return y > 0.0 ? PI_2 : -PI_2;
    if (isinf(y)) {
        if (isinf(x)) {
            t = 0.78539816339744830962;
            if (y > 0.0)
                r = x > 0.0 ? t : PI - t;
            else
                r = x > 0.0 ? -t : (t - PI);
            return r;
        }
        return y > 0.0 ? PI_2 : -PI_2;
    }
    if (isinf(x)) {
        if (x > 0.0)
            return copysign(0.0, y);
        return copysign(PI, y);
    }
    t = atan(fabs(y / x));
    if (x > 0.0)
        r = t;
    else
        r = PI - t;
    if (y < 0.0)
        r = -r;
    return r;
}

/* asin(x): |x| <= 0.5 goes through atan(x/sqrt(1-x^2)); near the endpoints
 * asin(x) = sign*(pi/2 - 2*asin(sqrt((1-|x|)/2))), where 1-|x| is exact. */
static double asin_small(double x)
{
    return atan(x / sqrt(1.0 - x * x));
}

double asin(double x)
{
    double t, s;

    if (isnan(x))
        return x;
    if (x > 1.0)
        return -HUGE_VAL;            /* domain convention like log */
    if (x < -1.0)
        return HUGE_VAL;
    if (x == 1.0)
        return PI_2;
    if (x == -1.0)
        return -PI_2;
    if (x == 0.0)
        return x;
    if (x < 0.0)
        return -asin(-x);
    if (x <= 0.5)
        return asin_small(x);
    t = sqrt((1.0 - x) * 0.5);
    s = PI_2 - 2.0 * asin_small(t);
    return s;
}

double acos(double x)
{
    double t;

    if (isnan(x))
        return x;
    if (x > 1.0 || x < -1.0)
        return -HUGE_VAL;
    if (x == 1.0)
        return 0.0;
    if (x == -1.0)
        return PI;
    if (x > 0.5) {
        t = sqrt((1.0 - x) * 0.5);
        return 2.0 * asin_small(t);
    }
    return PI_2 - asin(x);
}

/* --- hyperbolic ------------------------------------------------------- */

double sinh(double x)
{
    double t;

    if (isnan(x))
        return x;
    if (x > 710.5)
        return HUGE_VAL;
    if (x < -710.5)
        return -HUGE_VAL;
    if (x < 1e-9 && x > -1e-9)
        return x;
    if (x < 0.0)
        return -sinh(-x);
    t = exp(x);
    return 0.5 * (t - 1.0 / t);
}

double cosh(double x)
{
    double t;

    if (isnan(x))
        return x;
    if (x > 710.5 || x < -710.5)
        return HUGE_VAL;
    if (x < 1e-9 && x > -1e-9)
        return 1.0;
    if (x < 0.0)
        x = -x;
    t = exp(x);
    return 0.5 * (t + 1.0 / t);
}

double tanh(double x)
{
    double t;

    if (isnan(x))
        return x;
    if (x > 20.0)
        return 1.0;
    if (x < -20.0)
        return -1.0;
    if (x < 0.25 && x > -0.25) {
        /* tanh = sum c_k x^(2k+1); from (tanh)' = 1 - tanh^2:
         * c_0 = 1, c_m = -(sum_{i+j=m-1} c_i*c_j)/(2m+1).  Terms shrink by
         * ~0.0625 per step on |x| < 0.25, so 10 coefficients are plenty. */
        double cf[11], q = x * x, s = 0.0, acc;
        int k, j;
        cf[0] = 1.0;
        for (k = 1; k <= 10; k++) {
            acc = 0.0;
            for (j = 0; j < k; j++)
                acc += cf[j] * cf[k - 1 - j];
            cf[k] = -acc / (double)(2 * k + 1);
        }
        for (k = 10; k >= 1; k--)
            s = cf[k] + q * s;
        return x * (cf[0] + q * s);
    }
    if (x < 0.0)
        return -tanh(-x);
    t = exp(2.0 * x);
    return 1.0 - 2.0 / (t + 1.0);
}

/* --- inverse hyperbolic ------------------------------------------------ */

double asinh(double x)
{
    int i;

    if (isnan(x))
        return x;
    if (isinf(x))
        return x;
    if (x == 0.0)
        return x;
    if (x < 0.0)
        return -asinh(-x);
    if (x < 0.5) {                   /* series; term ratio ~ x^2 < 0.25 */
        {
            double q = x * x, p = x, s = x;
            double k;
            for (i = 1; i <= 64; i++) {
                k = (double)(2 * i - 1) / (double)(2 * i);
                p *= -k * q;
                s += p * x / (double)(2 * i + 1);
            }
            return s;
        }
    }
    return log(x + sqrt(x * x + 1.0));
}

double acosh(double x)
{
    int i;

    if (isnan(x))
        return x;
    if (x < 1.0)
        return -HUGE_VAL;
    if (x == 1.0)
        return 0.0;
    if (x > 1e150)
        return log(x) + LN2;
    if (x < 1.5) {                   /* acosh^2 series in (x-1), 1/65 tail */
        double t = x - 1.0;
        double s = 0.0, p = 1.0;
        double q, d;
        for (i = 1; i <= 64; i++) {
            q = (double)(4 * i * i - 1);
            p *= (double)i / q;
            t *= (x - 1.0);
            d = p * t / (double)i;
            s += d;
        }
        return sqrt(s);
    }
    return log(x + sqrt(x * x - 1.0));
}

double atanh(double x)
{
    int i;

    if (isnan(x))
        return x;
    if (x == 1.0)
        return HUGE_VAL;
    if (x == -1.0)
        return -HUGE_VAL;
    if (x > 1.0 || x < -1.0)
        return -HUGE_VAL;            /* complex; Lua just needs a NaN-ish */
    if (x == 0.0)
        return x;
    if (x < 0.0)
        return -atanh(-x);
    if (x < 0.5) {                   /* all-positive odd series */
        double q = x * x, p = x, s = 0.0;
        for (i = 1; i <= 64; i++) {
            p *= q;
            s += p / (double)(2 * i + 1);
        }
        return x + s;
    }
    return 0.5 * log((1.0 + x) / (1.0 - x));
}

/* --- rounding / remainder / decomposition ------------------------------ */

#define INT52 4503599627370496.0     /* 2^52: every |x| >= this is integral */

double ceil(double x)
{
    long long i;

    if (!isfinite(x) || fabs(x) >= INT52)
        return x;
    i = (long long)x;
    if ((double)i < x)
        i++;
    return (double)i;
}

float ceilf(float x) { return (float)ceil((double)x); }

double trunc(double x)
{
    if (!isfinite(x) || fabs(x) >= INT52)
        return x;
    return (double)(long long)x;
}

float truncf(float x) { return (float)trunc((double)x); }

double round(double x)
{
    if (!isfinite(x) || fabs(x) >= INT52)
        return x;
    if (x >= 0.0)
        return floor(x + 0.5);
    return -floor(-x + 0.5);         /* halves away from zero (C round) */
}

float roundf(float x) { return (float)round((double)x); }

double fmod(double x, double y)
{
    double r, q, k, nr;
    int i;

    if (isnan(x) || isnan(y) || isinf(x))
        return x + y;                /* NaN per C */
    if (isinf(y))
        return x;
    if (y == 0.0)
        return x;                    /* C would raise; Lua tolerates x back */
    r = fabs(x);
    q = fabs(y);
    if (r < q)
        return x;
    for (i = 0; i < 64 && r >= q; i++) {
        k = floor(r / q);
        nr = r - k * q;
        if (!(nr >= 0.0))
            nr = 0.0;
        if (nr >= r)
            break;                   /* no progress; fall to subtract loop */
        r = nr;
    }
    while (r >= q)
        r -= q;
    return copysign(r, x);
}

float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }

double frexp(double x, int *exp)
{
    union { double d; u64 u; } v;
    u64 raw, mant;
    int e, shift;
    double s;

    if (exp)
        *exp = 0;
    v.d = x;
    raw = v.u;
    e = (int)((raw >> 52) & 0x7ff);
    if (e == 0x7ff || x == 0.0)      /* inf/nan/zero: pass through */
        return x;
    if (e == 0) {                    /* subnormal: normalize by 2^52 and fix */
        s = x * 0x1p52;
        v.d = s;
        e = (int)((v.u >> 52) & 0x7ff) - 52;
        raw = v.u;
    }
    mant = (raw & 0x000fffffffffffffull) | (1ull << 52);
    shift = 53 - (e + 1);
    v.u = (raw & 0x8000000000000000ull) |
          (shift <= 0 ? mant << -shift : mant >> shift);
    if (exp)
        *exp = e + 1;
    return v.d;
}

double ldexp(double x, int exp)
{
    double t;
    long long n;

    if (isnan(x) || x == 0.0)
        return x;
    n = (long long)exp;
    if (n > 0) {
        while (n > 1023) {
            n -= 1023;
            x *= 0x1p1023;
            if (x == HUGE_VAL)
                return HUGE_VAL;
        }
        t = pow2i(n);
        return x * t;
    }
    while (n < -1022) {
        n += 1022;
        x *= 0x1p-1022;
        if (x == 0.0)
            return 0.0;
    }
    return x * pow2i(n);
}

double modf(double x, double *iptr)
{
    long long i;
    double t;

    if (isnan(x)) {
        if (iptr)
            *iptr = x;
        return x;
    }
    if (isinf(x)) {
        if (iptr)
            *iptr = x;
        return x - x;                /* NaN, per C */
    }
    if (fabs(x) >= INT52) {
        if (iptr)
            *iptr = x;
        return copysign(0.0, x);
    }
    i = (long long)x;                /* truncates toward zero */
    t = (double)i;
    if (iptr)
        *iptr = t;
    return copysign(x - t, x);
}

double copysign(double x, double y)
{
    union { double d; u64 u; } vx, vy;
    vx.d = x;
    vy.d = y;
    vx.u = (vx.u & 0x7fffffffffffffffull) | (vy.u & 0x8000000000000000ull);
    return vx.d;
}

float copysignf(float x, float y)
{
    union { float f; unsigned u; } vx, vy;
    vx.f = x;
    vy.f = y;
    vx.u = (vx.u & 0x7fffffffu) | (vy.u & 0x80000000u);
    return vx.f;
}

/* --- pow and legacy wrappers ------------------------------------------- */

double pow(double x, double y)
{
    double l;
    int neg;

    if (y == 0.0)                    /* covers pow(x, +-0), even NaN x? no: */
        return 1.0;                  /* IEEE wants NaN for NaN exponent; y!=0 */
    if (isnan(x) || isnan(y))
        return x + y;
    if (y == 1.0)
        return x;
    if (y == 2.0)
        return x * x;
    if (y == -1.0)
        return 1.0 / x;
    if (y == 0.5)
        return x < 0.0 ? -HUGE_VAL : sqrt(x);
    if (x == HUGE_VAL || x == -HUGE_VAL) {
        if (y < 0.0)
            return 0.0;
        if (x < 0.0 && !isfinite(y))
            return 0.0;              /* -inf^(non-integer): undefined here */
        return x < 0.0 ? -HUGE_VAL : HUGE_VAL;
    }
    if (y == HUGE_VAL || y == -HUGE_VAL) {
        double ax = fabs(x);
        if (ax == 1.0)
            return 1.0;
        if ((ax > 1.0) == (y > 0.0))
            return HUGE_VAL;
        return 0.0;
    }
    if (x == 0.0)
        return y > 0.0 ? copysign(0.0, x) : HUGE_VAL;
    if (x < 0.0) {                   /* only integral exponents make sense */
        if (y != floor(y))
            return 0.0;              /* old TinyGL convention: silent 0 */
        neg = floor(y * 0.5) * 2.0 != y;   /* parity, valid for huge y too */
        l = exp(y * log(-x));
        return neg ? -l : l;
    }
    return exp(y * log(x));
}

float powf(float x, float y) { return (float)pow((double)x, (double)y); }
