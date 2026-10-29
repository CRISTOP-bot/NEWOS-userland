/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_input.c
 * Description: Keyboard, mouse and menu actions.
 * Added: 2026-10-22
 */
/* Keyboard, mouse and menu actions. */

#include "desktop.h"

void start_menu_action(int item)
{
    sm_open = 0;
    switch (item) {
    case 0: open_window(WSYS); log_put("focus System", TH.accent); break;
    case 1: open_window(WFILES); log_put("focus Files", TH.accent2); break;
    case 2: open_window(WABOUT); log_put("focus About", TH.warn); break;
    case 3: open_window(WLOG); log_put("focus Log", TH.ok); break;
    case 4: launch_path("/bin/sh", 0, 0); break;
    case 5: help_open = 1; log_put("help opened", TH.dim); break;
    case 6: cycle_theme(); break;
    case 7: g_quit = 1; log_put("session close requested", TH.danger); break;
    default: break;
    }
}

void esc_action(void)
{
    if (help_open) {
        help_open = 0;
        log_put("help closed", TH.dim);
    } else if (sm_open) {
        sm_open = 0;
    } else {
        set_toast("CTRL+Q exits the GUI", TH.warn);
    }
}

void menu_up(void)
{
    if (sm_sel > 0)
        sm_sel--;
}

void menu_down(void)
{
    if (sm_sel < SM_ITEMS - 1)
        sm_sel++;
}

void arrow_up(void)
{
    if (sm_open) {
        menu_up();
        return;
    }
    if (g_focused == WFILES)
        select_entry(-1);
}

void arrow_down(void)
{
    if (sm_open) {
        menu_down();
        return;
    }
    if (g_focused == WFILES)
        select_entry(1);
}

int handle_click(void)
{
    long x = g_mx, y = g_my;
    long dx, dy, dw, dh, smx, smy, smw, smh;
    int i;

    if (help_open) {
        help_open = 0;
        log_put("help closed", TH.dim);
        return 1;
    }
    start_menu_rect(&smx, &smy, &smw, &smh);
    if (sm_open) {
        if (x >= smx && x < smx + smw && y >= smy && y < smy + smh) {
            int item = (int)((y - smy - 52) / SM_ITEM_H);
            if (item >= 0 && item < SM_ITEMS) {
                sm_sel = item;
                start_menu_action(item);
                return 1;
            }
        }
        sm_open = 0;
        return 1;
    }

    taskbar_rect(&dx, &dy, &dw, &dh);
    if (x >= dx && x < dx + dw && y >= dy && y < dy + dh) {
        long sx = 4, sy = dy + 4, sw = TB_START_W - 8, sh = TASKBAR_H - 8;
        long tx, tyy, tw, th;

        if (x >= sx && x < sx + sw && y >= sy && y < sy + sh) {
            sm_open = !sm_open;
            sm_sel = 0;
            log_put(sm_open ? "start menu open" : "start menu closed",
                    TH.dim);
            return 1;
        }
        if (tray_rect(&tx, &tyy, &tw, &th) &&
            x >= tx && x < tx + tw && y >= tyy && y < tyy + th) {
            struct nsh_time t;
            char datebuf[24];
            if (sys_gettime(&t) == 0) {
                nstrcpy(datebuf, "date: ");
                fmt_date(datebuf + 6, &t);
                set_toast(datebuf, TH.dim);
                log_put(datebuf, TH.dim);
            }
            return 1;
        }
        for (i = 0; i < (int)MAX_W; i++) {
            int id = (int)i;
            int s = find_slot(id);
            if (!taskbar_button_hit(id, x, y))
                continue;
            if (s >= 0 && WOPEN[s]) {
                if (WMIN[s]) {
                    WMIN[s] = 0;
                    (void)focus_identity(id);
                    log_put2("restore ", WTITLE[id], TH.accent);
                } else if (g_focused == id) {
                    minimize_identity(id);
                } else {
                    (void)focus_identity(id);
                    log_put2("focus ", WTITLE[id], TH.accent);
                }
            } else {
                open_window(id);
                log_put2("open ", WTITLE[id], TH.accent);
            }
            return 1;
        }
        return 1;       /* empty bar area: swallow the click */
    }

    for (i = 0; i < (int)MAX_W; i++) {
        long wx = WX[i], wy = WY[i], ww = WW[i], wh = WH[i];
        long cy = wy + 17;
        if (!WOPEN[i] || WMIN[i])
            continue;
        if (x < wx || x >= wx + ww || y < wy || y >= wy + wh)
            continue;

        if (iabs(x - (wx + ww - 20)) <= 10 && iabs(y - cy) <= 10) {
            close_identity(WID[i]);
            return 1;
        }
        if (iabs(x - (wx + ww - 44)) <= 10 && iabs(y - cy) <= 10) {
            int id = WID[i];
            raise_window(i);
            g_focused = id;
            toggle_maximize(id);
            return 1;
        }
        if (iabs(x - (wx + ww - 68)) <= 10 && iabs(y - cy) <= 10) {
            minimize_identity(WID[i]);
            return 1;
        }
        if (y < wy + TITLE_H) {
            int id = WID[i];
            raise_window(i);
            g_focused = id;
            if (last_title_id == id && g_frame - last_title_frame <= 16) {
                toggle_maximize(id);
                drag_mode = 0;
                last_title_id = -1;
                return 1;
            }
            drag_mode = 1;
            drag_ox = x - WX[0];
            drag_oy = y - WY[0];
            last_title_id = id;
            last_title_frame = g_frame;
            log_put2("move ", WTITLE[id], TH.dim);
            return 1;
        }
        if (x >= wx + ww - 18 && y >= wy + wh - 18) {
            int id = WID[i];
            raise_window(i);
            g_focused = id;
            if (!WMAX[0]) {
                drag_mode = 2;
                drag_ox = x - (WX[0] + WW[0]);
                drag_oy = y - (WY[0] + WH[0]);
                log_put2("resize ", WTITLE[id], TH.dim);
            }
            return 1;
        }
        {
            int id = WID[i];
            int row;
            raise_window(i);
            g_focused = id;
            row = files_row_at(x, y);
            if (id == WFILES && row >= 0) {
                bin_selected = row;
                launch_bin_entry(row);
                return 1;
            }
            if (id == WABOUT) {
                if (ui_button_hit(&about_theme_btn, (int)x, (int)y)) {
                    cycle_theme();
                    return 1;
                }
                if (ui_button_hit(&about_help_btn, (int)x, (int)y)) {
                    help_open = 1;
                    return 1;
                }
                if (ui_button_hit(&about_term_btn, (int)x, (int)y)) {
                    launch_path("/bin/sh", 0, 0);
                    return 1;
                }
            }
            log_put2("focus ", WTITLE[id], TH.accent);
            return 1;
        }
    }
    return 0;
}

void handle_key(unsigned char kc)
{
    if (kc == 0x1b) {
        kbd_esc = 1;
        kbd_esc_idle = 0;
        return;
    }
    if (kbd_esc) {
        kbd_esc = 0;
        if (kc == '[') {
            kbd_csi = 1;
            return;
        }
        esc_action();
        return;
    }
    if (kbd_csi) {
        kbd_csi = 0;
        if (kc == 'A') arrow_up();
        else if (kc == 'B') arrow_down();
        return;
    }
    switch (kc) {
    case '\t':
        (void)focus_next();
        break;
    case '\n':
    case '\r':
        if (sm_open)
            start_menu_action(sm_sel);
        else if (g_focused == WFILES)
            launch_bin_entry(bin_selected);
        break;
    case '?':
        help_open = !help_open;
        log_put(help_open ? "help opened" : "help closed", TH.dim);
        break;
    case 'f':
    case 'F':
        toggle_maximize(g_focused);
        break;
    case 'm':
    case 'M':
        minimize_identity(g_focused);
        break;
    case 't':
    case 'T':
        cycle_theme();
        break;
    case '`':
    case '~':
        sm_open = !sm_open;
        sm_sel = 0;
        break;
    case 0x11:              /* Ctrl+Q */
        g_quit = 1;
        break;
    case '1': case '2': case '3': case '4': case '5': case '6':
        (void)focus_identity(kc - '1');
        break;
    default:
        break;
    }
}
