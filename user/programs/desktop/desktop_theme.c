/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/desktop/desktop_theme.c
 * Description: Theme switching for the desktop.
 * Added: 2026-10-22
 */
/* Theme switching for the desktop.
 *
 * Widgets read the active theme through TH at draw time, so switching only
 * has to bump the index and repaint.
 */

#include "desktop.h"

void cycle_theme(void)
{
    g_theme = (g_theme + 1) % (int)g_theme_count;
    set_toast(TH.name, TH.accent);
    log_put2("theme ", TH.name, TH.accent);
}
