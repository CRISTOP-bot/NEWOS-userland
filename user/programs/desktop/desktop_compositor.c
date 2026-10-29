/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_compositor.c
 * Description: Damage-based compositor.
 * Added: 2026-10-22
 */
/* Damage-based compositor.
 *
 * The desktop still renders into one XRGB canvas, but it no longer submits a
 * full frame for every cursor move. Callers mark the rectangles they changed;
 * desktop_present() repaints only that region, in z-order, and uploads it in
 * small row chunks through SYS_FBWRITE.
 */

#include "desktop.h"

static SurfaceRect damage_rect;
static int damage_active;
static u32 blit_buf[DESK_MAX_W * 16];

/* The cursor is a compositor overlay, never part of `canvas`.  Keeping it
 * out of the scene buffer means an old cursor can never survive a partial
 * repaint: the damage pass always uploads clean wallpaper/window pixels and
 * the current cursor is stamped into the staging row only afterwards. */
static const u16 cursor_bits[16] = {
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xFFC0, 0xFE00, 0xD800, 0x8C00, 0x0C00, 0x0600, 0x0000
};
static long cursor_x = -1, cursor_y = -1;
static int cursor_visible;

static long min_long(long a, long b) { return a < b ? a : b; }
static long max_long(long a, long b) { return a > b ? a : b; }

/* Stamp one cursor pass into the staging buffer for a row chunk.  The
 * shadow pass runs first so a later white pixel can cover its own shadow,
 * exactly like the old canvas painter, but without touching `canvas`. */
static void cursor_pass(const SurfaceRect *d, long row, long rows, int shadow)
{
    long r, c;

    if (!cursor_visible)
        return;
    for (r = 0; r < 16; r++) {
        for (c = 0; c < 16; c++) {
            long ax, ay, lx, ly;
            if (!(cursor_bits[r] & (u16)(1u << c)))
                continue;
            ax = cursor_x + c + (shadow ? 1 : 0);
            ay = cursor_y + r + (shadow ? 1 : 0);
            if (ax < 0 || ay < 0 || ax >= (long)g_pw || ay >= (long)g_ph)
                continue;
            lx = ax - d->x;
            ly = ay - (d->y + row);
            if (lx < 0 || lx >= d->width || ly < 0 || ly >= rows)
                continue;
            blit_buf[ly * d->width + lx] = shadow ? 0x000000u : 0xffffffu;
        }
    }
}

void desktop_damage(long x, long y, long w, long h)
{
    long x0, y0, x1, y1;

    if (w <= 0 || h <= 0 || !g_pw || !g_ph)
        return;
    x0 = max_long(0, x);
    y0 = max_long(0, y);
    x1 = min_long((long)g_pw, x + w);
    y1 = min_long((long)g_ph, y + h);
    if (x0 >= x1 || y0 >= y1)
        return;

    if (!damage_active) {
        damage_rect.x = (int)x0;
        damage_rect.y = (int)y0;
        damage_rect.width = (int)(x1 - x0);
        damage_rect.height = (int)(y1 - y0);
        damage_active = 1;
        return;
    }

    x0 = min_long(x0, damage_rect.x);
    y0 = min_long(y0, damage_rect.y);
    x1 = max_long(x1, damage_rect.x + damage_rect.width);
    y1 = max_long(y1, damage_rect.y + damage_rect.height);
    x1 = min_long(x1, (long)g_pw);
    y1 = min_long(y1, (long)g_ph);
    damage_rect.x = (int)x0;
    damage_rect.y = (int)y0;
    damage_rect.width = (int)(x1 - x0);
    damage_rect.height = (int)(y1 - y0);
}

void desktop_damage_all(void)
{
    desktop_damage(0, 0, (long)g_pw, (long)g_ph);
}

/* Mark the chrome under a point, so hover state and the cursor are repainted
 * without touching unrelated windows. */
void desktop_damage_at(long x, long y)
{
    long dx, dy, dw, dh;
    int i;

    if (y < (long)PANEL_H) {
        desktop_damage(0, 0, (long)g_pw, (long)PANEL_H);
        return;
    }

    taskbar_rect(&dx, &dy, &dw, &dh);
    if (x >= dx && x < dx + dw && y >= dy && y < dy + dh) {
        desktop_damage(dx, dy, dw, dh);
        return;
    }

    if (sm_open) {
        start_menu_rect(&dx, &dy, &dw, &dh);
        if (x >= dx && x < dx + dw && y >= dy && y < dy + dh) {
            desktop_damage(dx - 4, dy - 4, dw + 8, dh + 8);
            return;
        }
    }

    for (i = 0; i < (int)MAX_W; i++) {
        if (!WOPEN[i] || WMIN[i])
            continue;
        if (x >= WX[i] - 8 && x < WX[i] + WW[i] + 8 &&
            y >= WY[i] - 8 && y < WY[i] + WH[i] + 8) {
            desktop_damage(WX[i] - 10, WY[i] - 10,
                           WW[i] + 20, WH[i] + 20);
            return;
        }
    }
}

static void blit_damage(const SurfaceRect *d)
{
    long row;

    for (row = 0; row < d->height; row += 16) {
        long rows = d->height - row;
        long r, x;
        struct nsh_fbwrite rq;

        if (rows > 16)
            rows = 16;
        for (r = 0; r < rows; r++) {
            const u32 *src = canvas + (u64)(d->y + row + r) * g_pw + d->x;
            u32 *dst = blit_buf + (u64)r * d->width;
            for (x = 0; x < d->width; x++)
                dst[x] = src[x];
        }
        cursor_pass(d, row, rows, 1);
        cursor_pass(d, row, rows, 0);
        rq.x = (u64)d->x;
        rq.y = (u64)(d->y + row);
        rq.w = (u64)d->width;
        rq.h = (u64)rows;
        rq.pixels = (u64)blit_buf;
        rq.pixlen = (u64)rows * (u64)d->width * sizeof(blit_buf[0]);
        (void)sys_fbwrite(&rq);
    }
}

void desktop_present(void)
{
    SurfaceRect d;
    int old_has_clip;
    SurfaceRect old_clip;
    int i;
    int moved = g_mx != cursor_x || g_my != cursor_y;
    int new_visible = g_mx >= 0 && g_my >= 0;

    /* The compositor owns the old-cursor damage.  Desktop code may also mark
     * it, but this check is the backstop that makes a missed caller damage
     * harmless: the old rectangle is always re-uploaded from clean canvas. */
    if (moved) {
        if (cursor_visible)
            desktop_damage(cursor_x - 1, cursor_y - 1, 18, 18);
        if (new_visible)
            desktop_damage(g_mx - 1, g_my - 1, 18, 18);
    } else if (new_visible && !cursor_visible) {
        desktop_damage(g_mx - 1, g_my - 1, 18, 18);
    }

    if (!damage_active) {
        cursor_x = g_mx;
        cursor_y = g_my;
        cursor_visible = new_visible;
        return;
    }

    d = damage_rect;
    damage_active = 0;

    old_has_clip = g_surface.has_clip;
    old_clip = g_surface.clip;
    surface_set_clip(&g_surface, d);

    draw_wallpaper_rect(d);
    for (i = (int)MAX_W - 1; i >= 0; i--)
        draw_window(i);
    draw_taskbar();
    draw_panel();
    draw_start_menu();
    draw_toast();
    draw_help();

    cursor_x = g_mx;
    cursor_y = g_my;
    cursor_visible = new_visible;

    if (old_has_clip)
        surface_set_clip(&g_surface, old_clip);
    else
        surface_clear_clip(&g_surface);

    blit_damage(&d);
}
