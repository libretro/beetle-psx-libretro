/* The MDEC's IDCT: two 1-D passes over an 8x8 block through the matrix the
 * scale table command loads.  Self-contained so tools/mdec_idct can test
 * it against the plain sums.
 *
 * Pass 1 takes each row of coefficients to the eight sums of it with the
 * matrix's rows, rounded to 16 bits; pass 2 does the same to the
 * transposed result, wraps each value to 9 bits as the hardware does and
 * saturates it to 8.  Every product fits in 27 bits and every sum of eight
 * in 30, so the order the sums are taken in does not change them: the
 * vector arms compute a row's eight sums at once and skip rows of zeros,
 * which most coefficient blocks are mostly made of. */
#ifndef __MDFN_PSX_MDEC_IDCT_H
#define __MDFN_PSX_MDEC_IDCT_H

#include <stdint.h>
#include <string.h>

#include <retro_inline.h>

#if defined(__SSE2__)
#include <emmintrin.h>
#define MDEC_IDCT_SSE2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define MDEC_IDCT_NEON 1
#endif

/* The matrix as the passes use it, rebuilt from the loaded one
 * (mdec_idct_prepare) when that changes.  SSE2: for each pair of columns
 * u, u + 1 and each half of the rows, the pairs (m[x][u], m[x][u + 1]) of
 * four rows x.  NEON: the transposed matrix, a row per column u. */
typedef struct mdec_idct_matrix
{
#if defined(MDEC_IDCT_SSE2)
   __m128i pairs[8];
#elif defined(MDEC_IDCT_NEON)
   int16x8_t cols[8];
#else
   int16_t m[64];
#endif
} mdec_idct_matrix;

static INLINE void mdec_idct_prepare(mdec_idct_matrix *p, const int16_t *m)
{
#if defined(MDEC_IDCT_SSE2)
   unsigned k, h, i;
   for (k = 0; k < 4; k++)
      for (h = 0; h < 2; h++)
      {
         int16_t v[8];
         for (i = 0; i < 4; i++)
         {
            v[i * 2 + 0] = m[(h * 4 + i) * 8 + k * 2 + 0];
            v[i * 2 + 1] = m[(h * 4 + i) * 8 + k * 2 + 1];
         }
         p->pairs[k * 2 + h] = _mm_loadu_si128((const __m128i *)v);
      }
#elif defined(MDEC_IDCT_NEON)
   unsigned u, x;
   for (u = 0; u < 8; u++)
   {
      int16_t v[8];
      for (x = 0; x < 8; x++)
         v[x] = m[x * 8 + u];
      p->cols[u] = vld1q_s16(v);
   }
#else
   memcpy(p->m, m, sizeof(p->m));
#endif
}

static INLINE int8_t mdec_idct_clamp(int32_t v)
{
   v = (int32_t)((uint32_t)v << 23) >> 23;   /* 9 bits, wrapping */
   if (v < -128)
      v = -128;
   if (v > 127)
      v = 127;
   return (int8_t)v;
}

#if defined(MDEC_IDCT_SSE2)
/* The eight sums of a row with the matrix's rows, rounded: lo has 0..3 */
#define MDEC_IDCT_ROW(p, r, lo, hi) \
   do { \
      const __m128i b0_ = _mm_shuffle_epi32((r), 0x00); \
      const __m128i b1_ = _mm_shuffle_epi32((r), 0x55); \
      const __m128i b2_ = _mm_shuffle_epi32((r), 0xAA); \
      const __m128i b3_ = _mm_shuffle_epi32((r), 0xFF); \
      const __m128i rnd_ = _mm_set1_epi32(0x4000); \
      (lo) = _mm_add_epi32( \
            _mm_add_epi32(_mm_madd_epi16((p)->pairs[0], b0_), \
                          _mm_madd_epi16((p)->pairs[2], b1_)), \
            _mm_add_epi32(_mm_madd_epi16((p)->pairs[4], b2_), \
                          _mm_madd_epi16((p)->pairs[6], b3_))); \
      (hi) = _mm_add_epi32( \
            _mm_add_epi32(_mm_madd_epi16((p)->pairs[1], b0_), \
                          _mm_madd_epi16((p)->pairs[3], b1_)), \
            _mm_add_epi32(_mm_madd_epi16((p)->pairs[5], b2_), \
                          _mm_madd_epi16((p)->pairs[7], b3_))); \
      (lo) = _mm_srai_epi32(_mm_add_epi32((lo), rnd_), 15); \
      (hi) = _mm_srai_epi32(_mm_add_epi32((hi), rnd_), 15); \
   } while (0)

static INLINE void mdec_idct(const mdec_idct_matrix *p,
      const int16_t *in, int8_t *out)
{
   const __m128i zero = _mm_setzero_si128();
   __m128i t[8];
   unsigned i;

   /* Pass 1: a row of sums for each row of coefficients */
   for (i = 0; i < 8; i++)
   {
      const __m128i r = _mm_load_si128((const __m128i *)&in[i * 8]);
      if (_mm_movemask_epi8(_mm_cmpeq_epi16(r, zero)) == 0xFFFF)
         t[i] = zero;
      else
      {
         __m128i lo, hi;
         MDEC_IDCT_ROW(p, r, lo, hi);
         t[i] = _mm_packs_epi32(lo, hi);   /* fits: no saturation */
      }
   }

   /* Transposed, the rows pass 2 takes */
   {
      const __m128i a0 = _mm_unpacklo_epi16(t[0], t[1]);
      const __m128i a1 = _mm_unpackhi_epi16(t[0], t[1]);
      const __m128i a2 = _mm_unpacklo_epi16(t[2], t[3]);
      const __m128i a3 = _mm_unpackhi_epi16(t[2], t[3]);
      const __m128i a4 = _mm_unpacklo_epi16(t[4], t[5]);
      const __m128i a5 = _mm_unpackhi_epi16(t[4], t[5]);
      const __m128i a6 = _mm_unpacklo_epi16(t[6], t[7]);
      const __m128i a7 = _mm_unpackhi_epi16(t[6], t[7]);
      const __m128i b0 = _mm_unpacklo_epi32(a0, a2);
      const __m128i b1 = _mm_unpackhi_epi32(a0, a2);
      const __m128i b2 = _mm_unpacklo_epi32(a1, a3);
      const __m128i b3 = _mm_unpackhi_epi32(a1, a3);
      const __m128i b4 = _mm_unpacklo_epi32(a4, a6);
      const __m128i b5 = _mm_unpackhi_epi32(a4, a6);
      const __m128i b6 = _mm_unpacklo_epi32(a5, a7);
      const __m128i b7 = _mm_unpackhi_epi32(a5, a7);
      t[0] = _mm_unpacklo_epi64(b0, b4);
      t[1] = _mm_unpackhi_epi64(b0, b4);
      t[2] = _mm_unpacklo_epi64(b1, b5);
      t[3] = _mm_unpackhi_epi64(b1, b5);
      t[4] = _mm_unpacklo_epi64(b2, b6);
      t[5] = _mm_unpackhi_epi64(b2, b6);
      t[6] = _mm_unpacklo_epi64(b3, b7);
      t[7] = _mm_unpackhi_epi64(b3, b7);
   }

   /* Pass 2: wrapped to 9 bits, saturated to 8 */
   for (i = 0; i < 8; i += 2)
   {
      __m128i lo0, hi0, lo1, hi1, w0, w1;
      MDEC_IDCT_ROW(p, t[i], lo0, hi0);
      MDEC_IDCT_ROW(p, t[i + 1], lo1, hi1);
      lo0 = _mm_srai_epi32(_mm_slli_epi32(lo0, 23), 23);
      hi0 = _mm_srai_epi32(_mm_slli_epi32(hi0, 23), 23);
      lo1 = _mm_srai_epi32(_mm_slli_epi32(lo1, 23), 23);
      hi1 = _mm_srai_epi32(_mm_slli_epi32(hi1, 23), 23);
      w0  = _mm_packs_epi32(lo0, hi0);
      w1  = _mm_packs_epi32(lo1, hi1);
      _mm_storeu_si128((__m128i *)&out[i * 8], _mm_packs_epi16(w0, w1));
   }
}
#undef MDEC_IDCT_ROW

#elif defined(MDEC_IDCT_NEON)
#define MDEC_IDCT_ROW(p, r, lo, hi) \
   do { \
      const int16x4_t rl_ = vget_low_s16(r); \
      const int16x4_t rh_ = vget_high_s16(r); \
      (lo) = vmull_lane_s16(vget_low_s16((p)->cols[0]), rl_, 0); \
      (hi) = vmull_lane_s16(vget_high_s16((p)->cols[0]), rl_, 0); \
      (lo) = vmlal_lane_s16((lo), vget_low_s16((p)->cols[1]), rl_, 1); \
      (hi) = vmlal_lane_s16((hi), vget_high_s16((p)->cols[1]), rl_, 1); \
      (lo) = vmlal_lane_s16((lo), vget_low_s16((p)->cols[2]), rl_, 2); \
      (hi) = vmlal_lane_s16((hi), vget_high_s16((p)->cols[2]), rl_, 2); \
      (lo) = vmlal_lane_s16((lo), vget_low_s16((p)->cols[3]), rl_, 3); \
      (hi) = vmlal_lane_s16((hi), vget_high_s16((p)->cols[3]), rl_, 3); \
      (lo) = vmlal_lane_s16((lo), vget_low_s16((p)->cols[4]), rh_, 0); \
      (hi) = vmlal_lane_s16((hi), vget_high_s16((p)->cols[4]), rh_, 0); \
      (lo) = vmlal_lane_s16((lo), vget_low_s16((p)->cols[5]), rh_, 1); \
      (hi) = vmlal_lane_s16((hi), vget_high_s16((p)->cols[5]), rh_, 1); \
      (lo) = vmlal_lane_s16((lo), vget_low_s16((p)->cols[6]), rh_, 2); \
      (hi) = vmlal_lane_s16((hi), vget_high_s16((p)->cols[6]), rh_, 2); \
      (lo) = vmlal_lane_s16((lo), vget_low_s16((p)->cols[7]), rh_, 3); \
      (hi) = vmlal_lane_s16((hi), vget_high_s16((p)->cols[7]), rh_, 3); \
      (lo) = vrshrq_n_s32((lo), 15); \
      (hi) = vrshrq_n_s32((hi), 15); \
   } while (0)

static INLINE void mdec_idct(const mdec_idct_matrix *p,
      const int16_t *in, int8_t *out)
{
   int16_t t[64];
   unsigned i;

   /* Pass 1, stored transposed */
   for (i = 0; i < 8; i++)
   {
      const int16x8_t r = vld1q_s16(&in[i * 8]);
      int16x8_t       v;
      if (!(vgetq_lane_u64(vreinterpretq_u64_s16(r), 0)
               | vgetq_lane_u64(vreinterpretq_u64_s16(r), 1)))
         v = vdupq_n_s16(0);
      else
      {
         int32x4_t lo, hi;
         MDEC_IDCT_ROW(p, r, lo, hi);
         v = vcombine_s16(vmovn_s32(lo), vmovn_s32(hi));
      }
      t[0 * 8 + i] = vgetq_lane_s16(v, 0);
      t[1 * 8 + i] = vgetq_lane_s16(v, 1);
      t[2 * 8 + i] = vgetq_lane_s16(v, 2);
      t[3 * 8 + i] = vgetq_lane_s16(v, 3);
      t[4 * 8 + i] = vgetq_lane_s16(v, 4);
      t[5 * 8 + i] = vgetq_lane_s16(v, 5);
      t[6 * 8 + i] = vgetq_lane_s16(v, 6);
      t[7 * 8 + i] = vgetq_lane_s16(v, 7);
   }

   /* Pass 2: wrapped to 9 bits, saturated to 8 */
   for (i = 0; i < 8; i++)
   {
      int32x4_t lo, hi;
      const int16x8_t r = vld1q_s16(&t[i * 8]);
      MDEC_IDCT_ROW(p, r, lo, hi);
      lo = vshrq_n_s32(vshlq_n_s32(lo, 23), 23);
      hi = vshrq_n_s32(vshlq_n_s32(hi, 23), 23);
      vst1_s8(&out[i * 8],
            vqmovn_s16(vcombine_s16(vqmovn_s32(lo), vqmovn_s32(hi))));
   }
}
#undef MDEC_IDCT_ROW

#else
static INLINE void mdec_idct(const mdec_idct_matrix *p,
      const int16_t *in, int8_t *out)
{
   int16_t  t[64];
   unsigned col, x, u;

   for (col = 0; col < 8; col++)
      for (x = 0; x < 8; x++)
      {
         int32_t sum = 0;
         for (u = 0; u < 8; u++)
            sum += in[col * 8 + u] * p->m[x * 8 + u];
         t[x * 8 + col] = (int16_t)((sum + 0x4000) >> 15);
      }
   for (col = 0; col < 8; col++)
      for (x = 0; x < 8; x++)
      {
         int32_t sum = 0;
         for (u = 0; u < 8; u++)
            sum += t[col * 8 + u] * p->m[x * 8 + u];
         out[col * 8 + x] = mdec_idct_clamp((sum + 0x4000) >> 15);
      }
}
#endif

#endif
