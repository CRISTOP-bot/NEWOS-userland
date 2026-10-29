/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/time.h
 * Description: Wall/monotonic time via the SYSINFO uptime counter (seconds since
 * Added: 2026-10-17
 */
#ifndef NEWOS_LIBC_TIME_H
#define NEWOS_LIBC_TIME_H

/* Time in NEWOS comes from two kernel clocks:
 *
 *   SYS_GETTIMEOFDAY - Unix epoch seconds read from the CMOS RTC, plus a
 *                      tick-derived sub-second fraction. Second granularity,
 *                      no timezone (the kernel has no TZ database), so the
 *                      epoch value is UTC-as-kept-by-the-RTC and
 *                      localtime() == gmtime().
 *   SYS_CLOCK_GETTIME(CLOCK_MONOTONIC) - tick clock, used for clock().
 *
 * Calendar conversion (struct tm, mktime, strftime) is done in libc with the
 * civil-days algorithm, so ports get real dates rather than "seconds since
 * boot". */

#include <stddef.h>

typedef long time_t;
typedef long clock_t;
typedef long suseconds_t;

#define CLOCKS_PER_SEC 1000000L
#define TIME_NULL ((time_t)0)

struct tm {
    int tm_sec;    /* 0-60 (leap second tolerance) */
    int tm_min;    /* 0-59 */
    int tm_hour;   /* 0-23 */
    int tm_mday;   /* 1-31 */
    int tm_mon;    /* 0-11 */
    int tm_year;   /* years since 1900 */
    int tm_wday;   /* 0-6, Sunday first */
    int tm_yday;   /* 0-365 */
    int tm_isdst;  /* always 0: NEWOS has no DST */
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

/* Seconds since the Unix epoch, or (time_t)-1 when the RTC is unavailable. */
time_t time(time_t *t);
int gettimeofday(struct timeval *tv, void *tz);
int clock_gettime(int clk_id, struct timespec *ts);

/* Monotonic CPU-elapsed proxy in CLOCKS_PER_SEC units: the tick clock, not a
 * per-process CPU accounting (the kernel tracks neither). */
clock_t clock(void);

double difftime(time_t a, time_t b);
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
struct tm *gmtime_r(const time_t *t, struct tm *res);
struct tm *localtime_r(const time_t *t, struct tm *res);
time_t mktime(struct tm *tm);
size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);

/* clk_id values accepted by clock_gettime (Linux numbers). */
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

#endif
