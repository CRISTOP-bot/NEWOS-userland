/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/time.c
 * Description: NEWOS libc module
 * Added: 2026-10-17
 */
#include "../include/time.h"
#include "../include/errno.h"
#include "../../abi/syscall_abi.h"
#include "syscall.h"

/* ------------------------------------------------------------------ */
/* Kernel clock access                                                 */
/* ------------------------------------------------------------------ */

time_t time(time_t *t)
{
    struct nsh_timeval tv;
    time_t s;

    if (libc_sys1(SYS_GETTIMEOFDAY, (long)&tv) != 0)
        return (time_t)-1;
    s = (time_t)tv.tv_sec;
    if (t)
        *t = s;
    return s;
}

int gettimeofday(struct timeval *tv, void *tz)
{
    struct nsh_timeval k;

    (void)tz; /* no timezone support: struct timezone is obsolete anyway */
    if (!tv)
        return 0;
    if (libc_sys1(SYS_GETTIMEOFDAY, (long)&k) != 0) {
        errno = EFAULT;
        return -1;
    }
    tv->tv_sec = (time_t)k.tv_sec;
    tv->tv_usec = (suseconds_t)k.tv_usec;
    return 0;
}

int clock_gettime(int clk_id, struct timespec *ts)
{
    struct nsh_timespec k;

    if (!ts) {
        errno = EFAULT;
        return -1;
    }
    if (clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC) {
        errno = EINVAL;
        return -1;
    }
    if (libc_sys2(SYS_CLOCK_GETTIME, clk_id, (long)&k) != 0) {
        errno = EFAULT;
        return -1;
    }
    ts->tv_sec = (time_t)k.tv_sec;
    ts->tv_nsec = (long)k.tv_nsec;
    return 0;
}

clock_t clock(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return (clock_t)-1;
    return (clock_t)(ts.tv_sec * 1000000L + ts.tv_nsec / 1000L);
}

/* ------------------------------------------------------------------ */
/* Civil calendar (Howard Hinnant's algorithm, days since 1970-01-01)  */
/* ------------------------------------------------------------------ */

#define SECS_PER_DAY  86400L
#define UNIX_EPOCH_WDAY 4 /* 1970-01-01 was a Thursday */

static long days_from_civil(long y, unsigned m, unsigned d)
{
    long era, yoe, doe;
    unsigned mp, doy;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    mp = m + (m > 2 ? -3 : 9);
    doy = (153u * mp + 2u) / 5u + d - 1u;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + (long)doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long z, long *py, unsigned *pm, unsigned *pd)
{
    long era, doe, yoe, y, mp;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doe = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doe + 2) / 153;
    *pd = (unsigned)(doe - (153 * mp + 2) / 5 + 1);
    *pm = (unsigned)(mp + (mp < 10 ? 3 : -9));
    *py = y + (*pm <= 2 ? 1 : 0);
}

static const char *wday_full[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

static const char *wday_names[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static const char *mon_names[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char *mon_abbr[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* ------------------------------------------------------------------ */
/* time_t <-> struct tm                                                */
/* ------------------------------------------------------------------ */

static struct tm *broken_down(const time_t *t, struct tm *res)
{
    long days, secs;
    unsigned m, d;
    long y;

    if (!t)
        return 0;
    days = (long)(*t / SECS_PER_DAY);
    secs = (long)(*t % SECS_PER_DAY);
    if (secs < 0) {
        secs += SECS_PER_DAY;
        days -= 1;
    }
    civil_from_days(days, &y, &m, &d);
    res->tm_year = (int)(y - 1900);
    res->tm_mon = (int)m - 1;
    res->tm_mday = (int)d;
    res->tm_hour = (int)(secs / 3600);
    res->tm_min = (int)((secs % 3600) / 60);
    res->tm_sec = (int)(secs % 60);
    res->tm_wday = (int)(((days % 7) + UNIX_EPOCH_WDAY) % 7);
    if (res->tm_wday < 0)
        res->tm_wday += 7;
    res->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    res->tm_isdst = 0;
    return res;
}

struct tm *gmtime_r(const time_t *t, struct tm *res)
{
    return res ? broken_down(t, res) : 0;
}

struct tm *gmtime(const time_t *t)
{
    static struct tm buf;
    return broken_down(t, &buf);
}

/* No timezone data in NEWOS: local time *is* the RTC's UTC. */
struct tm *localtime_r(const time_t *t, struct tm *res)
{
    return gmtime_r(t, res);
}

struct tm *localtime(const time_t *t)
{
    return gmtime(t);
}

/* Floor division/modulus: the normalisation below must push negative
 * remainders upward (1900-02-31 style underflows) rather than truncate. */
static long floor_div(long a, long b)
{
    long q = a / b;
    if (a % b != 0 && (a < 0) != (b < 0))
        q--;
    return q;
}

static long floor_mod(long a, long b)
{
    return a - floor_div(a, b) * b;
}

time_t mktime(struct tm *tm)
{
    long sec, minute, hour, day, mon, year, days;
    time_t t;

    if (!tm)
        return (time_t)-1;
    sec = tm->tm_sec;
    minute = tm->tm_min;
    hour = tm->tm_hour;
    day = tm->tm_mday;
    mon = tm->tm_mon;
    year = (long)tm->tm_year + 1900;

    minute += floor_div(sec, 60);
    sec = floor_mod(sec, 60);
    hour += floor_div(minute, 60);
    minute = floor_mod(minute, 60);
    day += floor_div(hour, 24);
    hour = floor_mod(hour, 24);
    year += floor_div(mon, 12);
    mon = floor_mod(mon, 12);

    /* Days are re-derived from the epoch via the civil algorithm, so a
     * 35 April simply rolls into May. */
    days = days_from_civil(year, (unsigned)(mon + 1), 1) + (day - 1);
    t = (time_t)days * SECS_PER_DAY + (time_t)hour * 3600
        + (time_t)minute * 60 + (time_t)sec;
    broken_down(&t, tm);
    return t;
}

double difftime(time_t a, time_t b)
{
    return (double)(a - b);
}

/* ------------------------------------------------------------------ */
/* strftime                                                            */
/* ------------------------------------------------------------------ */

struct sout {
    char *dst;
    size_t max;
    size_t len;
    int full;
};

static void s_put(struct sout *o, int c)
{
    if (o->len + 1 < o->max)
        o->dst[o->len] = (char)c;
    else
        o->full = 1;
    o->len++;
}

static void s_puts(struct sout *o, const char *s)
{
    while (*s)
        s_put(o, *s++);
}

static void s_pad(struct sout *o, long v, int width)
{
    char buf[24];
    int n = 0;
    int neg = v < 0;

    if (neg)
        v = -v;
    do {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    if (neg)
        buf[n++] = '-';
    while (n < width)
        buf[n++] = '0';
    while (n--)
        s_put(o, buf[n]);
}

/* %e: day of month, space-padded to two columns. */
static void s_pad_space(struct sout *o, long v, int width)
{
    char buf[24];
    int n = 0;

    do {
        buf[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    while (n < width)
        buf[n++] = ' ';
    while (n--)
        s_put(o, buf[n]);
}

/* %s without going through mktime(): mktime would rewrite the const tm. */
static time_t tm2epoch(const struct tm *tm)
{
    long days = days_from_civil((long)tm->tm_year + 1900,
                                (unsigned)(tm->tm_mon + 1),
                                (unsigned)tm->tm_mday);
    return (time_t)days * SECS_PER_DAY
        + (time_t)tm->tm_hour * 3600 + (time_t)tm->tm_min * 60
        + (time_t)tm->tm_sec;
}

/* %H:%M:%S, shared by %T, %X and %c. */
static void s_hms(struct sout *o, const struct tm *tm)
{
    s_pad(o, tm->tm_hour, 2);
    s_put(o, ':');
    s_pad(o, tm->tm_min, 2);
    s_put(o, ':');
    s_pad(o, tm->tm_sec, 2);
}

static int is_leap(long y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/* ISO 8601 week numbering: the week containing the year's first Thursday,
 * so the ISO year can differ from the calendar year at both ends. */
static void iso_week(const struct tm *tm, long *week, long *year)
{
    long y = (long)tm->tm_year + 1900;
    long ordinal = tm->tm_yday + 1;
    long wd = tm->tm_wday == 0 ? 7 : tm->tm_wday;
    long thursday = ordinal - wd + 4;
    long len = 365 + (is_leap(y) ? 1 : 0);

    if (thursday < 1) {
        y -= 1;
        thursday += 365 + (is_leap(y) ? 1 : 0);
    } else if (thursday > len) {
        thursday -= len;
        y += 1;
    }
    *year = y;
    *week = (thursday - 1) / 7 + 1;
}

static long hour12(int h)
{
    long r = h % 12;
    return r ? r : 12;
}

size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
    struct sout out = { s, max, 0, 0 };
    long iy, iw;

    if (!s || !fmt || !tm || max == 0)
        return 0;
    iso_week(tm, &iw, &iy);
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            s_put(&out, *fmt);
            continue;
        }
        fmt++;
        if (*fmt == '\0')
            break;
        if (*fmt == 'E' || *fmt == 'O')
            fmt++; /* locale-extension modifiers: render the plain form */
        switch (*fmt) {
        case 'a': s_puts(&out, wday_names[tm->tm_wday % 7]); break;
        case 'A': s_puts(&out, wday_full[tm->tm_wday % 7]); break;
        case 'b': case 'h':
            s_puts(&out, mon_abbr[tm->tm_mon % 12]); break;
        case 'B': s_puts(&out, mon_names[tm->tm_mon % 12]); break;
        case 'c':
            s_puts(&out, wday_names[tm->tm_wday % 7]);
            s_put(&out, ' ');
            s_puts(&out, mon_abbr[tm->tm_mon % 12]);
            s_put(&out, ' ');
            s_pad_space(&out, tm->tm_mday, 2);
            s_put(&out, ' ');
            s_hms(&out, tm);
            s_put(&out, ' ');
            s_pad(&out, (long)tm->tm_year + 1900, 4);
            break;
        case 'C': s_pad(&out, ((long)tm->tm_year + 1900) / 100, 2); break;
        case 'd': s_pad(&out, tm->tm_mday, 2); break;
        case 'e': s_pad_space(&out, tm->tm_mday, 2); break;
        case 'F':
            s_pad(&out, (long)tm->tm_year + 1900, 4);
            s_put(&out, '-');
            s_pad(&out, tm->tm_mon + 1, 2);
            s_put(&out, '-');
            s_pad(&out, tm->tm_mday, 2);
            break;
        case 'g': s_pad(&out, iy % 100, 2); break;
        case 'G': s_pad(&out, iy, 4); break;
        case 'H': s_pad(&out, tm->tm_hour, 2); break;
        case 'I': s_pad(&out, hour12(tm->tm_hour), 2); break;
        case 'j': s_pad(&out, tm->tm_yday + 1, 3); break;
        case 'm': s_pad(&out, tm->tm_mon + 1, 2); break;
        case 'M': s_pad(&out, tm->tm_min, 2); break;
        case 'n': s_put(&out, '\n'); break;
        case 'p': s_puts(&out, tm->tm_hour < 12 ? "AM" : "PM"); break;
        case 'r':
            s_pad(&out, hour12(tm->tm_hour), 2);
            s_put(&out, ':');
            s_pad(&out, tm->tm_min, 2);
            s_put(&out, ':');
            s_pad(&out, tm->tm_sec, 2);
            s_put(&out, ' ');
            s_puts(&out, tm->tm_hour < 12 ? "AM" : "PM");
            break;
        case 'R':
            s_pad(&out, tm->tm_hour, 2);
            s_put(&out, ':');
            s_pad(&out, tm->tm_min, 2);
            break;
        case 's': s_pad(&out, tm2epoch(tm), 1); break;
        case 'S': s_pad(&out, tm->tm_sec, 2); break;
        case 't': s_put(&out, '\t'); break;
        case 'T': s_hms(&out, tm); break;
        case 'u': s_put(&out, '0' + (tm->tm_wday == 0 ? 7 : tm->tm_wday)); break;
        case 'U':
            s_pad(&out, (tm->tm_yday + 7 - tm->tm_wday) / 7, 2);
            break;
        case 'V': s_pad(&out, iw, 2); break;
        case 'v':
            s_pad_space(&out, tm->tm_mday, 2);
            s_put(&out, '-');
            s_puts(&out, mon_abbr[tm->tm_mon % 12]);
            s_put(&out, '-');
            s_pad(&out, (long)tm->tm_year + 1900, 4);
            break;
        case 'w': s_put(&out, '0' + tm->tm_wday); break;
        case 'W':
            s_pad(&out,
                  (tm->tm_yday + 7
                   - (tm->tm_wday == 0 ? 6 : tm->tm_wday - 1)) / 7, 2);
            break;
        case 'x':
            s_pad(&out, tm->tm_mon + 1, 2);
            s_put(&out, '/');
            s_pad(&out, tm->tm_mday, 2);
            s_put(&out, '/');
            s_pad(&out, ((long)tm->tm_year + 1900) % 100, 2);
            break;
        case 'X': s_hms(&out, tm); break;
        case 'y': s_pad(&out, ((long)tm->tm_year + 1900) % 100, 2); break;
        case 'Y': s_pad(&out, (long)tm->tm_year + 1900, 4); break;
        case 'z': s_puts(&out, "+0000"); break;
        case 'Z': s_puts(&out, "UTC"); break;
        case '%': s_put(&out, '%'); break;
        default:
            return 0;
        }
    }
    if (out.full)
        return 0;
    s[out.len] = '\0';
    return out.len;
}
