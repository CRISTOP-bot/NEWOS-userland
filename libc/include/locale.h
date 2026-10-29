/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/include/locale.h
 * Description: Locale is fixed at the C locale: setlocale succeeds for "C"/""
 * Added: 2026-10-27
 */
#ifndef NEWOS_LIBC_LOCALE_H
#define NEWOS_LIBC_LOCALE_H

/* Locale is fixed at the C locale: setlocale succeeds for "C"/"" and fails for
 * anything else, so ports that probe for UTF-8 or a national decimal point get
 * an honest answer. */

#define LC_ALL      0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5

struct lconv {
    char *decimal_point;
    char *thousands_sep;
    char *grouping;
    char *int_curr_symbol;
    char *currency_symbol;
    char *mon_decimal_point;
    char *mon_thousands_sep;
    char *mon_grouping;
    char *positive_sign;
    char *negative_sign;
    char int_frac_digits;
    char frac_digits;
    char p_cs_precedes;
    char p_sep_by_space;
    char n_cs_precedes;
    char n_sep_by_space;
    char p_sign_posn;
    char n_sign_posn;
};

char *setlocale(int category, const char *locale);
struct lconv *localeconv(void);

#endif
