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
   d->StateValid            = false;
   d->PrevDRect_h           = 0;
   d->PrevDRect_x           = 0;
   d->DeintType             = DEINT_WEAVE;
   d->LastDisturbedMargins  = false;
   d->MadHist[0]            = NULL;
   d->MadHist[1]            = NULL;
   d->MadScratch      = NULL;
   d->MadW            = 0;
   d->MadH            = 0;
   d->MadScratchW     = 0;
   d->MadIdx          = 0;
   d->MadFramesValid  = 0;
}

void Deinterlacer_Cleanup(Deinterlacer *d)
{
   if (!d)
      return;
   free(d->MadHist[0]);
   free(d->MadHist[1]);
   free(d->MadScratch);
   d->MadHist[0] = d->MadHist[1] = NULL;
   d->MadScratch = NULL;
   d->MadW = d->MadH = d->MadScratchW = 0;
   d->MadIdx = 0;
   d->MadFramesValid = 0;
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

bool Deinterlacer_DidDisturbMargins(const Deinterlacer *d)
{
   return d->LastDisturbedMargins;
}

void Deinterlacer_ClearState(Deinterlacer *d)
{
   d->StateValid            = false;
   d->PrevDRect_h           = 0;
   d->PrevDRect_x           = 0;
   d->MadFramesValid        = 0;
   d->LastDisturbedMargins  = false;
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

      /* The memmove shifts active pixels left by shift_pix.  The
       * tail [w_native*upscale - shift_pix .. dmw) is untouched
       * and still holds old margin zeros, but the head [0 ..
       * shift_pix) of the destination has been overwritten with
       * what used to live at [shift_pix .. 2*shift_pix) - which
       * straddles the source row's left margin (zero) and its
       * active region.  Net effect: the row's left margin (which
       * the scanout cache assumed was still zero) now has active
       * pixel data in it.  Signal the disturbance so the SW
       * scanout cache invalidates. */
      d->LastDisturbedMargins = true;
   }

   return weave_good;
}

/* ====================================================================
 * FastMAD (Motion Adaptive Deinterlacing)
 *
 * Backported from PCSX2's CPU FastMAD in
 * pcsx2/GS/Renderers/SW/GSDeviceSW.cpp, a CPU port of
 * interlace.glsl's ps_main3 + ps_main4 (MAD_BUFFER /
 * MAD_RECONSTRUCT shader passes).
 *
 * Algorithm (one Deinterlacer_Process call):
 *
 *   For each row of opposite-field parity in the output:
 *     - top/bottom edge       -> weave with whatever is already
 *                                in surface[y] (= previous call's
 *                                MAD output, approximating last
 *                                field's content).
 *     - interior              -> per-native-pixel motion test
 *                                between current frame and a
 *                                snapshot from 2 calls ago.  If
 *                                any of three vertically-adjacent
 *                                pixels exceeds the sensitivity
 *                                threshold in any RGB channel ->
 *                                interpolate (hn+ln)/2.  Else
 *                                weave (keep cn).
 *
 *   After processing, snapshot the surface at native resolution
 *   into the currently-active history bank, then toggle the bank
 *   pointer.  Next call reads from the OTHER bank which by that
 *   point holds 2-call-old state.
 *
 * Memory:
 *   2 banks * w_native * h_native * 4 bytes.
 *   ~2.7 MB worst case for PAL 480i (704x576).  Above
 *   MAD_MAX_PIXELS (1M native pixels) the allocation is skipped
 *   and the mode falls through to weave behaviour silently.
 *
 * Warmup:
 *   The first MAD_WARMUP calls after a fresh allocation skip the
 *   reconstruction pass; visible result is plain weave for ~67 ms
 *   at 60 Hz.  This avoids the brief "everything looks like
 *   motion" softening when history is still zero-filled.
 *
 * Upscaling:
 *   Motion detection runs at NATIVE resolution to bound history
 *   size regardless of psx_gpu_upscale_shift.  The chosen
 *   weave/interpolate output for each native pixel is replicated
 *   across the upscale row/col block.
 *
 * SSE2 codepath in the inner per-pixel motion+blend loop; scalar
 * fallback otherwise (and on non-x86 targets).
 * ==================================================================== */

#if defined(__SSE2__)
#include <emmintrin.h>
#define MAD_HAVE_SSE2 1
#endif

#define MAD_SENSITIVITY 20
#define MAD_MAX_PIXELS  (1024 * 1024)
#define MAD_WARMUP      4

static int mad_pixel_motion(uint32_t newer, uint32_t older, int threshold)
{
   int nr, ng, nb, or_, og, ob;
   int dr, dg, db, m;

   nr  = (int)((newer      ) & 0xFFu);
   ng  = (int)((newer >>  8) & 0xFFu);
   nb  = (int)((newer >> 16) & 0xFFu);
   or_ = (int)((older      ) & 0xFFu);
   og  = (int)((older >>  8) & 0xFFu);
   ob  = (int)((older >> 16) & 0xFFu);

   dr = nr - or_;
   dg = ng - og;
   db = nb - ob;
   if (dr < 0) dr = -dr;
   if (dg < 0) dg = -dg;
   if (db < 0) db = -db;
   dr -= threshold;
   dg -= threshold;
   db -= threshold;

   m = dr;
   if (dg > m) m = dg;
   if (db > m) m = db;
   return m;
}

static uint32_t mad_average_pixels(uint32_t a, uint32_t b)
{
   uint32_t ar, ag, ab, br, bg, bb, r, g, bl;

   ar = (a      ) & 0xFFu;
   ag = (a >>  8) & 0xFFu;
   ab = (a >> 16) & 0xFFu;
   br = (b      ) & 0xFFu;
   bg = (b >>  8) & 0xFFu;
   bb = (b >> 16) & 0xFFu;
   r  = (ar + br + 1u) >> 1;
   g  = (ag + bg + 1u) >> 1;
   bl = (ab + bb + 1u) >> 1;
   return (a & 0xFF000000u) | (bl << 16) | (g << 8) | r;
}

#if MAD_HAVE_SSE2
/* Per-32-bit-lane motion mask for 4 pixels at a time.  Result lane
 * is all-ones where motion was detected in any of the three triples
 * in any RGB channel, all-zeros otherwise. */
static __m128i mad_motion_mask_sse2(
      __m128i hn, __m128i ho,
      __m128i cn, __m128i co,
      __m128i ln, __m128i lo,
      __m128i thresh,
      __m128i alpha_mask)
{
   __m128i dh, dc, dl, any, still;

   dh = _mm_or_si128(_mm_subs_epu8(hn, ho), _mm_subs_epu8(ho, hn));
   dc = _mm_or_si128(_mm_subs_epu8(cn, co), _mm_subs_epu8(co, cn));
   dl = _mm_or_si128(_mm_subs_epu8(ln, lo), _mm_subs_epu8(lo, ln));

   dh = _mm_subs_epu8(dh, thresh);
   dc = _mm_subs_epu8(dc, thresh);
   dl = _mm_subs_epu8(dl, thresh);

   any   = _mm_or_si128(_mm_or_si128(dh, dc), dl);
   any   = _mm_and_si128(any, alpha_mask);

   still = _mm_cmpeq_epi8(any, _mm_setzero_si128());
   still = _mm_cmpeq_epi32(still, _mm_set1_epi32(-1));
   return _mm_xor_si128(still, _mm_set1_epi32(-1));
}
#endif

/* Reconstruct one native row of MAD output into dst (w pixels). */
static void mad_reconstruct_row(
      uint32_t       *dst,
      const uint32_t *cn_row,
      const uint32_t *hn_row,
      const uint32_t *ln_row,
      const uint32_t *co_row,
      const uint32_t *ho_row,
      const uint32_t *lo_row,
      int             w)
{
   int x = 0;

#if MAD_HAVE_SSE2
   {
      const __m128i thresh     = _mm_set1_epi8((char)MAD_SENSITIVITY);
      const __m128i alpha_mask = _mm_set1_epi32(0x00FFFFFF);
      for (; x + 4 <= w; x += 4)
      {
         __m128i hn   = _mm_loadu_si128((const __m128i*)(hn_row + x));
         __m128i cn   = _mm_loadu_si128((const __m128i*)(cn_row + x));
         __m128i ln   = _mm_loadu_si128((const __m128i*)(ln_row + x));
         __m128i ho   = _mm_loadu_si128((const __m128i*)(ho_row + x));
         __m128i co   = _mm_loadu_si128((const __m128i*)(co_row + x));
         __m128i lo   = _mm_loadu_si128((const __m128i*)(lo_row + x));
         __m128i mask = mad_motion_mask_sse2(hn, ho, cn, co, ln, lo,
               thresh, alpha_mask);
         __m128i avg  = _mm_avg_epu8(hn, ln);
         __m128i out  = _mm_or_si128(
               _mm_and_si128(mask, avg),
               _mm_andnot_si128(mask, cn));
         _mm_storeu_si128((__m128i*)(dst + x), out);
      }
   }
#endif
   for (; x < w; x++)
   {
      uint32_t hn = hn_row[x];
      uint32_t cn = cn_row[x];
      uint32_t ln = ln_row[x];
      uint32_t ho = ho_row[x];
      uint32_t co = co_row[x];
      uint32_t lo = lo_row[x];
      int mh = mad_pixel_motion(hn, ho, MAD_SENSITIVITY);
      int mc = mad_pixel_motion(cn, co, MAD_SENSITIVITY);
      int ml = mad_pixel_motion(ln, lo, MAD_SENSITIVITY);
      dst[x] = (mh > 0 || mc > 0 || ml > 0)
         ? mad_average_pixels(hn, ln)
         : cn;
   }
}

/* Snapshot the current surface at native resolution into dst,
 * sampling one pixel per upscale block. */
static void mad_snapshot_native(
      uint32_t       *dst,
      int             w_native,
      const uint32_t *surf,
      int             surf_pitch_pix,
      int             dy_native,
      int             snap_h,
      int             upscale)
{
   int y, x;

   for (y = 0; y < snap_h; y++)
   {
      const uint32_t *src     = surf
         + (size_t)((y + dy_native) * upscale) * surf_pitch_pix;
      uint32_t       *out_row = dst + (size_t)y * w_native;
      if (upscale == 1)
         memcpy(out_row, src, (size_t)w_native * sizeof(uint32_t));
      else
         for (x = 0; x < w_native; x++)
            out_row[x] = src[x * upscale];
   }
}

/* Expand one native row to the upscale row/col block at dst_block_first
 * (= first surface row of the upscale block; surf_pitch_pix is in
 * pixels, upscale is the linear factor). */
static void mad_expand_row(
      uint32_t       *dst_block_first,
      int             surf_pitch_pix,
      const uint32_t *native_row,
      int             w_native,
      int             upscale)
{
   int v;

   if (upscale == 1)
   {
      memcpy(dst_block_first, native_row,
             (size_t)w_native * sizeof(uint32_t));
      return;
   }
   {
      int x, u;
      for (x = 0; x < w_native; x++)
      {
         uint32_t  pix = native_row[x];
         uint32_t *p   = dst_block_first + (size_t)x * upscale;
         for (u = 0; u < upscale; u++)
            p[u] = pix;
      }
   }
   for (v = 1; v < upscale; v++)
      memcpy(dst_block_first + (size_t)v * surf_pitch_pix,
             dst_block_first,
             (size_t)(w_native * upscale) * sizeof(uint32_t));
}

/* Ensure history banks and scratch row are sized for the current
 * geometry.  Returns 0 on success, -1 on allocation failure or
 * if the surface exceeds the size cap (caller falls back to
 * weave). */
static int mad_ensure_buffers(Deinterlacer *d, int w_native, int h_native)
{
   const size_t bank_pix = (size_t)w_native * (size_t)h_native;

   if (w_native <= 0 || h_native <= 0)
      return -1;
   if (bank_pix > (size_t)MAD_MAX_PIXELS)
      return -1;

   if (   d->MadHist[0] == NULL
       || d->MadW != w_native
       || d->MadH != h_native)
   {
      free(d->MadHist[0]);
      free(d->MadHist[1]);
      d->MadHist[0] = (uint32_t*)calloc(bank_pix, sizeof(uint32_t));
      d->MadHist[1] = (uint32_t*)calloc(bank_pix, sizeof(uint32_t));
      if (!d->MadHist[0] || !d->MadHist[1])
      {
         free(d->MadHist[0]);
         free(d->MadHist[1]);
         d->MadHist[0] = d->MadHist[1] = NULL;
         d->MadW = d->MadH = 0;
         d->MadFramesValid = 0;
         return -1;
      }
      d->MadW            = w_native;
      d->MadH            = h_native;
      d->MadIdx          = 0;
      d->MadFramesValid  = 0;
   }
   if (d->MadScratch == NULL || d->MadScratchW < w_native)
   {
      free(d->MadScratch);
      d->MadScratch  = (uint32_t*)malloc((size_t)w_native
                                          * sizeof(uint32_t));
      if (!d->MadScratch)
      {
         d->MadScratchW = 0;
         return -1;
      }
      d->MadScratchW = w_native;
   }
   return 0;
}

/* Top-level FastMAD entry, called from Deinterlacer_Process. */
static void deint_fastmad(
      Deinterlacer    *d,
      uint32_t        *pixels,
      int32_t          pitch_pix,
      const MDFN_Rect *DisplayRect,
      bool             field,
      unsigned         s)
{
   const int       upscale  = 1 << s;
   const int       w_native = (DisplayRect->w > 0)
                              ? DisplayRect->w
                              : (pitch_pix / upscale);
   const int       h_native = DisplayRect->h;
   const int       dy       = DisplayRect->y;
   const uint32_t *hist;
   int             k, y, v;

   if (mad_ensure_buffers(d, w_native, h_native) < 0)
      return;     /* silent weave fallback */

   hist = d->MadHist[d->MadIdx];

   if (d->MadFramesValid >= MAD_WARMUP)
   {
      for (y = 0; y < h_native; y++)
      {
         int       is_current_parity, is_edge;
         uint32_t *first_dst;

         is_current_parity = ((y & 1) == (int)field);
         is_edge           = (y == 0 || y == h_native - 1);
         if (is_current_parity)
            continue;     /* GPU already filled this row */

         first_dst = pixels + (size_t)((y + dy) * upscale) * pitch_pix;
         if (is_edge)
         {
            /* Weave from surface.  Just replicate the GPU's first-
             * row-of-block across the rest of the upscale block. */
            for (v = 1; v < upscale; v++)
               memcpy(first_dst + (size_t)v * pitch_pix, first_dst,
                      (size_t)(w_native * upscale) * sizeof(uint32_t));
            continue;
         }

         {
            const int       y_above   = y - 1;
            const int       y_below   = y + 1;
            const uint32_t *cn_row    = pixels
               + (size_t)((y       + dy) * upscale) * pitch_pix;
            const uint32_t *hn_row    = pixels
               + (size_t)((y_above + dy) * upscale) * pitch_pix;
            const uint32_t *ln_row    = pixels
               + (size_t)((y_below + dy) * upscale) * pitch_pix;
            const uint32_t *co_row    = hist + (size_t)y       * w_native;
            const uint32_t *ho_row    = hist + (size_t)y_above * w_native;
            const uint32_t *lo_row    = hist + (size_t)y_below * w_native;

            if (upscale == 1)
            {
               mad_reconstruct_row(d->MadScratch,
                     cn_row, hn_row, ln_row,
                     co_row, ho_row, lo_row,
                     w_native);
            }
            else
            {
               /* Subsample current-frame rows on the fly.  Forfeits
                * SSE2 in the upscaled path; at upscale > 1 the
                * history buffer's column-stride still gives us
                * tight reads for the older-frame side. */
               int x;
               for (x = 0; x < w_native; x++)
               {
                  uint32_t hn = hn_row[x * upscale];
                  uint32_t cn = cn_row[x * upscale];
                  uint32_t ln = ln_row[x * upscale];
                  uint32_t ho = ho_row[x];
                  uint32_t co = co_row[x];
                  uint32_t lo = lo_row[x];
                  int mh = mad_pixel_motion(hn, ho, MAD_SENSITIVITY);
                  int mc = mad_pixel_motion(cn, co, MAD_SENSITIVITY);
                  int ml = mad_pixel_motion(ln, lo, MAD_SENSITIVITY);
                  d->MadScratch[x] = (mh > 0 || mc > 0 || ml > 0)
                     ? mad_average_pixels(hn, ln)
                     : cn;
               }
            }

            mad_expand_row(first_dst, pitch_pix,
                  d->MadScratch, w_native, upscale);
         }
      }

      /* Replicate current-field rows across the upscale row block.
       * The GPU's immediate-scanout path only wrote the first row
       * of each block; rows 1..upscale-1 are scratch. */
      if (upscale > 1)
      {
         for (k = 0; k < (h_native / 2); k++)
         {
            const int    native_y    = (k * 2) + (int)field;
            uint32_t    *first_dst   = pixels
               + (size_t)((native_y + dy) * upscale) * pitch_pix;
            for (v = 1; v < upscale; v++)
               memcpy(first_dst + (size_t)v * pitch_pix, first_dst,
                      (size_t)(w_native * upscale) * sizeof(uint32_t));
         }
      }
   }
   else
   {
      d->MadFramesValid++;
   }

   /* Snapshot the surface at native resolution into the bank we'll
    * READ from 2 calls from now. */
   mad_snapshot_native(d->MadHist[d->MadIdx], w_native,
         pixels, pitch_pix, dy, h_native, upscale);

   /* Toggle bank pointer. */
   d->MadIdx ^= 1;
}

void Deinterlacer_Process(Deinterlacer *d, MDFN_Surface *surface,
      MDFN_Rect *DisplayRect, int32_t *LineWidths, const bool field)
{
   const unsigned s          = psx_gpu_upscale_shift;
   uint32_t      *pixels     = surface->pixels;
   const int32_t  pitch_pix  = surface->pitchinpix;
   const int32_t  prev_h     = DisplayRect->h;
   const int32_t  prev_x     = DisplayRect->x;

   /* Reset per-call disturbance flag.  Set to true only if the
    * mode actually mutated surface pixels outside the active
    * region (currently just WEAVE's XReposition path). */
   d->LastDisturbedMargins = false;

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

      case DEINT_FASTMAD:
         deint_fastmad(d, pixels, pitch_pix, DisplayRect, field, s);
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
