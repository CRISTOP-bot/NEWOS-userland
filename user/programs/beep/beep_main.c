/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/beep/beep_main.c
 * Description: NEWOS userland audio demo - plays an A4 tone via SYS_AUDIO_*.
 *              Proves the HDA PCM path is reachable from ring 3.
 * Added: 2026-11-23
 */
#include "../../lib/nshlib.h"

/* Quarter-wave table: A*sin(p*pi/128), A=20000, integer-only. */
static const short quarter[65] = {
    0, 491, 981, 1471, 1960, 2448, 2935, 3419, 3902, 4382, 4860,
    5334, 5806, 6274, 6738, 7198, 7654, 8105, 8551, 8992, 9428,
    9858, 10282, 10700, 11111, 11516, 11914, 12305, 12688, 13063,
    13431, 13791, 14142, 14485, 14819, 15144, 15460, 15767, 16064,
    16352, 16629, 16897, 17155, 17402, 17638, 17864, 18080, 18284,
    18478, 18660, 18831, 18991, 19139, 19276, 19401, 19514, 19616,
    19706, 19784, 19850, 19904, 19946, 19976, 19994, 20000
};

static short sine_sample(unsigned int phase)
{
    unsigned int idx = (phase >> 24) & 0xffu;
    unsigned int pos = idx & 0x3fu;
    short base = (idx >> 6) & 1u ? quarter[64u - pos] : quarter[pos];
    return (idx >> 6) & 2u ? (short)-base : base;
}

int main(int argc, char **argv)
{
    static short pcm[12000 * 2];        /* 0.25 s @ 48 kHz stereo 16-bit */
    unsigned int freq = 440;
    unsigned int step, phase = 0;
    long h, n;
    unsigned int i;

    (void)argc; (void)argv;
    if (argc > 1)
        freq = (unsigned int)natoi(argv[1]);
    if (!freq || freq > 20000)
        freq = 440;

    h = sys_audio_open();
    if (h < 0) {
        nputs("beep: no audio device (boot with -device intel-hda)\n");
        return 1;
    }

    /* 32-bit phase accumulator: step = freq * 2^32 / 48000. */
    step = (unsigned int)(((unsigned long long)freq << 32) / 48000ull);
    for (i = 0; i < 12000u; i++) {
        short v = sine_sample(phase);
        pcm[2u * i] = v;
        pcm[2u * i + 1u] = v;
        phase += step;
    }

    nputs("beep: playing tone\n");
    n = sys_audio_write(h, pcm, (u32)sizeof(pcm));
    putf("beep: played %d bytes\n", (int)n);
    sys_audio_close(h);
    return n == (long)sizeof(pcm) ? 0 : 1;
}
