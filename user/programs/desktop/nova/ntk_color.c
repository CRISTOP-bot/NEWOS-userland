// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include "ntk_color.h"
NtkColor ntk_color_from_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFFu << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

NtkColor ntk_color_from_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

NtkColor ntk_color_from_hex(const char *hex) {
    if (!hex) return 0xFF000000;
    if (*hex == '#') hex++;

    unsigned int val = 0;
    int len = 0;
    const char *p = hex;
    while (*p && len < 8) {
        char c = *p++;
        unsigned int d;
        if (c >= '0' && c <= '9')      d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned int)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned int)(c - 'A' + 10);
        else break;
        val = (val << 4) | d;
        len++;
    }

    if (len == 6) {
        return 0xFF000000 | val;
    } else if (len == 8) {
        return val;
    } else if (len == 3) {
        uint8_t r = (uint8_t)((val >> 8) & 0xF);
        uint8_t g = (uint8_t)((val >> 4) & 0xF);
        uint8_t b = (uint8_t)( val       & 0xF);
        return 0xFF000000 | ((uint32_t)(r | (r << 4)) << 16)
                          | ((uint32_t)(g | (g << 4)) <<  8)
                          |  (uint32_t)(b | (b << 4));
    }

    return 0xFF000000;
}

/* NEWOS adaptation: integer-only interpolation for the kernel's no-FPU ABI.
 * t is an 8-bit weight: 0 selects a, 255 selects b. */
static uint8_t ntk_lerp_u8(uint8_t a, uint8_t b, uint8_t t) {
    uint32_t inv = 255u - t;
    return (uint8_t)(((uint32_t)a * inv + (uint32_t)b * t + 127u) / 255u);
}

NtkColor ntk_color_lerp_u8(NtkColor a, NtkColor b, uint8_t t) {
    return ntk_color_from_rgba(
        ntk_lerp_u8(ntk_color_r(a), ntk_color_r(b), t),
        ntk_lerp_u8(ntk_color_g(a), ntk_color_g(b), t),
        ntk_lerp_u8(ntk_color_b(a), ntk_color_b(b), t),
        ntk_lerp_u8(ntk_color_a(a), ntk_color_a(b), t));
}

NtkColor ntk_color_with_alpha(NtkColor c, uint8_t alpha) {
    return (c & 0x00FFFFFF) | ((uint32_t)alpha << 24);
}

void ntk_color_to_rgb(NtkColor c, uint8_t *r, uint8_t *g, uint8_t *b) {
    if (r) *r = ntk_color_r(c);
    if (g) *g = ntk_color_g(c);
    if (b) *b = ntk_color_b(c);
}

void ntk_color_to_rgba(NtkColor c, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    if (r) *r = ntk_color_r(c);
    if (g) *g = ntk_color_g(c);
    if (b) *b = ntk_color_b(c);
    if (a) *a = ntk_color_a(c);
}

const char* ntk_color_to_hex(NtkColor c) {
    static char buf[10];
    static const char digits[] = "0123456789ABCDEF";
    int i;
    buf[0] = '#';
    for (i = 0; i < 8; i++)
        buf[i + 1] = digits[(c >> ((7 - i) * 4)) & 0x0fu];
    buf[9] = '\0';
    return buf;
}
