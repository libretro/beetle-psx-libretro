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

/*
 MDEC_READ_FIFO(tfr) vs InCounter vs MDEC_DMACanRead() is a bit fragile right now.  Actually, the entire horrible state machine monstrosity is fragile.

 TODO: OutFIFOReady, so <16bpp works right.

 TODO CODE:

  bool InFIFOReady;

  if(FastFIFO_CanWrite(&InFIFO))
  {
   FastFIFO_Write(&InFIFO, V);

   if(InCommand)
   {
    if(InCounter != 0xFFFF)
    {
     InCounter--;

     // This condition when FastFIFO_CanWrite(&InFIFO) != 0 is a bit buggy on real hardware, decoding loop *seems* to be reading too
     // much and pulling garbage from the FIFO.
     if(InCounter == 0xFFFF)
      InFIFOReady = true;
    }

    if(FastFIFO_CanWrite(&InFIFO) == 0)
     InFIFOReady = true;
   }
  }
*/

/* Good test-case games:
 *	Dragon Knight 4(bad disc?)
 *	Final Fantasy 7 intro movie.
 *	GameShark Version 4.0 intro movie; (clever) abuse of DMA channel 0.
 *	SimCity 2000 startup. */


#include <math.h>
#include <stdint.h>
#include <string.h>

#include <boolean.h>
#include <retro_miscellaneous.h>

#include "../mednafen-types.h"
#include "../masmem.h"
#include "../math_ops.h"
#include "../state_helpers.h"

#include "mdec.h"
#include "FastFIFO.h"

#if defined(__SSE2__)
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

/* NEON counterpart to the SSE2 MDEC fast paths (YCbCr->RGB).
 * __ARM_NEON covers AArch64 (mandatory) and 32-bit ARM -mfpu=neon;
 * arm_neon.h supplies the same intrinsics on both.  Only used when
 * __SSE2__ is absent. */
#if !defined(__SSE2__) && (defined(__ARM_NEON) || defined(__ARM_NEON__))
#include <arm_neon.h>
#define MDEC_HAVE_NEON 1
#endif

#if defined(ARCH_POWERPC_ALTIVEC) && defined(HAVE_ALTIVEC_H)
 #include <altivec.h>
#endif

static int32_t ClockCounter;
static unsigned MDRPhase;
static FastFIFO InFIFO;
static FastFIFO OutFIFO;

static int8_t block_y[8][8];
static int8_t block_cb[8][8];	/* [y >> 1][x >> 1] */
static int8_t block_cr[8][8];	/* [y >> 1][x >> 1] */

static uint32_t Control;
static uint32_t Command;
static bool InCommand;

static uint8_t QMatrix[2][64];
static uint32_t QMIndex;

MDFN_ALIGN(16) static int16_t IDCTMatrix[64];
static uint32_t IDCTMIndex;

static uint8_t QScale;

MDFN_ALIGN(16) static int16_t Coeff[64];
static uint32_t CoeffIndex;
static uint32_t DecodeWB;

static union
{
 uint32_t pix32[48];
 uint16_t pix16[96];
 uint8_t   pix8[192];
} PixelBuffer;
static uint32_t PixelBufferReadOffset;
static uint32_t PixelBufferCount32;

static uint16_t InCounter;

static uint8_t RAMOffsetY;
static uint8_t RAMOffsetCounter;
static uint8_t RAMOffsetWWS;

static const uint8_t ZigZag[64] =
{
 0x00, 0x08, 0x01, 0x02, 0x09, 0x10, 0x18, 0x11,
 0x0a, 0x03, 0x04, 0x0b, 0x12, 0x19, 0x20, 0x28,
 0x21, 0x1a, 0x13, 0x0c, 0x05, 0x06, 0x0d, 0x14,
 0x1b, 0x22, 0x29, 0x30, 0x38, 0x31, 0x2a, 0x23,
 0x1c, 0x15, 0x0e, 0x07, 0x0f, 0x16, 0x1d, 0x24,
 0x2b, 0x32, 0x39, 0x3a, 0x33, 0x2c, 0x25, 0x1e,
 0x17, 0x1f, 0x26, 0x2d, 0x34, 0x3b, 0x3c, 0x35,
 0x2e, 0x27, 0x2f, 0x36, 0x3d, 0x3e, 0x37, 0x3f,
};

extern int32_t EventCycles;

void MDEC_Power(void)
{
   ClockCounter = 0;
   MDRPhase = 0;

   FastFIFO_Flush(&InFIFO);
   FastFIFO_Flush(&OutFIFO);

   memset(block_y, 0, sizeof(block_y));
   memset(block_cb, 0, sizeof(block_cb));
   memset(block_cr, 0, sizeof(block_cr));

   Control = 0;
   Command = 0;
   InCommand = false;

   memset(QMatrix, 0, sizeof(QMatrix));
   QMIndex = 0;

   memset(IDCTMatrix, 0, sizeof(IDCTMatrix));
   IDCTMIndex = 0;

   QScale = 0;

   memset(Coeff, 0, sizeof(Coeff));
   CoeffIndex = 0;
   DecodeWB = 0;

   memset(PixelBuffer.pix32, 0, sizeof(PixelBuffer.pix32));
   PixelBufferReadOffset = 0;
   PixelBufferCount32 = 0;

   InCounter = 0;

   RAMOffsetY = 0;
   RAMOffsetCounter = 0;
   RAMOffsetWWS = 0;
}

int MDEC_StateAction(StateMem *sm, int load, int data_only)
{
   SFORMAT StateRegs[] =
   {
      SFVAR(ClockCounter),
      SFVAR(MDRPhase),

#define SFFIFO32(fifoobj)  SFARRAY32(&fifoobj.data[0], sizeof(fifoobj.data) / sizeof(fifoobj.data[0])),	\
      SFVAR(fifoobj.read_pos),				\
      SFVAR(fifoobj.write_pos),				\
      SFVAR(fifoobj.in_count)

      SFFIFO32(InFIFO),
      SFFIFO32(OutFIFO),
#undef SFFIFO

      SFARRAY(&block_y[0][0], sizeof(block_y) / sizeof(block_y[0][0])),
      SFARRAY(&block_cb[0][0], sizeof(block_cb) / sizeof(block_cb[0][0])),
      SFARRAY(&block_cr[0][0], sizeof(block_cr) / sizeof(block_cr[0][0])),

      SFVAR(Control),
      SFVAR(Command),
      SFVAR(InCommand),

      SFARRAY(&QMatrix[0][0], sizeof(QMatrix) / sizeof(QMatrix[0][0])),
      SFVAR(QMIndex),

      SFARRAY16(&IDCTMatrix[0], sizeof(IDCTMatrix) / sizeof(IDCTMatrix[0])),
      SFVAR(IDCTMIndex),

      SFVAR(QScale),

      SFARRAY16(&Coeff[0], sizeof(Coeff) / sizeof(Coeff[0])),
      SFVAR(CoeffIndex),
      SFVAR(DecodeWB),

      SFARRAY32(&PixelBuffer.pix32[0], sizeof(PixelBuffer.pix32) / sizeof(PixelBuffer.pix32[0])),
      SFVAR(PixelBufferReadOffset),
      SFVAR(PixelBufferCount32),

      SFVAR(InCounter),

      SFVAR(RAMOffsetY),
      SFVAR(RAMOffsetCounter),
      SFVAR(RAMOffsetWWS),

      SFEND
   };

   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "MDEC");

   if(load)
   {
      FastFIFO_SaveStatePostLoad(&InFIFO);
      FastFIFO_SaveStatePostLoad(&OutFIFO);

      PixelBufferCount32 %= (sizeof(PixelBuffer.pix32) / sizeof(PixelBuffer.pix32[0])) + 1;
   }

   return(ret);
}

static INLINE int8_t Mask9ClampS8(int32_t v)
{
   v = sign_x_to_s32(9, v);

   if(v < -128)
      v = -128;

   if(v > 127)
      v = 127;

   return v;
}

/* Two specialized IDCT 1-D pass functions.
 * The pass-1 (int16) variant writes the result transposed to feed pass-2;
 * pass-2 (int8) writes non-transposed and saturates through Mask9ClampS8. */
static void IDCT_1D_Pass1(const int16_t *in_coeff, int16_t *out_coeff)
{
   unsigned col, x;

   for (col = 0; col < 8; col++)
   {
#if defined(__SSE2__)
      __m128i c =  _mm_load_si128((const __m128i *)&in_coeff[(col * 8)]);
#endif

      for (x = 0; x < 8; x++)
      {
#ifdef __SSE2__
         MDFN_ALIGN(16) int32_t tmp[4];
         __m128i m   = _mm_load_si128((__m128i *)&IDCTMatrix[(x * 8)]);
         __m128i sum = _mm_madd_epi16(m, c);
         sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, (3 << 0) | (2 << 2) | (1 << 4) | (0 << 6)));
         sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, (1 << 0) | (0 << 2)));

         _mm_store_si128((__m128i*)tmp, sum);

         out_coeff[(x * 8) + col] = (tmp[0] + 0x4000) >> 15;
#else
         int32_t sum = 0;
         unsigned u;

         for (u = 0; u < 8; u++)
            sum += (in_coeff[(col * 8) + u] * IDCTMatrix[(x * 8) + u]);

         out_coeff[(x * 8) + col] = (sum + 0x4000) >> 15;
#endif
      }
   }
}

static void IDCT_1D_Pass2(const int16_t *in_coeff, int8_t *out_coeff)
{
   unsigned col, x;

   for (col = 0; col < 8; col++)
   {
#if defined(__SSE2__)
      __m128i c =  _mm_load_si128((const __m128i *)&in_coeff[(col * 8)]);
#endif

      for (x = 0; x < 8; x++)
      {
#ifdef __SSE2__
         MDFN_ALIGN(16) int32_t tmp[4];
         __m128i m   = _mm_load_si128((__m128i *)&IDCTMatrix[(x * 8)]);
         __m128i sum = _mm_madd_epi16(m, c);
         sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, (3 << 0) | (2 << 2) | (1 << 4) | (0 << 6)));
         sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, (1 << 0) | (0 << 2)));

         _mm_store_si128((__m128i*)tmp, sum);

         out_coeff[(col * 8) + x] = Mask9ClampS8((tmp[0] + 0x4000) >> 15);
#else
         int32_t sum = 0;
         unsigned u;

         for (u = 0; u < 8; u++)
            sum += (in_coeff[(col * 8) + u] * IDCTMatrix[(x * 8) + u]);

         out_coeff[(col * 8) + x] = Mask9ClampS8((sum + 0x4000) >> 15);
#endif
      }
   }
}

static void IDCT(int16_t *in_coeff, int8_t *out_coeff)
{
   MDFN_ALIGN(16) int16_t tmpbuf[64];

   IDCT_1D_Pass1(in_coeff, tmpbuf);
   IDCT_1D_Pass2(tmpbuf, out_coeff);
}

static INLINE void YCbCr_to_RGB(const int8_t y, const int8_t cb, const int8_t cr, int *r, int *g, int *b)
{
   /* The formula for green is still a bit off(precision/rounding issues when both cb and cr are non-zero). */
   *r = Mask9ClampS8(y + (((359 * cr) + 0x80) >> 8));
   /*g = Mask9ClampS8(y + (((-88 * cb) + (-183 * cr) + 0x80) >> 8)); */
   *g = Mask9ClampS8(y + ((((-88 * cb) &~ 0x1F) + ((-183 * cr) &~ 0x07) + 0x80) >> 8));
   *b = Mask9ClampS8(y + (((454 * cb) + 0x80) >> 8));

   *r ^= 0x80;
   *g ^= 0x80;
   *b ^= 0x80;
}

static INLINE uint16_t RGB_to_RGB555(uint8_t r, uint8_t g, uint8_t b)
{
   r = (r + 4) >> 3;
   g = (g + 4) >> 3;
   b = (b + 4) >> 3;

   if(r > 0x1F)
      r = 0x1F;

   if(g > 0x1F)
      g = 0x1F;

   if(b > 0x1F)
      b = 0x1F;

   return((r << 0) | (g << 5) | (b << 10));
}

#if defined(__SSE2__) || defined(MDEC_HAVE_NEON)
/*
 * Vectorised YCbCr->RGB for one 8-pixel row of an MDEC macroblock.
 * by[0..7] is luma (one per pixel); cb[0..3]/cr[0..3] are chroma (one
 * per two pixels, i.e. the x>>1 subsampling).  Two output variants
 * match the scalar EncodeImage 24bpp and 16bpp arms bit-for-bit.
 *
 * Per-pixel scalar reference (YCbCr_to_RGB):
 *   r = M9(y + ((359*cr + 0x80) >> 8))                          ^ 0x80
 *   g = M9(y + (((-88*cb &~ 0x1F) + (-183*cr &~ 0x07) + 0x80) >> 8)) ^ 0x80
 *   b = M9(y + ((454*cb + 0x80) >> 8))                          ^ 0x80
 * with M9(v) = clamp(sign9(v), -128, 127), sign9(v) the low 9 bits of
 * v interpreted signed.  The chroma products overflow int16 (454*127
 * > 32767), so the multiplies and the >>8 run in 32-bit lanes (split
 * into low/high halves); results pack back to 16-bit for M9 (a
 * 16-bit (v<<7)>>7 reproduces sign9 exactly for the actual operand
 * range, which is within +/-512) and the channel assembly.  ^0x80
 * maps the signed [-128,127] result into the [0,255] byte domain.
 *
 * SSE2 has no 32-bit packed multiply (mullo_epi32 is SSE4.1) or
 * unsigned 32->16 pack, so MUL32 emulates the multiply via two
 * mul_epu32 halves; the channel values stay small enough that
 * packs_epi32 (signed) is exact.  NEON has vmulq_s32 / vmovn_s32
 * directly.
 */
#if defined(__SSE2__)
#define MDEC_MUL32(a, b) _mm_or_si128( \
   _mm_and_si128(_mm_mul_epu32(a, b), _mm_set1_epi64x(0xFFFFFFFF)), \
   _mm_slli_epi64(_mm_mul_epu32(_mm_srli_epi64(a, 32), _mm_srli_epi64(b, 32)), 32))
#define MDEC_M9(v) _mm_min_epi16(_mm_max_epi16( \
   _mm_srai_epi16(_mm_slli_epi16(v, 7), 7), _mm_set1_epi16(-128)), _mm_set1_epi16(127))

static INLINE void mdec_ycbcr_row(const int8_t *by, const int8_t *cb,
      const int8_t *cr, __m128i *R_out, __m128i *G_out, __m128i *B_out)
{
   __m128i y16 = _mm_srai_epi16(_mm_slli_epi16(
                   _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i*)by), _mm_setzero_si128()), 8), 8);
   int16_t cbd[8], crd[8];
   int l;
   for (l = 0; l < 4; l++) { cbd[2*l]=cbd[2*l+1]=cb[l]; crd[2*l]=crd[2*l+1]=cr[l]; }
   {
      __m128i CB = _mm_loadu_si128((const __m128i*)cbd);
      __m128i CR = _mm_loadu_si128((const __m128i*)crd);
      __m128i cb_lo=_mm_srai_epi32(_mm_unpacklo_epi16(CB,CB),16), cb_hi=_mm_srai_epi32(_mm_unpackhi_epi16(CB,CB),16);
      __m128i cr_lo=_mm_srai_epi32(_mm_unpacklo_epi16(CR,CR),16), cr_hi=_mm_srai_epi32(_mm_unpackhi_epi16(CR,CR),16);
      __m128i y_lo=_mm_srai_epi32(_mm_unpacklo_epi16(y16,y16),16), y_hi=_mm_srai_epi32(_mm_unpackhi_epi16(y16,y16),16);
      const __m128i k359=_mm_set1_epi32(359),k454=_mm_set1_epi32(454),km88=_mm_set1_epi32(-88),km183=_mm_set1_epi32(-183),bias=_mm_set1_epi32(0x80);
      __m128i r_lo=_mm_add_epi32(y_lo,_mm_srai_epi32(_mm_add_epi32(MDEC_MUL32(k359,cr_lo),bias),8));
      __m128i r_hi=_mm_add_epi32(y_hi,_mm_srai_epi32(_mm_add_epi32(MDEC_MUL32(k359,cr_hi),bias),8));
      __m128i b_lo=_mm_add_epi32(y_lo,_mm_srai_epi32(_mm_add_epi32(MDEC_MUL32(k454,cb_lo),bias),8));
      __m128i b_hi=_mm_add_epi32(y_hi,_mm_srai_epi32(_mm_add_epi32(MDEC_MUL32(k454,cb_hi),bias),8));
      __m128i a0=_mm_andnot_si128(_mm_set1_epi32(0x1F),MDEC_MUL32(km88,cb_lo));
      __m128i a1=_mm_andnot_si128(_mm_set1_epi32(0x1F),MDEC_MUL32(km88,cb_hi));
      __m128i c0=_mm_andnot_si128(_mm_set1_epi32(0x07),MDEC_MUL32(km183,cr_lo));
      __m128i c1=_mm_andnot_si128(_mm_set1_epi32(0x07),MDEC_MUL32(km183,cr_hi));
      __m128i g_lo=_mm_add_epi32(y_lo,_mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(a0,c0),bias),8));
      __m128i g_hi=_mm_add_epi32(y_hi,_mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(a1,c1),bias),8));
      *R_out = MDEC_M9(_mm_packs_epi32(r_lo,r_hi));
      *G_out = MDEC_M9(_mm_packs_epi32(g_lo,g_hi));
      *B_out = MDEC_M9(_mm_packs_epi32(b_lo,b_hi));
   }
}

static INLINE void EncodeRow24(const int8_t *by, const int8_t *cb,
      const int8_t *cr, uint8_t rgb_xor, uint8_t *out)
{
   __m128i R, G, B;
   __m128i flip = _mm_set1_epi16((short)(0x80 ^ rgb_xor));
   uint8_t r8[8], g8[8], b8[8];
   int i;
   mdec_ycbcr_row(by, cb, cr, &R, &G, &B);
   R = _mm_xor_si128(_mm_and_si128(R, _mm_set1_epi16(0xFF)), flip);
   G = _mm_xor_si128(_mm_and_si128(G, _mm_set1_epi16(0xFF)), flip);
   B = _mm_xor_si128(_mm_and_si128(B, _mm_set1_epi16(0xFF)), flip);
   _mm_storel_epi64((__m128i*)r8, _mm_packus_epi16(R, R));
   _mm_storel_epi64((__m128i*)g8, _mm_packus_epi16(G, G));
   _mm_storel_epi64((__m128i*)b8, _mm_packus_epi16(B, B));
   for (i = 0; i < 8; i++) { out[i*3+0]=r8[i]; out[i*3+1]=g8[i]; out[i*3+2]=b8[i]; }
}

static INLINE void EncodeRow16(const int8_t *by, const int8_t *cb,
      const int8_t *cr, uint16_t pixel_xor, uint16_t *out)
{
   __m128i R, G, B, r5, g5, b5, pix;
   __m128i o80 = _mm_set1_epi16(0x80), four=_mm_set1_epi16(4), m1F=_mm_set1_epi16(0x1F);
   mdec_ycbcr_row(by, cb, cr, &R, &G, &B);
   R = _mm_add_epi16(R, o80); G = _mm_add_epi16(G, o80); B = _mm_add_epi16(B, o80);
   r5 = _mm_min_epi16(_mm_srli_epi16(_mm_add_epi16(R, four), 3), m1F);
   g5 = _mm_min_epi16(_mm_srli_epi16(_mm_add_epi16(G, four), 3), m1F);
   b5 = _mm_min_epi16(_mm_srli_epi16(_mm_add_epi16(B, four), 3), m1F);
   pix = _mm_or_si128(r5, _mm_or_si128(_mm_slli_epi16(g5, 5), _mm_slli_epi16(b5, 10)));
   pix = _mm_xor_si128(pix, _mm_set1_epi16((short)pixel_xor));
   _mm_storeu_si128((__m128i*)out, pix);
}
#undef MDEC_MUL32
#undef MDEC_M9

#else /* MDEC_HAVE_NEON */
#define MDEC_M9(v) vminq_s16(vmaxq_s16( \
   vshrq_n_s16(vshlq_n_s16(v, 7), 7), vdupq_n_s16(-128)), vdupq_n_s16(127))

static INLINE void mdec_ycbcr_row(const int8_t *by, const int8_t *cb,
      const int8_t *cr, int16x8_t *R_out, int16x8_t *G_out, int16x8_t *B_out)
{
   int16x8_t y16 = vmovl_s8(vld1_s8(by));
   int16_t cbd[8], crd[8];
   int l;
   for (l = 0; l < 4; l++) { cbd[2*l]=cbd[2*l+1]=cb[l]; crd[2*l]=crd[2*l+1]=cr[l]; }
   {
      int16x8_t CB = vld1q_s16(cbd), CR = vld1q_s16(crd);
      int32x4_t cb_lo=vmovl_s16(vget_low_s16(CB)), cb_hi=vmovl_s16(vget_high_s16(CB));
      int32x4_t cr_lo=vmovl_s16(vget_low_s16(CR)), cr_hi=vmovl_s16(vget_high_s16(CR));
      int32x4_t y_lo=vmovl_s16(vget_low_s16(y16)), y_hi=vmovl_s16(vget_high_s16(y16));
      const int32x4_t bias=vdupq_n_s32(0x80);
      int32x4_t r_lo=vaddq_s32(y_lo,vshrq_n_s32(vaddq_s32(vmulq_n_s32(cr_lo,359),bias),8));
      int32x4_t r_hi=vaddq_s32(y_hi,vshrq_n_s32(vaddq_s32(vmulq_n_s32(cr_hi,359),bias),8));
      int32x4_t b_lo=vaddq_s32(y_lo,vshrq_n_s32(vaddq_s32(vmulq_n_s32(cb_lo,454),bias),8));
      int32x4_t b_hi=vaddq_s32(y_hi,vshrq_n_s32(vaddq_s32(vmulq_n_s32(cb_hi,454),bias),8));
      int32x4_t a0=vbicq_s32(vmulq_n_s32(cb_lo,-88),vdupq_n_s32(0x1F));
      int32x4_t a1=vbicq_s32(vmulq_n_s32(cb_hi,-88),vdupq_n_s32(0x1F));
      int32x4_t c0=vbicq_s32(vmulq_n_s32(cr_lo,-183),vdupq_n_s32(0x07));
      int32x4_t c1=vbicq_s32(vmulq_n_s32(cr_hi,-183),vdupq_n_s32(0x07));
      int32x4_t g_lo=vaddq_s32(y_lo,vshrq_n_s32(vaddq_s32(vaddq_s32(a0,c0),bias),8));
      int32x4_t g_hi=vaddq_s32(y_hi,vshrq_n_s32(vaddq_s32(vaddq_s32(a1,c1),bias),8));
      *R_out = MDEC_M9(vcombine_s16(vmovn_s32(r_lo),vmovn_s32(r_hi)));
      *G_out = MDEC_M9(vcombine_s16(vmovn_s32(g_lo),vmovn_s32(g_hi)));
      *B_out = MDEC_M9(vcombine_s16(vmovn_s32(b_lo),vmovn_s32(b_hi)));
   }
}

static INLINE void EncodeRow24(const int8_t *by, const int8_t *cb,
      const int8_t *cr, uint8_t rgb_xor, uint8_t *out)
{
   int16x8_t R, G, B;
   int16x8_t flip = vdupq_n_s16((int16_t)(0x80 ^ rgb_xor));
   uint8x8_t r8, g8, b8;
   uint8_t rb[8], gb[8], bb[8];
   int i;
   mdec_ycbcr_row(by, cb, cr, &R, &G, &B);
   R = veorq_s16(vandq_s16(R, vdupq_n_s16(0xFF)), flip);
   G = veorq_s16(vandq_s16(G, vdupq_n_s16(0xFF)), flip);
   B = veorq_s16(vandq_s16(B, vdupq_n_s16(0xFF)), flip);
   r8 = vqmovun_s16(R); g8 = vqmovun_s16(G); b8 = vqmovun_s16(B);
   vst1_u8(rb, r8); vst1_u8(gb, g8); vst1_u8(bb, b8);
   for (i = 0; i < 8; i++) { out[i*3+0]=rb[i]; out[i*3+1]=gb[i]; out[i*3+2]=bb[i]; }
}

static INLINE void EncodeRow16(const int8_t *by, const int8_t *cb,
      const int8_t *cr, uint16_t pixel_xor, uint16_t *out)
{
   int16x8_t R, G, B, r5, g5, b5, pix;
   int16x8_t o80=vdupq_n_s16(0x80), four=vdupq_n_s16(4), m1F=vdupq_n_s16(0x1F);
   mdec_ycbcr_row(by, cb, cr, &R, &G, &B);
   R = vaddq_s16(R, o80); G = vaddq_s16(G, o80); B = vaddq_s16(B, o80);
   r5 = vminq_s16(vshrq_n_s16(vaddq_s16(R, four), 3), m1F);
   g5 = vminq_s16(vshrq_n_s16(vaddq_s16(G, four), 3), m1F);
   b5 = vminq_s16(vshrq_n_s16(vaddq_s16(B, four), 3), m1F);
   pix = vorrq_s16(r5, vorrq_s16(vshlq_n_s16(g5, 5), vshlq_n_s16(b5, 10)));
   pix = veorq_s16(pix, vdupq_n_s16((int16_t)pixel_xor));
   vst1q_u16(out, vreinterpretq_u16_s16(pix));
}
#undef MDEC_M9
#endif
#endif /* SSE2 || NEON */

static void EncodeImage(const unsigned ybn)
{

   PixelBufferCount32 = 0;

   switch((Command >> 27) & 0x3)
   {
      case 0:	/* 4bpp */
         {
            const uint8_t us_xor = (Command & (1U << 26)) ? 0x00 : 0x88;
            uint8_t* pix_out = PixelBuffer.pix8;
            int y, x;

            for(y = 0; y < 8; y++)
            {
               for(x = 0; x < 8; x += 2)
               {
                  int v0 = block_y[y][x + 0] + 8;
                  int v1 = block_y[y][x + 1] + 8;
                  uint8_t p0 = (uint8_t)(v0 > 127 ? 127 : v0);
                  uint8_t p1 = (uint8_t)(v1 > 127 ? 127 : v1);

                  *pix_out = ((p0 >> 4) | (p1 & 0xF0)) ^ us_xor;
                  pix_out++;
               }
            }
            PixelBufferCount32 = 8;
         }
         break;


      case 1:	/* 8bpp */
         {
            const uint8_t us_xor = (Command & (1U << 26)) ? 0x00 : 0x80;
            uint8_t* pix_out = PixelBuffer.pix8;
            int y, x;

            for(y = 0; y < 8; y++)
            {
               for(x = 0; x < 8; x++)
               {
                  *pix_out = (uint8_t)block_y[y][x] ^ us_xor;
                  pix_out++;
               }
            }
            PixelBufferCount32 = 16;
         }
         break;

      case 2:	/* 24bpp */
         {
            const uint8_t rgb_xor = (Command & (1U << 26)) ? 0x80 : 0x00;
            uint8_t* pix_out = PixelBuffer.pix8;
            int y;

            for(y = 0; y < 8; y++)
            {
               const int8_t* by = &block_y[y][0];
               const int8_t* cb = &block_cb[(y >> 1) | ((ybn & 2) << 1)][(ybn & 1) << 2];
               const int8_t* cr = &block_cr[(y >> 1) | ((ybn & 2) << 1)][(ybn & 1) << 2];

#if defined(__SSE2__) || defined(MDEC_HAVE_NEON)
               EncodeRow24(by, cb, cr, rgb_xor, pix_out);
               pix_out += 24;
#else
               int x;
               for(x = 0; x < 8; x++)
               {
                  int r, g, b;

                  YCbCr_to_RGB(by[x], cb[x >> 1], cr[x >> 1], &r, &g, &b);

                  pix_out[0] = r ^ rgb_xor;
                  pix_out[1] = g ^ rgb_xor;
                  pix_out[2] = b ^ rgb_xor;
                  pix_out += 3;
               }
#endif
            }
            PixelBufferCount32 = 48;
         }
         break;

      case 3:	/* 16bpp */
         {
            uint16_t pixel_xor = ((Command & 0x02000000) ? 0x8000 : 0x0000) | ((Command & (1U << 26)) ? 0x4210 : 0x0000);
            uint16_t* pix_out = PixelBuffer.pix16;
            int y;

            for(y = 0; y < 8; y++)
            {
               const int8_t* by = &block_y[y][0];
               const int8_t* cb = &block_cb[(y >> 1) | ((ybn & 2) << 1)][(ybn & 1) << 2];
               const int8_t* cr = &block_cr[(y >> 1) | ((ybn & 2) << 1)][(ybn & 1) << 2];

#if defined(__SSE2__) || defined(MDEC_HAVE_NEON)
               /* Native 128-bit store; the SSE2/NEON targets are all
                * little-endian, matching the scalar StoreU16_LE. */
               EncodeRow16(by, cb, cr, pixel_xor, pix_out);
               pix_out += 8;
#else
               int x;
               for(x = 0; x < 8; x++)
               {
                  int r, g, b;

                  YCbCr_to_RGB(by[x], cb[x >> 1], cr[x >> 1], &r, &g, &b);

                  StoreU16_LE(pix_out, pixel_xor ^ RGB_to_RGB555(r, g, b));
                  pix_out++;
               }
#endif
            }
            PixelBufferCount32 = 32;
         }
         break;

   }
}

static INLINE void WriteImageData(uint16_t V, int32_t* eat_cycles)
{
   const uint32_t qmw = (bool)(DecodeWB < 2);


   if(!CoeffIndex)
   {
      if(V == 0xFE00)
      {
         return;
      }

      QScale = V >> 10;

      {
         int q = QMatrix[qmw][0];	/* No QScale here! */
         int ci = sign_10_to_s16(V & 0x3FF);
         int tmp;

         if(q != 0)
            tmp = (int32_t)((uint32_t)(ci * q) << 4) + (ci ? ((ci < 0) ? 8 : -8) : 0);
         else
            tmp = (uint32_t)(ci * 2) << 4;

         /* Not sure if it should be 0x3FFF or 0x3FF0 or maybe 0x3FF8? */
         { int _v = tmp; if (_v < -0x4000) _v = -0x4000; if (_v > 0x3FFF) _v = 0x3FFF; Coeff[ZigZag[0]] = _v; }
         CoeffIndex++;
      }
   }
   else
   {
      if(V == 0xFE00)
      {
         while(CoeffIndex < 64)
            Coeff[ZigZag[CoeffIndex++]] = 0;
      }
      else
      {
         uint32_t rlcount = V >> 10;
         uint32_t i;

         for(i = 0; i < rlcount && CoeffIndex < 64; i++)
         {
            Coeff[ZigZag[CoeffIndex]] = 0;
            CoeffIndex++;
         }

         if(CoeffIndex < 64)
         {
            int q = QScale * QMatrix[qmw][CoeffIndex];
            int ci = sign_10_to_s16(V & 0x3FF);
            int tmp;

            if(q != 0)
               tmp = (int32_t)((uint32_t)((ci * q) >> 3) << 4) + (ci ? ((ci < 0) ? 8 : -8) : 0);
            else
               tmp = (uint32_t)(ci * 2) << 4;

            /* Not sure if it should be 0x3FFF or 0x3FF0 or maybe 0x3FF8? */
            { int _v = tmp; if (_v < -0x4000) _v = -0x4000; if (_v > 0x3FFF) _v = 0x3FFF; Coeff[ZigZag[CoeffIndex]] = _v; }
            CoeffIndex++;
         }
      }
   }

   if(CoeffIndex == 64)
   {
      CoeffIndex = 0;


      switch(DecodeWB)
      {
         case 0:
            IDCT(Coeff, &block_cr[0][0]);
            break;
         case 1:
            IDCT(Coeff, &block_cb[0][0]);
            break;
         case 2:
         case 3:
         case 4:
         case 5:
            IDCT(Coeff, &block_y[0][0]);
            break;
      }

      /* Timing in the actual PS1 MDEC is complex due to (apparent) pipelining, but the average when decoding a large number of blocks is
       * about 512. */
      *eat_cycles += 512;

      if(DecodeWB >= 2)
         EncodeImage((DecodeWB + 4) % 6);

      DecodeWB++;
      if(DecodeWB == (((Command >> 27) & 2) ? 6 : 3))
         DecodeWB = ((Command >> 27) & 2) ? 0 : 2;
   }
}

void MDEC_Run(int32_t clocks)
{
   static const unsigned MDRPhaseBias = 0 + 1;

   ClockCounter += clocks;

   if(ClockCounter > EventCycles)
      ClockCounter = EventCycles;

   switch(MDRPhase + MDRPhaseBias)
   {
      for(;;)
      {
         InCommand = false;
         { { case 1: if(!(InFIFO.in_count)) { MDRPhase = 2 - MDRPhaseBias - 1; return; } }; Command = FastFIFO_Read(&InFIFO); };
         InCommand = true;
         { ClockCounter -= (1); { case 3: if(!(ClockCounter > 0)) { MDRPhase = 4 - MDRPhaseBias - 1; return; } }; };


         /*
          *
          * */
         if(((Command >> 29) & 0x7) == 1)
         {
            InCounter = Command & 0xFFFF;
            FastFIFO_Flush(&OutFIFO);
            /*OutBuffer.Flush(); */

            PixelBufferCount32 = 0;
            CoeffIndex = 0;

            if((Command >> 27) & 2)
               DecodeWB = 0;
            else
               DecodeWB = 2;

            switch((Command >> 27) & 0x3)
            {
               case 0:
               case 1: RAMOffsetWWS = 0; break;
               case 2: RAMOffsetWWS = 6; break;
               case 3: RAMOffsetWWS = 4; break;
            }
            RAMOffsetY = 0;
            RAMOffsetCounter = RAMOffsetWWS;

            InCounter--;
            do
            {
               uint32_t tfr;
               int32_t need_eat; /* = 0; */

               { { case 5: if(!(InFIFO.in_count)) { MDRPhase = 6 - MDRPhaseBias - 1; return; } }; tfr = FastFIFO_Read(&InFIFO); };
               InCounter--;


               need_eat = 0;
               PixelBufferCount32 = 0;
               WriteImageData(tfr, &need_eat);
               WriteImageData(tfr >> 16, &need_eat);

               { ClockCounter -= (need_eat); { case 7: if(!(ClockCounter > 0)) { MDRPhase = 8 - MDRPhaseBias - 1; return; } }; };

               PixelBufferReadOffset = 0;

               while(PixelBufferReadOffset < PixelBufferCount32)
               {
                  { { case 9: if(!(FastFIFO_CanWrite(&OutFIFO))) { MDRPhase = 10 - MDRPhaseBias - 1; return; } }; FastFIFO_Write(&OutFIFO, LoadU32_LE(&PixelBuffer.pix32[PixelBufferReadOffset++])); };
               }
            } while(InCounter != 0xFFFF);
         }
         /*
          *
          * */
         else if(((Command >> 29) & 0x7) == 2)
         {
            QMIndex = 0;
            InCounter = 0x10 + ((Command & 0x1) ? 0x10 : 0x00);

            InCounter--;
            do
            {
               uint32_t tfr;
               int i;

               { { case 11: if(!(InFIFO.in_count)) { MDRPhase = 12 - MDRPhaseBias - 1; return; } }; tfr = FastFIFO_Read(&InFIFO); };
               InCounter--;


               for(i = 0; i < 4; i++)
               {
                  QMatrix[QMIndex >> 6][QMIndex & 0x3F] = (uint8_t)tfr;
                  QMIndex = (QMIndex + 1) & 0x7F;
                  tfr >>= 8;
               }
            } while(InCounter != 0xFFFF);
         }
         /*
          *
          * */
         else if(((Command >> 29) & 0x7) == 3)
         {
            IDCTMIndex = 0;
            InCounter = 0x20;

            InCounter--;
            do
            {
               uint32_t tfr;
               unsigned i;

               { { case 13: if(!(InFIFO.in_count)) { MDRPhase = 14 - MDRPhaseBias - 1; return; } }; tfr = FastFIFO_Read(&InFIFO); };
               InCounter--;

               for(i = 0; i < 2; i++)
               {
                  IDCTMatrix[((IDCTMIndex & 0x7) << 3) | ((IDCTMIndex >> 3) & 0x7)] = (int16_t)(tfr & 0xFFFF) >> 3;
                  IDCTMIndex = (IDCTMIndex + 1) & 0x3F;

                  tfr >>= 16;
               }
            } while(InCounter != 0xFFFF);
         }
         else
         {
            InCounter = Command & 0xFFFF;
         }
      } /* end for(;;) */
   }
}

void MDEC_DMAWrite(uint32_t V)
{
   if(!FastFIFO_CanWrite(&InFIFO))
      return;

   FastFIFO_Write(&InFIFO, V);
   MDEC_Run(0);
}

uint32_t MDEC_DMARead(uint32_t* offs)
{
   uint32_t V = 0;

   *offs = 0;

   if(MDFN_LIKELY(OutFIFO.in_count))
   {
      V = FastFIFO_Read(&OutFIFO);

      *offs = (RAMOffsetY & 0x7) * RAMOffsetWWS;

      if(RAMOffsetY & 0x08)
      {
         *offs = (*offs - RAMOffsetWWS*7);
      }

      RAMOffsetCounter--;
      if(!RAMOffsetCounter)
      {
         RAMOffsetCounter = RAMOffsetWWS;
         RAMOffsetY++;
      }

      MDEC_Run(0);
   }

   return(V);
}

bool MDEC_DMACanWrite(void)
{
 return((FastFIFO_CanWrite(&InFIFO) >= 0x20) && (Control & (1U << 30)) && InCommand && InCounter != 0xFFFF);
}

bool MDEC_DMACanRead(void)
{
 return((OutFIFO.in_count >= 0x20) && (Control & (1U << 29)));
}

void MDEC_Write(const int32_t timestamp, uint32_t A, uint32_t V)
{
   if(A & 4)
   {
      if(V & 0x80000000) /* Reset? */
      {
         MDRPhase = 0;
         InCounter = 0;
         Command = 0;
         InCommand = false;

         PixelBufferCount32 = 0;
         ClockCounter = 0;
         QMIndex = 0;
         IDCTMIndex = 0;

         QScale = 0;

         memset(Coeff, 0, sizeof(Coeff));
         CoeffIndex = 0;
         DecodeWB = 0;

         FastFIFO_Flush(&InFIFO);
         FastFIFO_Flush(&OutFIFO);
      }
      Control = V & 0x7FFFFFFF;
   }
   else
   {
      if(FastFIFO_CanWrite(&InFIFO))
      {
         FastFIFO_Write(&InFIFO, V);

         if(!InCommand)
         {
            if(ClockCounter < 1)
               ClockCounter = 1;
         }
         MDEC_Run(0);
      }
   }
}

uint32_t MDEC_Read(const int32_t timestamp, uint32_t A)
{
 uint32_t ret = 0;

 if(A & 4)
 {
  ret = 0;

  ret |= (OutFIFO.in_count == 0) << 31;
  ret |= (FastFIFO_CanWrite(&InFIFO) == 0) << 30;
  ret |= InCommand << 29;

  ret |= MDEC_DMACanWrite() << 28;
  ret |= MDEC_DMACanRead() << 27;

  ret |= ((Command >> 25) & 0xF) << 23;

  /* Needs refactoring elsewhere to work right: ret |= ((DecodeWB + 4) % 6) << 16; */

  ret |= InCounter & 0xFFFF;
 }
 else
 {
  if(OutFIFO.in_count)
   ret = FastFIFO_Read(&OutFIFO);
 }


 return(ret);
}
