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
   DEINT_BOB_OFFSET = 0, /* Code will fall-through to this case under certain conditions, too. */
   DEINT_BOB,
   DEINT_WEAVE
};

/*
 * Plain-C deinterlacer state. Was a C++ class with a std::vector
 * member and a templated InternalProcess<T>; now a plain struct
 * with a manually-sized int32_t buffer. The template parameter
 * T was always uint32 in this build (the libretro core fixes
 * NEED_BPP=32, the WANT_16BPP path was never instantiated) and
 * has been collapsed to plain uint32_t throughout.
 */
typedef struct
{
   MDFN_Surface *FieldBuffer;

   /*
    * Per-output-line width buffer. Sized to FieldBuffer->h on
    * (re)allocation of FieldBuffer; written by the WEAVE path of
    * the inner loop, read on the next call when the previously-
    * stored field is woven into the current surface.
    */
   int32_t      *LWBuffer;
   size_t        LWBuffer_size;       /* in entries */

   bool          StateValid;
   MDFN_Rect     PrevDRect;
   unsigned      DeintType;
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
