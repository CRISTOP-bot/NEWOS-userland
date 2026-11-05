/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_wallpaper.c
 * Description: /etc/wallpaper.bmp loader and sampler for the desktop.
 * Added: 2026-10-30
 */
/* Wallpaper plate: /etc/wallpaper.bmp, read once and sampled on demand.
 *
 * The plate ships as a brand asset in the only raster format the NEWOS codec
 * decodes (uncompressed 24-bit BMP with a BITMAPINFOHEADER), the same rules
 * the `img` viewer enforces. The file stays in its raw form in one heap block:
 * draw_wallpaper_rect() maps each damaged panel pixel to the nearest source
 * pixel, so a repaint pays for the rectangle it touches and never for a
 * full-size decode buffer.
 *
 * Every failure (no file, short read, unsupported header) leaves the loader
 * inactive and the shell keeps its procedural gradient wallpaper, so the
 * desktop still paints a complete background when the asset is missing.
 *
 * Integer-only by contract: userland is built -mgeneral-regs-only, so the
 * stretch is nearest neighbour and every multiply stays in 64-bit integers.
 */

#include "desktop.h"
#include <stdlib.h>
#include <string.h>

#define WP_PATH "/etc/wallpaper.bmp"
#define WP_MAX  (2u * 1024u * 1024u)  /* the brand plate is under 700 KiB */

static u8 *wp_data;                   /* raw file bytes, kept for sampling */
static u32 wp_w, wp_h;                /* plate geometry in pixels */
static u64 wp_off;                    /* offset of the first raster byte */
static u64 wp_rowbytes;               /* padded BMP row stride */
static u32 wp_bpp;                    /* 24 or 32 */

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
           ((u32)p[3] << 24);
}

static u16 rd16(const u8 *p)
{
    return (u16)((u32)p[0] | ((u32)p[1] << 8));
}

/* Validate a BMP header; 1 = 24/32-bit uncompressed, descriptor filled.
 * Mirrors bmp_open() in the img viewer, plus a size cap so a corrupt length
 * cannot ask the heap for more than the plate budget. */
static int wp_header(const u8 *d, u64 len, u32 *w, u32 *h, u64 *off,
                     u64 *rowbytes, u32 *bpp)
{
    u64 ww, hh;

    if (len < 54 || d[0] != 'B' || d[1] != 'M')
        return 0;
    if (rd32(d + 14) != 40)           /* BITMAPINFOHEADER only */
        return 0;
    ww = rd32(d + 18);
    hh = rd32(d + 22);
    if (!ww || !hh || ww > 4096u || hh > 4096u)
        return 0;
    if (rd16(d + 26) != 1)            /* one plane */
        return 0;
    *bpp = rd16(d + 28);
    if (*bpp != 24 && *bpp != 32)
        return 0;
    if (rd32(d + 30) != 0)            /* BI_RGB (uncompressed) only */
        return 0;
    *off = rd32(d + 10);
    *rowbytes = ((ww * *bpp + 31u) / 32u) * 4u;
    if (*off < 54 || *off + *rowbytes * hh > WP_MAX)
        return 0;
    *w = (u32)ww;
    *h = (u32)hh;
    return 1;
}

/* Read the whole plate into one heap block. Returns 0 on any failure. */
static int wp_read(long fd, const u8 *hdr, u64 total)
{
    u8 *buf;
    u64 got = 54;

    buf = malloc((size_t)total);
    if (!buf)
        return 0;
    memcpy(buf, hdr, 54);
    while (got < total) {
        u64 want = total - got;
        long n;

        if (want > 32768u)
            want = 32768u;
        n = sys_read((int)fd, buf + got, want);
        if (n <= 0)
            break;
        got += (u64)n;
    }
    if (got < total) {
        free(buf);
        return 0;
    }
    wp_data = buf;
    return 1;
}

void wallpaper_load(void)
{
    u8 hdr[54];
    long fd, n;

    if (wp_data)
        return;
    fd = sys_open(WP_PATH, O_READ);
    if (fd < 0)
        return;                       /* no plate: keep the gradient */
    n = sys_read((int)fd, hdr, sizeof(hdr));
    if (n != (long)sizeof(hdr) ||
        !wp_header(hdr, (u64)n, &wp_w, &wp_h, &wp_off, &wp_rowbytes,
                   &wp_bpp)) {
        sys_close((int)fd);
        nputs("desktop: " WP_PATH " is not a 24-bit BMP; using the built-in "
              "wallpaper\n");
        return;
    }
    if (!wp_read(fd, hdr, wp_off + wp_rowbytes * wp_h)) {
        sys_close((int)fd);
        nputs("desktop: " WP_PATH " is truncated; using the built-in "
              "wallpaper\n");
        return;
    }
    sys_close((int)fd);
}

void wallpaper_free(void)
{
    free(wp_data);
    wp_data = NULL;
    wp_w = wp_h = 0;
}

int wallpaper_active(void)
{
    return wp_data != NULL;
}

/* Nearest-neighbour sample of the plate at panel coordinates. BMP raster
 * rows are stored bottom-up, hence (wp_h - 1 - sy). The result carries no
 * alpha: the surface treats XRGB zero alpha as opaque. */
u32 wallpaper_pixel(long x, long y)
{
    u64 sx, sy;
    const u8 *p;

    if (!wp_data || !g_pw || !g_ph)
        return TH.bg0;
    sx = (u64)x * wp_w / g_pw;
    sy = (u64)y * wp_h / g_ph;
    if (sx >= wp_w)
        sx = wp_w - 1;
    if (sy >= wp_h)
        sy = wp_h - 1;
    p = wp_data + wp_off + (wp_h - 1 - sy) * wp_rowbytes +
        sx * (wp_bpp / 8u);
    return ((u32)p[2] << 16) | ((u32)p[1] << 8) | (u32)p[0];
}
