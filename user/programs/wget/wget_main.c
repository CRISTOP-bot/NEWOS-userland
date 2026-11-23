/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/wget/wget_main.c
 * Description: NEWOS wget — HTTP GET via nsh_http_fetch.
 * Added: 2026-11-22
 */
#include "../../lib/nshlib.h"

int main(int argc, char **argv)
{
    int out_fd = 1;
    int head_only = 0;
    const char *url = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (nstrcmp(argv[i], "-q") == 0)
            continue;
        if (nstrcmp(argv[i], "-O") == 0) {
            if (i + 1 < argc) {
                out_fd = (int)sys_open(argv[i + 1], O_WRITE | O_CREATE);
                i++;
            }
            continue;
        }
        if (nstrcmp(argv[i], "--head") == 0) {
            head_only = 1;
            continue;
        }
        url = argv[i];
    }

    if (!url)
        return fail("wget", "usage: wget [-O FILE] URL");

    if (nsh_http_fetch(url, out_fd, head_only, 0, 0) != 0)
        return fail("wget", "download failed");
    if (out_fd != 1)
        sys_close(out_fd);
    return 0;
}
