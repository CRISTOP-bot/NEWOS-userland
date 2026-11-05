/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_state.c
 * Description: Shared desktop state.
 * Added: 2026-10-22
 */
/* Shared desktop state. */

#include "desktop.h"

/* Scene buffer and compositor staging buffer; sized to the framebuffer in
 * desktop_main() once the boot mode is known. */
u32 *canvas;
u32 *blit_buf;

/* One buffer-backed drawing surface and the window button set. */
Surface g_surface;
UiButton about_theme_btn, about_help_btn, about_term_btn;

u64 g_pw, g_ph;
u64 g_mem_used = 1, g_mem_total = 1, g_uptime;
int g_proc_count = 1;
u64 g_frame;
long g_mx = -1, g_my = -1;
int g_mb, g_mw;
int g_focused = WSYS;
char g_release[80] = "0.3.0-gui";
char g_machine[80] = "x86_64";

const UiTheme THEMES[] = {
    {
        "Midnight",
        0x101228, 0x29305d,
        0x1b1d36, 0x14162a,
        0x1b1d36,
        0x1b1d36, 0x242746, 0x2e3260,
        0xf4f5fa, 0xa1a5bb, 0x6f7392,
        0x4a4dc8, 0x35b6c8, 0x3a3f6b, 0x2b3f7a, 0xc42b1c, 0x7bd45a, 0xffd23f,
        0,
    },
    {
        "Win95",
        0x008080, 0x006666,
        0xc0c0c0, 0xa8a8a8,
        0xc0c0c0,
        0xc0c0c0, 0xd4d0c8, 0xe8e8e8,
        0x000000, 0x404040, 0x808080,
        0x000080, 0x1084d0, 0x808080, 0x000080, 0xc42b1c, 0x008000, 0xffd23f,
        1,
    },
    {
        "Periwinkle",
        0x4a4dc8, 0x7b7fe8,
        0xe8e8f0, 0xd8d8e8,
        0xe8e8f0,
        0xf4f5fa, 0xe8e8f0, 0xd8d8e8,
        0x1b1d36, 0x4a4d6b, 0x8a8da8,
        0x29305d, 0x4a4dc8, 0xb8bad0, 0xc8d0f0, 0xc42b1c, 0x2e7d32, 0xdc8621,
        0,
    },
};
unsigned g_theme_count = sizeof(THEMES) / sizeof(THEMES[0]);
int g_theme;

#define TH (THEMES[g_theme])

const char *const WTITLE[MAX_W] = {
    "System", "Files", "Mouse", "About", "Log"
};
long WX[MAX_W] = {24, 452, 880, 24, 648};
long WY[MAX_W] = {48, 48, 48, 440, 440};
long WW[MAX_W] = {410, 410, 376, 600, 600};
long WH[MAX_W] = {370, 370, 370, 290, 290};
int WOPEN[MAX_W] = {1, 1, 1, 1, 1};
int WMIN[MAX_W] = {0, 0, 0, 0, 0};
int WMAX[MAX_W] = {0, 0, 0, 0, 0};
long WSX[MAX_W], WSY[MAX_W], WSW[MAX_W], WSH[MAX_W];
int WID[MAX_W] = {WSYS, WFILES, WMOUSE, WABOUT, WLOG};

char log_msg[LOG_LINES][96];
char log_time[LOG_LINES][6];
u32 log_color[LOG_LINES];
int log_next, log_total;

char toast_text[96] = "";
u32 toast_color;
int toast_ttl;

int g_quit;

/* --- program list ----------------------------------------------------- */

char bin_entries[BIN_MAX][NSH_NAME_LEN];
int bin_entry_count;
int bin_scroll;
int bin_selected;
int help_open;
int sm_open, sm_sel;


int kbd_esc, kbd_csi, kbd_esc_idle;
int drag_mode;           /* 0 none, 1 move, 2 resize */
long drag_ox, drag_oy;
u64 last_title_frame;
int last_title_id = -1;
