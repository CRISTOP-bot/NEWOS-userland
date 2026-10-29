/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_wm.c
 * Description: Window manager: z-order, focus, layout and window chrome.
 * Added: 2026-10-22
 */
/* Window manager: z-order, focus, layout and window chrome. */

#include "desktop.h"

u32 win_accent(int id)
{
    if (TH.unified_titles)
        return TH.accent;
    switch (id) {
    case WSYS:   return TH.accent;
    case WFILES: return TH.accent2;
    case WMOUSE: return 0xb98cff;
    case WABOUT: return TH.warn;
    default:     return TH.ok;
    }
}

int find_slot(int id)
{
    int i;
    for (i = 0; i < (int)MAX_W; i++)
        if (WID[i] == id)
            return i;
    return -1;
}

void raise_window(int idx)
{
    long x, y, w, h, sx, sy, sw, sh;
    int open, minimized, maximized, identity, i;
    if (idx <= 0 || idx >= (int)MAX_W)
        return;
    x = WX[idx]; y = WY[idx]; w = WW[idx]; h = WH[idx];
    sx = WSX[idx]; sy = WSY[idx]; sw = WSW[idx]; sh = WSH[idx];
    open = WOPEN[idx]; minimized = WMIN[idx]; maximized = WMAX[idx];
    identity = WID[idx];
    for (i = idx; i > 0; i--) {
        WX[i] = WX[i - 1]; WY[i] = WY[i - 1];
        WW[i] = WW[i - 1]; WH[i] = WH[i - 1];
        WSX[i] = WSX[i - 1]; WSY[i] = WSY[i - 1];
        WSW[i] = WSW[i - 1]; WSH[i] = WSH[i - 1];
        WOPEN[i] = WOPEN[i - 1]; WMIN[i] = WMIN[i - 1];
        WMAX[i] = WMAX[i - 1]; WID[i] = WID[i - 1];
    }
    WX[0] = x; WY[0] = y; WW[0] = w; WH[0] = h;
    WSX[0] = sx; WSY[0] = sy; WSW[0] = sw; WSH[0] = sh;
    WOPEN[0] = open; WMIN[0] = minimized; WMAX[0] = maximized;
    WID[0] = identity;
}

int top_identity(void)
{
    int i;
    for (i = 0; i < (int)MAX_W; i++)
        if (WOPEN[i] && !WMIN[i])
            return WID[i];
    return -1;
}

int focus_identity(int id)
{
    int slot = find_slot(id);
    if (slot < 0)
        return -1;
    if (!WOPEN[slot]) {
        WOPEN[slot] = 1;
        WMIN[slot] = 0;
    }
    if (WMIN[slot])
        WMIN[slot] = 0;
    raise_window(slot);
    g_focused = WID[0];
    return 0;
}

int focus_next(void)
{
    int i;
    for (i = 1; i < (int)MAX_W; i++) {
        if (!WOPEN[i] || WMIN[i])
            continue;
        raise_window(i);
        g_focused = WID[0];
        return 0;
    }
    return -1;
}

void open_window(int id)
{
    int slot = find_slot(id);
    if (slot < 0)
        return;
    if (!WOPEN[slot]) {
        WOPEN[slot] = 1;
        WMIN[slot] = 0;
    }
    (void)focus_identity(id);
}

void layout_windows(void)
{
    long margin = g_pw >= 1000u ? 24 : 12;
    long gap = g_pw >= 1000u ? 16 : 10;
    long top = (long)PANEL_H + 14;
    long bottom = (long)g_ph - (long)TASKBAR_H;
    long work = bottom - top;
    int i;

    if (work < 300)
        work = 300;
    if (g_pw >= 1080u && work >= 500) {
        long th = (work * 54) / 100;
        long bh = work - th - gap;
        long w3 = ((long)g_pw - 2 * margin - 2 * gap) / 3;
        for (i = 0; i < 3; i++) {
            WX[i] = margin + i * (w3 + gap);
            WY[i] = top;
            WW[i] = w3;
            WH[i] = th;
        }
        for (i = 3; i < (int)MAX_W; i++) {
            WX[i] = margin + (i - 3) * (w3 + gap);
            WY[i] = top + th + gap;
            WW[i] = w3;
            WH[i] = bh;
        }
    } else {
        long w2 = ((long)g_pw - 2 * margin - gap) / 2;
        long h3 = (work - 2 * gap) / 3;
        if (h3 < 150) h3 = 150;
        for (i = 0; i < (int)MAX_W; i++) {
            int row = i / 2;
            int col = i % 2;
            WX[i] = margin + col * (w2 + gap);
            WY[i] = top + row * (h3 + gap);
            WW[i] = w2;
            WH[i] = h3;
        }
    }
    for (i = 0; i < (int)MAX_W; i++) {
        if (WW[i] < 260) WW[i] = 260;
        if (WH[i] < 150) WH[i] = 150;
        if (WX[i] + WW[i] > (long)g_pw)
            WX[i] = (long)g_pw - WW[i];
        if (WY[i] + WH[i] > bottom)
            WH[i] = bottom - WY[i];
        if (WX[i] < 0) WX[i] = 0;
        if (WY[i] < (long)PANEL_H) WY[i] = PANEL_H;
        WSX[i] = WX[i]; WSY[i] = WY[i];
        WSW[i] = WW[i]; WSH[i] = WH[i];
        WMAX[i] = 0;
    }
}

/* --- Win95 taskbar geometry -------------------------------------------- */

void taskbar_rect(long *x, long *y, long *w, long *h)
{
    *x = 0;
    *y = (long)g_ph - (long)TASKBAR_H;
    *w = (long)g_pw;
    *h = TASKBAR_H;
}

/* One task button per open window (minimized included), laid left to right
 * between the START button and the tray, in identity order — the same order
 * the drawing code walks. This helper is used by both draw and hit-test so
 * the two can never drift apart. */
static int taskbar_open_count(void)
{
    int id, n = 0;
    for (id = 0; id < (int)MAX_W; id++) {
        int s = find_slot(id);
        if (s >= 0 && WOPEN[s])
            n++;
    }
    return n;
}

int taskbar_button_rect(int id, long *bx, long *by, long *bw, long *bh)
{
    long ax, aw, w;
    int wid, n, opened = 0;

    if (id < 0 || id >= (int)MAX_W)
        return 0;
    n = taskbar_open_count();
    if (n <= 0)
        return 0;
    ax = TB_START_W;
    aw = (long)g_pw - TB_TRAY_W - ax - 12;
    if (aw < TB_BTN_MIN)
        aw = TB_BTN_MIN;
    w = aw / n;
    if (w > TB_BTN_MAX)
        w = TB_BTN_MAX;
    if (w < TB_BTN_MIN)
        w = TB_BTN_MIN;
    for (wid = 0; wid < (int)MAX_W; wid++) {
        int s = find_slot(wid);
        if (s < 0 || !WOPEN[s])
            continue;
        opened++;
        if (wid == id)
            break;
    }
    if (wid == (int)MAX_W)
        return 0;
    *bx = ax + (opened - 1) * w;
    *by = (long)g_ph - (long)TASKBAR_H + 4;
    *bw = w - 1;               /* 1px visual gap between task buttons */
    *bh = TASKBAR_H - 8;
    return *bw > 0;
}

int taskbar_button_hit(int id, long px, long py)
{
    long bx, by, bw, bh;
    if (!taskbar_button_rect(id, &bx, &by, &bw, &bh))
        return 0;
    return px >= bx && px < bx + bw && py >= by && py < by + bh;
}

int tray_rect(long *tx, long *ty, long *tw, long *th)
{
    if ((long)g_pw < TB_START_W + TB_TRAY_W + 40)
        return 0;
    *tx = (long)g_pw - TB_TRAY_W;
    *ty = (long)g_ph - (long)TASKBAR_H + 5;
    *tw = TB_TRAY_W;
    *th = TASKBAR_H - 10;
    return 1;
}

void start_menu_rect(long *x, long *y, long *w, long *h)
{
    *w = 310;
    if (*w > (long)g_pw - 20)
        *w = (long)g_pw - 20;
    *h = 52 + SM_ITEMS * SM_ITEM_H + 28;
    /* Anchored above the START button, which sits at the left of the
     * bottom taskbar (classic Win95 anchoring).  On short screens the
     * menu shrinks toward the top panel instead. */
    *x = 6;
    *y = (long)g_ph - (long)TASKBAR_H - *h - 4;
    if (*y < (long)PANEL_H)
        *y = (long)PANEL_H;
}

static SurfaceRect clip_intersect(SurfaceRect a, SurfaceRect b)
{
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    int y1 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    if (x1 <= x0 || y1 <= y0)
        return (SurfaceRect){0, 0, 0, 0};
    return (SurfaceRect){x0, y0, x1 - x0, y1 - y0};
}

void draw_window(int i)
{
    long x = WX[i], y = WY[i], w = WW[i], h = WH[i];
    int focused, identity;
    u32 fg;
    int outer_has_clip;
    SurfaceRect outer_clip;
    SurfaceRect client, content;

    if (!WOPEN[i] || WMIN[i])
        return;
    if (x >= (long)g_pw || y >= (long)g_ph)
        return;
    outer_has_clip = g_surface.has_clip;
    outer_clip = g_surface.clip;
    if (outer_has_clip && clip_intersect(outer_clip,
            (SurfaceRect){(int)x, (int)y, (int)w, (int)h}).width == 0)
        return;
    if (x + w > (long)g_pw) w = (long)g_pw - x;
    if (y + h > (long)g_ph) h = (long)g_ph - y;
    if (w < 140 || h < 80)
        return;
    identity = WID[i];
    focused = identity == g_focused;
    fg = contrast_text(win_accent(identity));

    round_rect_alpha(x + 7, y + 9, w, h, 14, 0x000000u, 70);
    round_rect_alpha(x + 3, y + 5, w, h, 13, 0x000000u, 80);
    round_rect_alpha(x + 1, y + 2, w, h, 12, 0x000000u, 90);

    round_rect(x, y, w, h, 12,
               focused ? win_accent(identity) : TH.border);
    round_rect(x + 2, y + 2, w - 4, h - 4, 10, TH.card);

    round_top_gradient(x + 2, y + 2, w - 4, TITLE_H,
                       10, win_accent(identity),
                       rgb_lerp(win_accent(identity), TH.card, 205));
    hline(x + 10, y + TITLE_H + 1, w - 20, TH.border);

    round_rect(x + 9, y + 7, 20, 20, 7,
               rgb_lerp(win_accent(identity), TH.card, 70));
    draw_icon(identity == WSYS ? ICON_SYS :
              identity == WFILES ? ICON_FILES :
              identity == WMOUSE ? ICON_MOUSE :
              identity == WABOUT ? ICON_INFO : ICON_LOG,
              x + 13, y + 11, 12, fg);
    text8_bold(WTITLE[identity], x + 37, y + 12, 1, fg);

    /* window buttons */
    {
        long cy = y + 17;
        long cxs[3] = {x + w - 68, x + w - 44, x + w - 20};
        int hover[3];
        for (int b = 0; b < 3; b++) {
            hover[b] = iabs(g_mx - cxs[b]) <= 10 &&
                       iabs(g_my - cy) <= 10;
            {
                u32 bbg = hover[b] ?
                          (b == 2 ? TH.danger : TH.card3) :
                          rgb_lerp(win_accent(identity), TH.card, 150);
                round_rect(cxs[b] - 9, cy - 9, 18, 18, 9, bbg);
                if (b == 0)
                    draw_icon(ICON_MIN, cxs[b] - 4, cy - 1, 8,
                              contrast_text(bbg));
                else if (b == 1)
                    draw_icon(ICON_MAX, cxs[b] - 4, cy - 4, 8,
                              contrast_text(bbg));
                else
                    draw_icon(ICON_CLOSE, cxs[b] - 4, cy - 4, 8,
                              contrast_text(bbg));
            }
        }
    }

    client = (SurfaceRect){(int)(x + 3), (int)(y + TITLE_H + 2),
                       (int)(w - 6), (int)(h - TITLE_H - 5)};
    content = outer_has_clip ? clip_intersect(client, outer_clip) : client;
    if (content.width <= 0 || content.height <= 0) {
        if (outer_has_clip)
            surface_set_clip(&g_surface, outer_clip);
        else
            surface_clear_clip(&g_surface);
        return;
    }
    surface_set_clip(&g_surface, content);

    if (identity == WSYS)
        draw_sys(x, y, w, h);
    else if (identity == WFILES)
        draw_files(x, y, w, h);
    else if (identity == WMOUSE)
        draw_mouse(x, y, w, h);
    else if (identity == WABOUT)
        draw_about(x, y, w, h);
    else
        draw_log(x, y, w, h);

    /* resize grip */
    for (int g = 0; g < 3; g++) {
        long gx = x + w - 8 - g * 3;
        long gy = y + h - 8;
        line(gx - 6, gy, gx, gy - 6, TH.border);
    }
    if (outer_has_clip)
        surface_set_clip(&g_surface, outer_clip);
    else
        surface_clear_clip(&g_surface);
}

void damage_window(int id)
{
    int slot = find_slot(id);
    if (slot < 0 || !WOPEN[slot] || WMIN[slot])
        return;
    desktop_damage(WX[slot] - 10, WY[slot] - 10,
                   WW[slot] + 20, WH[slot] + 20);
}

void toggle_maximize(int id)
{
    int slot = find_slot(id);
    long top = (long)PANEL_H + 8;
    long bottom = (long)g_ph - (long)TASKBAR_H;
    if (slot < 0 || !WOPEN[slot] || WMIN[slot])
        return;
    if (WMAX[slot]) {
        WX[slot] = WSX[slot]; WY[slot] = WSY[slot];
        WW[slot] = WSW[slot]; WH[slot] = WSH[slot];
        WMAX[slot] = 0;
        log_put2("restore ", WTITLE[id], TH.dim);
    } else {
        WSX[slot] = WX[slot]; WSY[slot] = WY[slot];
        WSW[slot] = WW[slot]; WSH[slot] = WH[slot];
        WX[slot] = 0;
        WY[slot] = top;
        WW[slot] = (long)g_pw;
        WH[slot] = bottom - top;
        WMAX[slot] = 1;
        log_put2("maximize ", WTITLE[id], TH.accent);
    }
    (void)focus_identity(id);
}

void minimize_identity(int id)
{
    int slot = find_slot(id);
    if (slot < 0 || !WOPEN[slot] || WMIN[slot])
        return;
    WMIN[slot] = 1;
    if (g_focused == id)
        g_focused = top_identity();
    log_put2("minimize ", WTITLE[id], TH.dim);
}

void close_identity(int id)
{
    int slot = find_slot(id);
    if (slot < 0)
        return;
    WOPEN[slot] = 0;
    WMIN[slot] = 0;
    if (g_focused == id)
        g_focused = top_identity();
    log_put2("close ", WTITLE[id], TH.danger);
}
