/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop.h
 * Description: NEWOS desktop alpha: shared declarations.
 * Added: 2026-10-22
 */
/* NEWOS desktop alpha: shared declarations.
 *
 * The desktop is split into small modules so the drawing surface, the widget
 * layer and the compositor can grow independently. The compositor keeps one
 * XRGB canvas but only repaints and submits the rectangles that actually
 * changed; it never streams a full frame unless the caller asks for it.
 */

#ifndef NEWOS_DESKTOP_H
#define NEWOS_DESKTOP_H

#include "../../lib/nshlib.h"
#include "desktop_surface.h"
#include "desktop_widgets.h"

#define PANEL_H       34
#define TASKBAR_H     40
#define TITLE_H       34
#define MAX_W         5
#define DESK_MAX_W    2048u
#define DESK_MAX_H    1200u
#define BIN_MAX       64
#define LOG_LINES     12

/* --- Win95 taskbar metrics --------------------------------------------- */

#define TB_START_W    108        /* START button, bar-relative left block  */
#define TB_TRAY_W     104        /* clock tray, bar-relative right block   */
#define TB_BTN_MAX    160        /* task button width cap                  */
#define TB_BTN_MIN    44         /* task button width floor                */

#define WSYS    0
#define WFILES  1
#define WMOUSE  2
#define WABOUT  3
#define WLOG    4

#define SM_ITEMS 8
#define SM_ITEM_H 34

/* --- theme ------------------------------------------------------------ */

typedef struct {
    const char *name;
    u32 bg0, bg1;
    u32 panel0, panel1;
    u32 dock;
    u32 card, card2, card3;
    u32 text, dim, muted;
    u32 accent, accent2, border, select, danger, ok, warn;
    int unified_titles;
} UiTheme;

extern const UiTheme THEMES[];
extern unsigned g_theme_count;
extern int g_theme;
#define TH (THEMES[g_theme])

/* --- shared state ----------------------------------------------------- */

extern u32 canvas[DESK_MAX_W * DESK_MAX_H];
extern Surface g_surface;
extern UiButton about_theme_btn, about_help_btn, about_term_btn;

extern u64 g_pw, g_ph;
extern u64 g_mem_used, g_mem_total, g_uptime;
extern int g_proc_count;
extern u64 g_frame;
extern long g_mx, g_my;
extern int g_mb, g_mw;
extern int g_focused;
extern char g_release[80];
extern char g_machine[80];
extern int g_quit;

extern const char *const WTITLE[MAX_W];
extern long WX[MAX_W], WY[MAX_W], WW[MAX_W], WH[MAX_W];
extern int WOPEN[MAX_W], WMIN[MAX_W], WMAX[MAX_W];
extern long WSX[MAX_W], WSY[MAX_W], WSW[MAX_W], WSH[MAX_W];
extern int WID[MAX_W];

extern char log_msg[LOG_LINES][96];
extern char log_time[LOG_LINES][6];
extern u32 log_color[LOG_LINES];
extern int log_next, log_total;

extern char toast_text[96];
extern u32 toast_color;
extern int toast_ttl;

extern char bin_entries[BIN_MAX][NSH_NAME_LEN];
extern int bin_entry_count;
extern int bin_scroll;
extern int bin_selected;
extern int help_open;
extern int sm_open, sm_sel;

extern int kbd_esc, kbd_csi, kbd_esc_idle;
extern int drag_mode;
extern long drag_ox, drag_oy;
extern u64 last_title_frame;
extern int last_title_id;

/* --- graphics --------------------------------------------------------- */

enum {
    ICON_LOGO, ICON_SYS, ICON_FILES, ICON_MOUSE, ICON_INFO,
    ICON_LOG, ICON_TERM, ICON_HELP, ICON_THEME, ICON_POWER,
    ICON_IMG, ICON_VID, ICON_EDIT, ICON_PKG, ICON_CHIP,
    ICON_DESKTOP, ICON_APP, ICON_TEST,
    ICON_MIN, ICON_MAX, ICON_CLOSE
};

long iabs(long v);
u32 isqrt32(u32 v);
u32 rgb_lerp(u32 a, u32 b, u32 t);
u32 contrast_text(u32 bg);
void px_set(long x, long y, u32 color);
void px_blend(long x, long y, u32 color, u32 alpha);
void solid(long x, long y, long w, long h, u32 color);
void hline(long x, long y, long w, u32 color);
void vline(long x, long y, long h, u32 color);
void hline_alpha(long x, long y, long w, u32 color, u32 alpha);
void round_rect(long x, long y, long w, long h, long r, u32 color);
void round_rect_alpha(long x, long y, long w, long h, long r, u32 color,
                      u32 alpha);
void disc(long cx, long cy, long r, u32 color);
void disc_alpha(long cx, long cy, long r, u32 color, u32 alpha);
void ring(long cx, long cy, long r, long thick, u32 color);
void line(long x0, long y0, long x1, long y1, u32 color);
u64 text8_width(const char *s, u64 sc);
void text8_max(const char *s, long x, long y, u64 sc, u32 fg, long maxw);
void text8(const char *s, long x, long y, u64 sc, u32 fg);
void text8_bold(const char *s, long x, long y, u64 sc, u32 fg);
void text8_right(const char *s, long right, long y, u64 sc, u32 fg);
void text8_center(const char *s, long cx, long y, u64 sc, u32 fg);
char *put_u64(char *dst, u64 v);
char *put_2d(char *dst, u64 v);
void fmt_uptime(char *out, u64 sec);
void fmt_clock(char *out, const struct nsh_time *t);
void fmt_date(char *out, const struct nsh_time *t);
void w95_bevel(long x, long y, long w, long h, u32 face);
void w95_bevel_pressed(long x, long y, long w, long h, u32 face);
void w95_screen(long x, long y, long w, long h);
void draw_icon(int kind, long x, long y, long s, u32 color);
int prog_icon(const char *name);
void draw_keycap(const char *label, long x, long y);
void gradient_rect(long x, long y, long w, long h, u32 top_rgb,
                   u32 bottom_rgb);
void round_top_gradient(long x, long y, long w, long h, long r, u32 top_rgb,
                        u32 bottom_rgb);

/* --- window manager --------------------------------------------------- */

u32 win_accent(int id);
int find_slot(int id);
void raise_window(int idx);
int top_identity(void);
int focus_identity(int id);
int focus_next(void);
void open_window(int id);
void layout_windows(void);
void taskbar_rect(long *x, long *y, long *w, long *h);
void start_menu_rect(long *x, long *y, long *w, long *h);
int taskbar_button_rect(int id, long *bx, long *by, long *bw, long *bh);
int taskbar_button_hit(int id, long px, long py);
int tray_rect(long *tx, long *ty, long *tw, long *th);
void toggle_maximize(int id);
void minimize_identity(int id);
void close_identity(int id);
void draw_window(int i);
void damage_window(int id);

/* --- theme service ---------------------------------------------------- */

void cycle_theme(void);

/* --- shell ------------------------------------------------------------ */

void log_put(const char *msg, u32 color);
void log_put2(const char *a, const char *b, u32 color);
void set_toast(const char *msg, u32 color);
void toast_rect(long *x, long *y, long *w, long *h);
void damage_toast(void);
void draw_wallpaper_rect(SurfaceRect r);
void draw_wallpaper(void);
void draw_panel(void);
void draw_taskbar(void);
void draw_start_menu(void);
void draw_toast(void);
void draw_help(void);

/* --- applications ----------------------------------------------------- */

void load_bin_entries(void);
int files_visible(long h);
long files_list_top(long y);
int files_row_at(long px, long py);
void select_entry(int delta);
const char *prog_desc(const char *name);
u32 prog_color(const char *name);
void launch_path(const char *path, const char *arg, const char *arg2);
void launch_bin_entry(int entry);
void draw_kv(const char *label, const char *value, long x, long y, long w,
             u32 lc, u32 vc);
void draw_tile(const char *label, const char *value, long x, long y, long w,
               long h, u32 accent);
void draw_sys(long x, long y, long w, long h);
void draw_files(long x, long y, long w, long h);
void draw_mouse(long x, long y, long w, long h);
void draw_about(long x, long y, long w, long h);
void draw_log(long x, long y, long w, long h);

/* --- input ------------------------------------------------------------ */

void start_menu_action(int item);
void esc_action(void);
void menu_up(void);
void menu_down(void);
void arrow_up(void);
void arrow_down(void);
int handle_click(void);
void handle_key(unsigned char kc);

/* --- compositor ------------------------------------------------------- */

void desktop_damage(long x, long y, long w, long h);
void desktop_damage_all(void);
void desktop_damage_at(long x, long y);
void desktop_present(void);

#endif /* NEWOS_DESKTOP_H */
