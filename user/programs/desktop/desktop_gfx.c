/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_gfx.c
 * Description: Primitive drawing, bitmap text, formatting and the Win95 icon set.
 * Added: 2026-10-22
 */
/* Primitive drawing, bitmap text, formatting and the Win95 icon set. */

#include "desktop.h"
#include "font8x8.h"

long iabs(long v) { return v < 0 ? -v : v; }

u32 isqrt32(u32 v)
{
    u32 r = 0;
    while ((r + 1u) * (r + 1u) <= v)
        r++;
    return r;
}

u32 rgb_lerp(u32 a, u32 b, u32 t)
{
    u32 ar = (a >> 16) & 0xffu, ag = (a >> 8) & 0xffu, ab = a & 0xffu;
    u32 br = (b >> 16) & 0xffu, bg = (b >> 8) & 0xffu, bb = b & 0xffu;
    u32 r = ar + ((br - ar) * t) / 255u;
    u32 g = ag + ((bg - ag) * t) / 255u;
    u32 bl = ab + ((bb - ab) * t) / 255u;
    return (r << 16) | (g << 8) | bl;
}

u32 contrast_text(u32 bg)
{
    u32 r = (bg >> 16) & 0xffu;
    u32 g = (bg >> 8) & 0xffu;
    u32 b = bg & 0xffu;
    u32 lum = r * 299u + g * 587u + b * 114u;
    return lum > 140000u ? 0x000000u : 0xffffffu;
}

void px_set(long x, long y, u32 color)
{
    surface_put(&g_surface, (int)x, (int)y, color);
}

void px_blend(long x, long y, u32 color, u32 alpha)
{
    surface_blend(&g_surface, (int)x, (int)y, color,
                                (u8)alpha);
}

void solid(long x, long y, long w, long h, u32 color)
{
    surface_set_color(&g_surface, color);
    surface_fill(&g_surface, (SurfaceRect){(int)x, (int)y,
                                                (int)w, (int)h});
}

void hline(long x, long y, long w, u32 color)
{
    solid(x, y, w, 1, color);
}

void vline(long x, long y, long h, u32 color)
{
    solid(x, y, 1, h, color);
}

void hline_alpha(long x, long y, long w, u32 color, u32 alpha)
{
    u8 old = g_surface.opacity;
    surface_set_color(&g_surface, color);
    surface_set_opacity(&g_surface, (u8)alpha);
    surface_fill(&g_surface, (SurfaceRect){(int)x, (int)y, (int)w, 1});
    surface_set_opacity(&g_surface, old);
}

void round_rect(long x, long y, long w, long h, long r, u32 color)
{
    surface_set_color(&g_surface, color);
    surface_fill_round(&g_surface, (SurfaceRect){(int)x, (int)y,
                                                        (int)w, (int)h},
                                  (int)r);
}

void round_rect_alpha(long x, long y, long w, long h, long r,
                             u32 color, u32 alpha)
{
    u8 old = g_surface.opacity;
    surface_set_color(&g_surface, color);
    surface_set_opacity(&g_surface, (u8)alpha);
    surface_fill_round(&g_surface, (SurfaceRect){(int)x, (int)y,
                                                        (int)w, (int)h},
                                  (int)r);
    surface_set_opacity(&g_surface, old);
}

void disc(long cx, long cy, long r, u32 color)
{
    surface_set_color(&g_surface, color);
    surface_circle(&g_surface, (int)cx, (int)cy, (int)r);
}

void disc_alpha(long cx, long cy, long r, u32 color, u32 alpha)
{
    u8 old = g_surface.opacity;
    surface_set_color(&g_surface, color);
    surface_set_opacity(&g_surface, (u8)alpha);
    surface_circle(&g_surface, (int)cx, (int)cy, (int)r);
    surface_set_opacity(&g_surface, old);
}

void ring(long cx, long cy, long r, long thick, u32 color)
{
    long yy, xx, inner = r - thick;
    for (yy = -r; yy <= r; yy++)
        for (xx = -r; xx <= r; xx++) {
            long d2 = xx * xx + yy * yy;
            if (d2 <= r * r && d2 >= inner * inner)
                px_set(cx + xx, cy + yy, color);
        }
}

void line(long x0, long y0, long x1, long y1, u32 color)
{
    surface_set_color(&g_surface, color);
    surface_line(&g_surface, (int)x0, (int)y0, (int)x1, (int)y1);
}

/* --- 8x8 text --------------------------------------------------------- */

u64 text8_width(const char *s, u64 sc)
{
    if (sc < 1) sc = 1;
    if (sc > 4) sc = 4;
    return nstrlen(s) * 8u * sc;
}

void text8_max(const char *s, long x, long y, u64 sc, u32 fg, long maxw)
{
    long cx = x;
    if (sc < 1) sc = 1;
    if (sc > 4) sc = 4;
    if (maxw <= 0)
        maxw = (long)g_pw - x;
    while (*s) {
        unsigned char ch = (unsigned char)*s;
        const unsigned char *g;
        int r, c;
        u64 ky, kx;
        if (ch < 32 || ch > 127)
            ch = '?';
        if (cx - x + (long)(8u * sc) > maxw)
            break;
        g = font8x8_basic[ch];
        for (r = 0; r < 8; r++) {
            unsigned char bits = g[r];
            for (c = 0; c < 8; c++) {
                if (!(bits & (unsigned char)(1u << c)))
                    continue;
                for (ky = 0; ky < sc; ky++)
                    for (kx = 0; kx < sc; kx++)
                        px_set(cx + (long)((u64)c * sc + kx),
                               y + (long)((u64)r * sc + ky), fg);
            }
        }
        cx += (long)(8u * sc);
        s++;
    }
}

void text8(const char *s, long x, long y, u64 sc, u32 fg)
{
    text8_max(s, x, y, sc, fg, (long)g_pw - x);
}

void text8_bold(const char *s, long x, long y, u64 sc, u32 fg)
{
    long cx = x;
    long maxw = (long)g_pw - x;

    if (sc < 1) sc = 1;
    if (sc > 4) sc = 4;
    while (*s) {
        unsigned char ch = (unsigned char)*s;
        const unsigned char *g;
        int r, c;
        u64 ky, kx;
        if (ch < 32 || ch > 127)
            ch = '?';
        if (cx - x + (long)(8u * sc) > maxw)
            break;
        g = font8x8_basic[ch];
        for (r = 0; r < 8; r++) {
            unsigned char bits = g[r];
            for (c = 0; c < 8; c++) {
                if (!(bits & (unsigned char)(1u << c)))
                    continue;
                for (ky = 0; ky < sc; ky++) {
                    for (kx = 0; kx < sc; kx++)
                        px_set(cx + (long)((u64)c * sc + kx),
                               y + (long)((u64)r * sc + ky), fg);
                    /* Embolden by one column inside the 8px cell instead of
                     * shifting the whole glyph or doubling the stroke at 2x. */
                    if (c < 7 && !(bits & (unsigned char)(1u << (c + 1))))
                        px_set(cx + (long)((u64)(c + 1) * sc),
                               y + (long)((u64)r * sc + ky), fg);
                }
            }
        }
        cx += (long)(8u * sc);
        s++;
    }
}

void text8_right(const char *s, long right, long y, u64 sc, u32 fg)
{
    long w = (long)text8_width(s, sc);
    text8(s, right - w, y, sc, fg);
}

void text8_center(const char *s, long cx, long y, u64 sc, u32 fg)
{
    long w = (long)text8_width(s, sc);
    text8(s, cx - w / 2, y, sc, fg);
}

/* --- number formatting ------------------------------------------------ */

char *put_u64(char *dst, u64 v)
{
    char d[20];
    int n = 0;
    do {
        d[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && n < (int)sizeof(d));
    while (n)
        *dst++ = d[--n];
    return dst;
}

char *put_2d(char *dst, u64 v)
{
    *dst++ = (char)('0' + (v / 10u) % 10u);
    *dst++ = (char)('0' + v % 10u);
    return dst;
}

void fmt_uptime(char *out, u64 sec)
{
    out = put_u64(out, sec / 3600u);
    *out++ = ':';
    out = put_2d(out, (sec / 60u) % 60u);
    *out++ = ':';
    out = put_2d(out, sec % 60u);
    *out = '\0';
}

void fmt_clock(char *out, const struct nsh_time *t)
{
    out = put_2d(out, t->hour);
    *out++ = ':';
    out = put_2d(out, t->min);
    *out = '\0';
}

const char *const MONTHS[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

void fmt_date(char *out, const struct nsh_time *t)
{
    out = put_2d(out, t->day);
    *out++ = ' ';
    if (t->mon >= 1 && t->mon <= 12)
        nstrcpy(out, MONTHS[t->mon - 1]);
    else
        nstrcpy(out, "---");
}

void draw_keycap(const char *label, long x, long y)
{
    UiButton b;
    long w = (long)text8_width(label, 1) + 14;
    ui_button_init(&b, (SurfaceRect){(int)x, (int)y, (int)w, 20}, label);
    ui_button_draw(&b);
}

void gradient_rect(long x, long y, long w, long h,
                   u32 top_rgb, u32 bottom_rgb)
{
    long row;

    for (row = 0; row < h; row++) {
        u32 weight = h > 1 ? (u32)((row * 255) / (h - 1)) : 0u;
        hline(x, y + row, w, rgb_lerp(top_rgb, bottom_rgb, weight));
    }
}

void round_top_gradient(long x, long y, long w, long h, long r,
                        u32 top_rgb, u32 bottom_rgb)
{
    long row;

    for (row = 0; row < h; row++) {
        long inset = 0;
        u32 t = h > 1 ? (u32)((row * 255) / (h - 1)) : 0u;
        if (r > 0 && row < r) {
            long dy = r - 1 - row;
            inset = r - (long)isqrt32((u32)(r * r - dy * dy));
        }
        hline(x + inset, y + row, w - 2 * inset,
              rgb_lerp(top_rgb, bottom_rgb, t));
    }
}
