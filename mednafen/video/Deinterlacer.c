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

#define BOB_LORES

void Deinterlacer_Init(Deinterlacer *d)
{
   d->FieldBuffer    = NULL;
   d->LWBuffer       = NULL;
   d->LWBuffer_size  = 0;
   d->StateValid     = false;
   d->DeintType      = DEINT_WEAVE;

   d->PrevDRect.x    = 0;
   d->PrevDRect.y    = 0;
   d->PrevDRect.w    = 0;
   d->PrevDRect.h    = 0;
}

void Deinterlacer_Cleanup(Deinterlacer *d)
{
   if (d->FieldBuffer)
   {
      MDFN_Surface_Delete(d->FieldBuffer);
      d->FieldBuffer = NULL;
   }
   if (d->LWBuffer)
   {
      free(d->LWBuffer);
      d->LWBuffer      = NULL;
      d->LWBuffer_size = 0;
   }
}

void Deinterlacer_SetType(Deinterlacer *d, unsigned dt)
{
   if (d->DeintType != dt)
   {
      d->DeintType = dt;

      /* std::vector's resize(0) doesn't deallocate, just adjusts
       * size; here we keep the LWBuffer allocation around for
       * reuse by the next field-buffer reallocation. */
      if (d->FieldBuffer)
      {
         MDFN_Surface_Delete(d->FieldBuffer);
         d->FieldBuffer = NULL;
      }
      d->StateValid = false;
   }
}

unsigned Deinterlacer_GetType(const Deinterlacer *d)
{
   return d->DeintType;
}

void Deinterlacer_ClearState(Deinterlacer *d)
{
   d->StateValid    = false;

   d->PrevDRect.x   = 0;
   d->PrevDRect.y   = 0;
   d->PrevDRect.w   = 0;
   d->PrevDRect.h   = 0;
}

/*
 * Resize the LWBuffer to hold `n` int32_t entries. Was previously
 * std::vector::resize(); now a manual realloc that also tracks
 * allocated capacity. We don't shrink (matching std::vector's
 * typical strategy of "capacity grows, doesn't fall") since the
 * only call sites either grow it to FieldBuffer->h or no-op.
 */
static void LWBuffer_Resize(Deinterlacer *d, size_t n)
{
   if (n <= d->LWBuffer_size)
      return;
   d->LWBuffer      = (int32_t *)realloc(d->LWBuffer, n * sizeof(int32_t));
   d->LWBuffer_size = n;
}

/*
 * Was a template<typename T> in C++. T resolved to uint32 for
 * the only build configuration the libretro core uses
 * (NEED_BPP=32 unconditionally in Makefile); the WANT_16BPP
 * path was dead. Collapsed to plain uint32_t.
 */
static void Deinterlacer_InternalProcess(Deinterlacer *d, MDFN_Surface *surface,
      MDFN_Rect *DisplayRect, int32_t *LineWidths, const bool field)
{
   int y;
   /*
    * We need to output with LineWidths as always being valid to
    * handle the case of horizontal resolution change between
    * fields while in interlace mode, so clear the first
    * LineWidths entry if it's == ~0.
    */
   const bool LineWidths_In_Valid = (LineWidths[0] != ~(int32_t)0);
   const bool WeaveGood           = (d->StateValid
         && d->PrevDRect.h == DisplayRect->h
         && d->DeintType == DEINT_WEAVE);
   /*
    * XReposition stuff is to prevent exceeding the dimensions of
    * the video surface under certain conditions (weave
    * deinterlacer, previous field has higher horizontal
    * resolution than current field, and current field's
    * rectangle has an x offset that's too large when taking into
    * consideration the previous field's width; for simplicity,
    * we don't check widths, but just assume that the previous
    * field's maximum width is >= than the current field's
    * maximum width).
    */
   const int32_t XReposition = ((WeaveGood && DisplayRect->x > d->PrevDRect.x) ? DisplayRect->x : 0);

   if (XReposition)
      DisplayRect->x = 0;

   if (surface->h && !LineWidths_In_Valid)
      LineWidths[0] = 0;

   for (y = 0; y < DisplayRect->h / 2; y++)
   {
      /*
       * Set all relevant source line widths to the contents of
       * DisplayRect (also simplifies the src_lw and related
       * pointer calculation code further below).
       */
      if (!LineWidths_In_Valid)
         LineWidths[(y * 2) + field + DisplayRect->y] = DisplayRect->w;

      if (XReposition)
      {
         memmove(surface->pixels + ((y * 2) + field + DisplayRect->y) * surface->pitchinpix,
               surface->pixels + ((y * 2) + field + DisplayRect->y) * surface->pitchinpix + XReposition,
               LineWidths[(y * 2) + field + DisplayRect->y] * sizeof(uint32_t));
      }

      if (WeaveGood)
      {
         const uint32_t *src   = d->FieldBuffer->pixels + y * d->FieldBuffer->pitchinpix;
         uint32_t       *dest  = surface->pixels + ((y * 2) + (field ^ 1) + DisplayRect->y) * surface->pitchinpix + DisplayRect->x;
         int32_t        *dest_lw = &LineWidths[(y * 2) + (field ^ 1) + DisplayRect->y];

         *dest_lw = d->LWBuffer[y];

         if (psx_gpu_upscale_shift == 0)
            memcpy(dest, src, d->LWBuffer[y] * sizeof(uint32_t));
      }
      else if (d->DeintType == DEINT_BOB)
      {
         const uint32_t *src   = surface->pixels + ((y * 2) + field + DisplayRect->y) * surface->pitchinpix + DisplayRect->x;
#ifdef BOB_LORES
         uint32_t       *dest  = surface->pixels + (y + DisplayRect->y) * surface->pitchinpix + DisplayRect->x;
#else
         uint32_t       *dest  = surface->pixels + ((y * 2) + (field ^ 1) + DisplayRect->y) * surface->pitchinpix + DisplayRect->x;
#endif
         const int32_t  *src_lw  = &LineWidths[(y * 2) + field + DisplayRect->y];
         int32_t        *dest_lw = &LineWidths[(y * 2) + (field ^ 1) + DisplayRect->y];

         *dest_lw = *src_lw;

         memcpy(dest, src, *src_lw * sizeof(uint32_t));
      }
      else
      {
         const int32_t  *src_lw = &LineWidths[(y * 2) + field + DisplayRect->y];
         const uint32_t *src    = surface->pixels + ((y * 2) + field + DisplayRect->y) * surface->pitchinpix + DisplayRect->x;
         const int32_t   dly    = ((y * 2) + (field + 1) + DisplayRect->y);
         uint32_t       *dest   = surface->pixels + dly * surface->pitchinpix + DisplayRect->x;

         if (y == 0 && field)
         {
            uint32_t  black = MAKECOLOR(0, 0, 0, 0);
            uint32_t *dm2   = surface->pixels + (dly - 2) * surface->pitchinpix;
            int       x;

            LineWidths[dly - 2] = *src_lw;

            for (x = 0; x < *src_lw; x++)
               dm2[x] = black;
         }

         if (dly < (DisplayRect->y + DisplayRect->h))
         {
            LineWidths[dly] = *src_lw;
            memcpy(dest, src, *src_lw * sizeof(uint32_t));
         }
      }

      if (d->DeintType == DEINT_WEAVE)
      {
         const int32_t  *src_lw = &LineWidths[(y * 2) + field + DisplayRect->y];
         const uint32_t *src    = surface->pixels + ((y * 2) + field + DisplayRect->y) * surface->pitchinpix + DisplayRect->x;
         uint32_t       *dest   = d->FieldBuffer->pixels + y * d->FieldBuffer->pitchinpix;

         memcpy(dest, src, *src_lw * sizeof(uint32_t));
         d->LWBuffer[y] = *src_lw;

         d->StateValid  = true;
      }
   }
}

void Deinterlacer_Process(Deinterlacer *d, MDFN_Surface *surface,
      MDFN_Rect *DisplayRect, int32_t *LineWidths, const bool field)
{
   const MDFN_Rect DisplayRect_Original = *DisplayRect;

   if (d->DeintType == DEINT_WEAVE)
   {
      if (!d->FieldBuffer || d->FieldBuffer->w < surface->w
            || d->FieldBuffer->h < (surface->h / 2))
      {
         if (d->FieldBuffer)
            MDFN_Surface_Delete(d->FieldBuffer);

         d->FieldBuffer = MDFN_Surface_New(surface->w, surface->h / 2, surface->w);
         LWBuffer_Resize(d, d->FieldBuffer->h);
      }
      /*
       * Originally the else-if here memcmp'd the surface's old
       * MDFN_PixelFormat against FieldBuffer's and called
       * FieldBuffer->SetFormat() if they differed. With the
       * format struct gone (the libretro core only ever uses one
       * fixed 32bpp RGBA layout), the check could never trigger
       * and the SetFormat call - which had a body of
       * `format = nf;` only - was a no-op. Whole branch dropped.
       */
   }

   Deinterlacer_InternalProcess(d, surface, DisplayRect, LineWidths, field);

   d->PrevDRect = DisplayRect_Original;
}
