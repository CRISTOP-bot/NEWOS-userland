/*
 * Maintainer: Cristopher Borjas <borjascristopher88@gmail.com>
 * Source: userland/user/programs/doom/compat/values.h
 * Description: values.h shim for the NEWOS DOOM port.
 * Added: 2026-10-30
 */
/* values.h shim: <values.h> is a removed POSIX header that DOOM only uses
 * for its integer range macros. The upstream non-LINUX branch of doomtype.h
 * spells the same values out, so this file mirrors it verbatim and lets the
 * engine keep its -DLINUX build configuration.
 */
#ifndef __NEWOS_VALUES_H
#define __NEWOS_VALUES_H

#define MAXCHAR		((char)0x7f)
#define MAXSHORT	((short)0x7fff)
#define MAXINT		((int)0x7fffffff)
#define MAXLONG		((long)0x7fffffff)
#define MINCHAR		((char)0x80)
#define MINSHORT	((short)0x8000)
#define MININT		((int)0x80000000)
#define MINLONG		((long)0x80000000)

#define BITS32		32

#endif /* __NEWOS_VALUES_H */
