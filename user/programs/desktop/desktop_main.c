/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_main.c
 * Description: NEWOS desktop entry point.
 * Added: 2026-10-17
 */
/* NEWOS desktop entry point.
 *
 * The desktop is a small Win95-style session: a clipped drawing surface, a
 * caller-owned widget layer, a modular shell and a damage-based compositor.
 * Input, window management, applications, icons and theme code live in
 * sibling modules.
 *
 * Keyboard: TAB focus, 1-5 direct focus, UP/DOWN select, ENTER run,
 * F maximize, M minimize, T theme, ` or ~ start menu, ? help, CTRL+Q exit.
 * Mouse: drag a title bar, drag the bottom-right corner to resize, click the
 * taskbar (START, task buttons, clock tray) to focus/minimize, use the wheel
 * over FILES.
 */

#include "desktop.h"
#include <stdlib.h>

int main(int argc, char **argv)
{
    struct nsh_fbinfo fi;
    struct nsh_sysinfo si;
    struct nsh_uname uname;
    struct nsh_mouse m;
    u64 mseq = ~0ull;
    u64 last_uptime = ~0ull;
    int pmb = 0;

    (void)argc;
    (void)argv;
    if (sys_fbinfo(&fi) != 0 || !fi.present) {
        nputs("desktop: no framebuffer - text mode\n");
        nputs("Desktop features require a Limine framebuffer boot.\n");
        return 1;
    }
    if (!fi.width || !fi.height || fi.width < 300 || fi.height < 100) {
        nputs("desktop: framebuffer geometry is too small\n");
        return 1;
    }

    /* The scene buffer tracks the mode the bootloader picked, so the desktop
     * renders at the monitor's native resolution rather than a compiled-in
     * ceiling. Both buffers grow the process break (SYS_BRK) on demand, and
     * every pixel is painted by the wallpaper pass before the first upload,
     * so the uninitialised heap contents are never shown. */
    g_pw = fi.width;
    g_ph = fi.height;
    canvas = malloc((size_t)g_pw * (size_t)g_ph * sizeof(*canvas));
    blit_buf = malloc((size_t)g_pw * 16u * sizeof(*blit_buf));
    if (!canvas || !blit_buf) {
        nputs("desktop: out of memory allocating the framebuffer canvas\n");
        free(canvas);
        free(blit_buf);
        return 1;
    }
    if (sys_kbd_grab(1) != 0)
        nputs("desktop: keyboard is owned by another process\n");
    if (sys_console_gui(1) != 0)
        nputs("desktop: framebuffer console is owned by another process\n");

    surface_init(&g_surface, canvas, (int)g_pw, (int)g_ph);
    /* The plate is read once, before the first paint, so the opening frame
     * already shows it; if it is missing or invalid the shell keeps its
     * procedural gradient. */
    wallpaper_load();
    ui_button_init(&about_theme_btn, (SurfaceRect){0, 0, 82, 22}, "THEME");
    ui_button_init(&about_help_btn, (SurfaceRect){0, 0, 70, 22}, "HELP");
    ui_button_init(&about_term_btn, (SurfaceRect){0, 0, 54, 22}, "SH");

    load_bin_entries();
    if (sys_uname(&uname) == 0) {
        nstrcpy(g_release, uname.release);
        nstrcpy(g_machine, uname.machine);
    }
    if (sys_sysinfo(&si) == 0 && si.total_frames) {
        g_mem_total = si.total_frames * 4096u / (1024u * 1024u);
        {
            u64 free_mb = si.free_frames * 4096u / (1024u * 1024u);
            g_mem_used = free_mb < g_mem_total ? g_mem_total - free_mb : 0;
        }
        g_uptime = si.uptime_sec;
    }
    {
        struct nsh_proc procs[32];
        long n = sys_ps(procs, sizeof(procs));
        if (n > 0)
            g_proc_count = (int)n;
    }

    layout_windows();
    g_focused = WID[0];

    log_put("desktop session ready", TH.accent);
    {
        char buf[48], *p = put_u64(buf, g_pw);
        *p++ = 'x';
        p = put_u64(p, g_ph);
        nstrcpy(p, " framebuffer");
        log_put(buf, TH.dim);
    }
    log_put("keyboard ownership acquired", TH.ok);
    {
        char buf[32], *p = put_u64(buf, MAX_W);
        nstrcpy(p, " windows opened");
        log_put(buf, TH.dim);
    }
    set_toast("Welcome to NEWOS", TH.accent);

    desktop_damage_all();
    desktop_present();

    for (;;) {
        int structural = 0;
        char keys[64];
        long nk;
        long old_mx = g_mx, old_my = g_my;

        g_frame++;
        nk = sys_kbd_read(keys, sizeof(keys));
        if (nk > 0) {
            kbd_esc_idle = 0;
            structural = 1;
            for (long ki = 0; ki < nk; ki++) {
                handle_key((unsigned char)keys[ki]);
                if (g_quit)
                    break;
            }
        } else if (kbd_esc) {
            if (++kbd_esc_idle >= 3) {
                kbd_esc = 0;
                esc_action();
                structural = 1;
            }
        }
        if (g_quit)
            break;

        if (sys_mouse_get(&m) == 0 && m.seq != mseq) {
            mseq = m.seq;
            g_mx = (long)m.x;
            g_my = (long)m.y;
            g_mb = (int)m.buttons;
            g_mw = (int)m.wheel;
            if (g_mx != old_mx || g_my != old_my) {
                /* The compositor adds the old/new cursor rectangles itself;
                 * here we refresh the chrome whose hover state changed.  The
                 * Mouse window also renders the live pointer position, so its
                 * body must be repainted even when the pointer is elsewhere. */
                desktop_damage_at(old_mx, old_my);
                desktop_damage_at(g_mx, g_my);
                damage_window(WMOUSE);
            } else if (g_mb != pmb) {
                desktop_damage_at(g_mx, g_my);
                damage_window(WMOUSE);
            }
            if (g_mw != 0) {
                damage_window(WMOUSE);
                if (g_focused == WFILES) {
                    select_entry(g_mw > 0 ? -1 : 1);
                    damage_window(WFILES);
                }
            }
        }

        if ((g_mb & 1) && !(pmb & 1)) {
            if (handle_click())
                structural = 1;
        }
        if (!(g_mb & 1)) {
            if (drag_mode)
                damage_window(WID[0]);
            drag_mode = 0;
        }
        if (drag_mode && (g_mb & 1)) {
            long ox = WX[0], oy = WY[0], ow = WW[0], oh = WH[0];
            if (drag_mode == 1) {
                long bottom = (long)g_ph - (long)TASKBAR_H;
                WX[0] = g_mx - drag_ox;
                WY[0] = g_my - drag_oy;
                if (WX[0] < 0) WX[0] = 0;
                if (WY[0] < (long)PANEL_H) WY[0] = PANEL_H;
                if (WX[0] + WW[0] > (long)g_pw)
                    WX[0] = (long)g_pw - WW[0];
                if (WY[0] + WH[0] > bottom)
                    WY[0] = bottom - WH[0];
            } else if (drag_mode == 2) {
                long maxw = (long)g_pw - WX[0];
                long maxh = (long)g_ph - (long)TASKBAR_H - WY[0];
                WW[0] = g_mx - drag_ox - WX[0];
                WH[0] = g_my - drag_oy - WY[0];
                if (WW[0] < 260) WW[0] = 260;
                if (WH[0] < 160) WH[0] = 160;
                if (WW[0] > maxw) WW[0] = maxw;
                if (WH[0] > maxh) WH[0] = maxh;
            }
            desktop_damage(ox - 10, oy - 10, ow + 20, oh + 20);
            damage_window(WID[0]);
        }
        pmb = g_mb;

        if (sys_sysinfo(&si) == 0 && si.total_frames) {
            u64 old_used = g_mem_used;
            u64 old_total = g_mem_total;
            u64 old_up = g_uptime;
            u64 free_mb = si.free_frames * 4096u / (1024u * 1024u);
            g_mem_total = si.total_frames * 4096u / (1024u * 1024u);
            g_mem_used = free_mb < g_mem_total ? g_mem_total - free_mb : 0;
            g_uptime = si.uptime_sec;
            if (g_uptime != last_uptime) {
                last_uptime = g_uptime;
                desktop_damage(0, 0, (long)g_pw, PANEL_H);
                damage_window(WSYS);
                damage_window(WABOUT);
            }
            if ((g_frame & 63u) == 0) {
                struct nsh_proc procs[32];
                long n = sys_ps(procs, sizeof(procs));
                if (n > 0 && (int)n != g_proc_count) {
                    g_proc_count = (int)n;
                    damage_window(WSYS);
                    damage_window(WABOUT);
                }
            }
            if (g_mem_used != old_used || g_mem_total != old_total ||
                g_uptime != old_up) {
                damage_window(WSYS);
                damage_window(WABOUT);
            }
        }

        if (toast_ttl > 0) {
            damage_toast();
            toast_ttl--;
            if (toast_ttl == 0)
                damage_toast();
        }

        if (structural)
            desktop_damage_all();
        desktop_present();
        sys_sleep(20);
    }

    solid(0, 0, (long)g_pw, (long)g_ph, 0x000000u);
    {
        struct nsh_fbwrite rq;
        rq.x = 0;
        rq.y = 0;
        rq.w = g_pw;
        rq.h = g_ph;
        rq.pixels = (u64)canvas;
        rq.pixlen = g_pw * g_ph * sizeof(canvas[0]);
        (void)sys_fbwrite(&rq);
    }
    (void)sys_kbd_grab(0);
    (void)sys_console_gui(0);
    free(canvas);
    free(blit_buf);
    wallpaper_free();
    nputs("\x1B[2J\x1B[H");
    nputs("desktop: done\n");
    return 0;
}
