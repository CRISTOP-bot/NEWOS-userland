/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/df/df_main.c
 * Description: 1K blocks
 * Added: 2026-10-17
 */
#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    u64 total_b = 0, free_b = 0, used_b, pct;

    (void)argc;
    (void)argv;
    if (nsh_proc_meminfo(&total_b, &free_b) != 0) {
        static struct nsh_sysinfo si;
        if (sys_sysinfo(&si) != 0)
            return fail("df", "cannot read system info");
        total_b = si.total_frames * 4;   /* 1K blocks */
        free_b = si.free_frames * 4;
    }
    used_b = total_b >= free_b ? total_b - free_b : 0;
    pct = total_b ? (used_b * 100) / total_b : 0;
    nputs("Filesystem  1K-blocks  Used  Available  Use%  Mounted on\n");
    putf("memfs       %8u  %8u  %9u  %3u%%  /\n", total_b, used_b, free_b,
         pct);
    return 0;
}
