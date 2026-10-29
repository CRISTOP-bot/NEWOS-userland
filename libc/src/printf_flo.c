/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/printf_flo.c
 * Description: Float renderer for the printf engine: the only formatter file
 * Added: 2026-10-27
 */
/* Float renderer for the printf engine (libc/src/stdio.c).
 *
 * This is the only part of the formatter that uses the FPU, so it is compiled
 * without -mgeneral-regs-only; stdio.c itself stays integer-only and just
 * pads/truncates what __fcvt hands back.
 *
 * Method: |v| is scaled into one leading digit plus guard digits (decimal) or
 * split by exponent field (hexadecimal), then rounded once before rendering.
 * Accuracy: within 1 ulp of the last printed digit for ordinary magnitudes,
 * which covers Lua's "%.14g" and string.format("%.2f"). Ties round away from
 * zero instead of to even. The exact decimal expansion of huge or tiny values
 * is NOT computed: at most MAXSIG digits exist in a double's working set, so
 * anything past that is padded with zeros and the request is clamped.
 */

#include "../include/stddef.h"
#include "fmt.h"

typedef unsigned long long u64;

#define MAXSIG 40

/* Exact in binary64 up to 1e22. */
static const double P10[23] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12,
    1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22
};

struct flt_out {
    char *dst;
    int cap;
    int len;
    int flags;
};

static void fo_put(struct flt_out *o, char c)
{
    if (o->len < o->cap)
        o->dst[o->len] = c;
    o->len++;
}

static void fo_str(struct flt_out *o, const char *s)
{
    while (*s)
        fo_put(o, *s++);
}

static double pow10e(int e)
{
    int neg = e < 0;
    int k = neg ? -e : e;
    double p = 1.0;

    if (k > 350)
        k = 350;
    while (k >= 22) {
        p *= P10[22];
        k -= 22;
    }
    p *= P10[k];
    return neg ? 1.0 / p : p;
}

/* Fill d with `want` decimal digits of the normalised form of av (which is
 * finite and > 0): value == 0.d[0]d[1]... * 10^(*e10 + 1). The starting
 * exponent comes from the binary exponent and is then corrected by the
 * scaling loops, so a coarse estimate is enough. */
static void dec_digits(double av, int want, char *d, int *e10_out)
{
    union { double dd; u64 u; } b;
    double m;
    int e10, i, k2;

    b.dd = av;
    k2 = (int)((b.u >> 52) & 0x7ff) - 1023;
    if (k2 < -1022)
        e10 = -323;                 /* subnormal */
    else
        e10 = (int)((double)k2 * 0.30102999566398114);
    if (e10 > 320)
        e10 = 320;
    if (e10 < -320)
        e10 = -320;
    m = av / pow10e(e10);
    while (m >= 10.0) {
        m /= 10.0;
        e10++;
    }
    while (m < 1.0) {
        m *= 10.0;
        e10--;
    }
    for (i = 0; i < want; i++) {
        int dg = (int)m;
        if (dg > 9)
            dg = 9;
        if (dg < 0)
            dg = 0;
        d[i] = (char)('0' + dg);
        m = (m - (double)dg) * 10.0;
    }
    *e10_out = e10;
}

/* Round d[0..L-1] against d[L] plus a sticky bit over what is left. Returns 1
 * when the carry escaped the string (it then represents 10^L). */
static int round_digits(char *d, int avail, int L)
{
    int i, up = 0;

    if (L < avail && d[L] >= '5') {
        up = (d[L] > '5');
        if (!up) {
            for (i = L + 1; i < avail; i++) {
                if (d[i] != '0') {
                    up = 1;
                    break;
                }
            }
        }
    }
    if (!up)
        return 0;
    i = L - 1;
    while (i >= 0) {
        if (d[i] == '9') {
            d[i] = '0';
            i--;
        } else {
            d[i]++;
            return 0;
        }
    }
    return 1;
}

static void fo_sign(struct flt_out *o, int flags, int neg)
{
    if (neg)
        fo_put(o, '-');
    else if (flags & F_PLUS)
        fo_put(o, '+');
    else if (flags & F_SPACE)
        fo_put(o, ' ');
}

static void fo_exponent(struct flt_out *o, char marker, int exp)
{
    if (marker == 'e' && (o->flags & F_UPPER))
        marker = 'E';
    if (marker == 'p' && (o->flags & F_UPPER))
        marker = 'P';
    int neg = exp < 0;
    unsigned v = neg ? (unsigned)-exp : (unsigned)exp;
    char buf[8];
    int n = 0;

    fo_put(o, marker);
    fo_put(o, neg ? '-' : '+');
    if (!v)
        buf[n++] = '0';
    while (v) {
        buf[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    if (n < 2)                        /* C wants at least two digits */
        fo_put(o, '0');
    while (n--)
        fo_put(o, buf[n]);
}

/* %a: bit-exact hexadecimal. h[0] is the implicit nibble, h[1..13] the
 * mantissa. */
static void hex_render(struct flt_out *o, int neg, int ebin, u64 man,
                       long prec, int flags)
{
    static const char hd_lower[] = "0123456789abcdef";
    static const char hd_upper[] = "0123456789ABCDEF";
    const char *hd = (flags & F_UPPER) ? hd_upper : hd_lower;
    int h[15];
    int i, np, e2, carry_from;

    if (ebin == 0) {
        e2 = -1022;
        h[0] = 0;                     /* subnormal: no implicit 1 */
    } else {
        e2 = ebin - 1023;
        h[0] = 1;
    }
    for (i = 0; i < 13; i++)
        h[1 + i] = (int)((man >> ((12 - i) * 4)) & 0xF);

    if (prec >= 0) {
        np = (int)prec;
        if (np > 13)
            np = 13;                  /* a binary64 mantissa holds 13 nibbles */
    } else {
        np = 13;                      /* enough for exactness ... */
        while (np > 0 && h[1 + np - 1] == 0)
            np--;                     /* ... minus trailing zeros */
    }
    if (prec >= 0 && np < 13) {
        /* round the fraction at np nibbles */
        carry_from = (h[1 + np] >= 8);
        if (carry_from) {
            i = np;
            while (i > 0) {
                if (h[i] == 15) {
                    h[i] = 0;
                    i--;
                } else {
                    h[i]++;
                    carry_from = 0;
                    break;
                }
            }
            if (carry_from) {         /* 0xF..F rounded up to 1.000 * 2^(e+1) */
                h[0]++;
                if (h[0] == 2) {
                    h[0] = 1;
                    for (i = 1; i <= np; i++)
                        h[i] = 0;
                    e2++;
                }
            }
        }
    }
    fo_sign(o, flags, neg);
    fo_str(o, (flags & F_UPPER) ? "0X" : "0x");
    fo_put(o, hd[h[0]]);
    if (np > 0 || (flags & F_ALT)) {
        fo_put(o, '.');
        for (i = 1; i <= np; i++)
            fo_put(o, hd[h[i]]);
    }
    fo_exponent(o, 'p', e2);
}

int __fcvt(char *out, int cap, int conv, long prec, int flags, double v)
{
    struct flt_out o = { out, cap, 0, flags };
    union { double d; u64 u; } b;
    char d[MAXSIG + 1];
    u64 ub;
    int ebin, neg, i;
    u64 man;

    b.d = v;
    ub = b.u;
    neg = (int)(ub >> 63);
    ebin = (int)((ub >> 52) & 0x7ff);
    man = ub & 0xfffffffffffffull;

    if (ebin == 0x7ff) {
        fo_sign(&o, flags, neg);
        fo_str(&o, man ? ((flags & F_UPPER) ? "NAN" : "nan")
                       : ((flags & F_UPPER) ? "INF" : "inf"));
        return o.len > o.cap ? -1 : o.len;
    }
    if (conv == 'a') {
        hex_render(&o, neg, ebin, man, prec, flags);
        return o.len > o.cap ? -1 : o.len;
    }

    if (conv == 'e') {
        long p = (prec < 0) ? 6 : prec;
        int L = (int)p + 1;
        int e10, ovf;
        if (L > MAXSIG)
            L = MAXSIG;
        if (v == 0.0) {
            for (i = 0; i < MAXSIG; i++)
                d[i] = '0';
            e10 = 0;
            ovf = 0;
        } else {
            dec_digits(neg ? -v : v, MAXSIG, d, &e10);
            ovf = round_digits(d, MAXSIG, L);
        }
        if (ovf) {
            d[0] = '1';
            for (i = 1; i < L; i++)
                d[i] = '0';
            e10++;
        }
        fo_sign(&o, flags, neg);
        fo_put(&o, d[0]);
        if (L > 1 || (flags & F_ALT)) {
            fo_put(&o, '.');
            for (i = 1; i < L; i++)
                fo_put(&o, i < MAXSIG ? d[i] : '0');
        }
        fo_exponent(&o, 'e', e10);
        return o.len > o.cap ? -1 : o.len;
    }

    if (conv == 'f') {
        long p = (prec < 0) ? 6 : prec;
        int e10 = 0, pos, frac, need, L, ovf = 0;
        if (v == 0.0) {
            for (i = 0; i < MAXSIG; i++)
                d[i] = '0';
        } else {
            dec_digits(neg ? -v : v, MAXSIG, d, &e10);
        }
        pos = e10 + 1;                 /* digits before the point */
        frac = (int)p;
        need = pos + frac;
        if (need < 1)
            need = 1;
        L = (need + 1 <= MAXSIG) ? need + 1 : MAXSIG;
        if (v != 0.0)
            ovf = round_digits(d, MAXSIG, L - 1);
        if (ovf) {
            d[0] = '1';
            for (i = 1; i < MAXSIG; i++)
                d[i] = '0';
            pos++;
        }
        fo_sign(&o, flags, neg);
        if (pos > 0) {
            for (i = 0; i < pos; i++)
                fo_put(&o, i < L - 1 ? d[i] : '0');
        } else {
            fo_put(&o, '0');
        }
        if (frac > 0 || (flags & F_ALT))
            fo_put(&o, '.');
        for (i = 0; i < frac; i++) {
            int k = pos + i;
            fo_put(&o, (k >= 0 && k < L - 1) ? d[k] : '0');
        }
        return o.len > o.cap ? -1 : o.len;
    }

    /* %g: shortest of the two forms, then stripped of noise. */
    {
        long P = (prec < 0) ? 6 : prec;
        int e10 = 0, X, ovf, use_e, pos, frac, L;
        if (P == 0)
            P = 1;
        if (P > MAXSIG)
            P = MAXSIG;
        L = (int)P;
        if (v == 0.0) {
            for (i = 0; i < MAXSIG; i++)
                d[i] = '0';
            ovf = 0;
        } else {
            dec_digits(neg ? -v : v, MAXSIG, d, &e10);
            ovf = round_digits(d, MAXSIG, L);
        }
        if (ovf) {
            d[0] = '1';
            for (i = 1; i < L; i++)
                d[i] = '0';
            e10++;
        }
        X = e10;
        use_e = (X < -4 || X >= (int)P);
        fo_sign(&o, flags, neg);
        if (use_e) {
            fo_put(&o, d[0]);
            {
                int np = L - 1;
                if (!(flags & F_ALT)) {
                    while (np > 0 && d[np] == '0')
                        np--;
                }
                if (np > 0 || (flags & F_ALT)) {
                    fo_put(&o, '.');
                    for (i = 1; i <= np; i++)
                        fo_put(&o, d[i]);
                }
            }
            fo_exponent(&o, 'e', X);
        } else {
            pos = X + 1;
            frac = (int)P - 1 - X;
            if (frac < 0)
                frac = 0;
            if (!(flags & F_ALT)) {
                while (frac > 0) {
                    int k = pos + frac - 1;
                    if (k >= L || (k >= 0 && d[k] != '0'))
                        break;
                    if (k < 0)
                        break;
                    frac--;
                }
            }
            if (pos > 0) {
                for (i = 0; i < pos; i++)
                    fo_put(&o, i < L ? d[i] : '0');
            } else {
                fo_put(&o, '0');
            }
            if (frac > 0 || (flags & F_ALT))
                fo_put(&o, '.');
            for (i = 0; i < frac; i++) {
                int k = pos + i;
                if (k < 0)
                    fo_put(&o, '0');
                else
                    fo_put(&o, k < L ? d[k] : '0');
            }
        }
        return o.len > o.cap ? -1 : o.len;
    }
}

int __fvafmt(char *out, int cap, int conv, long prec, int flags, int lenmod,
             va_list ap)
{
    double v;

    if (lenmod == 8)
        v = (double)va_arg(ap, long double);
    else
        v = va_arg(ap, double);
    return __fcvt(out, cap, conv, prec, flags, v);
}
