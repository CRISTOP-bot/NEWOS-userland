/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/doom/i_system.c
 * Description: NEWOS system services for the DOOM port (replaces upstream
 *              i_system.c).
 * Added: 2026-10-30
 */
/* i_system.c - NEWOS replacement for the upstream UNIX system layer.
 *
 * id Software's linuxdoom 1.10 keeps every operating-system service behind
 * four I_ files; this is the one that owns timing, the zone base, the fatal
 * error path and process start/teardown. The UNIX original used
 * gettimeofday()/usleep()/malloc plus X11-specific cleanup; here the same
 * jobs go through the NEWOS syscall ABI (SYS_CLOCK_GETTIME, SYS_SLEEP) and
 * the input/screen grab is released on every exit path so a crashed or
 * quit DOOM can never strand the console.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "doomdef.h"
#include "d_event.h"
#include "d_net.h"
#include "m_argv.h"
#include "m_misc.h"
#include "i_system.h"
#include "i_video.h"
#include "i_sound.h"

#include "../../lib/nshlib.h"

/* Zone memory budget in MiB. Upstream defaulted to 6 MiB and let the
 * SNDSERV table tune it; NEWOS grows the heap with SYS_BRK up to the
 * 256 MiB brk ceiling, so the shareware set fits with room to spare.
 */
int	mb_used = 8;

/* Rumble output: NEWOS has no force-feedback device. */
void
I_Tactile
( int	on,
  int	off,
  int	total )
{
    (void)on;
    (void)off;
    (void)total;
}

ticcmd_t	emptycmd;

ticcmd_t* I_BaseTiccmd (void)
{
    return &emptycmd;
}

int I_GetHeapSize (void)
{
    return mb_used * 1024 * 1024;
}

byte* I_ZoneBase (int *size)
{
    *size = mb_used * 1024 * 1024;
    return (byte *)malloc(*size);
}

/*
 * I_GetTime: game tics since start at TICRATE (35/s).
 *
 * CLOCK_MONOTONIC is the kernel tick clock (PIT, 1 kHz), so the tic count
 * cannot jump backwards if the RTC is stepped by `date`.
 */
int I_GetTime (void)
{
    struct timespec	ts;
    static long		base_sec = -1;
    long		sec;
    long		nsec;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (base_sec < 0)
	base_sec = (long)ts.tv_sec;

    sec  = (long)ts.tv_sec - base_sec;
    nsec = (long)ts.tv_nsec;
    if (nsec < 0) {
	nsec += 1000000000L;
	sec--;
    }
    return (int)(sec * TICRATE + nsec * TICRATE / 1000000000L);
}

void I_Init (void)
{
    I_InitSound();
}

/* Release everything the console was denied to us. Called from every exit
 * path, including I_Error(), because a grabbed keyboard would outlive the
 * process only until the kernel's on-exit cleanup - and the fbcon would
 * stay switched off.
 */
void I_Quit (void)
{
    D_QuitNetGame();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults();
    I_ShutdownGraphics();
}

void I_WaitVBL (int count)
{
    /* The panel has no retrace; one 60 Hz frame is the closest useful
     * delay and SYS_SLEEP yields to the scheduler.
     */
    sys_sleep((u64)count * 1000 / 60);
}

void I_BeginRead (void) { }
void I_EndRead (void) { }

/* "Low" DOS memory has no NEWOS meaning: the screen copies V_Init asks for
 * are ordinary heap.
 */
byte* I_AllocLow (int length)
{
    return (byte *)malloc((size_t)length);
}

void I_Error (char *error, ...)
{
    va_list	args;

    fflush(stdout);
    fprintf(stderr, "Error: ");
    va_start(args, error);
    vfprintf(stderr, error, args);
    va_end(args);
    fprintf(stderr, "\n\n");
    fflush(stderr);

    I_ShutdownGraphics();
    exit(1);
}
