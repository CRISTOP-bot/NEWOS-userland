/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/doom/compat/malloc.h
 * Description: malloc.h shim for the NEWOS DOOM port.
 * Added: 2026-10-30
 */
/* malloc.h shim: w_wad.c includes it only for the allocator prototypes,
 * which NEWOS libc already declares in <stdlib.h>.
 */
#ifndef __NEWOS_MALLOC_H
#define __NEWOS_MALLOC_H

#include <stdlib.h>

#endif /* __NEWOS_MALLOC_H */
