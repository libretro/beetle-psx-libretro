/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __MDFN_DEINTERLACER_H
#define __MDFN_DEINTERLACER_H

#include "../mednafen-types.h"
#include <boolean.h>

#include "surface.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   DEINT_BOB_OFFSET = 0, /* fallback default when state is reset */
   DEINT_BOB,
   DEINT_WEAVE,
   DEINT_OFF             /* SW renderer matches HW: bypass dfe and
                          * use deferred end-of-frame scanout */
};

/*
 * Software-renderer deinterlacer state.
 *
 * The deinterlacer sees an MDFN_Surface that the GPU has just
 * written one interlaced field of pixels into.  Output rows for
 * that field are at upscaled-row blocks
 *   [(2k + field) << s .. (2k + field) << s + up - 1]
 * for k in [0, native_field_h); the opposite-parity row blocks
 * still hold whatever was there from the previous Process() call.
 *
 * Three modes:
 *   - WEAVE:      no-op; trusts the surface to already hold both
 *                 fields' row blocks since the previous frame's
 *                 opposite field was never overwritten.  Surface
 *                 height presented to libretro is full interlaced
 *                 height.
 *   - BOB:        copies this field's row blocks into compact
 *                 sequential rows, halving vertical resolution.
 *                 Surface height presented to libretro is half.
 *   - BOB_OFFSET: copies this field's row blocks down by one
 *                 native row, leaving the originals in place so
 *                 the surface ends up double-rowed at full height.
 *
 * The previous implementation kept a separate FieldBuffer surface
 * and an LWBuffer line-widths array to support an out-of-place
 * WEAVE scheme that copied this field out, then copied the previous
 * field back in from the buffer.  The buffer round-trip was pure
 * waste; both fields are already in the surface, just at different
 * row-block stripes.  Dropped both members.
 */
typedef struct
{
   bool      StateValid;
   int32_t   PrevDRect_h;
   int32_t   PrevDRect_x;
   unsigned  DeintType;
} Deinterlacer;

void     Deinterlacer_Init(Deinterlacer *d);
void     Deinterlacer_Cleanup(Deinterlacer *d);

void     Deinterlacer_SetType(Deinterlacer *d, unsigned t);
unsigned Deinterlacer_GetType(const Deinterlacer *d);

void     Deinterlacer_Process(Deinterlacer *d, MDFN_Surface *surface,
            MDFN_Rect *DisplayRect, int32_t *LineWidths, const bool field);

void     Deinterlacer_ClearState(Deinterlacer *d);

#ifdef __cplusplus
}
#endif

#endif
