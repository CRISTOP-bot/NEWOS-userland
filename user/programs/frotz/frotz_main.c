/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/frotz/frotz_main.c
 * Description: frotz_main.c - /bin/frotz entry for the NEWOS Frotz port.
 * Added: 2026-10-22
 */
/* frotz_main.c - /bin/frotz entry for the NEWOS Frotz port.
 *
 * Frotz 2.44 (Z-machine interpreter) is GPL-2+, upstream
 * https://github.com/DavidGriffith/frotz (see COPYING.frotz and README).
 * The upstream entry point lives in frotz_run.c, renamed main ->
 * frotz_main. This thin file only adapts it to the NEWOS per-program
 * main(argc, argv) convention carried by user/lib/start.S. */

#include "../../lib/nshlib.h"

/* Entry point implemented by frotz_run.c (upstream main.c, renamed main
 * -> frotz_main for the NEWOS /bin/frotz convention). */
int frotz_main(int argc, char *argv[]);

int main(int argc, char **argv)
{
    return frotz_main(argc, argv);
}
