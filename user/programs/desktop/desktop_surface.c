/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_surface.c
 * Description: Clipped XRGB drawing surface for the NEWOS desktop.
 * Added: 2026-10-27
 */
/* Clipped XRGB drawing surface for the NEWOS desktop. */

#include "desktop_surface.h"

static int surf_abs(int v) { return v < 0 ? -v : v; }

static u32 surf_sqrt(u32 v)
{
    u32 r = 0;
    while (r < 46340u && (r + 1u) * (r + 1u) <= v)
        r++;
    return r;
}

static int surf_visible(const Surface *s, int x, int y)
{
    if (!s || !s->pixels)
        return 0;
    if (x < 0 || y < 0 || x >= s->width || y >= s->height)
        return 0;
    if (!s->has_clip)
        return 1;
    return x >= s->clip.x && y >= s->clip.y &&
           x < s->clip.x + s->clip.width &&
           y < s->clip.y + s->clip.height;
}

void surface_init(Surface *s, u32 *pixels, int width, int height)
{
    if (!s)
        return;
    s->pixels = pixels;
    s->width = width;
    s->height = height;
    s->color = 0xffffffu;
    s->stroke = 1;
    s->opacity = 255;
    s->has_clip = 0;
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.width = width;
    s->clip.height = height;
}

void surface_set_color(Surface *s, u32 rgb)
{
    if (s)
        s->color = rgb;
}

void surface_set_stroke(Surface *s, int width)
{
    if (s)
        s->stroke = width < 1 ? 1 : width;
}

void surface_set_opacity(Surface *s, u8 opacity)
{
    if (s)
        s->opacity = opacity;
}

void surface_set_clip(Surface *s, SurfaceRect rect)
{
    if (!s)
        return;
    s->has_clip = 1;
    s->clip = rect;
}

void surface_clear_clip(Surface *s)
{
    if (s)
        s->has_clip = 0;
}

void surface_blend(Surface *s, int x, int y, u32 rgb, u8 alpha)
{
    u32 *dst, dr, dg, db, sr, sg, sb, inv;

    if (!surf_visible(s, x, y) || alpha == 0)
        return;
    dst = &s->pixels[y * s->width + x];
    if (alpha == 255) {
        *dst = rgb & 0xffffffu;
        return;
    }
    inv = 255u - alpha;
    dr = (*dst >> 16) & 0xffu;
    dg = (*dst >> 8) & 0xffu;
    db = *dst & 0xffu;
    sr = (rgb >> 16) & 0xffu;
    sg = (rgb >> 8) & 0xffu;
    sb = rgb & 0xffu;
    dr = (sr * alpha + dr * inv + 127u) / 255u;
    dg = (sg * alpha + dg * inv + 127u) / 255u;
    db = (sb * alpha + db * inv + 127u) / 255u;
    *dst = (dr << 16) | (dg << 8) | db;
}

void surface_put(Surface *s, int x, int y, u32 rgb)
{
    u32 alpha;

    if (!s)
        return;
    alpha = (rgb >> 24) & 0xffu;
    if (alpha == 0)                     /* XRGB: zero alpha means opaque */
        alpha = 255;
    if (s->opacity != 255)
        alpha = (alpha * s->opacity + 127u) / 255u;
    surface_blend(s, x, y, rgb, (u8)alpha);
}

void surface_line(Surface *s, int x0, int y0, int x1, int y1)
{
    int dx, dy, sx, sy, err;

    if (!s)
        return;
    dx = surf_abs(x1 - x0);
    dy = -surf_abs(y1 - y0);
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        int e2, step;
        for (step = 0; step < s->stroke; step++)
            surface_put(s, x0 + step, y0, s->color);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void surface_fill(Surface *s, SurfaceRect rect)
{
    int x0, y0, x1, y1, x, y;
    u32 alpha;

    if (!s || !s->pixels || rect.width <= 0 || rect.height <= 0)
        return;
    x0 = rect.x;
    y0 = rect.y;
    x1 = rect.x + rect.width;
    y1 = rect.y + rect.height;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->width) x1 = s->width;
    if (y1 > s->height) y1 = s->height;
    if (s->has_clip) {
        if (x0 < s->clip.x) x0 = s->clip.x;
        if (y0 < s->clip.y) y0 = s->clip.y;
        if (x1 > s->clip.x + s->clip.width) x1 = s->clip.x + s->clip.width;
        if (y1 > s->clip.y + s->clip.height) y1 = s->clip.y + s->clip.height;
    }
    if (x0 >= x1 || y0 >= y1)
        return;

    alpha = (s->color >> 24) & 0xffu;
    if ((alpha == 0 || alpha == 255) && s->opacity == 255) {
        u32 c = s->color & 0xffffffu;
        for (y = y0; y < y1; y++) {
            u32 *row = &s->pixels[y * s->width];
            for (x = x0; x < x1; x++)
                row[x] = c;
        }
        return;
    }
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            surface_put(s, x, y, s->color);
}

static int surf_round_inset(int row, int height, int radius)
{
    int dy;

    if (radius <= 0)
        return 0;
    if (row < radius) {
        dy = radius - 1 - row;
        return radius - (int)surf_sqrt((u32)(radius * radius - dy * dy));
    }
    if (row >= height - radius) {
        dy = row - (height - radius);
        return radius - (int)surf_sqrt((u32)(radius * radius - dy * dy));
    }
    return 0;
}

static int surf_round_base(SurfaceRect *rect, int radius)
{
    if (radius < 0)
        radius = 0;
    if (radius * 2 > rect->width)
        radius = rect->width / 2;
    if (radius * 2 > rect->height)
        radius = rect->height / 2;
    return radius;
}

void surface_fill_round(Surface *s, SurfaceRect rect, int radius)
{
    int row;

    if (!s || rect.width <= 0 || rect.height <= 0)
        return;
    radius = surf_round_base(&rect, radius);
    for (row = 0; row < rect.height; row++) {
        int inset = surf_round_inset(row, rect.height, radius);
        surface_fill(s, (SurfaceRect){rect.x + inset, rect.y + row,
                                      rect.width - 2 * inset, 1});
    }
}

void surface_stroke_round(Surface *s, SurfaceRect rect, int radius)
{
    int row;

    if (!s || rect.width <= 0 || rect.height <= 0)
        return;
    radius = surf_round_base(&rect, radius);
    for (row = 0; row < rect.height; row++) {
        int inset = surf_round_inset(row, rect.height, radius);
        surface_put(s, rect.x + inset, rect.y + row, s->color);
        surface_put(s, rect.x + rect.width - 1 - inset, rect.y + row,
                    s->color);
    }
    surface_fill(s, (SurfaceRect){rect.x + radius, rect.y,
                                  rect.width - 2 * radius, 1});
    surface_fill(s, (SurfaceRect){rect.x + radius,
                                  rect.y + rect.height - 1,
                                  rect.width - 2 * radius, 1});
}

void surface_circle(Surface *s, int cx, int cy, int r)
{
    int yy, xx;

    if (!s || r < 0)
        return;
    for (yy = -r; yy <= r; yy++)
        for (xx = -r; xx <= r; xx++)
            if (xx * xx + yy * yy <= r * r)
                surface_put(s, cx + xx, cy + yy, s->color);
}

void surface_clear(Surface *s, u32 rgb)
{
    u32 old;

    if (!s)
        return;
    old = s->color;
    s->color = rgb;
    surface_clear_clip(s);
    surface_fill(s, (SurfaceRect){0, 0, s->width, s->height});
    s->color = old;
}
