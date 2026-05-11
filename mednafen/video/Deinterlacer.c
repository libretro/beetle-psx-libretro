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

#include <stdlib.h>
#include <string.h>

#include "../mednafen-types.h"
#include <boolean.h>

#include "surface.h"
#include "Deinterlacer.h"
#include "../../beetle_psx_globals.h"

/*
 * Layout the deinterlacer assumes:
 *
 *   - The surface holds an interlaced framebuffer where the GPU just
 *     wrote one field's row blocks.  At upscale shift s, each native
 *     scanline takes up `1 << s` consecutive rows in the surface.
 *   - For a frame's field f, this-field row blocks are at native
 *     line indices `f, f+2, f+4, ..., f + 2*(field_h - 1)`; the
 *     opposite field's row blocks (at f^1, f^1+2, ...) still hold
 *     the previous Process() call's data because nothing has
 *     overwritten them.
 *   - DisplayRect.h is the total interlaced height in native lines,
 *     i.e. 2 * field_h.  DisplayRect.y is the top native line.
 *     DisplayRect.x is normally 0; non-zero values come up only
 *     after an inter-field horizontal-resolution change and are
 *     handled by the XReposition path in WEAVE.
 *   - LineWidths[k] holds the active width (in native pixels) for
 *     native line k as written by the GPU.  The deinterlacer used
 *     to write this array but the libretro frontend only reads
 *     LineWidths[0]; we no longer touch it here at all.
 */

void Deinterlacer_Init(Deinterlacer *d)
{
   d->StateValid   = false;
   d->PrevDRect_h  = 0;
   d->PrevDRect_x  = 0;
   d->DeintType    = DEINT_WEAVE;
}

void Deinterlacer_Cleanup(Deinterlacer *d)
{
   /* No allocations to release - kept for API symmetry. */
   (void)d;
}

void Deinterlacer_SetType(Deinterlacer *d, unsigned dt)
{
   if (d->DeintType != dt)
   {
      d->DeintType  = dt;
      d->StateValid = false;
   }
}

unsigned Deinterlacer_GetType(const Deinterlacer *d)
{
   return d->DeintType;
}

void Deinterlacer_ClearState(Deinterlacer *d)
{
   d->StateValid  = false;
   d->PrevDRect_h = 0;
   d->PrevDRect_x = 0;
}

/*
 * Per-mode inner loops.  Each one runs `field_h` iterations and
 * reads/writes upscaled row blocks.  pitch_pix is the surface pitch
 * in pixels, up = 1 << s, copy_pix = surface_w << s if you want to
 * copy padding too, but we use the active native width from
 * LineWidths to avoid clobbering the opposite field's pixels past
 * the active area (matters for BOB/BOB_OFFSET when WEAVE is the
 * "fallback" recovery path users may switch to mid-frame).
 *
 * BOB writes are destructive to opposite-field data anyway (the
 * dest_block IS one of the row-block stripes the next frame's
 * GPU will use), but we still bound to the active width so the
 * far-right padding in the surface stays at whatever the GPU left
 * there.  Cheap insurance against future width-change shenanigans.
 */
static void deint_bob(uint32_t *pixels, int32_t pitch_pix,
      const MDFN_Rect *DisplayRect, const int32_t *LineWidths,
      bool field, unsigned s)
{
   const unsigned up      = 1u << s;
   const int32_t  field_h = DisplayRect->h / 2;
   const int32_t  dy      = DisplayRect->y;
   int32_t        k;

   for (k = 0; k < field_h; k++)
   {
      const int32_t   src_native = (k * 2) + (int32_t)field + dy;
      const int32_t   dst_native = k + dy;
      const size_t    copy_pix   = (size_t)LineWidths[src_native] << s;
      const uint32_t *src        = pixels
         + (size_t)(src_native << s) * pitch_pix
         + DisplayRect->x;
      uint32_t       *dst        = pixels
         + (size_t)(dst_native << s) * pitch_pix
         + DisplayRect->x;
      unsigned u;

      /* k=0, field=0 case: src and dst are the same row block; the
       * memcpy would be a no-op but skipping it saves the work. */
      if (src == dst)
         continue;

      for (u = 0; u < up; u++)
         memcpy(dst + (size_t)u * pitch_pix,
                src + (size_t)u * pitch_pix,
                copy_pix * sizeof(uint32_t));
   }
}

static void deint_bob_offset(uint32_t *pixels, int32_t pitch_pix,
      const MDFN_Rect *DisplayRect, const int32_t *LineWidths,
      bool field, unsigned s)
{
   const unsigned up      = 1u << s;
   const int32_t  field_h = DisplayRect->h / 2;
   const int32_t  dy      = DisplayRect->y;
   const int32_t  rect_end_native = dy + DisplayRect->h;
   int32_t        k;

   /*
    * For y=0 on the second field (field == true) only, the row block
    * two native lines above the first destination is from the
    * previous-previous frame and would show as stale.  Black it out
    * before the main loop runs; doing this once outside the loop
    * avoids the per-iteration `if (k == 0 && field)` branch.
    */
   if (field)
   {
      const int32_t  first_dly  = (int32_t)field + 1 + dy;        /* k=0 dly */
      const int32_t  blank_line = first_dly - 2;
      if (blank_line >= 0)
      {
         const size_t   copy_pix = (size_t)LineWidths[(int32_t)field + dy] << s;
         uint32_t      *dst      = pixels
            + (size_t)(blank_line << s) * pitch_pix;
         unsigned u;
         for (u = 0; u < up; u++)
         {
            uint32_t *row = dst + (size_t)u * pitch_pix;
            size_t x;
            for (x = 0; x < copy_pix; x++)
               row[x] = 0; /* MAKECOLOR(0,0,0,0) - all zero shifts */
         }
      }
   }

   for (k = 0; k < field_h; k++)
   {
      const int32_t   src_native = (k * 2) + (int32_t)field + dy;
      const int32_t   dly        = (k * 2) + (int32_t)field + 1 + dy;
      const size_t    copy_pix   = (size_t)LineWidths[src_native] << s;
      const uint32_t *src;
      uint32_t       *dst;
      unsigned u;

      if (dly >= rect_end_native)
         continue;

      src = pixels + (size_t)(src_native << s) * pitch_pix + DisplayRect->x;
      dst = pixels + (size_t)(dly        << s) * pitch_pix + DisplayRect->x;

      for (u = 0; u < up; u++)
         memcpy(dst + (size_t)u * pitch_pix,
                src + (size_t)u * pitch_pix,
                copy_pix * sizeof(uint32_t));
   }
}

/*
 * WEAVE in-place handler.
 *
 * The "happy path" is to do nothing at all - the surface already has
 * both fields' row blocks (this frame's just-written field plus the
 * previous frame's opposite field).  WEAVE is genuinely a no-op in
 * that case.
 *
 * The exception is XReposition: when the previous field had a wider
 * active area than the current field and was offset by a non-zero
 * DisplayRect.x, the leftover pixels at the right edge of the
 * previous-field row blocks would extend beyond where the current
 * field's content ends, and the surface presented to libretro would
 * have a horizontally misaligned previous-field stripe.  In that
 * case we shift the current field's row blocks left so their x=0
 * lines up with the previous field's x=0; the caller (libretro.c)
 * then resets DisplayRect.x to 0 so both fields are presented from
 * the same origin.
 *
 * Returns whether WEAVE was usable (state valid + height match).
 * On the no-state-yet first call, the surface still contains
 * whatever was there before (typically zeroed by alloc_surface);
 * that's a one-frame visual transient, not a correctness problem.
 */
static bool deint_weave(Deinterlacer *d, uint32_t *pixels, int32_t pitch_pix,
      MDFN_Rect *DisplayRect, const int32_t *LineWidths,
      bool field, unsigned s)
{
   const bool weave_good = (d->StateValid && d->PrevDRect_h == DisplayRect->h);
   const int32_t XReposition = (weave_good && DisplayRect->x > d->PrevDRect_x)
      ? DisplayRect->x : 0;

   if (XReposition)
   {
      const unsigned up      = 1u << s;
      const int32_t  field_h = DisplayRect->h / 2;
      const int32_t  dy      = DisplayRect->y;
      const size_t   shift_pix = (size_t)XReposition << s;
      int32_t        k;

      for (k = 0; k < field_h; k++)
      {
         const int32_t  src_native = (k * 2) + (int32_t)field + dy;
         const size_t   copy_pix   = (size_t)LineWidths[src_native] << s;
         uint32_t      *base       = pixels + (size_t)(src_native << s) * pitch_pix;
         unsigned u;
         for (u = 0; u < up; u++)
         {
            uint32_t *row = base + (size_t)u * pitch_pix;
            memmove(row, row + shift_pix, copy_pix * sizeof(uint32_t));
         }
      }
      DisplayRect->x = 0;
   }

   return weave_good;
}

void Deinterlacer_Process(Deinterlacer *d, MDFN_Surface *surface,
      MDFN_Rect *DisplayRect, int32_t *LineWidths, const bool field)
{
   const unsigned s          = psx_gpu_upscale_shift;
   uint32_t      *pixels     = surface->pixels;
   const int32_t  pitch_pix  = surface->pitchinpix;
   const int32_t  prev_h     = DisplayRect->h;
   const int32_t  prev_x     = DisplayRect->x;

   /*
    * The libretro frontend reads only LineWidths[0]; everything
    * past index 0 is scratch from the deinterlacer's perspective.
    * We still consult LineWidths[src_native] in BOB/BOB_OFFSET as
    * the active row width, so the caller must have those entries
    * populated by the GPU (which it does - GPU writes
    * LineWidths[dest_line] = dmw for every active scanline).
    *
    * One catch: for field=1 frames the GPU's dest_line values are
    * 1, 3, 5, ... so it never writes LineWidths[0].  The frame's
    * scanline-0 init in gpu.c also fills the array with placeholder
    * value 2 (an old PSX-side overscan-padding leftover), which is
    * what LineWidths[0] then carries when field=1.  The frontend
    * picks this up as `width = 2` and presents an 8-pixel-wide
    * column (or 2 at native), which appears as a black flicker on
    * every other frame.
    *
    * Fix: synthesize LineWidths[0] from the first written entry
    * (field + DisplayRect->y).  For field=0 this is LineWidths[0]
    * itself and the assignment is a no-op; for field=1 this is
    * LineWidths[1] which the GPU did write to dmw.  Cheap, and
    * doesn't depend on a magic placeholder value to detect.
    */
   {
      const int32_t first = (int32_t)field + DisplayRect->y;
      if (first < surface->h)
         LineWidths[0] = LineWidths[first];
   }

   switch (d->DeintType)
   {
      case DEINT_OFF:
         /* The combless presentation is achieved upstream in
          * gpu.c (LineSkipTest bypass + deferred end-of-frame
          * scanout).  Nothing to do here beyond the LineWidths[0]
          * fixup that runs ahead of the switch for all modes. */
         break;

      case DEINT_WEAVE:
         (void)deint_weave(d, pixels, pitch_pix, DisplayRect,
               LineWidths, field, s);
         break;

      case DEINT_BOB:
         deint_bob(pixels, pitch_pix, DisplayRect,
               LineWidths, field, s);
         break;

      case DEINT_BOB_OFFSET:
      default:
         deint_bob_offset(pixels, pitch_pix, DisplayRect,
               LineWidths, field, s);
         break;
   }

   d->StateValid  = (d->DeintType == DEINT_WEAVE);
   d->PrevDRect_h = prev_h;
   d->PrevDRect_x = prev_x;
}
