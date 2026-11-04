/* Ordered software raster output. Included only by gpu.c and its tests. */
#ifndef PSX_GPU_SW_H
#define PSX_GPU_SW_H

#include <stdint.h>
#include <string.h>

/* Compile-time ISA selection never enables instructions above the target's
 * baseline. GPU_DISABLE_SIMD also disables the older GPU fast paths. */
#if !defined(GPU_DISABLE_SIMD) && !defined(GPU_SW_REFERENCE)
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#define GPU_HAVE_SSE2 1
#include <emmintrin.h>
#elif defined(GPU_HAVE_NEON) || defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#ifndef GPU_HAVE_NEON
#define GPU_HAVE_NEON 1
#endif
#include <arm_neon.h>
#endif
#else
#undef GPU_HAVE_SSE2
#undef GPU_HAVE_NEON
#endif

#define GPU_SW_BLOCK 64

/* Subtract channels independently so borrow bits never cross a channel or
 * lane boundary. Unlike the packed scalar borrow formula this handles eight
 * pixels in 16-bit lanes. Callers supply the output mask bit separately. */
#if defined(GPU_HAVE_SSE2)
static INLINE __m128i GPU_SW_Subtract16(__m128i bg, __m128i fg)
{
   const __m128i r = _mm_set1_epi16(0x001f);
   const __m128i g = _mm_set1_epi16(0x03e0);
   const __m128i b = _mm_set1_epi16(0x7c00);
   return _mm_or_si128(_mm_subs_epu16(_mm_and_si128(bg, r), _mm_and_si128(fg, r)),
         _mm_or_si128(_mm_subs_epu16(_mm_and_si128(bg, g), _mm_and_si128(fg, g)),
            _mm_subs_epu16(_mm_and_si128(bg, b), _mm_and_si128(fg, b))));
}
#elif defined(GPU_HAVE_NEON)
static INLINE uint16x8_t GPU_SW_Subtract16(uint16x8_t bg, uint16x8_t fg)
{
   const uint16x8_t r = vdupq_n_u16(0x001f);
   const uint16x8_t g = vdupq_n_u16(0x03e0);
   const uint16x8_t b = vdupq_n_u16(0x7c00);
   return vorrq_u16(vqsubq_u16(vandq_u16(bg, r), vandq_u16(fg, r)),
         vorrq_u16(vqsubq_u16(vandq_u16(bg, g), vandq_u16(fg, g)),
            vqsubq_u16(vandq_u16(bg, b), vandq_u16(fg, b))));
}
#endif

/* Producer-owned dependency map in native VRAM rows. The worker never
 * accesses it. False positives cost a wait; false negatives are forbidden. */

/* 32 native columns by 8 rows per dependency tile. Bounds are conservative;
 * unrelated textures on the opposite side of VRAM need no batch drain. */
static uint32_t gpu_sw_dirty_tiles[64];

static INLINE uint32_t GPU_SW_ColumnMask(unsigned first, unsigned last)
{
   uint32_t left = ~UINT32_C(0) << (first >> 5);
   uint32_t right = ~UINT32_C(0) >> (31 - (last >> 5));
   return first <= last ? left & right : left | right;
}

static INLINE int GPU_SW_TilesDirty(unsigned first_y, unsigned last_y, uint32_t columns)
{
   unsigned band;
   for (band = first_y >> 3; band <= (last_y >> 3); band++)
      if (gpu_sw_dirty_tiles[band] & columns) return 1;
   return 0;
}

typedef struct GPU_SW_Packet GPU_SW_Packet;
struct GPU_SW_Packet
{
   void (*execute)(const GPU_SW_Packet *);
   uint16_t *dest;
   uint16_t *vram;
   uint16_t source[GPU_SW_BLOCK];
   /* Separate coverage is essential: modulation can turn a visible texel
    * into black, which must still write the framebuffer. */
   uint16_t covered[GPU_SW_BLOCK];
   uint16_t red[GPU_SW_BLOCK], green[GPU_SW_BLOCK], blue[GPU_SW_BLOCK];
   int16_t dither[GPU_SW_BLOCK];
   unsigned count, stride, native_shift, native_row;
   int blend;
   unsigned mask_eval, mask_set, textured, modulate;
   /* Untextured spans carry interpolants instead of materialised pixels,
    * keeping colour interpolation and dithering on the SIMD worker. */
   uint32_t color[3], delta[3];
   unsigned render_shift, dither_shift, pixel_x, pixel_y, gouraud, dither_on;
};

#include "gpu_sw_simd.h"

static INLINE void GPU_SW_MarkPacket(const GPU_SW_Packet *p)
{
   uint32_t columns = ~UINT32_C(0);
   if (p->vram)
   {
      size_t offset = (size_t)(p->dest - p->vram);
      unsigned x = (unsigned)(offset & (p->stride - 1));
      unsigned last = x + (p->count << p->native_shift) - 1;
      columns = GPU_SW_ColumnMask(x >> p->render_shift, last >> p->render_shift);
   }
   gpu_sw_dirty_tiles[p->native_row >> 3] |= columns;
}

/* Contiguous physical fills and top-left native sampling are also used by
 * VRAM transfers, palette refreshes, save states and resolution changes. */
static INLINE void GPU_SW_Fill16(uint16_t *dst, unsigned count, uint16_t value)
{
   unsigned i = 0;
#if defined(GPU_HAVE_SSE2)
   __m128i fill = _mm_set1_epi16((short)value);
   for (; i + 8 <= count; i += 8) _mm_storeu_si128((__m128i *)(dst + i), fill);
#elif defined(GPU_HAVE_NEON)
   uint16x8_t fill = vdupq_n_u16(value);
   for (; i + 8 <= count; i += 8) vst1q_u16(dst + i, fill);
#endif
   for (; i < count; i++) dst[i] = value;
}

static INLINE void GPU_SW_ReadNative(uint16_t *dst, const uint16_t *src,
      unsigned count, unsigned shift)
{
   unsigned i = 0;
#if defined(GPU_HAVE_SSE2) || defined(GPU_HAVE_NEON)
   if (shift <= 3)
   {
      for (; i + 8 <= count; i += 8)
         GPU_SW_Store16(dst + i, GPU_SW_Gather8(src + (i << shift), shift), 8);
      if (i + 4 <= count)
      {
         GPU_SW_Store16(dst + i, GPU_SW_Gather4(src + (i << shift), shift), 4);
         i += 4;
      }
   }
#endif
   for (; i < count; i++) dst[i] = src[i << shift];
}

static INLINE void GPU_SW_Coverage(GPU_SW_Packet *p)
{
   unsigned i = 0;
#if defined(GPU_HAVE_SSE2)
   const __m128i zero = _mm_setzero_si128(), ones = _mm_set1_epi16(-1);
   for (; i + 8 <= p->count; i += 8)
      _mm_storeu_si128((__m128i *)(p->covered + i), _mm_andnot_si128(
               _mm_cmpeq_epi16(_mm_loadu_si128((const __m128i *)(p->source + i)), zero), ones));
   if (i + 4 <= p->count)
   {
      _mm_storel_epi64((__m128i *)(p->covered + i), _mm_andnot_si128(
               _mm_cmpeq_epi16(_mm_loadl_epi64((const __m128i *)(p->source + i)), zero), ones));
      i += 4;
   }
#elif defined(GPU_HAVE_NEON)
   for (; i + 8 <= p->count; i += 8)
      vst1q_u16(p->covered + i, vcgtq_u16(vld1q_u16(p->source + i), vdupq_n_u16(0)));
   if (i + 4 <= p->count)
   {
      vst1_u16(p->covered + i, vcgt_u16(vld1_u16(p->source + i), vdup_n_u16(0)));
      i += 4;
   }
#endif
   for (; i < p->count; i++) p->covered[i] = p->source[i] ? 0xffff : 0;
}

static INLINE uint16_t GPU_SW_ModulatePixel(const GPU_SW_Packet *p, unsigned i)
{
   unsigned c, pixel = p->source[i] & 0x8000;
   const uint16_t colors[3] = { p->red[i], p->green[i], p->blue[i] };
   for (c = 0; c < 3; c++)
   {
      int value = (((p->source[i] >> (c * 5)) & 31) * colors[c]) >> 4;
      value = (value + p->dither[i]) >> 3;
      if (value < 0) value = 0;
      if (value > 31) value = 31;
      pixel |= value << (c * 5);
   }
   return pixel;
}
static INLINE void GPU_SW_Modulation(GPU_SW_Packet *p, unsigned r,
      unsigned g, unsigned b, unsigned x, unsigned y)
{
   static const int8_t matrix[4][4] = {
      { -4, 0, -3, 1 }, { 2, -2, 3, -1 },
      { -3, 1, -4, 0 }, { 3, -1, 2, -2 }
   };
   p->modulate = 1;
   p->red[p->count] = r;
   p->green[p->count] = g;
   p->blue[p->count] = b;
   p->dither[p->count] = matrix[y][x];
}

static uint16_t GPU_SW_Blend(uint16_t bg, uint16_t fg, int mode)
{
   uint32_t sum, carry, diff, borrow;
   switch (mode)
   {
      case 0:
         bg |= 0x8000;
         fg = ((fg + bg) - ((fg ^ bg) & 0x0421)) >> 1;
         break;
      case 3:
         fg = ((fg >> 2) & 0x1ce7) | 0x8000;
         /* fall through */
      case 1:
         bg &= 0x7fff;
         sum = fg + bg;
         carry = (sum - ((fg ^ bg) & 0x8421)) & 0x8420;
         fg = (sum - carry) | (carry - (carry >> 5));
         break;
      case 2:
         bg |= 0x8000;
         fg &= 0x7fff;
         diff = bg - fg + 0x108420;
         borrow = (diff - ((bg ^ fg) & 0x108420)) & 0x108420;
         fg = (diff - borrow) & (borrow - (borrow >> 5));
         break;
   }
   return fg;
}

static void GPU_SW_Execute(const GPU_SW_Packet *p)
{
   unsigned i = 0;
   const uint16_t *source = p->source;
   if (p->execute)
   {
      p->execute(p);
      return;
   }

   /* Shade and composite in one pass, including native sprites at 2x/4x/8x.
    * Four-pixel loads/stores keep short runs inside the VRAM allocation. */
#if defined(GPU_HAVE_SSE2) || defined(GPU_HAVE_NEON)
   if (p->native_shift <= 3)
      for (; i + 4 <= p->count;)
      {
         unsigned count = p->count - i >= 8 ? 8 : 4;
         GPU_SW_Vector fg = GPU_SW_Load16(source + i, count);
         GPU_SW_Vector bg = GPU_SW_Background16(p, i, count);
         GPU_SW_Vector write = GPU_SW_WriteMask16(p, GPU_SW_Load16(p->covered + i, count), bg);
         if (!GPU_SW_AnyWritten16(write)) { i += count; continue; }
         if (p->modulate) fg = GPU_SW_Shade16(p, i, count, fg);
         GPU_SW_StoreFragments(p, i, count, GPU_SW_Compose16(p, fg, bg), write, bg);
         i += count;
      }
#endif
   for (; i < p->count; i++)
   {
      unsigned dx, dy, scale = 1u << p->native_shift;
      uint16_t *dest = p->dest + (i << p->native_shift);
      uint16_t fg = p->modulate ? GPU_SW_ModulatePixel(p, i) : source[i], bg = *dest;
      if (!p->covered[i] || (p->mask_eval && (bg & 0x8000)))
         continue;
      if (p->blend >= 0 && (fg & 0x8000))
         fg = GPU_SW_Blend(bg, fg, p->blend);
      fg = (fg & (p->textured ? 0xffff : 0x7fff)) | p->mask_set;
      for (dy = 0; dy < scale; dy++)
      {
#if defined(GPU_HAVE_SSE2)
         if (scale >= 8)
         {
            __m128i fill = _mm_set1_epi16((short)fg);
            for (dx = 0; dx < scale; dx += 8) _mm_storeu_si128((__m128i *)(dest + dy * p->stride + dx), fill);
         }
         else if (scale == 4)
            _mm_storel_epi64((__m128i *)(dest + dy * p->stride), _mm_set1_epi16((short)fg));
         else
#elif defined(GPU_HAVE_NEON)
         if (scale >= 8)
         {
            uint16x8_t fill = vdupq_n_u16(fg);
            for (dx = 0; dx < scale; dx += 8) vst1q_u16(dest + dy * p->stride + dx, fill);
         }
         else if (scale == 4)
            vst1_u16(dest + dy * p->stride, vdup_n_u16(fg));
         else
#endif
            for (dx = 0; dx < scale; dx++) dest[dy * p->stride + dx] = fg;
      }
   }
}

/* Caller has synchronized VRAM and bounded this run to one native row.
 * Reuse the compositor's mask/replication rules without texture transparency
 * or blending. Copy commands stage all 128 source words before calling. */
static INLINE void GPU_SW_WriteNative(uint16_t *dst, unsigned stride, unsigned shift,
      const uint16_t *src, unsigned count, unsigned mask_eval, unsigned mask_set)
{
   GPU_SW_Packet p;
   if (!shift && !mask_eval && !mask_set)
   {
      memcpy(dst, src, count * sizeof(*src));
      return;
   }
   p.execute = NULL; p.modulate = 0; p.textured = 1; p.blend = -1;
   p.native_shift = shift; p.stride = stride;
   p.mask_eval = mask_eval; p.mask_set = mask_set; p.dest = dst;
   GPU_SW_Fill16(p.covered, GPU_SW_BLOCK, 0xffff);
   while (count)
   {
      p.count = count < GPU_SW_BLOCK ? count : GPU_SW_BLOCK;
      memcpy(p.source, src, p.count * sizeof(*src));
      GPU_SW_Execute(&p);
      count -= p.count; src += p.count; p.dest += p.count << shift;
   }
}

#include "gpu_sw_thread.h"

#endif
