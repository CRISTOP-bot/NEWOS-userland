/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/strtod.c
 * Description: Freestanding strtod/strtof/atof for the Lua numeric literal
 *              parser.
 * Added: 2026-10-30
 */
/* Freestanding decimal/hex float parsing (NEWOS libc, FP-flag file).
 *
 * No host libc, no long double, no calls into math.c, no libgcc runtime
 * helpers (the only __int128 uses are variable shifts, which GCC lowers
 * inline on x86-64; the (u64)u128 truncations are pure register moves).
 *
 * Strategy: the first <= 19 significant digits are kept exactly in a u64
 * mantissa m with a sticky flag for everything dropped, then the value
 * m * 10^e (decimal) or m * 2^e (hex, where scaling is exact) is evaluated
 * in double-double arithmetic (106-bit mantissa, Knuth-style Dekker split
 * products; each multiply/divide by ten adds ~2^-106 of error).  The final
 * rounding compares the stored residual against half an ulp; the residual
 * dd error (~2^-97 relative) only flips decisions for inputs within ~2^-45
 * of a rounding boundary, which never happens for round-trips of "%.14g".
 * Correctly rounded for all ordinary inputs, within 1 ulp always.
 */

#include "../include/stdlib.h"

typedef unsigned long long u64;

#define SPLITC 134217729.0           /* 2^27 + 1, Dekker splitter */

/* Overflow result for decimal exponents beyond the double range; GCC folds
 * this to the IEEE +infinity bit pattern at compile time. */
#define INF_D (__builtin_huge_val())

static int lc(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int kw(const char *p, const char *w)
{
    while (*w) {
        if (lc(*p) != *w)
            return 0;
        p++;
        w++;
    }
    return 1;
}

static double bits2d(u64 u)
{
    union { double d; u64 u; } v;
    v.u = u;
    return v.d;
}

/* Exact multiplication by 2^n for normal inputs: chunks of 2^700/2^-700
 * (both normal) then single doublings, so nothing passes through the
 * subnormal range and every step is loss-free. */
static double scale2(double v, int n)
{
    while (n > 700) {
        v *= 0x1p700;
        n -= 700;
    }
    while (n < -700) {
        v *= 0x1p-700;
        n += 700;
    }
    while (n > 0) {
        v *= 2.0;
        n--;
    }
    while (n < 0) {
        v *= 0.5;
        n++;
    }
    return v;
}

/* Dekker splitting: with a,b normal and p = fl(a*b) the exact product is
 * p + prod_err(a,b,p) up to terms below 2^-106 (which is what the
 * double-double needs). */
static double prod_err(double a, double b, double p)
{
    double ahi, alo, bhi, blo;

    ahi = (a + SPLITC) - a;
    alo = a - ahi;
    bhi = (b + SPLITC) - b;
    blo = b - bhi;
    return ((ahi * bhi - p) + ahi * blo + alo * bhi) + alo * blo;
}

/* Knuth two-sum residual: (a + b) - sum exactly, for normal a, b. */
static double sum_err(double a, double b, double s)
{
    double bb = s - a;

    return (a - (s - bb)) + (b - bb);
}

/* (fhi + flo) * 2^K scaled by ten, renormalized to fhi in [1,2). */
static void dd_mul10(double *fhi, double *flo, long long *K)
{
    double a = *fhi, b = *flo;
    double p, t, e2;
    int n = 0;

    p = a * 10.0;
    t = p + b * 10.0;
    e2 = sum_err(p, b * 10.0, t) + prod_err(a, 10.0, p);
    while (t >= 2.0) {
        t *= 0.5;
        e2 *= 0.5;
        n++;
    }
    *fhi = t;
    *flo = e2;
    *K += n;
}

/* Division by ten: q = a/10 is within 2 ulp; the correction step
 * dq = (a - 10q - err(10q) + b)/10 recovers to ~2^-106 relative. */
static void dd_div10(double *fhi, double *flo, long long *K)
{
    double a = *fhi, b = *flo;
    double q, pq, dq, t, e2;
    int n = 0;

    q = a / 10.0;
    pq = q * 10.0;
    dq = ((a - pq) + prod_err(q, 10.0, pq) + b) / 10.0;
    t = q + dq;
    e2 = sum_err(q, dq, t);
    while (t < 1.0) {
        t *= 2.0;
        e2 *= 2.0;
        n--;
    }
    *fhi = t;
    *flo = e2;
    *K += n;
}

/*
 * Assemble the double nearest to +/- (m64 * base^e), where `base10` picks
 * the radix (10 or 2) and `sticky` says lower digits were dropped.
 */
static double dd_build(int neg, u64 m64, long long e, int base10, int sticky)
{
    double hi, lo, t, se, w, x, half;
    union { double d; u64 u; } v;
    u64 M, sign;
    long long K;
    int E, s, up;

    sign = neg ? 0x8000000000000000ull : 0;
    if (m64 == 0)
        return bits2d(sign);
    if (base10) {
        if (e > 350)
            return neg ? -INF_D : INF_D;
        if (e < -350)
            return bits2d(sign);     /* underflow: m64*10^-351 < 2^-1100 */
    }
    hi = (double)m64;                /* u64 -> double (rounds) */
    v.d = hi;
    if (m64 >= (u64)hi)
        lo = (double)(m64 - (u64)hi);
    else
        lo = -(double)((u64)hi - m64);
    E = (int)((v.u >> 52) & 0x7ff) - 1023;
    K = E;
    t = scale2(hi, -E);              /* normalize: exact */
    se = scale2(lo, -E);             /* exact: power-of-two scaling */
    lo = t + se;
    se = sum_err(t, se, lo);         /* now t in [1,2] and exact residual */
    t = lo;
    if (t >= 2.0) {                  /* carry across the binade */
        t = 1.0;
        se *= 0.5;
        K++;
    }

    if (base10) {
        while (e > 0) {
            dd_mul10(&t, &se, &K);
            e--;
        }
        while (e < 0) {
            dd_div10(&t, &se, &K);
            e++;
        }
    } else {
        K += e;
    }

    if (K > 1023)
        return neg ? -INF_D : INF_D;
    if (K < -1150)
        return bits2d(sign);

    v.d = t;
    M = (v.u & 0x000fffffffffffffull) | (1ull << 52);
    E = (int)K;
    /* Residual of the true value past M, in units of M's ulp:
     * value = (M + se * 2^52) * 2^(K-52). */
    w = se * 4503599627370496.0;     /* == se * 2^52, exact */
    s = (E < -1022) ? (int)(-1022 - E) : 0;

    if (s == 0) {
        up = 0;
        if (w > 0.5 || (w == 0.5 && (sticky || (M & 1))))
            up = 1;
        if (up) {
            M++;
            if (M >> 53) {
                M >>= 1;
                E++;
                if (E > 1023)
                    return neg ? -INF_D : INF_D;
            }
        }
        return bits2d(sign | ((u64)(E + 1023) << 52) |
                      (M & 0xfffffffffffffull));
    }
    if (s > 53)
        return bits2d(sign);
    {
        u64 keep = M >> s;
        u64 low = M - (keep << s);
        half = (double)(1ull << (s - 1));
        x = (double)low + scale2(w, s);
        up = 0;
        if (x > half || (x == half && (sticky || (keep & 1))))
            up = 1;
        if (up)
            keep++;
        if (keep >> 52)              /* rounded up into the normals */
            return bits2d(sign | (1ull << 52));
        if (keep == 0)
            return bits2d(sign);
        return bits2d(sign | keep);
    }
}

/* Parse an exponent tail ("eNNN" or "pNNN") with saturation. */
static long long parse_exp(const unsigned char **pp, int radix16)
{
    const unsigned char *p = *pp;
    int esign = 0;
    long long ev = 0;
    int c = lc(*p);

    if (c != (radix16 ? 'p' : 'e'))
        return 0;
    p++;
    if (*p == '+' || *p == '-')
        esign = (*p++ == '-') ? -1 : 1;
    if (!(*p >= '0' && *p <= '9'))
        return 0;                    /* not a real exponent: rewind not
                                      * needed; caller did not consume */
    while (*p >= '0' && *p <= '9') {
        if (ev < 100000)
            ev = ev * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    return esign < 0 ? -ev : ev;
}

double strtod(const char *s, char **end)
{
    const unsigned char *p = (const unsigned char *)s;
    int neg = 0, sticky = 0, base10 = 1, saw = 0, hexm = 0;
    int nd = 0;                      /* significant digits collected in m64 */
    u64 m64 = 0;
    long long fcnt = 0;              /* digits after the point */
    long long dropped = 0;           /* significant digits beyond the 19th */
    long long e;

    while (*p == ' ' || (*p >= 9 && *p <= 13))
        p++;
    if (*p == '+' || *p == '-')
        neg = (*p++ == '-');

    if (kw((const char *)p, "nan")) {
        p += 3;
        if (*p == '(') {
            while (*p && *p != ')')
                p++;
            if (*p == ')')
                p++;
        }
        if (end)
            *end = (char *)p;
        return bits2d(neg ? 0xffffffffffffffffull : 0x7ff8000000000000ull);
    }
    if (kw((const char *)p, "inf")) {
        p += 3;
        if (kw((const char *)p, "inity"))
            p += 5;
        if (end)
            *end = (char *)p;
        return neg ? -INF_D : INF_D;
    }

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        hexm = 1;
        base10 = 0;
        p += 2;
    }

    /* mantissa digits: [hex prefix] digits [. digits] */
    {
        int point = 0;
        while (1) {
            int d = *p;
            int v;
            if (!point && d == '.') {
                point = 1;
                p++;
                continue;
            }
            if (d >= '0' && d <= '9')
                v = d - '0';
            else if (hexm && lc(d) >= 'a' && lc(d) <= 'f')
                v = lc(d) - 'a' + 10;
            else
                break;
            saw = 1;
            if (point)
                fcnt++;
            if (v || nd) {               /* significant */
                int lim = hexm ? 16 : 19;
                if (nd < lim) {
                    m64 = m64 * (u64)(hexm ? 16 : 10) + (u64)v;
                    nd++;
                } else {
                    dropped++;
                    if (v)
                        sticky = 1;
                }
            }
            p++;
        }
    }

    if (!saw) {
        if (end)
            *end = (char *)s;
        return 0.0;
    }

    e = parse_exp(&p, !base10);

    if (base10)
        e = e - fcnt + dropped;
    else
        e = e - 4 * fcnt + 4 * dropped;

    if (end)
        *end = (char *)p;
    return dd_build(neg, m64, e, base10, sticky);
}

float strtof(const char *s, char **end)
{
    return (float)strtod(s, end);
}

double atof(const char *s)
{
    return strtod(s, (char **)0);
}
