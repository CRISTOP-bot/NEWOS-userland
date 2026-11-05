/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/doom/i_net.c
 * Description: NEWOS network layer for the DOOM port (replaces upstream
 *              i_net.c).
 * Added: 2026-10-30
 */
/* i_net.c - NEWOS replacement for the upstream DOOMCOM/UDP network driver.
 *
 * The released linuxdoom talks IPX-over-UDP through <sys/socket.h>,
 * gethostname() and a BSD socket pair. NEWOS has no IP stack (PCnet loopback
 * only), so this layer implements the single-player path and refuses -net
 * explicitly instead of pretending to join a game: the engine's net code
 * (d_net.c) is driven entirely through the doomcom record filled in here, so
 * one node with ticdup 1 is the honest, complete configuration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_net.h"
#include "d_event.h"
#include "m_argv.h"
#include "i_system.h"
#include "i_net.h"

/* The engine expects a heap record it can keep a pointer to for its
 * lifetime; it is never freed.
 */
static	doomcom_t	doomcom_store;

void I_NetCmd (void)
{
    /* No remote nodes exist, so there is nothing to send or receive. */
}

void I_InitNetwork (void)
{
    int	i;

    doomcom = &doomcom_store;
    memset(doomcom, 0, sizeof(*doomcom));

    doomcom->id = DOOMCOM_ID;
    doomcom->ticdup = 1;
    doomcom->extratics = 0;

    if ((i = M_CheckParm("-ticdup")) && i < myargc - 1) {
	doomcom->ticdup = myargv[i + 1][0] - '0';
	if (doomcom->ticdup < 1)
	    doomcom->ticdup = 1;
	if (doomcom->ticdup > 9)
	    doomcom->ticdup = 9;
    }

    if (M_CheckParm("-net"))
	I_Error("I_InitNetwork: NEWOS has no IP network stack; "
		"DOOM is single player only");

    /* Single player. */
    netgame = false;
    deathmatch = false;
    doomcom->numnodes = 1;
    doomcom->numplayers = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
    doomcom->skill = sk_medium;

    consoleplayer = 0;
}
