/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_shell.c
 * Description: Panel, dock, start menu, wallpapers, overlays, log and cursor.
 * Added: 2026-10-22
 */
/* Panel, dock, start menu, wallpapers, overlays, log and cursor. */

#include "desktop.h"

void log_put(const char *msg, u32 color)
{
    int idx = log_next;
    struct nsh_time t;
    char *p;
    log_next = (log_next + 1) % LOG_LINES;
    log_total++;
    if (sys_gettime(&t) == 0) {
        p = put_2d(log_time[idx], t.hour);
        *p++ = ':';
        p = put_2d(p, t.min);
        *p = '\0';
    } else {
        nstrcpy(log_time[idx], "--:--");
    }
    nstrcpy(log_msg[idx], msg);
    log_color[idx] = color;
}

void log_put2(const char *a, const char *b, u32 color)
{
    char buf[96];
    nstrcpy(buf, a);
    nstrcat(buf, b);
    log_put(buf, color);
}

void set_toast(const char *msg, u32 color)
{
    nstrcpy(toast_text, msg);
    toast_color = color;
    toast_ttl = 60;
}

static void shard_fill(SurfaceRect clip, long y0, long y1, long xt, long xb,
                      long wt, long wb, u32 color, u32 alpha)
{
    long y, span = y1 - y0;
    long ya, yb;

    if (span <= 0)
        return;
    ya = y0 > clip.y ? y0 : clip.y;
    yb = y1 < clip.y + clip.height ? y1 : clip.y + clip.height;
    color = rgb_lerp(TH.bg1, color, alpha);
    for (y = ya; y < yb; y++) {
        long t = (y - y0) * 256 / span;
        long x = xt + (xb - xt) * t / 256;
        long w = wt + (wb - wt) * t / 256;
        long x0 = x > clip.x ? x : clip.x;
        long x1 = x + w < clip.x + clip.width ? x + w :
                                                  clip.x + clip.width;
        if (x1 > x0)
            hline(x0, y, x1 - x0, color);
    }
}

void draw_wallpaper_rect(SurfaceRect r)
{
    long y, x;

    if (r.width <= 0 || r.height <= 0)
        return;
    if (r.x < 0) { r.width += r.x; r.x = 0; }
    if (r.y < 0) { r.height += r.y; r.y = 0; }
    if (r.x + r.width > (int)g_pw) r.width = (int)g_pw - r.x;
    if (r.y + r.height > (int)g_ph) r.height = (int)g_ph - r.y;
    if (r.width <= 0 || r.height <= 0)
        return;

    /* The brand plate wins over the procedural background whenever
     * /etc/wallpaper.bmp decoded: it is stretched to the panel with
     * nearest-neighbour sampling, one pixel at a time, so a partial
     * repaint only pays for the damaged rectangle. */
    if (wallpaper_active()) {
        for (y = r.y; y < r.y + r.height; y++)
            for (x = r.x; x < r.x + r.width; x++)
                px_set(x, y, wallpaper_pixel(x, y));
        return;
    }

    for (y = r.y; y < r.y + r.height; y++) {
        u32 t = g_ph > 1 ? (u32)((u64)y * 255u / (g_ph - 1u)) : 0u;
        hline(0, y, (long)g_pw, rgb_lerp(TH.bg0, TH.bg1, t));
    }
    /* Angular shards. They are clipped to the damage rect by the surface,
     * so a partial repaint never pays for the whole screen. */
    shard_fill(r, 0, (long)g_ph, -160, 260, 520, 120, TH.accent, 40);
    shard_fill(r, 0, (long)g_ph, (long)g_pw - 260, (long)g_pw + 80,
               420, 160, TH.accent2, 30);
    shard_fill(r, (long)g_ph / 2, (long)g_ph, -80, (long)g_pw / 2,
               260, 480, TH.select, 46);
    shard_fill(r, 0, (long)g_ph * 2 / 3, (long)g_pw * 2 / 3,
               (long)g_pw / 3, 180, 380, TH.accent, 24);
    for (y = (long)PANEL_H + 24; y < (long)g_ph; y += 48)
        for (x = 24; x < (long)g_pw; x += 48)
            px_blend(x, y, 0xffffffu, 6);
    /* subtle corner watermark */
    if (g_pw > 900u && g_ph > 500u) {
        text8("NEWOS", 28, (long)g_ph - (long)TASKBAR_H - 62, 3, TH.card3);
        text8("A SMALL SYSTEM WITH ROOM TO GROW", 30,
              (long)g_ph - (long)TASKBAR_H - 28, 1, TH.border);
    }
}

void draw_wallpaper(void)
{
    draw_wallpaper_rect((SurfaceRect){0, 0, (int)g_pw, (int)g_ph});
}

void draw_panel(void)
{
    struct nsh_time t;
    char clock[8] = "--:--";
    char date[12] = "---";
    char up[24];
    long rx;
    int have_time = sys_gettime(&t) == 0;

    gradient_rect(0, 0, g_pw, PANEL_H, TH.panel0, TH.panel1);
    hline(0, (long)PANEL_H - 1, (long)g_pw, TH.border);
    hline_alpha(0, (long)PANEL_H - 2, (long)g_pw, TH.accent, 70);

    round_rect(10, 7, 22, 22, 7, TH.accent);
    draw_icon(ICON_LOGO, 14, 11, 14, TH.panel1);
    text8_bold("NEWOS", 40, 8, 1, TH.text);
    if (g_pw > 560u)
        text8_max(g_release, 40, 20, 1, TH.muted, 150);

    if (g_pw > 900u) {
        long sx = (long)g_pw / 2 - 150;
        long sw;
        round_rect(sx, 8, 62, 18, 9, TH.card2);
        disc(sx + 10, 17, 3, TH.ok);
        text8("GUI", sx + 18, 13, 1, TH.dim);
        sx += 70;
        round_rect(sx, 8, 66, 18, 9, TH.card2);
        disc(sx + 10, 17, 3, TH.accent2);
        text8("PS/2", sx + 18, 13, 1, TH.dim);
        sx += 74;
        {
            char fb[24], *fp = put_u64(fb, g_pw);
            *fp++ = 'x';
            fp = put_u64(fp, g_ph);
            *fp = '\0';
            sw = (long)text8_width(fb, 1) + 18;
            round_rect(sx, 8, sw, 18, 9, TH.card2);
            text8(fb, sx + 9, 13, 1, TH.dim);
            sx += sw + 10;
        }
        fmt_uptime(up, g_uptime);
        text8("UP ", sx, 13, 1, TH.muted);
        text8(up, sx + 22, 13, 1, TH.dim);
    }

    if (have_time) {
        fmt_clock(clock, &t);
        fmt_date(date, &t);
    }
    if (g_pw > 620u) {
        rx = (long)g_pw - 94;
        text8_bold(clock, rx, 4, 2, TH.text);
        text8(date, rx + 2, 22, 1, TH.muted);
    } else {
        rx = (long)g_pw - 52;
        text8(clock, rx, 13, 1, TH.text);
    }
}

void draw_taskbar(void)
{
    long i, ty = (long)g_ph - (long)TASKBAR_H;
    struct nsh_time t;
    char clockbuf[8] = "--:--";

    /* Classic bottom bar: flat face, light top edge, dark bottom edge. */
    solid(0, ty, (long)g_pw, TASKBAR_H, TH.dock);
    hline(0, ty, (long)g_pw, 0xffffffu);
    hline(0, ty + (long)TASKBAR_H - 1, (long)g_pw, 0x000000u);
    if (g_pw > 1u)
        vline(0, ty, TASKBAR_H, 0xffffffu);

    /* Windows 95 keeps a raised bevel on the START button; it stays
     * pressed while the start menu is open. */
    {
        long bx = 4, by = ty + 4, bw = TB_START_W - 8, bh = TASKBAR_H - 8;
        if (sm_open)
            w95_bevel_pressed(bx, by, bw, bh, TH.dock);
        else
            w95_bevel(bx, by, bw, bh, TH.dock);
        draw_icon(ICON_LOGO, bx + 16, by + bh / 2 - 9, 18, 0x000080u);
        text8_bold("START", bx + 40, by + bh / 2 - 5, 1,
                   sm_open ? 0x000080u : 0x000000u);
    }

    for (i = 0; i < (int)MAX_W; i++) {
        long bx, by, bw, bh;
        int id = (int)i;
        int s = find_slot(id);
        int open, min, active;
        u32 col;

        if (!taskbar_button_rect(id, &bx, &by, &bw, &bh))
            continue;
        open = s >= 0 && WOPEN[s];
        min = s >= 0 && WMIN[s];
        active = open && !min && g_focused == id;
        col = open ? win_accent(id) : TH.muted;
        if (active)
            w95_bevel_pressed(bx, by, bw, bh, TH.dock);
        else
            w95_bevel(bx, by, bw, bh, TH.dock);
        draw_icon(id == WSYS ? ICON_SYS :
                  id == WFILES ? ICON_FILES :
                  id == WMOUSE ? ICON_MOUSE :
                  id == WABOUT ? ICON_INFO : ICON_LOG,
                  bx + 6, by + bh / 2 - 8, 16,
                  min ? 0x808080u : col);
        text8_max(WTITLE[id], bx + 27, by + bh / 2 - 4, 1,
                  active ? 0x000000u : 0x404040u, bw - 32);
    }

    /* Clock tray: a sunken well on the right, exactly like the Win95
     * notification area (network/volume share widen it later). */
    {
        long tx, tyy, tw, th;
        if (tray_rect(&tx, &tyy, &tw, &th)) {
            w95_bevel_pressed(tx, tyy, tw, th, TH.dock);
            if (sys_gettime(&t) == 0)
                fmt_clock(clockbuf, &t);
            text8(clockbuf, tx + (tw - (long)text8_width(clockbuf, 2)) / 2,
                  tyy + th / 2 - 10, 2, 0x000000u);
        }
    }
}

void draw_start_menu(void)
{
    long x, y, w, h, i;
    if (!sm_open)
        return;
    start_menu_rect(&x, &y, &w, &h);
    solid(x + 4, y + 5, w, h, 0x000000u);           /* hard shadow */
    /* Square frame: the start menu hangs off the taskbar in Win95. */
    w95_bevel(x, y, w, h, TH.card);
    hline_alpha(x + 18, y, w - 36, TH.accent, 120);

    round_rect(x + 14, y + 12, 26, 26, 8, TH.accent);
    draw_icon(ICON_LOGO, x + 19, y + 18, 16, TH.card);
    text8_bold("NEWOS", x + 50, y + 14, 1, TH.text);
    text8_max(g_release, x + 50, y + 28, 1, TH.muted, w - 60);
    {
        UiSeparator sep;
        ui_separator_init(&sep, (SurfaceRect){(int)(x + 12), (int)(y + 47),
                                           (int)(w - 24), 2}, 0);
        ui_separator_draw(&sep);
    }

    for (i = 0; i < SM_ITEMS; i++) {
        long iy = y + 52 + i * SM_ITEM_H;
        int selected = i == sm_sel;
        static const char *const labels[SM_ITEMS] = {
            "System monitor", "Program launcher", "About NEWOS",
            "Event log", "Open terminal", "Keyboard help",
            "Change theme", "Close session"
        };
        static const char *const hints[SM_ITEMS] = {
            "1", "2", "3", "4", "SH", "?", "T", "^Q"
        };
        static const int icons[SM_ITEMS] = {
            ICON_SYS, ICON_FILES, ICON_INFO, ICON_LOG,
            ICON_TERM, ICON_HELP, ICON_THEME, ICON_POWER
        };
        if (selected)
            round_rect(x + 8, iy, w - 16, SM_ITEM_H - 4, 8, TH.card3);
        draw_icon(icons[i], x + 18, iy + 8, 18,
                  selected ? win_accent(i < 4 ? i : WSYS) : TH.dim);
        text8(labels[i], x + 46, iy + 12, 1,
              selected ? TH.text : TH.dim);
        text8_right(hints[i], x + w - 18, iy + 12, 1, TH.muted);
    }
    {
        UiSeparator sep;
        ui_separator_init(&sep, (SurfaceRect){(int)(x + 12), (int)(y + h - 28),
                                           (int)(w - 24), 2}, 0);
        ui_separator_draw(&sep);
    }
    text8("UP/DOWN  ENTER  ESC", x + 16, y + h - 20, 1, TH.muted);
}

void toast_rect(long *x, long *y, long *w, long *h)
{
    *w = (long)text8_width(toast_text, 1) + 30;
    if (*w > (long)g_pw - 40)
        *w = (long)g_pw - 40;
    *x = ((long)g_pw - *w) / 2;
    *y = (long)g_ph - (long)TASKBAR_H - 50;
    *h = 30;
}

void damage_toast(void)
{
    long x, y, w, h;
    if (toast_ttl <= 0 || !toast_text[0])
        return;
    toast_rect(&x, &y, &w, &h);
    desktop_damage(x - 4, y - 4, w + 8, h + 8);
}

void draw_toast(void)
{
    long w, x, y, h;
    if (toast_ttl <= 0 || !toast_text[0])
        return;
    toast_rect(&x, &y, &w, &h);
    round_rect_alpha(x + 3, y + 4, w, 30, 10, 0x000000u, 110);
    round_rect(x, y, w, 30, 10, TH.card);
    vline(x + 1, y + 6, 18, toast_color);
    text8_center(toast_text, x + w / 2 + 2, y + 11, 1, TH.text);
}

void draw_help(void)
{
    long w, h, x, y, col_w, row_h;
    static const char *const keys[] = {
        "TAB", "1-6", "UP/DOWN", "ENTER", "` / ~", "F", "M",
        "T", "?", "CTRL+Q", "DRAG", "WHEEL"
    };
    static const char *const desc[] = {
        "cycle focus", "focus window", "select program", "run selection",
        "start menu", "maximize", "minimize", "change theme",
        "this help", "exit session", "move / resize", "scroll files"
    };
    if (!help_open)
        return;
    w = 620;
    if (w > (long)g_pw - 40)
        w = (long)g_pw - 40;
    col_w = w / 2 - 24;
    h = 6 * 30 + 84;
    if (h > (long)g_ph - 40)
        h = (long)g_ph - 40;
    x = ((long)g_pw - w) / 2;
    y = ((long)g_ph - h) / 2;
    round_rect_alpha(x + 5, y + 8, w, h, 16, 0x000000u, 120);
    round_rect(x, y, w, h, 16, TH.card);
    hline_alpha(x + 20, y, w - 40, TH.accent, 140);
    text8_bold("KEYBOARD & MOUSE", x + 22, y + 16, 1, TH.text);
    text8("ESC closes overlays", x + w - 176, y + 16, 1, TH.muted);
    {
        UiSeparator sep;
        ui_separator_init(&sep, (SurfaceRect){(int)(x + 20), (int)(y + 34),
                                           (int)(w - 40), 2}, 0);
        ui_separator_draw(&sep);
    }
    row_h = 30;
    for (long i = 0; i < 12; i++) {
        long cx = x + 22 + (i / 6) * col_w;
        long cy = y + 48 + (i % 6) * row_h;
        draw_keycap(keys[i], cx, cy);
        text8(desc[i], cx + (long)text8_width(keys[i], 1) + 24,
              cy + 6, 1, TH.dim);
    }
    {
        UiSeparator sep;
        ui_separator_init(&sep, (SurfaceRect){(int)(x + 20), (int)(y + h - 30),
                                           (int)(w - 40), 2}, 0);
        ui_separator_draw(&sep);
    }
    text8("Drag a title bar to move. Drag the lower-right corner to resize.",
          x + 22, y + h - 21, 1, TH.muted);
}


