/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/doom/i_video.c
 * Description: NEWOS framebuffer console and input layer for the DOOM port
 *              (replaces upstream i_video.c).
 * Added: 2026-10-30
 */
/* i_video.c - NEWOS replacement for the X11 video/input driver.
 *
 * Upstream draws an 8-bit 320x200 image into screens[0] and asks this file
 * to put it on a display and to feed the game an event stream. Here that is
 * done with the NEWOS vendor syscalls only:
 *
 *   SYS_FBINFO        discover the panel (Limine hands one over, PVH/GRUB
 *                     do not, which is what makes a headless run possible)
 *   SYS_CONSOLE_GUI   take the panel from fbcon
 *   SYS_FBWRITE       blit the scaled frame as XRGB8888
 *   SYS_KBD_GRAB      route the PS/2 keyboard to SYS_KBD_READ
 *   SYS_KBD_READ      decoded bytes (ANSI escapes for the arrows)
 *   SYS_MOUSE_GET     absolute pointer state, sequenced per packet
 *
 * There is no double buffer and no retrace: the panel is uncacheable memory
 * behind SYS_FBWRITE, so tearing is inherent, exactly as it was over the X
 * ShmImage upstream.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_main.h"
#include "m_argv.h"
#include "v_video.h"
#include "i_system.h"
#include "i_video.h"

#include "../../lib/nshlib.h"

#define DOOM_W		320
#define DOOM_H		200
#define MAX_SCALE	4

/*
 * A key stays "down" this long after its last byte. The PS/2 keyboard
 * delivers typematic repeats (the driver never disables them), so holding a
 * key refreshes the latch about every 33 ms and releasing it lets the latch
 * expire; the alternative - raw make/break codes - is not exported to
 * userland by NEWOS. See docs/development/doom.md.
 */
#define KEY_HOLD_MS	120

extern int	mouseSensitivity;	/* m_menu.c, set from default.cfg */
extern int	usemouse;		/* m_misc.c, "use_mouse" default */

/* Set by -timedemo / -record: DOOM then wants to run flat out. */
extern boolean	timingdemo;

static int	nullvideo = 1;		/* no panel: draw nowhere */
static int	nomouse;		/* -nomouse */
static int	multiply = 1;
static int	outw, outh, orgx, orgy;
static u32	*outbuf;
static u32	pal_lut[256];
static int	graphics_started;
static long	frame_next_ms;

/* Input bookkeeping. */
static long	key_ms[256];
static u64	last_seq;
static s64	last_mx, last_my;
static u32	last_buttons;
static int	pending_escape;
static long	pending_escape_ms;

/* Monotonic milliseconds from the kernel tick clock. */
static long
now_ms (void)
{
    struct timespec	ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + (long)(ts.tv_nsec / 1000000L);
}

static void
set_neutral_palette (void)
{
    int	i;

    /* A grey ramp, so a frame shown before PLAYPAL arrives is readable
     * rather than random.
     */
    for (i = 0 ; i < 256 ; i++)
	pal_lut[i] = 0x00ffffffu & ((u32)i << 16 | (u32)i << 8 | (u32)i);
}

void
I_SetPalette (byte* palette)
{
    int		i;
    byte*	g;
    int		gamma = usegamma;

    /* The engine ships five gamma ramps (gammatable[5][256], v_video.c) and
     * the options menu cycles 0..4; a hand-edited default.cfg can say
     * anything, so clamp rather than read past the table.
     */
    if (gamma < 0 || gamma > 4)
	gamma = 0;
    g = gammatable[gamma];

    /* PLAYPAL is 256 consecutive RGB triples; walk the cursor with the loop,
     * otherwise every entry of the lookup table gets colour 0.
     */
    for (i = 0 ; i < 256 ; i++, palette += 3)
	pal_lut[i] = (u32)g[palette[0]] << 16
		   | (u32)g[palette[1]] << 8
		   | (u32)g[palette[2]];
}

/* Paint the whole panel black once, so the fbcon text underneath DOOM is
 * gone and the letterbox borders are part of the picture.
 */
static void
clear_panel (const struct nsh_fbinfo* fi)
{
    u32*	full;
    u64		n = fi->width * fi->height;
    struct nsh_fbwrite rq;

    full = (u32 *)malloc((size_t)(n * 4));
    if (!full)
	return;
    memset(full, 0, (size_t)(n * 4));

    rq.x = 0;
    rq.y = 0;
    rq.w = fi->width;
    rq.h = fi->height;
    rq.pixels = (u64)full;
    rq.pixlen = n * 4;
    sys_fbwrite(&rq);
    free(full);
}

void I_InitGraphics (void)
{
    struct nsh_fbinfo	fi;
    int			fit;
    int			want;
    int			i;

    set_neutral_palette();
    nomouse = M_CheckParm("-nomouse");

    nullvideo = 1;
    if (sys_fbinfo(&fi) != 0 || !fi.present || fi.bpp != 32 ||
	fi.width < (nsh_u64)DOOM_W || fi.height < (nsh_u64)DOOM_H) {
	printf("I_InitGraphics: no framebuffer panel, running headless\n");
	return;
    }

    fit = 1;
    for (i = 1 ; i <= MAX_SCALE ; i++)
	if ((long)DOOM_W * i <= (long)fi.width &&
	    (long)DOOM_H * i <= (long)fi.height)
	    fit = i;

    /* Two pixels per game pixel is the default: it is the largest scale
     * that keeps a full 35 fps blit cheap on the uncacheable panel. Ask for
     * more with -scale 3 / -scale 4.
     */
    want = fit < 2 ? fit : 2;
    if ((i = M_CheckParm("-scale")) && i < myargc - 1) {
	int	s = atoi(myargv[i + 1]);

	if (s < 1)
	    s = 1;
	if (s > MAX_SCALE)
	    s = MAX_SCALE;
	if (s > fit) {
	    printf("I_InitGraphics: -scale %d does not fit the panel, "
		   "using %d\n", s, fit);
	    s = fit;
	}
	want = s;
    }

    multiply = want;
    outw = DOOM_W * multiply;
    outh = DOOM_H * multiply;
    orgx = (int)((fi.width - (nsh_u64)outw) / 2);
    orgy = (int)((fi.height - (nsh_u64)outh) / 2);

    outbuf = (u32 *)malloc((size_t)outw * outh * 4);
    if (!outbuf)
	I_Error("I_InitGraphics: out of memory for %dx%d", outw, outh);

    if (sys_console_gui(1) != 0)
	printf("I_InitGraphics: another GUI owns the panel\n");
    if (sys_kbd_grab(1) != 0)
	printf("I_InitGraphics: keyboard grab refused\n");

    clear_panel(&fi);
    nullvideo = 0;
    graphics_started = 1;
    printf("I_InitGraphics: %dx%d -> %dx%d at (%d,%d)\n",
	   DOOM_W, DOOM_H, outw, outh, orgx, orgy);
}

void I_ShutdownGraphics (void)
{
    if (!graphics_started)
	return;
    graphics_started = 0;
    sys_kbd_grab(0);
    sys_console_gui(0);
    free(outbuf);
    outbuf = 0;
}

void I_UpdateNoBlit (void) { }

void I_FinishUpdate (void)
{
    struct nsh_fbwrite	rq;
    byte*		src;
    u32*		row;
    int			x, y, i;

    if (nullvideo || !outbuf)
	return;

    /* -devparm draws the tic-rate dots along the bottom of the game image,
     * exactly where the DOS version put them.
     */
    if (devparm) {
	static int	lasttic;
	int		t = I_GetTime();
	int		n = t - lasttic;

	lasttic = t;
	if (n > 20)
	    n = 20;
	for (i = 0 ; i < n * 2 ; i += 2)
	    screens[0][(DOOM_H - 1) * DOOM_W + i] = 0xff;
	for ( ; i < 40 ; i += 2)
	    screens[0][(DOOM_H - 1) * DOOM_W + i] = 0x0;
    }

    for (y = 0 ; y < DOOM_H ; y++) {
	src = screens[0] + y * DOOM_W;
	row = outbuf + (long)y * multiply * outw;
	for (x = 0 ; x < DOOM_W ; x++) {
	    u32	p = pal_lut[src[x]];
	    int	k;

	    for (k = 0 ; k < multiply ; k++)
		row[x * multiply + k] = p;
	}
	for (i = 1 ; i < multiply ; i++)
	    memcpy(row + (long)i * outw, row, (size_t)outw * 4);
    }

    rq.x = (nsh_u64)orgx;
    rq.y = (nsh_u64)orgy;
    rq.w = (nsh_u64)outw;
    rq.h = (nsh_u64)outh;
    rq.pixels = (u64)outbuf;
    rq.pixlen = (nsh_u64)outw * outh * 4;
    sys_fbwrite(&rq);
}

void I_ReadScreen (byte* scr)
{
    memcpy(scr, screens[0], DOOM_W * DOOM_H);
}

/*
 * Input.
 *
 * The keyboard arrives as decoded bytes, so a key press is a byte and a held
 * key is a stream of that byte at the typematic rate. gamekeydown[] wants
 * edges, therefore a key counts as held while its byte keeps arriving, and
 * the release is posted when the latch expires.
 */
static void
post_key (int keynum, int down)
{
    event_t	ev;

    if (keynum < 0 || keynum >= 256)
	return;
    memset(&ev, 0, sizeof(ev));
    ev.type = down ? ev_keydown : ev_keyup;
    ev.data1 = keynum;
    D_PostEvent(&ev);
}

static int
translate_byte (unsigned char c)
{
    switch (c) {
    case 13:
    case 10:    return KEY_ENTER;	/* the PS/2 driver sends '\n' for Enter */
    case 9:     return KEY_TAB;
    case 127:   return KEY_BACKSPACE;
    case ' ':   return ' ';
    default:
	if (c < 32)
	    return 0;			/* bare control byte: no DOOM key */
	if (c > 127)
	    return 0;			/* UTF-8 payload: not a DOOM key */
	return c;
    }
}

static void
read_keyboard (void)
{
    unsigned char	buf[64];
    long		n;
    long		i;
    long		now = now_ms();
    int		k;

    n = sys_kbd_read(buf, sizeof(buf));

    /* A lone ESC has no follow-up byte to disambiguate it against, and the
     * kernel sends no break codes, so the deferral has to expire on its own:
     * without this the menu key only ever arrived bundled with the *next*
     * press. 40 ms is one I_StartTic poll at TICRATE plus slack.
     */
    if (pending_escape && now - pending_escape_ms >= 40) {
	pending_escape = 0;
	post_key(KEY_ESCAPE, 1);
	key_ms[KEY_ESCAPE] = now;
    }

    for (i = 0 ; i < n ; i++) {

	/* An escape sequence can straddle a read boundary: a trailing ESC
	 * is held for one poll before it is reported as the menu key.
	 */
	if (pending_escape) {
	    pending_escape = 0;
	    if (buf[i] != '[') {
		post_key(KEY_ESCAPE, 1);
		key_ms[KEY_ESCAPE] = now;
	    }
	}
	if (buf[i] == 0x1b) {
	    if (i + 2 < n && buf[i + 1] == '[') {
		int	k2 = 0;

		switch (buf[i + 2]) {
		case 'A': k2 = KEY_UPARROW; break;
		case 'B': k2 = KEY_DOWNARROW; break;
		case 'C': k2 = KEY_RIGHTARROW; break;
		case 'D': k2 = KEY_LEFTARROW; break;
		default:  k2 = 0; break;
		}
		i += 2;
		if (k2) {
		    post_key(k2, 1);
		    key_ms[k2] = now;
		    continue;
		}
		/* Unknown sequence: report plain ESC and rescan the rest. */
		post_key(KEY_ESCAPE, 1);
		key_ms[KEY_ESCAPE] = now;
		i -= 2;
		continue;
	    }
	    pending_escape = 1;
	    pending_escape_ms = now;
	    continue;
	}

	k = translate_byte(buf[i]);
	if (!k)
	    continue;
	if (!key_ms[k])
	    post_key(k, 1);
	key_ms[k] = now;
    }

    for (k = 0 ; k < 256 ; k++) {
	if (key_ms[k] && now - key_ms[k] > KEY_HOLD_MS) {
	    key_ms[k] = 0;
	    post_key(k, 0);
	}
    }
}

static void
read_mouse (void)
{
    struct nsh_mouse	m;
    event_t		ev;
    long		dx = 0;
    long		dy = 0;

    if (sys_mouse_get(&m) != 0)
	return;
    if (!m.seq)
	return;
    if (last_seq == m.seq)
	return;

    if (last_seq) {
	dx = (long)(m.x - last_mx);
	dy = (long)(m.y - last_my);
    }
    last_seq = m.seq;
    last_mx = m.x;
    last_my = m.y;

    if (m.buttons == last_buttons && !dx && !dy)
	return;
    last_buttons = m.buttons;

    memset(&ev, 0, sizeof(ev));
    ev.type = ev_mouse;
    ev.data1 = (int)m.buttons;
    ev.data2 = (int)dx;
    ev.data3 = (int)dy;
    D_PostEvent(&ev);
}

void I_GetEvent (void)
{
    read_keyboard();
    if (usemouse && !nomouse)
	read_mouse();
}

/* One loop iteration per game frame: pace to TICRATE unless the caller is
 * measuring raw throughput (-timedemo) or has no panel at all.
 */
void I_StartFrame (void)
{
    long	target;
    long	now;

    if (nullvideo || timingdemo)
	return;

    if (!frame_next_ms) {
	frame_next_ms = now_ms() + 1000 / TICRATE;
	return;
    }
    target = frame_next_ms;
    now = now_ms();
    if (now < target) {
	sys_sleep((u64)(target - now));
    }
    frame_next_ms = target + 1000 / TICRATE;
    /* Fall far behind (a save, a palette rebuild) and restart the clock
     * instead of firing a burst of catch-up frames.
     */
    if (frame_next_ms < now_ms() - 1000 / TICRATE)
	frame_next_ms = now_ms() + 1000 / TICRATE;
}

void I_StartTic (void)
{
    I_GetEvent();
}
