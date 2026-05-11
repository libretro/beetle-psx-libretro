#ifndef __MDFN_SURFACE_H
#define __MDFN_SURFACE_H

#include <stdlib.h>
#include <string.h>

#include "../mednafen-types.h"
#include <boolean.h>

/*
 * Pixel encoding constants, used by MAKECOLOR and MDFN_DecodeColor.
 * The libretro core fixes the framebuffer to a single 32bpp RGBA
 * layout so these are compile-time constants, not runtime state.
 *
 * (Originally MDFN_PixelFormat carried a copy of these as instance
 * fields plus a default and a 5-arg constructor, but every call site
 * was already passing the same R=16/G=8/B=0/A=24 shifts that the
 * macros below encode. The struct was redundant and is gone.)
 */
#define RED_SHIFT 16
#define GREEN_SHIFT 8
#define BLUE_SHIFT 0
#define ALPHA_SHIFT 24
#define MAKECOLOR(r, g, b, a) ((r << RED_SHIFT) | (g << GREEN_SHIFT) | (b << BLUE_SHIFT) | (a << ALPHA_SHIFT))

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
   int32_t x, y, w, h;
} MDFN_Rect;

/*
 * Decode a packed 32-bit RGBA framebuffer pixel into separate
 * channel values. Was previously MDFN_PixelFormat::DecodeColor
 * (with a `format` argument that was never actually consulted -
 * the bit positions were hard-coded constants identical to the
 * RED_SHIFT/GREEN_SHIFT/BLUE_SHIFT/ALPHA_SHIFT macros above).
 */
static INLINE void MDFN_DecodeColor(uint32_t value,
      int *r, int *g, int *b, int *a)
{
   *r = (value >> RED_SHIFT)   & 0xFF;
   *g = (value >> GREEN_SHIFT) & 0xFF;
   *b = (value >> BLUE_SHIFT)  & 0xFF;
   *a = (value >> ALPHA_SHIFT) & 0xFF;
}

/*
 * Framebuffer descriptor. A single calloc'd uint32 buffer of
 * pitchinpix*h pixels, plus the dimensions used by the GPU /
 * Deinterlacer / scaler code paths.
 *
 * Allocate via MDFN_Surface_New and free via MDFN_Surface_Delete;
 * neither function does anything fancy beyond malloc/calloc/free,
 * but keeping the helpers means callers don't have to reproduce
 * the (NULL, w, h, w) construction shape each time.
 *
 * The pitch32/pitchinpix union preserves the historical name
 * "pitch32" used by older call sites in gpu.cpp; new code should
 * use pitchinpix.
 */
typedef struct
{
   uint32_t *pixels;

   /* w, h, and pitchinpix should always be > 0 once initialised. */
   int32_t w;
   int32_t h;

   union
   {
      int32_t pitch32;
      int32_t pitchinpix;
   };
} MDFN_Surface;

/*
 * Allocate a new surface backed by a calloc'd uint32 pixel buffer
 * of pitchinpix*height entries. Returns NULL on allocation
 * failure (caller must check). Width and pitchinpix are normally
 * equal; the libretro driver picks them based on
 * MEDNAFEN_CORE_GEOMETRY_MAX_W and the current upscale shift.
 */
static INLINE MDFN_Surface *MDFN_Surface_New(uint32_t width, uint32_t height,
      uint32_t pitchinpix)
{
   MDFN_Surface *surf = (MDFN_Surface *)calloc(1, sizeof(MDFN_Surface));
   if (!surf)
      return NULL;
   surf->pixels = (uint32_t *)calloc(1, pitchinpix * height * sizeof(uint32_t));
   if (!surf->pixels)
   {
      free(surf);
      return NULL;
   }
   surf->w          = width;
   surf->h          = height;
   surf->pitchinpix = pitchinpix;
   return surf;
}

static INLINE void MDFN_Surface_Delete(MDFN_Surface *surf)
{
   if (!surf)
      return;
   free(surf->pixels);
   free(surf);
}

#ifdef __cplusplus
}
#endif

#endif
