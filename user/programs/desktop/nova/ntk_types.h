// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
//
// NEWOS compatibility adaptation of Nova's ntk_types.h: only the portable
// color type used by ntk_color is retained; the upstream UI/poll types are not
// part of this framebuffer-only slice.

#ifndef NTK_TYPES_H
#define NTK_TYPES_H

#include <stdint.h>
typedef uint32_t NtkColor;

#endif /* NTK_TYPES_H */
