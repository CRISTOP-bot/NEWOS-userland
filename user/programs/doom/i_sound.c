/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/doom/i_sound.c
 * Description: NEWOS audio layer for the DOOM port (replaces upstream
 *              i_sound.c).
 * Added: 2026-10-30
 */
/* i_sound.c - NEWOS replacement for the DMX/OSS sound driver.
 *
 * Upstream mixes the DOOM channels itself and hands PCM to the DMX library
 * (or writes an sndserver script under -DSNDSERV). NEWOS has no PCM path
 * reachable from ring 3 at all: the HDA driver is kernel-internal
 * (hda_play() takes a *physical* address and its only caller is the boot
 * self-test), there is no /dev/dsp, no audio syscall and no PC speaker.
 *
 * So this file implements the whole I_ audio contract against an absent
 * device: volumes and song state are tracked exactly as the engine expects,
 * no sound is ever claimed to be playing, and the one thing it does that is
 * visible is say so once, at startup. Adding real output is a backend swap
 * in I_SubmitSound() plus a PCM syscall; nothing in the engine changes.
 *
 * Music is a separate limit: DOOM plays MUS lumps through an OPL3 synthesiser
 * that upstream got from DMX. There is no FM synthesiser in NEWOS, so song
 * registration succeeds and stays silent rather than being faked.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "m_argv.h"
#include "w_wad.h"
#include "z_zone.h"
#include "sounds.h"
#include "i_sound.h"

static int	num_channels_want = 3;

/* Registered songs: DOOM registers at most one musinfo_t at a time but keeps
 * the handle it was given, so hand back the lump number.
 */
static int	song_playing = -1;
static int	song_paused;

void I_InitSound (void)
{
    if (M_CheckParm("-nosound"))
	return;

    printf("I_InitSound: no PCM output device is reachable from userland; "
	   "sound is off (see docs/development/doom.md)\n");
}

void I_ShutdownSound (void)
{
    song_playing = -1;
}

void I_InitMusic (void)
{
    song_playing = -1;
    song_paused = 0;
}

void I_ShutdownMusic (void)
{
    song_playing = -1;
}

void I_SetSfxVolume (int volume)
{
    /* Nothing to attenuate: there is no output stream. */
    (void)volume;
}

void I_SetMusicVolume (int volume)
{
    (void)volume;
}

void I_SetChannels (void)
{
    if (num_channels_want < 1)
	num_channels_want = 1;
}

/* The engine asks once per sound effect and caches the answer in
 * sfxinfo_t::lumpnum, so the "ds" prefix rule has to match upstream's.
 */
int
I_GetSfxLumpNum
( sfxinfo_t*	sfx )
{
    char	namebuf[9];

    sprintf(namebuf, "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int
I_StartSound
( int		id,
  int		vol,
  int		sep,
  int		pitch,
  int		priority )
{
    (void)id;
    (void)vol;
    (void)sep;
    (void)pitch;
    (void)priority;
    return -1;			/* no channel: nothing is playing */
}

void I_StopSound (int handle)
{
    (void)handle;
}

int I_SoundIsPlaying (int handle)
{
    (void)handle;
    return 0;
}

int I_SoundID (int handle)
{
    (void)handle;
    return 0;
}

void
I_UpdateSoundParams
( int		handle,
  int		vol,
  int		sep,
  int		pitch )
{
    (void)handle;
    (void)vol;
    (void)sep;
    (void)pitch;
}

void I_UpdateSound (void) { }
void I_SubmitSound (void) { }

void
I_SoundSetTimer
( int		condition )
{
    (void)condition;
}

void
I_SoundDelTimer
( int		condition )
{
    (void)condition;
}

void I_PauseSong (int handle)
{
    if (handle == song_playing)
	song_paused = 1;
}

void I_ResumeSong (int handle)
{
    if (handle == song_playing)
	song_paused = 0;
}

int
I_RegisterSong
( void*		data )
{
    (void)data;
    return 1;
}

void
I_PlaySong
( int		handle,
  int		looping )
{
    (void)handle;
    (void)looping;
    song_playing = handle;
    song_paused = 0;
}

void I_StopSong (int handle)
{
    if (handle == song_playing)
	song_playing = -1;
}

void I_UnRegisterSong (int handle)
{
    if (handle == song_playing)
	song_playing = -1;
}

int I_QrySongPlaying (int handle)
{
    return handle == song_playing && !song_paused;
}
