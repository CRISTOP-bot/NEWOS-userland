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

/* --- icons ------------------------------------------------------------ */

/* Windows 95 / NEWOS icon palette. The desktop keeps its dark window
 * chrome, but every pictogram uses the classic beveled face so the icon
 * family reads as one system. */
#define W95_BLACK   0x000000u
#define W95_FACE    0x00c0c0c0u
#define W95_LIGHT   0x00ffffffu
#define W95_SHADOW  0x00808080u
#define W95_DARK    0x00404040u
#define W95_NAVY    0x0029305du
#define W95_BLUE    0x004a4dc8u
#define W95_CYAN    0x0035b6c8u
#define W95_GREEN   0x0035a853u
#define W95_LIME    0x007bd45au
#define W95_YELLOW  0x00ffd23fu
#define W95_GOLD    0x00dc8621u
#define W95_ORANGE  0x00ff7a3cu
#define W95_RED     0x00c42b1cu
#define W95_PURPLE  0x008b5cf6u
#define W95_WHITE   0x00f4f5fau

void w95_bevel(long x, long y, long w, long h, u32 face)
{
    solid(x, y, w, h, face);
    hline(x, y, w, W95_LIGHT);
    vline(x, y, h, W95_LIGHT);
    hline(x, y + h - 1, w, W95_BLACK);
    vline(x + w - 1, y, h, W95_BLACK);
    hline(x + 1, y + h - 2, w - 2, W95_SHADOW);
    vline(x + w - 2, y + 1, h - 2, W95_SHADOW);
}

/* Sunken/pressed bevel: top and left edges go dark, bottom and right go
 * light — the exact inverse of w95_bevel. Used for the pressed START
 * button, the active task button and the clock tray well. */
void w95_bevel_pressed(long x, long y, long w, long h, u32 face)
{
    solid(x, y, w, h, face);
    hline(x, y, w, W95_SHADOW);
    vline(x, y, h, W95_SHADOW);
    hline(x, y + h - 1, w, W95_LIGHT);
    vline(x + w - 1, y, h, W95_LIGHT);
}

void w95_screen(long x, long y, long w, long h)
{
    solid(x, y, w, h, W95_BLACK);
    hline(x + 1, y + 1, w - 2, 0x00102040u);
    hline(x + 1, y + h - 2, w - 2, 0x00000818u);
}

void draw_icon(int kind, long x, long y, long s, u32 color)
{
    long h = s / 2;
    long q = s / 4;

    switch (kind) {
    case ICON_LOGO: {
        /* Four-pane Win95 flag, recolored with the NEWOS rainbow. */
        long gap = s >= 14 ? 2 : 1;
        long pw = (s - gap) / 2;
        long ph = (s - gap) / 2;
        long ox = x + (s - (pw * 2 + gap)) / 2;
        long oy = y + (s - (ph * 2 + gap)) / 2;
        solid(ox, oy, pw * 2 + gap, ph * 2 + gap, W95_BLACK);
        solid(ox + 1, oy + 1, pw - 1, ph - 1, W95_ORANGE);
        solid(ox + pw + gap, oy + 1, pw - 1, ph - 1, W95_LIME);
        solid(ox + 1, oy + ph + gap, pw - 1, ph - 1, W95_CYAN);
        solid(ox + pw + gap, oy + ph + gap, pw - 1, ph - 1, W95_BLUE);
        hline(ox + 1, oy + 1, pw - 1, 0x00ffb45au);
        hline(ox + pw + gap, oy + 1, pw - 1, 0x00d7ff8au);
        hline(ox + 1, oy + ph + gap, pw - 1, 0x008af0ffu);
        hline(ox + pw + gap, oy + ph + gap, pw - 1, 0x008f93ffu);
        break;
    }
    case ICON_SYS:
        w95_bevel(x, y, s, h + q, W95_FACE);
        w95_screen(x + 2, y + 2, s - 4, h + q - 4);
        if (s >= 16) {
            text8(">", x + 4, y + 3, 1, W95_WHITE);
            text8("_", x + 11, y + 3, 1, W95_YELLOW);
        } else {
            solid(x + 4, y + 4, 3, 1, W95_WHITE);
            solid(x + 6, y + 6, 2, 1, W95_WHITE);
            solid(x + 9, y + 7, 3, 1, W95_YELLOW);
        }
        solid(x + h - q / 2, y + h + q, q * 2, s - (h + q), W95_FACE);
        solid(x + h - q / 2, y + s - 1, q * 2, 1, W95_BLACK);
        break;
    case ICON_FILES:
        solid(x + 1, y + 2, s / 2, q, W95_GOLD);
        w95_bevel(x, y + q, s, s - q, W95_YELLOW);
        solid(x + 2, y + q + 2, s - 4, 2, 0x00ffe98au);
        hline(x + 2, y + s - 4, s - 4, W95_GOLD);
        break;
    case ICON_MOUSE:
        w95_bevel(x + q, y, s - q * 2, s, W95_FACE);
        solid(x + q + 1, y + 1, s / 2 - 1, h - 1, W95_LIGHT);
        solid(x + s / 2 + 1, y + 1, s - q - s / 2 - 2, h - 1, W95_FACE);
        vline(x + s / 2, y + 1, h - 1, W95_SHADOW);
        hline(x + q + 1, y + h, s - q * 2 - 2, W95_SHADOW);
        break;
    case ICON_INFO:
        disc(x + h, y + h, h - 1, W95_NAVY);
        disc(x + h, y + h, h - 2, W95_BLUE);
        if (s >= 12) {
            text8_center("?", x + h, y + h - 4, 1, W95_WHITE);
        } else {
            solid(x + h - 1, y + 2, 2, 2, W95_WHITE);
            solid(x + h - 1, y + 5, 2, s - 7, W95_WHITE);
        }
        break;
    case ICON_LOG:
        w95_bevel(x + 1, y, s - 2, s, W95_WHITE);
        solid(x + 1, y + 1, 3, s - 2, W95_FACE);
        for (long ly = y + 4; ly < y + s - 3; ly += 3)
            hline(x + 5, ly, s - 8, W95_BLUE);
        break;
    case ICON_TERM:
        w95_bevel(x, y, s, s - q, W95_FACE);
        w95_screen(x + 2, y + 2, s - 4, s - q - 4);
        if (s >= 14) {
            text8(">", x + 3, y + 3, 1, W95_LIME);
            text8("_", x + 10, y + 3, 1, W95_WHITE);
        } else {
            solid(x + 3, y + 4, 3, 1, W95_LIME);
            solid(x + 5, y + 6, 2, 1, W95_LIME);
            solid(x + 8, y + 7, 3, 1, W95_WHITE);
        }
        solid(x + h - 1, y + s - q, 2, q, W95_FACE);
        hline(x + h - q, y + s - 1, q * 2, W95_BLACK);
        break;
    case ICON_HELP:
        w95_bevel(x, y + 1, s, s - 1, W95_YELLOW);
        solid(x + 2, y + 2, s - 4, 2, W95_WHITE);
        disc(x + h, y + h, h - 2, W95_NAVY);
        if (s >= 12)
            text8_center("?", x + h, y + h - 4, 1, W95_WHITE);
        else
            solid(x + h - 1, y + h - 2, 2, 2, W95_WHITE);
        break;
    case ICON_THEME:
        w95_bevel(x, y + 1, s, s - 2, W95_FACE);
        disc(x + q + 1, y + h - 1, q - 1, W95_RED);
        disc(x + h + 1, y + h - 1, q - 1, W95_YELLOW);
        disc(x + s - q - 1, y + h - 1, q - 1, W95_BLUE);
        disc(x + h, y + q + 1, q - 1, W95_GREEN);
        break;
    case ICON_POWER:
        disc(x + h, y + h, h - 1, W95_RED);
        disc(x + h, y + h, h - 2, 0x00e05040u);
        solid(x + h - 1, y + 2, 2, h, W95_WHITE);
        ring(x + h, y + h, h - 3, 2, W95_WHITE);
        break;
    case ICON_IMG:
        w95_bevel(x, y + 1, s, s - 2, W95_FACE);
        w95_screen(x + 2, y + 3, s - 4, s - 6);
        solid(x + 3, y + s - 5, s - 6, 2, W95_GREEN);
        for (long i = 0; i < q; i++) {
            solid(x + 4 + i, y + s - 5 - i, 2, 2, W95_LIME);
            solid(x + s - 6 - i, y + s - 5 - i, 2, 2, W95_LIME);
        }
        disc(x + s - 5, y + 5, 2, W95_YELLOW);
        break;
    case ICON_VID:
        w95_bevel(x, y + q, s, s - q * 2, W95_DARK);
        for (long fy = y + q + 2; fy < y + s - q - 2; fy += 4)
            for (long fx = x + 2; fx < x + 4; fx++)
                solid(fx, fy, 2, 2, W95_FACE);
        solid(x + h - 2, y + h - 4, 2, 8, W95_WHITE);
        for (long i = 0; i < 5; i++)
            solid(x + h + i, y + h - 4 + i, 2, 8 - i * 2, W95_WHITE);
        break;
    case ICON_EDIT:
        w95_bevel(x + 1, y, s - 2, s, W95_WHITE);
        for (long ly = y + 4; ly < y + s - 3; ly += 3)
            hline(x + 4, ly, s - 8, W95_BLUE);
        solid(x + s - 5, y + 2, 2, s - 6, W95_GOLD);
        solid(x + s - 6, y + s - 2, 4, 2, W95_ORANGE);
        break;
    case ICON_PKG:
        w95_bevel(x + 1, y + q, s - 2, s - q, W95_GOLD);
        solid(x + 1, y + q, s - 2, 3, 0x00f0a050u);
        solid(x + h - 1, y + q, 2, s - q, W95_YELLOW);
        hline(x + 1, y + q, s - 2, W95_BLACK);
        break;
    case ICON_CHIP:
        w95_bevel(x + q, y + q, s - q * 2, s - q * 2, W95_FACE);
        solid(x + q + 2, y + q + 2, s - q * 2 - 4, s - q * 2 - 4,
              W95_BLUE);
        for (long i = 0; i < 3; i++) {
            solid(x + q + 2 + i * (s - q * 2 - 4) / 2, y + 1, 2, q - 1,
                  W95_SHADOW);
            solid(x + q + 2 + i * (s - q * 2 - 4) / 2, y + s - q + 1,
                  2, q - 2, W95_SHADOW);
            solid(x + 1, y + q + 2 + i * (s - q * 2 - 4) / 2, q - 1, 2,
                  W95_SHADOW);
            solid(x + s - q + 1, y + q + 2 + i * (s - q * 2 - 4) / 2,
                  q - 2, 2, W95_SHADOW);
        }
        break;
    case ICON_DESKTOP:
        w95_bevel(x, y + 1, s, s - 2, W95_FACE);
        solid(x + 2, y + 3, s - 4, q, W95_BLUE);
        draw_icon(ICON_LOGO, x + 3, y + 3, q, W95_WHITE);
        w95_screen(x + 2, y + q + 4, s - 4, s - q - 7);
        break;
    case ICON_APP:
        w95_bevel(x, y + 1, s, s - 2, W95_FACE);
        solid(x + 2, y + 3, s - 4, q, W95_NAVY);
        w95_screen(x + 2, y + q + 4, s - 4, s - q - 7);
        break;
    case ICON_TEST:
        disc(x + h, y + h, h - 1, W95_GREEN);
        solid(x + q, y + h, 2, 2, W95_WHITE);
        solid(x + q + 2, y + h + 2, 2, 2, W95_WHITE);
        solid(x + q + 4, y + h, 2, 2, W95_WHITE);
        solid(x + q + 6, y + h - 2, 2, 2, W95_WHITE);
        solid(x + q + 8, y + h - 4, 2, 2, W95_WHITE);
        break;
    case ICON_MIN:
        solid(x, y + h - 1, s, 2, color);
        break;
    case ICON_MAX:
        for (long i = 0; i < s; i++) {
            px_set(x + i, y, color);
            px_set(x + i, y + s - 1, color);
            px_set(x, y + i, color);
            px_set(x + s - 1, y + i, color);
        }
        break;
    case ICON_CLOSE:
        line(x, y, x + s - 1, y + s - 1, color);
        line(x + s - 1, y, x, y + s - 1, color);
        break;
    default:
        break;
    }
}

int prog_icon(const char *name)
{
    if (!name) return ICON_APP;
    if (nstrcmp(name, "sh") == 0 || nstrcmp(name, "shell") == 0 ||
        nstrcmp(name, "ls") == 0 || nstrcmp(name, "cat") == 0 ||
        nstrcmp(name, "echo") == 0 || nstrcmp(name, "grep") == 0 ||
        nstrcmp(name, "find") == 0 || nstrcmp(name, "sort") == 0 ||
        nstrcmp(name, "wc") == 0 || nstrcmp(name, "head") == 0)
        return ICON_TERM;
    if (nstrcmp(name, "img") == 0) return ICON_IMG;
    if (nstrcmp(name, "vid") == 0) return ICON_VID;
    if (nstrcmp(name, "nano") == 0) return ICON_EDIT;
    if (nstrcmp(name, "newpkg") == 0) return ICON_PKG;
    if (nstrcmp(name, "free") == 0 || nstrcmp(name, "ps") == 0)
        return ICON_CHIP;
    if (nstrcmp(name, "desktop") == 0) return ICON_DESKTOP;
    if (nstrcmp(name, "glxgears") == 0) return ICON_VID;
    if (nstrcmp(name, "busybox") == 0) return ICON_TERM;
    if (nstrcmp(name, "about") == 0 || nstrcmp(name, "newfetch") == 0)
        return ICON_INFO;
    if (nstrcmp(name, "mkdir") == 0 || nstrcmp(name, "rmdir") == 0 ||
        nstrcmp(name, "rm") == 0 || nstrcmp(name, "mv") == 0 ||
        nstrcmp(name, "touch") == 0)
        return ICON_FILES;
    return ICON_APP;
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
