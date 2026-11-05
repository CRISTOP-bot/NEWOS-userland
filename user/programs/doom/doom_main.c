/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/doom/doom_main.c
 * Description: /bin/doom entry point for the NEWOS DOOM port (replaces
 *              upstream i_main.c).
 * Added: 2026-10-30
 */
/* The /bin/doom entry point - upstream's i_main.c, with the NEWOS parts.
 *
 * Upstream installed SIGINT/SIGTERM handlers here so DMX could release its
 * DMA channel; NEWOS delivers no signals to userland (libc's signal() fails
 * honestly), so the shutdown path is the engine's own: m_menu.c and
 * g_game.c call I_Quit(), and atexit() covers I_Error()'s exit().
 *
 * What is left is the one job the engine cannot do for itself: hand it the
 * environment it insists on before D_DoomMain() reads it.
 *
 *   DOOMWADDIR  where D_FindIWAD() looks for an IWAD. The shareware doom1.wad
 *               ships at /usr/share/doom; a different IWAD dropped there, or
 *               named with -iwad, wins by the engine's own search order.
 *   HOME        d_main.c turns it into ~/.doomrc with sprintf and calls
 *               I_Error() when it is missing; /tmp is the writable tmpfs, so
 *               the config file is per-session unless the user says -config.
 */

#include <stdio.h>
#include <stdlib.h>

#include "doomdef.h"
#include "m_argv.h"
#include "d_main.h"
#include "i_system.h"

int
main
( int		argc,
  char**	argv )
{
    myargc = argc;
    myargv = argv;

    if (!getenv("DOOMWADDIR"))
	setenv("DOOMWADDIR", "/usr/share/doom", 1);
    if (!getenv("HOME"))
	setenv("HOME", "/tmp", 1);

    atexit(I_Quit);

    printf("DOOM Startup v1.10 (NEWOS port)\n");

    D_DoomMain();
    return 0;
}
