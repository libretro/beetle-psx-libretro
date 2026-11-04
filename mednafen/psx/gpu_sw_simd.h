/* SSE2/NEON fragment operations. Included after GPU_SW_Packet is defined. */
#ifndef PSX_GPU_SW_SIMD_H
#define PSX_GPU_SW_SIMD_H

#if defined(GPU_HAVE_SSE2)
typedef __m128i GPU_SW_Vector;
static INLINE GPU_SW_Vector GPU_SW_Load16(const uint16_t *src, unsigned count)
{
   return count == 8 ? _mm_loadu_si128((const __m128i *)src)
                     : _mm_loadl_epi64((const __m128i *)src);
}
static INLINE void GPU_SW_Store16(uint16_t *dst, GPU_SW_Vector value, unsigned count)
{
   if (count == 8) _mm_storeu_si128((__m128i *)dst, value);
   else _mm_storel_epi64((__m128i *)dst, value);
}
static INLINE GPU_SW_Vector GPU_SW_DuplicateLow(GPU_SW_Vector v) { return _mm_unpacklo_epi16(v, v); }
static INLINE GPU_SW_Vector GPU_SW_DuplicateHigh(GPU_SW_Vector v) { return _mm_unpackhi_epi16(v, v); }
static INLINE int GPU_SW_AnyWritten16(GPU_SW_Vector v) { return _mm_movemask_epi8(v) != 0; }
static INLINE int GPU_SW_AllWritten16(GPU_SW_Vector v, unsigned count)
{
   return _mm_movemask_epi8(v) == (count == 8 ? 0xffff : 0xff);
}
static INLINE GPU_SW_Vector GPU_SW_Select16(GPU_SW_Vector mask, GPU_SW_Vector yes, GPU_SW_Vector no)
{
   return _mm_or_si128(_mm_and_si128(mask, yes), _mm_andnot_si128(mask, no));
}
static INLINE GPU_SW_Vector GPU_SW_Shade16(const GPU_SW_Packet *p, unsigned i,
      unsigned count, GPU_SW_Vector t)
{
   const __m128i five = _mm_set1_epi16(31), zero = _mm_setzero_si128();
   __m128i d = GPU_SW_Load16((const uint16_t *)(p->dither + i), count);
   __m128i r = _mm_and_si128(t, five);
   __m128i g = _mm_and_si128(_mm_srli_epi16(t, 5), five);
   __m128i b = _mm_and_si128(_mm_srli_epi16(t, 10), five);
#define GPU_SW_SHADE(c, color) \
   c = _mm_srli_epi16(_mm_mullo_epi16(c, GPU_SW_Load16(p->color + i, count)), 4); \
   c = _mm_min_epi16(five, _mm_max_epi16(zero, _mm_srai_epi16(_mm_add_epi16(c, d), 3)))
   GPU_SW_SHADE(r, red); GPU_SW_SHADE(g, green); GPU_SW_SHADE(b, blue);
#undef GPU_SW_SHADE
   return _mm_or_si128(_mm_and_si128(t, _mm_set1_epi16((short)0x8000)),
         _mm_or_si128(r, _mm_or_si128(_mm_slli_epi16(g, 5), _mm_slli_epi16(b, 10))));
}
static INLINE GPU_SW_Vector GPU_SW_Compose16(const GPU_SW_Packet *p,
      GPU_SW_Vector fg, GPU_SW_Vector bg)
{
   const __m128i five = _mm_set1_epi16(31), high = _mm_set1_epi16((short)0x8000);
   __m128i out = fg;
   if (p->blend >= 0)
   {
      __m128i r = _mm_and_si128(fg, five), br = _mm_and_si128(bg, five);
      __m128i g = _mm_and_si128(_mm_srli_epi16(fg, 5), five), bgc = _mm_and_si128(_mm_srli_epi16(bg, 5), five);
      __m128i b = _mm_and_si128(_mm_srli_epi16(fg, 10), five), bb = _mm_and_si128(_mm_srli_epi16(bg, 10), five);
      __m128i blended;
#define GPU_SW_BLEND(c, back) \
      if (p->blend == 0) c = _mm_srli_epi16(_mm_add_epi16(back, c), 1); \
      else if (p->blend == 2) c = _mm_subs_epu16(back, c); \
      else { if (p->blend == 3) c = _mm_srli_epi16(c, 2); c = _mm_min_epi16(_mm_add_epi16(back, c), five); }
      GPU_SW_BLEND(r, br); GPU_SW_BLEND(g, bgc); GPU_SW_BLEND(b, bb);
#undef GPU_SW_BLEND
      blended = _mm_or_si128(_mm_and_si128(fg, high),
            _mm_or_si128(r, _mm_or_si128(_mm_slli_epi16(g, 5), _mm_slli_epi16(b, 10))));
      out = GPU_SW_Select16(_mm_srai_epi16(fg, 15), blended, fg);
   }
   if (!p->textured) out = _mm_andnot_si128(high, out);
   return _mm_or_si128(out, _mm_set1_epi16((short)p->mask_set));
}
static INLINE GPU_SW_Vector GPU_SW_WriteMask16(const GPU_SW_Packet *p,
      GPU_SW_Vector covered, GPU_SW_Vector bg)
{
   return p->mask_eval ? _mm_andnot_si128(_mm_srai_epi16(bg, 15), covered) : covered;
}
#elif defined(GPU_HAVE_NEON)
typedef uint16x8_t GPU_SW_Vector;
static INLINE GPU_SW_Vector GPU_SW_Load16(const uint16_t *src, unsigned count)
{
   return count == 8 ? vld1q_u16(src) : vcombine_u16(vld1_u16(src), vdup_n_u16(0));
}
static INLINE void GPU_SW_Store16(uint16_t *dst, GPU_SW_Vector value, unsigned count)
{
   if (count == 8) vst1q_u16(dst, value);
   else vst1_u16(dst, vget_low_u16(value));
}
static INLINE GPU_SW_Vector GPU_SW_DuplicateLow(GPU_SW_Vector v) { return vzipq_u16(v, v).val[0]; }
static INLINE GPU_SW_Vector GPU_SW_DuplicateHigh(GPU_SW_Vector v) { return vzipq_u16(v, v).val[1]; }
static INLINE int GPU_SW_AnyWritten16(GPU_SW_Vector v)
{
   return vget_lane_u64(vreinterpret_u64_u16(vorr_u16(vget_low_u16(v), vget_high_u16(v))), 0) != 0;
}
static INLINE int GPU_SW_AllWritten16(GPU_SW_Vector v, unsigned count)
{
   return vget_lane_u64(vreinterpret_u64_u16(vget_low_u16(v)), 0) == UINT64_MAX &&
      (count == 4 || vget_lane_u64(vreinterpret_u64_u16(vget_high_u16(v)), 0) == UINT64_MAX);
}
static INLINE GPU_SW_Vector GPU_SW_Select16(GPU_SW_Vector mask, GPU_SW_Vector yes, GPU_SW_Vector no)
{
   return vbslq_u16(mask, yes, no);
}
static INLINE GPU_SW_Vector GPU_SW_Shade16(const GPU_SW_Packet *p, unsigned i,
      unsigned count, GPU_SW_Vector t)
{
   const uint16x8_t five = vdupq_n_u16(31);
   int16x8_t d = vreinterpretq_s16_u16(GPU_SW_Load16((const uint16_t *)(p->dither + i), count));
   uint16x8_t r = vandq_u16(t, five);
   uint16x8_t g = vandq_u16(vshrq_n_u16(t, 5), five);
   uint16x8_t b = vandq_u16(vshrq_n_u16(t, 10), five);
#define GPU_SW_SHADE(c, color) \
   c = vshrq_n_u16(vmulq_u16(c, GPU_SW_Load16(p->color + i, count)), 4); \
   c = vreinterpretq_u16_s16(vminq_s16(vdupq_n_s16(31), vmaxq_s16(vdupq_n_s16(0), \
            vshrq_n_s16(vaddq_s16(vreinterpretq_s16_u16(c), d), 3))))
   GPU_SW_SHADE(r, red); GPU_SW_SHADE(g, green); GPU_SW_SHADE(b, blue);
#undef GPU_SW_SHADE
   return vorrq_u16(vandq_u16(t, vdupq_n_u16(0x8000)),
         vorrq_u16(r, vorrq_u16(vshlq_n_u16(g, 5), vshlq_n_u16(b, 10))));
}
static INLINE GPU_SW_Vector GPU_SW_Compose16(const GPU_SW_Packet *p,
      GPU_SW_Vector fg, GPU_SW_Vector bg)
{
   const uint16x8_t five = vdupq_n_u16(31), high = vdupq_n_u16(0x8000);
   uint16x8_t out = fg;
   if (p->blend >= 0)
   {
      uint16x8_t r = vandq_u16(fg, five), br = vandq_u16(bg, five);
      uint16x8_t g = vandq_u16(vshrq_n_u16(fg, 5), five), bgc = vandq_u16(vshrq_n_u16(bg, 5), five);
      uint16x8_t b = vandq_u16(vshrq_n_u16(fg, 10), five), bb = vandq_u16(vshrq_n_u16(bg, 10), five);
      uint16x8_t blended;
#define GPU_SW_BLEND(c, back) \
      if (p->blend == 0) c = vshrq_n_u16(vaddq_u16(back, c), 1); \
      else if (p->blend == 2) c = vqsubq_u16(back, c); \
      else { if (p->blend == 3) c = vshrq_n_u16(c, 2); c = vminq_u16(vaddq_u16(back, c), five); }
      GPU_SW_BLEND(r, br); GPU_SW_BLEND(g, bgc); GPU_SW_BLEND(b, bb);
#undef GPU_SW_BLEND
      blended = vorrq_u16(vandq_u16(fg, high),
            vorrq_u16(r, vorrq_u16(vshlq_n_u16(g, 5), vshlq_n_u16(b, 10))));
      out = vbslq_u16(vcgeq_u16(fg, high), blended, fg);
   }
   if (!p->textured) out = vbicq_u16(out, high);
   return vorrq_u16(out, vdupq_n_u16(p->mask_set));
}
static INLINE GPU_SW_Vector GPU_SW_WriteMask16(const GPU_SW_Packet *p,
      GPU_SW_Vector covered, GPU_SW_Vector bg)
{
   return p->mask_eval ? vbicq_u16(covered, vcgeq_u16(bg, vdupq_n_u16(0x8000))) : covered;
}
#endif

#if defined(GPU_HAVE_SSE2) || defined(GPU_HAVE_NEON)
/* Select the even words of sixteen input pixels, preserving their mask bits. */
#if defined(GPU_HAVE_SSE2)
static INLINE GPU_SW_Vector GPU_SW_EvenWords16(GPU_SW_Vector lo, GPU_SW_Vector hi)
{
   __m128i mask = _mm_set1_epi32(0xffff), bias = _mm_set1_epi32(0x8000);
   lo = _mm_sub_epi32(_mm_and_si128(lo, mask), bias);
   hi = _mm_sub_epi32(_mm_and_si128(hi, mask), bias);
   return _mm_add_epi16(_mm_packs_epi32(lo, hi), _mm_set1_epi16((short)0x8000));
}
static INLINE GPU_SW_Vector GPU_SW_GatherStride2(const uint16_t *src)
{
   return GPU_SW_EvenWords16(_mm_loadu_si128((const __m128i *)src),
         _mm_loadu_si128((const __m128i *)(src + 8)));
}
static INLINE GPU_SW_Vector GPU_SW_GatherStride4(const uint16_t *src)
{
   return GPU_SW_EvenWords16(GPU_SW_GatherStride2(src), GPU_SW_GatherStride2(src + 16));
}
#endif

/* Eight native samples from a contiguous upscaled row. Each reduction keeps
 * the first word of every pair; the last stage loads exactly the native
 * sample footprint and never crosses the physical row boundary. */
static INLINE GPU_SW_Vector GPU_SW_Gather8(const uint16_t *src, unsigned shift)
{
#if defined(GPU_HAVE_SSE2)
   if (!shift) return _mm_loadu_si128((const __m128i *)src);
   if (shift == 1) return GPU_SW_GatherStride2(src);
   if (shift == 2) return GPU_SW_GatherStride4(src);
   return GPU_SW_EvenWords16(GPU_SW_GatherStride4(src), GPU_SW_GatherStride4(src + 32));
#else
   if (!shift) return vld1q_u16(src);
   if (shift == 1) return vld2q_u16(src).val[0];
   if (shift == 2) return vld4q_u16(src).val[0];
   return vuzpq_u16(vld4q_u16(src).val[0], vld4q_u16(src + 32).val[0]).val[0];
#endif
}

static INLINE GPU_SW_Vector GPU_SW_Gather4(const uint16_t *src, unsigned shift)
{
#if defined(GPU_HAVE_SSE2)
   __m128i zero = _mm_setzero_si128(), samples;
   if (!shift) return _mm_loadl_epi64((const __m128i *)src);
   if (shift == 1) samples = _mm_loadu_si128((const __m128i *)src);
   else if (shift == 2) samples = GPU_SW_GatherStride2(src);
   else samples = GPU_SW_GatherStride4(src);
   return GPU_SW_EvenWords16(samples, zero);
#else
   uint16x4_t samples;
   if (!shift) samples = vld1_u16(src);
   else if (shift == 1) samples = vld2_u16(src).val[0];
   else if (shift == 2) samples = vld4_u16(src).val[0];
   else
   {
      uint16x8_t half = vld4q_u16(src).val[0];
      samples = vuzp_u16(vget_low_u16(half), vget_high_u16(half)).val[0];
   }
   return vcombine_u16(samples, vdup_n_u16(0));
#endif
}

static INLINE GPU_SW_Vector GPU_SW_Background16(const GPU_SW_Packet *p, unsigned i, unsigned count)
{
   const uint16_t *src = p->dest + (i << p->native_shift);
   if (!p->native_shift) return GPU_SW_Load16(src, count);
   if (count == 8) return GPU_SW_Gather8(src, p->native_shift);
   return GPU_SW_Gather4(src, p->native_shift);
}

/* Native sprite semantics sample only the top-left destination for blending
 * and mask evaluation. Expand that result and its coverage together; a
 * rejected native pixel must preserve every distinct upscaled destination. */
static INLINE void GPU_SW_StoreFragments(const GPU_SW_Packet *p, unsigned i,
      unsigned count, GPU_SW_Vector color, GPU_SW_Vector write, GPU_SW_Vector bg)
{
   uint16_t *dst = p->dest + (i << p->native_shift);
   GPU_SW_Vector colors[8], masks[8];
   unsigned level, blocks = 1, block, row;
   int all = GPU_SW_AllWritten16(write, count);
   if (!p->native_shift)
   {
      GPU_SW_Store16(dst, GPU_SW_Select16(write, color, bg), count);
      return;
   }
   colors[0] = color; masks[0] = write;
   for (level = 0; level < p->native_shift; level++)
   {
      for (block = blocks; block-- > 0;)
      {
         GPU_SW_Vector c = colors[block], m = masks[block];
         colors[2 * block] = GPU_SW_DuplicateLow(c);
         colors[2 * block + 1] = GPU_SW_DuplicateHigh(c);
         masks[2 * block] = GPU_SW_DuplicateLow(m);
         masks[2 * block + 1] = GPU_SW_DuplicateHigh(m);
      }
      blocks *= 2;
   }
   blocks = (count << p->native_shift) / 8;
   for (row = 0; row < (1u << p->native_shift); row++)
      for (block = 0; block < blocks; block++)
      {
         uint16_t *out = dst + row * p->stride + block * 8;
         if (all) GPU_SW_Store16(out, colors[block], 8);
         else
         {
            GPU_SW_Vector old = GPU_SW_Load16(out, 8);
            GPU_SW_Store16(out, GPU_SW_Select16(masks[block], colors[block], old), 8);
         }
      }
}
#endif
#endif
