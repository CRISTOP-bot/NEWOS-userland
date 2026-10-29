/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_surface.h
 * Description: Clipped XRGB drawing surface for the NEWOS desktop.
 * Added: 2026-10-27
 */
/* Clipped XRGB drawing surface for the NEWOS desktop.
 *
 * One caller-owned canvas, one optional clip rectangle, integer-only. This is
 * the desktop's own drawing layer: no widget toolkit, no allocation and no
 * floating point so it stays inside the -mgeneral-regs-only userland ABI.
 */

#ifndef NEWOS_DESKTOP_SURFACE_H
#define NEWOS_DESKTOP_SURFACE_H

#include "../../lib/nshlib.h"

typedef struct {
    int x, y;
    int width, height;
} SurfaceRect;

typedef struct {
    u32        *pixels;
    int         width, height;
    u32         color;
    int         stroke;
    u8          opacity;      /* 0..255, 255 = opaque */
    int         has_clip;
    SurfaceRect clip;
} Surface;

void surface_init(Surface *s, u32 *pixels, int width, int height);
void surface_set_color(Surface *s, u32 rgb);
void surface_set_stroke(Surface *s, int width);
void surface_set_opacity(Surface *s, u8 opacity);
void surface_set_clip(Surface *s, SurfaceRect rect);
void surface_clear_clip(Surface *s);

void surface_put(Surface *s, int x, int y, u32 rgb);
void surface_blend(Surface *s, int x, int y, u32 rgb, u8 alpha);
void surface_line(Surface *s, int x0, int y0, int x1, int y1);
void surface_fill(Surface *s, SurfaceRect rect);
void surface_fill_round(Surface *s, SurfaceRect rect, int radius);
void surface_stroke_round(Surface *s, SurfaceRect rect, int radius);
void surface_circle(Surface *s, int cx, int cy, int r);
void surface_clear(Surface *s, u32 rgb);

#endif /* NEWOS_DESKTOP_SURFACE_H */
