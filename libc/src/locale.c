/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/libc/src/locale.c
 * Description: NEWOS libc module
 * Added: 2026-10-27
 */
#include "../include/locale.h"
#include <stddef.h>

/* NEWOS ships no locale data, so the C locale is the only one. setlocale
 * keeps the standard "last successful call" contract: it returns "C" for
 * "C" and "" (the request for the environment default, which here *is* C)
 * and NULL for anything else, which is how a port learns that, say,
 * "en_US.UTF-8" was refused rather than silently half-applied. */

static const char *current_locale = "C";

/* The C locale: '.' decimal point, no grouping, no monetary convention. */
static struct lconv c_lconv = {
    .decimal_point = ".",
    .thousands_sep = "",
    .grouping = "",
    .int_curr_symbol = "",
    .currency_symbol = "",
    .mon_decimal_point = "",
    .mon_thousands_sep = "",
    .mon_grouping = "",
    .positive_sign = "",
    .negative_sign = "",
    .int_frac_digits = 127,
    .frac_digits = 127,
    .p_cs_precedes = 127,
    .p_sep_by_space = 127,
    .n_cs_precedes = 127,
    .n_sep_by_space = 127,
    .p_sign_posn = 127,
    .n_sign_posn = 127
};

char *setlocale(int category, const char *locale)
{
    (void)category;
    if (!locale)
        return (char *)current_locale;
    if (locale[0] == '\0' ||
        (locale[0] == 'C' && locale[1] == '\0')) {
        current_locale = "C";
        return (char *)"C";
    }
    return NULL;
}

struct lconv *localeconv(void)
{
    return &c_lconv;
}
