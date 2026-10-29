/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_apps.c
 * Description: Built-in desktop applications and program launcher.
 * Added: 2026-10-22
 */
/* Built-in desktop applications and program launcher. */

#include "desktop.h"

void load_bin_entries(void)
{
    struct nsh_dirent entries[64];
    long count = sys_readdir("/bin", entries, sizeof(entries));
    int i;
    bin_entry_count = 0;
    if (count <= 0)
        return;
    if (count > 64)
        count = 64;
    for (i = 0; i < count && bin_entry_count < BIN_MAX; i++) {
        if (!entries[i].name[0])
            continue;
        nstrcpy(bin_entries[bin_entry_count], entries[i].name);
        bin_entry_count++;
    }
    if (bin_selected >= bin_entry_count)
        bin_selected = bin_entry_count ? bin_entry_count - 1 : 0;
}

int files_visible(long h)
{
    long bh = h - TITLE_H - 26;
    long avail = bh - 24 - 22;
    int v = (int)(avail / 24);
    if (v < 1) v = 1;
    if (v > BIN_MAX) v = BIN_MAX;
    return v;
}

long files_list_top(long y)
{
    return y + TITLE_H + 14 + 24;
}

int files_row_at(long px, long py)
{
    int slot = find_slot(WFILES);
    long x, y, w, h, ly, lx, lw;
    int r, vis, e;
    if (slot < 0 || !WOPEN[slot] || WMIN[slot])
        return -1;
    x = WX[slot]; y = WY[slot]; w = WW[slot]; h = WH[slot];
    ly = files_list_top(y);
    lx = x + 10;
    lw = w - 20;
    if (px < lx || px >= lx + lw)
        return -1;
    r = (int)((py - ly) / 24);
    vis = files_visible(h);
    if (r < 0 || r >= vis)
        return -1;
    e = bin_scroll + r;
    if (e >= bin_entry_count)
        return -1;
    return e;
}

void select_entry(int delta)
{
    int max, slot = find_slot(WFILES), vis;
    if (!bin_entry_count)
        return;
    if (slot < 0)
        return;
    vis = files_visible(WH[slot]);
    max = bin_entry_count - 1;
    bin_selected += delta;
    if (bin_selected < 0) bin_selected = 0;
    if (bin_selected > max) bin_selected = max;
    if (bin_selected < bin_scroll)
        bin_scroll = bin_selected;
    if (bin_selected >= bin_scroll + vis)
        bin_scroll = bin_selected - vis + 1;
    if (bin_scroll < 0) bin_scroll = 0;
}

const char *prog_desc(const char *name)
{
    static const struct { const char *n; const char *d; } map[] = {
        {"sh", "shell"}, {"ls", "list files"}, {"cat", "show text"},
        {"echo", "print text"}, {"img", "view bitmap"}, {"vid", "fb demo"},
        {"nano", "text editor"}, {"tcc", "C compiler"},
        {"newpkg", "packages"}, {"ps", "processes"}, {"free", "memory"},
        {"df", "filesystems"}, {"uptime", "uptime"}, {"desktop", "compositor"},
        {"about", "system info"}, {"hello", "hello world"},
        {"hello-new", "package demo"},
        {"newfetch", "system banner"}, {"clear", "clear screen"},
        {"help", "command list"}, {"sleep", "pause"},
        {"busybox", "402 applets"}, {"glxgears", "TinyGL gears"},
    };
    unsigned int i;
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (nstrcmp(map[i].n, name) == 0)
            return map[i].d;
    return "program";
}

u32 prog_color(const char *name)
{
    static const u32 palette[] = {
        0x62d6c8u, 0x61a8ffu, 0xb98cffu, 0xf2b84bu, 0x7ee787u
    };
    unsigned int h = 0;
    while (*name)
        h = h * 131u + (unsigned char)*name++;
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

static int graphics_program(const char *path)
{
    const char *base = path;

    while (*path) {
        if (*path == '/')
            base = path + 1;
        path++;
    }
    return nstrcmp(base, "glxgears") == 0 ||
           nstrcmp(base, "img") == 0 ||
           nstrcmp(base, "vid") == 0;
}

void launch_path(const char *path, const char *arg, const char *arg2)
{
    char msg[96];
    char *argv[4];
    long pid;
    int argc = 1;
    int graphical = graphics_program(path);

    nstrcpy(msg, "Launching ");
    nstrcat(msg, path);
    set_toast(msg, TH.accent);
    log_put2("launch ", path, TH.accent2);
    desktop_damage_all();
    desktop_present();

    argv[0] = (char *)path;
    if (arg) {
        argv[1] = (char *)arg;
        argc = 2;
    }
    if (arg2) {
        argv[2] = (char *)arg2;
        argc = 3;
    }
    (void)sys_kbd_grab(0);
    if (!graphical) {
        /* Text programs need the kernel text console.  Resume fbcon and
         * start them on a clean screen; the compositor repaints on return. */
        (void)sys_console_gui(0);
        nputs("\x1B[2J\x1B[H");
    }
    pid = sys_spawn2(path, argv, argc);
    if (pid > 0) {
        (void)sys_waitpid((pid_t)pid);
        nstrcpy(msg, "Returned from ");
        nstrcat(msg, path);
        set_toast(msg, TH.ok);
        log_put2("returned ", path, TH.dim);
    } else {
        nstrcpy(msg, "Could not run ");
        nstrcat(msg, path);
        set_toast(msg, TH.danger);
        log_put2("failed ", path, TH.danger);
        sys_sleep(700);
    }
    if (!graphical)
        (void)sys_console_gui(1);
    (void)sys_kbd_grab(1);
    desktop_damage_all();
    desktop_present();
}

void launch_bin_entry(int entry)
{
    char path[NSH_MAX_PATH];
    const char *arg = 0;
    const char *arg2 = 0;
    if (entry < 0 || entry >= bin_entry_count)
        return;
    nstrcpy(path, "/bin/");
    nstrcat(path, bin_entries[entry]);
    if (nstrcmp(bin_entries[entry], "ls") == 0)
        arg = "/";
    else if (nstrcmp(bin_entries[entry], "img") == 0) {
        arg = "/etc/splash.bmp";
        /* Desktop owns the framebuffer; a hidden "press any key" prompt
         * would block the compositor.  Shell invocations still wait. */
        arg2 = "--no-wait";
    }
    else if (nstrcmp(bin_entries[entry], "nano") == 0)
        arg = "/etc/motd";
    launch_path(path, arg, arg2);
}

void draw_kv(const char *label, const char *value,
                    long x, long y, long w, u32 lc, u32 vc)
{
    long vx = x + 96;
    text8(label, x, y, 1, lc);
    if (vx < x + w)
        text8_max(value, vx, y, 1, vc, x + w - vx);
}

void draw_tile(const char *label, const char *value,
                      long x, long y, long w, long h, u32 accent)
{
    round_rect(x, y, w, h, 9, TH.card2);
    solid(x + 9, y, w - 18, 2, accent);
    text8(label, x + 9, y + 9, 1, TH.muted);
    text8_bold(value, x + 9, y + 25, 2, TH.text);
}

void draw_sys(long x, long y, long w, long h)
{
    long bx = x + 18, by = y + TITLE_H + 16, bw = w - 36;
    long bh = h - TITLE_H - 30;
    long tile_h, tile_gap = 8, tw;
    int ntiles = bw >= 330 ? 3 : 2;
    char v1[24], v2[24], v3[24];
    u64 pct = g_mem_total ? (g_mem_used * 100u) / g_mem_total : 0;
    char *p;
    long bar_y;

    if (bw < 80 || bh < 60)
        return;
    p = put_u64(v1, pct);
    *p++ = '%';
    *p = '\0';
    p = put_u64(v2, g_uptime / 3600u);
    *p++ = ':';
    p = put_2d(p, (g_uptime / 60u) % 60u);
    *p = '\0';
    p = put_u64(v3, (u64)g_proc_count);
    *p = '\0';

    tile_h = bh >= 180 ? 62 : 48;
    tw = (bw - (ntiles - 1) * tile_gap) / ntiles;
    draw_tile("MEMORY", v1, bx, by, tw, tile_h, TH.accent);
    draw_tile("UPTIME", v2, bx + tw + tile_gap, by, tw, tile_h, TH.accent2);
    if (ntiles == 3)
        draw_tile("PROCS", v3, bx + 2 * (tw + tile_gap), by,
                  tw, tile_h, TH.ok);

    bar_y = by + tile_h + 18;
    text8("RAM", bx, bar_y, 1, TH.muted);
    {
        char mem[32], *mp = mem;
        mp = put_u64(mp, g_mem_used);
        *mp++ = '/';
        mp = put_u64(mp, g_mem_total);
        *mp++ = 'M'; *mp++ = 'B'; *mp = '\0';
        text8_right(mem, bx + bw, bar_y, 1, TH.dim);
    }
    {
        UiProgress prog;
        ui_progress_init(&prog, (SurfaceRect){(int)bx, (int)(bar_y + 14),
                                           (int)bw, 10});
        ui_progress_set_value(&prog, (int)(pct > 100 ? 100 : pct));
        ui_progress_draw(&prog);
    }

    if (bh >= 190) {
        long ry = bar_y + 42;
        char fb[32], *fp = put_u64(fb, g_pw);
        *fp++ = 'x';
        fp = put_u64(fp, g_ph);
        nstrcpy(fp, " x32");
        draw_kv("KERNEL", g_release, bx, ry, bw, TH.muted, TH.dim);
        draw_kv("ARCH", g_machine, bx, ry + 18, bw, TH.muted, TH.dim);
        draw_kv("FRAMEBUFFER", fb, bx, ry + 36, bw, TH.muted, TH.dim);
        draw_kv("THEME", TH.name, bx, ry + 54, bw, TH.muted, TH.dim);
    }
}

void draw_files(long x, long y, long w, long h)
{
    long bx = x + 16, by = y + TITLE_H + 14, bw = w - 32;
    long ly = files_list_top(y);
    int vis = files_visible(h);
    int shown = 0;
    char count[24], *p;

    text8_bold("PROGRAMS", bx, by, 1, TH.text);
    p = put_u64(count, (u64)bin_entry_count);
    nstrcpy(p, " ITEMS");
    text8_right(count, bx + bw, by, 1, TH.muted);

    for (int row = 0; row < vis; row++) {
        int e = bin_scroll + row;
        long ry = ly + row * 24;
        int selected, hover;
        u32 col, rowbg;
        if (e >= bin_entry_count)
            break;
        if (ry + 22 > y + h - 24)
            break;
        selected = e == bin_selected;
        hover = g_mx >= x + 10 && g_mx < x + w - 10 &&
                g_my >= ry && g_my < ry + 22;
        rowbg = selected ? TH.select : (hover ? TH.card3 : TH.card);
        if (selected || hover)
            round_rect(x + 10, ry, w - 20, 22, 6, rowbg);
        if (selected)
            solid(x + 12, ry + 4, 3, 14, win_accent(WFILES));
        col = prog_color(bin_entries[e]);
        draw_icon(prog_icon(bin_entries[e]), bx + 4, ry + 3, 16, col);
        text8_max(bin_entries[e], bx + 28, ry + 7, 1,
                  (selected || hover) ? TH.text : TH.dim,
                  w - 150);
        if (w > 360)
            text8_right(prog_desc(bin_entries[e]), x + w - 32,
                        ry + 7, 1, TH.muted);
        shown++;
    }
    if (!shown)
        text8("Could not read /bin", bx, ly + 8, 1, TH.danger);

    if (bin_entry_count > vis) {
        UiScrollbar sb;
        ui_scrollbar_init(&sb, (SurfaceRect){(int)(x + w - 11), (int)ly,
                                         6, vis * 24}, 1);
        ui_scrollbar_set_range(&sb, bin_entry_count, vis, bin_scroll);
        ui_scrollbar_draw(&sb);
    }
    text8("UP/DOWN SELECT   ENTER RUN   WHEEL SCROLL",
          bx, y + h - 20, 1, TH.muted);
}

void draw_mouse(long x, long y, long w, long h)
{
    long bx = x + 18, by = y + TITLE_H + 16;
    long bw = w - 36, bh = h - TITLE_H - 30;
    long pad_w = bw * 56 / 100;
    long pad_h = bh - 8;
    long rx, dx, dy;
    char buf[40], *p;

    if (bw < 100 || bh < 80)
        return;
    if (pad_w < 100) pad_w = 100;
    if (pad_w > bw - 130) pad_w = bw - 130;
    if (pad_w < 70) pad_w = bw;
    if (pad_h > 150) pad_h = 150;
    if (pad_h < 70) pad_h = 70;

    round_rect(bx, by, pad_w, pad_h, 11, TH.card2);
    for (long gx = bx + 24; gx < bx + pad_w - 8; gx += 28)
        vline(gx, by + 10, pad_h - 20, TH.card3);
    for (long gy = by + 24; gy < by + pad_h - 8; gy += 28)
        hline(bx + 10, gy, pad_w - 20, TH.card3);

    dx = bx + 10;
    dy = by + 10;
    if (g_mx >= 0 && g_pw)
        dx = bx + 10 + (long)(((u64)(g_mx < 0 ? 0 : g_mx) *
                               (u64)(pad_w - 20)) / g_pw);
    if (g_my >= 0 && g_ph)
        dy = by + 10 + (long)(((u64)(g_my < 0 ? 0 : g_my) *
                               (u64)(pad_h - 20)) / g_ph);
    if (dx > bx + pad_w - 11) dx = bx + pad_w - 11;
    if (dy > by + pad_h - 11) dy = by + pad_h - 11;
    hline(bx + 10, dy, pad_w - 20, TH.accent);
    vline(dx, by + 10, pad_h - 20, TH.accent);
    disc(dx, dy, 5, TH.card);
    disc(dx, dy, 3, TH.accent);

    rx = bx + pad_w + 16;
    if (rx + 110 > bx + bw)
        rx = bx + 8;
    text8("POSITION", rx, by, 1, TH.muted);
    p = put_u64(buf, g_mx < 0 ? 0u : (u64)g_mx);
    *p++ = ',';
    *p++ = ' ';
    p = put_u64(p, g_my < 0 ? 0u : (u64)g_my);
    *p = '\0';
    text8_bold(buf, rx, by + 12, 2, TH.text);

    text8("BUTTONS", rx, by + 40, 1, TH.muted);
    for (int i = 0; i < 3; i++) {
        const char *lab = i == 0 ? "L" : (i == 1 ? "M" : "R");
        int bit = i == 0 ? 0 : (i == 1 ? 2 : 1);
        int on = (g_mb & (1 << bit)) != 0;
        long px = rx + i * 32;
        round_rect(px, by + 52, 26, 20, 6, on ? TH.accent : TH.card2);
        text8_center(lab, px + 13, by + 58, 1,
                     on ? contrast_text(TH.accent) : TH.dim);
    }
    text8("WHEEL", rx, by + 82, 1, TH.muted);
    p = buf;
    if (g_mw > 0) *p++ = '+';
    else if (g_mw < 0) *p++ = '-';
    p = put_u64(p, g_mw < 0 ? (u64)-g_mw : (u64)g_mw);
    *p = '\0';
    text8(buf, rx, by + 94, 1, TH.dim);

    if (bh > 170)
        text8("PS/2 pointer, absolute screen coordinates.",
              bx, y + h - 20, 1, TH.muted);
}

void draw_about(long x, long y, long w, long h)
{
    long bx = x + 18, by = y + TITLE_H + 16, bw = w - 36;
    char up[24], procs[16], *p;
    long ry;

    round_rect(bx, by, 54, 54, 12, win_accent(WABOUT));
    draw_icon(ICON_LOGO, bx + 9, by + 9, 36, contrast_text(win_accent(WABOUT)));
    text8_bold("NEWOS", bx + 70, by + 4, 2, TH.text);
    text8_max(g_release, bx + 70, by + 26, 1, TH.muted, bw - 74);
    text8_max(g_machine, bx + 70, by + 38, 1, TH.dim, bw - 74);

    ry = by + 70;
    {
        UiSeparator sep;
        ui_separator_init(&sep, (SurfaceRect){(int)bx, (int)(ry - 8),
                                           (int)bw, 2}, 0);
        ui_separator_draw(&sep);
    }
    fmt_uptime(up, g_uptime);
    p = put_u64(procs, (u64)g_proc_count);
    *p = '\0';
    draw_kv("KERNEL", g_release, bx, ry, bw, TH.muted, TH.dim);
    draw_kv("MACHINE", g_machine, bx, ry + 18, bw, TH.muted, TH.dim);
    draw_kv("UPTIME", up, bx, ry + 36, bw, TH.muted, TH.dim);
    draw_kv("PROCESSES", procs, bx, ry + 54, bw, TH.muted, TH.dim);
    if (h > 240)
        draw_kv("THEME", TH.name, bx, ry + 72, bw, TH.muted, TH.dim);

    if (h > 220) {
        long cy = y + h - 34;
        ui_button_set_rect(&about_theme_btn,
                            (SurfaceRect){(int)bx, (int)cy, 82, 22});
        ui_button_set_rect(&about_help_btn,
                            (SurfaceRect){(int)(bx + 90), (int)cy, 70, 22});
        ui_button_set_rect(&about_term_btn,
                            (SurfaceRect){(int)(bx + 168), (int)cy, 54, 22});
        ui_button_set_state(&about_theme_btn,
            ui_button_hit(&about_theme_btn, (int)g_mx, (int)g_my), 0);
        ui_button_set_state(&about_help_btn,
            ui_button_hit(&about_help_btn, (int)g_mx, (int)g_my), 0);
        ui_button_set_state(&about_term_btn,
            ui_button_hit(&about_term_btn, (int)g_mx, (int)g_my), 0);
        ui_button_draw(&about_theme_btn);
        ui_button_draw(&about_help_btn);
        ui_button_draw(&about_term_btn);
    }
}

void draw_log(long x, long y, long w, long h)
{
    long bx = x + 18, by = y + TITLE_H + 16, bw = w - 36;
    long bh = h - TITLE_H - 30;
    int vis = (int)((bh - 26) / 20);
    char count[24], *p;

    if (vis < 1) vis = 1;
    text8_bold("EVENT LOG", bx, by, 1, TH.text);
    p = put_u64(count, (u64)log_total);
    nstrcpy(p, " EVENTS");
    text8_right(count, bx + bw, by, 1, TH.muted);
    {
        UiSeparator sep;
        ui_separator_init(&sep, (SurfaceRect){(int)bx, (int)(by + 18),
                                           (int)bw, 2}, 0);
        ui_separator_draw(&sep);
    }

    for (int r = 0; r < vis && r < log_total; r++) {
        int idx = (log_next - 1 - r + LOG_LINES * 2) % LOG_LINES;
        long ly = by + 26 + r * 20;
        if (ly + 18 > y + h - 8)
            break;
        disc(bx + 3, ly + 7, 3, log_color[idx]);
        text8(log_time[idx], bx + 12, ly + 3, 1, TH.muted);
        text8_max(log_msg[idx], bx + 58, ly + 3, 1, TH.dim, bw - 62);
    }
}
