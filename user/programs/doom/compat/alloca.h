/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/doom/compat/alloca.h
 * Description: alloca.h shim for the NEWOS DOOM port.
 * Added: 2026-10-30
 */
/* alloca.h shim: r_data.c and w_wad.c include it under -DLINUX only to get
 * the declaration. GCC provides alloca as a builtin, so the declaration is
 * all that is needed; the frame-pointer-free NEWOS ABI keeps it valid.
 */
#ifndef __NEWOS_ALLOCA_H
#define __NEWOS_ALLOCA_H

#define alloca __builtin_alloca

#endif /* __NEWOS_ALLOCA_H */
