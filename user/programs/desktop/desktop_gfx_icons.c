/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_gfx_icons.c
 * Description: Win95-style icon set: palette, bevels and pictograms.
 * Added: 2026-10-30
 */
/* Win95-style icon set: palette, bevels and pictograms. */

#include "desktop.h"
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
