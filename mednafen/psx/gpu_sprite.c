/*
 * DrawSprite - rasterise one sprite (axis-aligned textured or
 * flat-shaded rectangle).
 *
 * Sprites use 1x VRAM coordinates (PlotNativePixel rather than
 * the upscale-aware PlotPixel) since they're commonly used for
 * UI overlays where preserving native pixel boundaries matters.
 *
 * Macro parameters:
 *   SUFFIX        - mangled-name suffix (see naming scheme below)
 *   T_LIT         - 1 for textured sprite, 0 for flat fill
 *   BM_VAL        - integer literal blend mode (-1 / 0..3)
 *   BM_TAG        - matching blend tag (BMopaque / BMavg / BMadd /
 *                   BMsub / BMaddq) used in the PlotNativePixel_<...>
 *                   mangled call
 *   TM_LIT        - 1 to modulate texel by vertex colour, 0 to use
 *                   texel verbatim. Forced 0 by SPR_HELPER when not
 *                   textured.
 *   MO_LIT        - 4bpp / 8bpp / 15bpp texture format (0 / 1 / 2)
 *   ME_LIT        - 1 to gate writes on destination mask bit
 *   FX_LIT        - horizontal flip of texture (PS1's GP0(0xE1)
 *                   bit 12)
 *   FY_LIT        - vertical flip of texture (GP0(0xE1) bit 13)
 *
 * The clipping math (ClipX0/ClipY0/ClipX1/ClipY1) handles
 * partial-offscreen sprites; texture coords are advanced into
 * the clipped region before drawing begins so the visible part
 * samples the right texels.
 */

#if defined(__SSE2__) || defined(GPU_HAVE_NEON)
/*
 * Vectorised inner loop for a *flat* (non-textured) sprite fill row
 * at native resolution.  The colour is the constant fill_color (which
 * always carries 0x8000), so this is the non-textured polygon span
 * with zero interpolation and no dither: build is trivial, only the
 * blend / mask-eval / mask-set vary.  Processes the row in SIMD
 * chunks and returns the pixel count consumed; the caller's scalar
 * loop finishes the remainder.
 *
 * Bit-exact with PlotNativePixel_*_T0:
 *   - BM_VAL >= 0: blend fill_color against the framebuffer with the
 *     same SWAR math as PlotPixelBlend_* (AVG/ADD/ADD_FOURTH in 16-bit
 *     lanes 8 px/iter; SUBTRACT in 32-bit lanes 4 px/iter, since its
 *     0x108420 borrow mask has a bit above lane 15);
 *   - ME_LIT: keep the destination word where its 0x8000 bit is set;
 *   - store (pix & 0x7FFF) | MaskSetOR.
 * Caller gates on upscale_shift == 0 (texel_put then reduces to a
 * single vram_put into the native row) and on a contiguous run within
 * [0,1023] (sprite x is clipped to ClipX0..ClipX1, so no x==1024 wrap).
 */
static INLINE int DrawSpriteFillVec(PS_GPU *gpu, int32_t x, int32_t y,
      int32_t w, uint16_t fill_color, const int BM_VAL, const int ME_LIT)
{
   uint16_t      *row  = &gpu->vram[(uint32_t)(y & 511) << 10];
   const uint16_t mso  = gpu->MaskSetOR;
   const int      step = (BM_VAL == BLEND_MODE_SUBTRACT) ? 4 : 8;
   int32_t        done = 0;
#if defined(__SSE2__)
   const __m128i  FP    = _mm_set1_epi16((short)fill_color);
   const __m128i  v7FFF = _mm_set1_epi16(0x7FFF);
   const __m128i  v8000 = _mm_set1_epi16((short)0x8000);
   const __m128i  vmso  = _mm_set1_epi16((short)mso);

   for (; done + step <= w; done += step)
   {
      const int32_t bx = x + done;
      __m128i pix = FP, bg = _mm_loadu_si128((const __m128i*)&row[bx]), out;

      if (BM_VAL == BLEND_MODE_AVERAGE)
      {
         __m128i bgo = _mm_or_si128(bg, v8000);
         __m128i xr  = _mm_and_si128(_mm_xor_si128(pix, bgo), _mm_set1_epi16(0x0421));
         pix = _mm_srli_epi16(_mm_sub_epi16(_mm_add_epi16(pix, bgo), xr), 1);
      }
      else if (BM_VAL == BLEND_MODE_ADD)
      {
         __m128i b2  = _mm_andnot_si128(v8000, bg);
         __m128i sum = _mm_add_epi16(pix, b2);
         __m128i car = _mm_and_si128(_mm_sub_epi16(sum, _mm_and_si128(_mm_xor_si128(pix, b2), _mm_set1_epi16((short)0x8421))), _mm_set1_epi16((short)0x8420));
         pix = _mm_or_si128(_mm_sub_epi16(sum, car), _mm_sub_epi16(car, _mm_srli_epi16(car, 5)));
      }
      else if (BM_VAL == BLEND_MODE_ADD_FOURTH)
      {
         __m128i b2  = _mm_andnot_si128(v8000, bg);
         __m128i fp2 = _mm_or_si128(_mm_and_si128(_mm_srli_epi16(pix, 2), _mm_set1_epi16(0x1CE7)), v8000);
         __m128i sum = _mm_add_epi16(fp2, b2);
         __m128i car = _mm_and_si128(_mm_sub_epi16(sum, _mm_and_si128(_mm_xor_si128(fp2, b2), _mm_set1_epi16((short)0x8421))), _mm_set1_epi16((short)0x8420));
         pix = _mm_or_si128(_mm_sub_epi16(sum, car), _mm_sub_epi16(car, _mm_srli_epi16(car, 5)));
      }
      else if (BM_VAL == BLEND_MODE_SUBTRACT)
      {
         __m128i z   = _mm_setzero_si128();
         __m128i m84 = _mm_set1_epi32(0x108420), m80 = _mm_set1_epi32(0x8000);
         __m128i fp32 = _mm_unpacklo_epi16(pix, z);
         __m128i bg32 = _mm_unpacklo_epi16(bg, z);
         __m128i bgo  = _mm_or_si128(bg32, m80);
         __m128i fpc  = _mm_andnot_si128(m80, fp32);
         __m128i diff = _mm_add_epi32(_mm_sub_epi32(bgo, fpc), m84);
         __m128i xr   = _mm_and_si128(_mm_xor_si128(bgo, fpc), m84);
         __m128i bor  = _mm_and_si128(_mm_sub_epi32(diff, xr), m84);
         __m128i res  = _mm_and_si128(_mm_sub_epi32(diff, bor), _mm_sub_epi32(bor, _mm_srli_epi32(bor, 5)));
         res = _mm_and_si128(res, _mm_set1_epi32(0xFFFF));
         pix = _mm_add_epi16(_mm_packs_epi32(_mm_sub_epi32(res, _mm_set1_epi32(0x8000)), z), _mm_set1_epi16((short)0x8000));
      }

      out = _mm_or_si128(_mm_and_si128(pix, v7FFF), vmso);
      if (ME_LIT)
      {
         __m128i keep = _mm_srai_epi16(bg, 15);
         out = _mm_or_si128(_mm_and_si128(keep, bg), _mm_andnot_si128(keep, out));
      }
      if (step == 8)
         _mm_storeu_si128((__m128i*)&row[bx], out);
      else
         _mm_storel_epi64((__m128i*)&row[bx], out);
   }
#else /* GPU_HAVE_NEON */
   const uint16x8_t FP    = vdupq_n_u16(fill_color);
   const uint16x8_t v7FFF = vdupq_n_u16(0x7FFF);
   const uint16x8_t v8000 = vdupq_n_u16(0x8000);
   const uint16x8_t vmso  = vdupq_n_u16(mso);

   for (; done + step <= w; done += step)
   {
      const int32_t bx = x + done;
      uint16x8_t pix = FP, bg = vld1q_u16(&row[bx]), out;

      if (BM_VAL == BLEND_MODE_AVERAGE)
      {
         uint16x8_t bgo = vorrq_u16(bg, v8000);
         uint16x8_t xr  = vandq_u16(veorq_u16(pix, bgo), vdupq_n_u16(0x0421));
         pix = vshrq_n_u16(vsubq_u16(vaddq_u16(pix, bgo), xr), 1);
      }
      else if (BM_VAL == BLEND_MODE_ADD)
      {
         uint16x8_t b2  = vbicq_u16(bg, v8000);
         uint16x8_t sum = vaddq_u16(pix, b2);
         uint16x8_t car = vandq_u16(vsubq_u16(sum, vandq_u16(veorq_u16(pix, b2), vdupq_n_u16(0x8421))), vdupq_n_u16(0x8420));
         pix = vorrq_u16(vsubq_u16(sum, car), vsubq_u16(car, vshrq_n_u16(car, 5)));
      }
      else if (BM_VAL == BLEND_MODE_ADD_FOURTH)
      {
         uint16x8_t b2  = vbicq_u16(bg, v8000);
         uint16x8_t fp2 = vorrq_u16(vandq_u16(vshrq_n_u16(pix, 2), vdupq_n_u16(0x1CE7)), v8000);
         uint16x8_t sum = vaddq_u16(fp2, b2);
         uint16x8_t car = vandq_u16(vsubq_u16(sum, vandq_u16(veorq_u16(fp2, b2), vdupq_n_u16(0x8421))), vdupq_n_u16(0x8420));
         pix = vorrq_u16(vsubq_u16(sum, car), vsubq_u16(car, vshrq_n_u16(car, 5)));
      }
      else if (BM_VAL == BLEND_MODE_SUBTRACT)
      {
         uint32x4_t m84 = vdupq_n_u32(0x108420), m80 = vdupq_n_u32(0x8000);
         uint32x4_t fp32 = vmovl_u16(vget_low_u16(pix));
         uint32x4_t bg32 = vmovl_u16(vget_low_u16(bg));
         uint32x4_t bgo  = vorrq_u32(bg32, m80);
         uint32x4_t fpc  = vbicq_u32(fp32, m80);
         uint32x4_t diff = vaddq_u32(vsubq_u32(bgo, fpc), m84);
         uint32x4_t xr   = vandq_u32(veorq_u32(bgo, fpc), m84);
         uint32x4_t bor  = vandq_u32(vsubq_u32(diff, xr), m84);
         uint32x4_t res  = vandq_u32(vsubq_u32(diff, bor), vsubq_u32(bor, vshrq_n_u32(bor, 5)));
         pix = vcombine_u16(vmovn_u32(res), vdup_n_u16(0));
      }

      out = vorrq_u16(vandq_u16(pix, v7FFF), vmso);
      if (ME_LIT)
      {
         uint16x8_t keep = vcgeq_u16(bg, v8000);
         out = vbslq_u16(keep, bg, out);
      }
      if (step == 8)
         vst1q_u16(&row[bx], out);
      else
         vst1_u16(&row[bx], vget_low_u16(out));
   }
#endif
   return done;
}
#endif /* SSE2 || NEON */

#define DEFINE_DrawSprite(SUFFIX, T_LIT, BM_VAL, BM_TAG, TM_LIT, MO_LIT, ME_LIT, FX_LIT, FY_LIT) \
static void DrawSprite_##SUFFIX(PS_GPU *gpu, int32_t x_arg, int32_t y_arg, int32_t w, int32_t h, \
      uint8_t u_arg, uint8_t v_arg, uint32_t color, uint32_t clut_offset) \
{ \
   uint8_t  u = 0, v = 0; \
   const int32_t r          = color & 0xFF; \
   const int32_t g          = (color >> 8) & 0xFF; \
   const int32_t b          = (color >> 16) & 0xFF; \
   const uint16_t fill_color = 0x8000 | ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10); \
   int32_t v_inc            = 1; \
   int32_t u_inc            = 1; \
   int32_t x_start          = x_arg; \
   int32_t x_bound          = x_arg + w; \
   int32_t y_start          = y_arg; \
   int32_t y_bound          = y_arg + h; \
   int32_t  y; \
   /*printf("[GPU] Sprite: x=%d, y=%d, w=%d, h=%d\n", x_arg, y_arg, w, h);*/ \
   if (T_LIT) \
   { \
      u = u_arg; \
      v = v_arg; \
      /* if(FlipX || FlipY || (u & 1) || (v & 1) || ((TexMode_TA == 0) && ((u & 3) || (v & 3)))) */ \
      /*    fprintf(stderr, "Flippy: %d %d 0x%02x 0x%02x\n", FX_LIT, FY_LIT, u, v); */ \
      if (FX_LIT) \
      { \
         u_inc = -1; \
         u |= 1; \
      } \
      /* FIXME: Something weird happens when lower bit of u is set and we're not doing horizontal flip, but I'm not sure what it is exactly(needs testing) */ \
      /* It may only happen to the first pixel, so look for that case too during testing. */ \
      /* else u = (u + 1) & ~1; */ \
      if (FY_LIT) \
         v_inc = -1; \
   } \
   if (x_start < gpu->ClipX0) \
   { \
      if (T_LIT) \
         u += (gpu->ClipX0 - x_start) * u_inc; \
      x_start = gpu->ClipX0; \
   } \
   if (y_start < gpu->ClipY0) \
   { \
      if (T_LIT) \
         v += (gpu->ClipY0 - y_start) * v_inc; \
      y_start = gpu->ClipY0; \
   } \
   if (x_bound > (gpu->ClipX1 + 1)) \
      x_bound = gpu->ClipX1 + 1; \
   if (y_bound > (gpu->ClipY1 + 1)) \
      y_bound = gpu->ClipY1 + 1; \
   /* HeightMode && !dfe && ((y & 1) == ((DisplayFB_YStart + !field_atvs) & 1)) && !DisplayOff */ \
   /* printf("%d:%d, %d, %d ---- heightmode=%d displayfb_ystart=%d field_atvs=%d displayoff=%d\n", w, h, scanline, dfe, HeightMode, DisplayFB_YStart, field_atvs, DisplayOff); */ \
   for (y = y_start; MDFN_LIKELY(y < y_bound); y++) \
   { \
      uint8_t u_r = 0; \
      int32_t x; \
      if (T_LIT) \
         u_r = u; \
      if (!LineSkipTest(gpu, y)) \
      { \
         if (y_bound > y_start && x_bound > x_start \
               && !DfeWouldSkip(gpu, y)) \
         { \
            /* Note(TODO): From tests on a PS1, even a 0-width sprite */ \
            /* takes up time to "draw" proportional to its height. */ \
            int32_t suck_time = /* 8 + */ (x_bound - x_start); \
            if (((BM_VAL) >= 0) || (ME_LIT)) \
               suck_time += (((x_bound + 1) & ~1) - (x_start & ~1)) >> 1; \
            gpu->DrawTimeAvail -= suck_time; \
         } \
         x = x_start; \
         /* Native-res flat-fill fast path.  Textured sprites and any \
          * upscale stay scalar; the run is within [ClipX0,ClipX1] so \
          * it never crosses the x==1024 VRAM wrap. */ \
         if (!(T_LIT) && gpu->upscale_shift == 0) \
         { \
            int _vn = DrawSpriteFillVec(gpu, x_start, y, x_bound - x_start, \
                  fill_color, (BM_VAL), (ME_LIT)); \
            x = x_start + _vn; \
         } \
         for (; MDFN_LIKELY(x < x_bound); x++) \
         { \
            if (T_LIT) \
            { \
               uint16_t fbw = GetTexel_TM##MO_LIT(gpu, u_r, v); \
               if (fbw) \
               { \
                  if (TM_LIT) \
                  { \
                     uint8_t *dither_offset = gpu->DitherLUT[2][3]; \
                     fbw = ModTexel(dither_offset, fbw, r, g, b); \
                  } \
                  PlotNativePixel_##BM_TAG##_ME##ME_LIT##_T1(gpu, x, y, fbw); \
               } \
            } \
            else \
               PlotNativePixel_##BM_TAG##_ME##ME_LIT##_T0(gpu, x, y, fill_color); \
            if (T_LIT) \
               u_r += u_inc; \
         } \
      } \
      if (T_LIT) \
         v += v_inc; \
   } \
}

/* DRAWSPRITE_T0_BMGROUP and DRAWSPRITE_T1_BMGROUP emit the 10
 * (BlendMode * MaskEval) specs for the given outer parameters,
 * keeping the explicit-instantiation list short.  Together they
 * produce all 250 unique-by-parameter DrawSprite specialisations
 * the dispatch table may reach.  For T0 each of the 4 FX/FY
 * combinations references the same body (FX/FY are only consulted
 * inside `if (T_LIT)` which is dead code), but each named
 * specialisation must exist so the SpriteFlip switch in
 * Command_DrawSprite has a callable target. */

#define DRAWSPRITE_T0_BMGROUP(FX, FY) \
   DEFINE_DrawSprite(T0_BMopaque_TM0_MO0_ME0_FX##FX##_FY##FY, 0, -1, BMopaque, 0, 0, 0, FX, FY) \
   DEFINE_DrawSprite(T0_BMopaque_TM0_MO0_ME1_FX##FX##_FY##FY, 0, -1, BMopaque, 0, 0, 1, FX, FY) \
   DEFINE_DrawSprite(T0_BMavg_TM0_MO0_ME0_FX##FX##_FY##FY,    0,  0, BMavg,    0, 0, 0, FX, FY) \
   DEFINE_DrawSprite(T0_BMavg_TM0_MO0_ME1_FX##FX##_FY##FY,    0,  0, BMavg,    0, 0, 1, FX, FY) \
   DEFINE_DrawSprite(T0_BMadd_TM0_MO0_ME0_FX##FX##_FY##FY,    0,  1, BMadd,    0, 0, 0, FX, FY) \
   DEFINE_DrawSprite(T0_BMadd_TM0_MO0_ME1_FX##FX##_FY##FY,    0,  1, BMadd,    0, 0, 1, FX, FY) \
   DEFINE_DrawSprite(T0_BMsub_TM0_MO0_ME0_FX##FX##_FY##FY,    0,  2, BMsub,    0, 0, 0, FX, FY) \
   DEFINE_DrawSprite(T0_BMsub_TM0_MO0_ME1_FX##FX##_FY##FY,    0,  2, BMsub,    0, 0, 1, FX, FY) \
   DEFINE_DrawSprite(T0_BMaddq_TM0_MO0_ME0_FX##FX##_FY##FY,   0,  3, BMaddq,   0, 0, 0, FX, FY) \
   DEFINE_DrawSprite(T0_BMaddq_TM0_MO0_ME1_FX##FX##_FY##FY,   0,  3, BMaddq,   0, 0, 1, FX, FY)

#define DRAWSPRITE_T1_BMGROUP(TM, MO, FX, FY) \
   DEFINE_DrawSprite(T1_BMopaque_TM##TM##_MO##MO##_ME0_FX##FX##_FY##FY, 1, -1, BMopaque, TM, MO, 0, FX, FY) \
   DEFINE_DrawSprite(T1_BMopaque_TM##TM##_MO##MO##_ME1_FX##FX##_FY##FY, 1, -1, BMopaque, TM, MO, 1, FX, FY) \
   DEFINE_DrawSprite(T1_BMavg_TM##TM##_MO##MO##_ME0_FX##FX##_FY##FY,    1,  0, BMavg,    TM, MO, 0, FX, FY) \
   DEFINE_DrawSprite(T1_BMavg_TM##TM##_MO##MO##_ME1_FX##FX##_FY##FY,    1,  0, BMavg,    TM, MO, 1, FX, FY) \
   DEFINE_DrawSprite(T1_BMadd_TM##TM##_MO##MO##_ME0_FX##FX##_FY##FY,    1,  1, BMadd,    TM, MO, 0, FX, FY) \
   DEFINE_DrawSprite(T1_BMadd_TM##TM##_MO##MO##_ME1_FX##FX##_FY##FY,    1,  1, BMadd,    TM, MO, 1, FX, FY) \
   DEFINE_DrawSprite(T1_BMsub_TM##TM##_MO##MO##_ME0_FX##FX##_FY##FY,    1,  2, BMsub,    TM, MO, 0, FX, FY) \
   DEFINE_DrawSprite(T1_BMsub_TM##TM##_MO##MO##_ME1_FX##FX##_FY##FY,    1,  2, BMsub,    TM, MO, 1, FX, FY) \
   DEFINE_DrawSprite(T1_BMaddq_TM##TM##_MO##MO##_ME0_FX##FX##_FY##FY,   1,  3, BMaddq,   TM, MO, 0, FX, FY) \
   DEFINE_DrawSprite(T1_BMaddq_TM##TM##_MO##MO##_ME1_FX##FX##_FY##FY,   1,  3, BMaddq,   TM, MO, 1, FX, FY)

DRAWSPRITE_T0_BMGROUP(0, 0)
DRAWSPRITE_T0_BMGROUP(0, 1)
DRAWSPRITE_T0_BMGROUP(1, 0)
DRAWSPRITE_T0_BMGROUP(1, 1)

DRAWSPRITE_T1_BMGROUP(0, 0, 0, 0)
DRAWSPRITE_T1_BMGROUP(0, 0, 0, 1)
DRAWSPRITE_T1_BMGROUP(0, 0, 1, 0)
DRAWSPRITE_T1_BMGROUP(0, 0, 1, 1)
DRAWSPRITE_T1_BMGROUP(0, 1, 0, 0)
DRAWSPRITE_T1_BMGROUP(0, 1, 0, 1)
DRAWSPRITE_T1_BMGROUP(0, 1, 1, 0)
DRAWSPRITE_T1_BMGROUP(0, 1, 1, 1)
DRAWSPRITE_T1_BMGROUP(0, 2, 0, 0)
DRAWSPRITE_T1_BMGROUP(0, 2, 0, 1)
DRAWSPRITE_T1_BMGROUP(0, 2, 1, 0)
DRAWSPRITE_T1_BMGROUP(0, 2, 1, 1)
DRAWSPRITE_T1_BMGROUP(1, 0, 0, 0)
DRAWSPRITE_T1_BMGROUP(1, 0, 0, 1)
DRAWSPRITE_T1_BMGROUP(1, 0, 1, 0)
DRAWSPRITE_T1_BMGROUP(1, 0, 1, 1)
DRAWSPRITE_T1_BMGROUP(1, 1, 0, 0)
DRAWSPRITE_T1_BMGROUP(1, 1, 0, 1)
DRAWSPRITE_T1_BMGROUP(1, 1, 1, 0)
DRAWSPRITE_T1_BMGROUP(1, 1, 1, 1)
DRAWSPRITE_T1_BMGROUP(1, 2, 0, 0)
DRAWSPRITE_T1_BMGROUP(1, 2, 0, 1)
DRAWSPRITE_T1_BMGROUP(1, 2, 1, 0)
DRAWSPRITE_T1_BMGROUP(1, 2, 1, 1)

/*
 * Command_DrawSprite - top-level GP0 sprite command handler.
 *
 * Parses the GP0 command buffer for position, dimensions, colour,
 * and texture coords, then dispatches to the appropriate DrawSprite
 * specialisation based on the runtime SpriteFlip register
 * (GP0(0xE1) bits 12-13).
 *
 * Macro parameters:
 *   SUFFIX        - mangled-name suffix (see naming scheme below)
 *   S_LIT         - sprite size class (see SPR_HELPER family in
 *                   gpu_common.h):
 *                     0 = variable, w/h come from extra GP0 word
 *                     1 = 1x1
 *                     2 = 8x8
 *                     3 = 16x16
 *   T_LIT         - 1 if sprite samples from texture page
 *   BM_VAL        - integer literal blend mode (-1 / 0..3)
 *   BM_TAG        - matching blend tag (BMopaque / BMavg / ...)
 *   TM_LIT        - texel-colour modulation flag (forced 0 when
 *                   not textured)
 *   MO_LIT        - 4 / 8 / 15bpp texture format (0 / 1 / 2)
 *   ME_LIT        - mask-bit gate
 *
 * The runtime switch on `gpu->SpriteFlip & 0x3000` selects one
 * of four DrawSprite specialisations (FlipX x FlipY). The
 * surrounding `if (!TM_LIT || color == 0x808080)` handles the
 * "neutral colour" optimisation: when the requested vertex
 * colour is exactly mid-grey, the texel-colour modulation
 * collapses to identity, so we route to the cheaper
 * TexMult=0 specialisation. Compiles to a single arm when
 * TM_LIT is 0 (always picks TM=0 path).
 *
 * Reached from the GP0 dispatch via Commands[0x60..0x7F].
 */

/* The optional rhi_intf_push_quad() backend hook is factored out
 * since it conditionally compiles based on backend availability.
 *
 * The hook does the (clut_x, clut_y) bit-extraction itself rather
 * than taking them as args; this keeps the variables out of the
 * caller entirely so the empty-hook build (no GL/GLES/Vulkan) does
 * not declare two locals just to drop them. */
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
#define GPU_SPR_RHI_PUSH_HOOK(gpu, x, y, w, h, u, v, color, clut, T_LIT, TM_LIT, MO_LIT, BM_VAL, ME_LIT) \
   do { \
      enum blending_modes blend_mode = BLEND_MODE_AVERAGE; \
      if (T_LIT) \
      { \
         if (TM_LIT) \
            blend_mode = BLEND_MODE_SUBTRACT; \
         else \
            blend_mode = BLEND_MODE_ADD; \
      } \
      if (rhi_intf_is_type() == RHI_OPENGL || rhi_intf_is_type() == RHI_VULKAN) \
      { \
         rhi_intf_push_quad(  (x),                    /* p0x */ \
                              (y),                    /* p0y */ \
                              1, \
                              (x) + (w),              /* p1x */ \
                              (y),                    /* p1y */ \
                              1, \
                              (x),                    /* p2x */ \
                              (y) + (h),              /* p2y */ \
                              1, \
                              (x) + (w),              /* p3x */ \
                              (y) + (h),              /* p3y */ \
                              1, \
                              (color), (color), (color), (color), \
                              (u),         (v), \
                              (u) + (w),   (v), \
                              (u),         (v) + (h), \
                              (u) + (w),   (v) + (h), \
                              (u), (v), \
                              (u) + (w) - 1, /* clamp UVs 1 pixel from edge (sampling should not quite reach it) */ \
                              (v) + (h) - 1, \
                              (gpu)->TexPageX, \
                              (gpu)->TexPageY, \
                              (uint16_t)((clut) & (0x3f << 4)), \
                              (uint16_t)(((clut) >> 10) & 0x1ff), \
                              blend_mode, \
                              2 - (MO_LIT), \
                              DitherEnabled(gpu), \
                              (BM_VAL), \
                              (ME_LIT), \
                              (gpu)->MaskSetOR != 0, \
                              true, true); \
      } \
   } while (0)
#else
#define GPU_SPR_RHI_PUSH_HOOK(gpu, x, y, w, h, u, v, color, clut, T_LIT, TM_LIT, MO_LIT, BM_VAL, ME_LIT) ((void)0)
#endif

/* Inner switch helper macro: emit the 4-way SpriteFlip switch with
 * the right per-arm DrawSprite specialisation calls.
 *
 * SPR_DISPATCH_TM0 - used when TM_LIT is 0; the original
 *    `if (!TexMult || color == 0x808080)` is always-true so we only
 *    emit the TM0 branch.  Since textured=0 implies TM=0 too, this
 *    macro covers both T0 and T1+TM0 cases.
 * SPR_DISPATCH_TM1 - used when TM_LIT is 1; both TM0 (mid-grey
 *    shortcut) and TM1 branches are reachable, so emit both.
 */
#define SPR_DISPATCH_TM0(BM_TAG, MO_LIT, ME_LIT, T_LIT) \
   switch (gpu->SpriteFlip & 0x3000) \
   { \
      case 0x0000: DrawSprite_T##T_LIT##_##BM_TAG##_TM0_MO##MO_LIT##_ME##ME_LIT##_FX0_FY0(gpu, x, y, w, h, u, v, color, clut); break; \
      case 0x1000: DrawSprite_T##T_LIT##_##BM_TAG##_TM0_MO##MO_LIT##_ME##ME_LIT##_FX1_FY0(gpu, x, y, w, h, u, v, color, clut); break; \
      case 0x2000: DrawSprite_T##T_LIT##_##BM_TAG##_TM0_MO##MO_LIT##_ME##ME_LIT##_FX0_FY1(gpu, x, y, w, h, u, v, color, clut); break; \
      case 0x3000: DrawSprite_T##T_LIT##_##BM_TAG##_TM0_MO##MO_LIT##_ME##ME_LIT##_FX1_FY1(gpu, x, y, w, h, u, v, color, clut); break; \
   }

#define SPR_DISPATCH_TM1(BM_TAG, MO_LIT, ME_LIT) \
   switch (gpu->SpriteFlip & 0x3000) \
   { \
      case 0x0000: \
         if (color == 0x808080) DrawSprite_T1_##BM_TAG##_TM0_MO##MO_LIT##_ME##ME_LIT##_FX0_FY0(gpu, x, y, w, h, u, v, color, clut); \
         else                   DrawSprite_T1_##BM_TAG##_TM1_MO##MO_LIT##_ME##ME_LIT##_FX0_FY0(gpu, x, y, w, h, u, v, color, clut); \
         break; \
      case 0x1000: \
         if (color == 0x808080) DrawSprite_T1_##BM_TAG##_TM0_MO##MO_LIT##_ME##ME_LIT##_FX1_FY0(gpu, x, y, w, h, u, v, color, clut); \
         else                   DrawSprite_T1_##BM_TAG##_TM1_MO##MO_LIT##_ME##ME_LIT##_FX1_FY0(gpu, x, y, w, h, u, v, color, clut); \
         break; \
      case 0x2000: \
         if (color == 0x808080) DrawSprite_T1_##BM_TAG##_TM0_MO##MO_LIT##_ME##ME_LIT##_FX0_FY1(gpu, x, y, w, h, u, v, color, clut); \
         else                   DrawSprite_T1_##BM_TAG##_TM1_MO##MO_LIT##_ME##ME_LIT##_FX0_FY1(gpu, x, y, w, h, u, v, color, clut); \
         break; \
      case 0x3000: \
         if (color == 0x808080) DrawSprite_T1_##BM_TAG##_TM0_MO##MO_LIT##_ME##ME_LIT##_FX1_FY1(gpu, x, y, w, h, u, v, color, clut); \
         else                   DrawSprite_T1_##BM_TAG##_TM1_MO##MO_LIT##_ME##ME_LIT##_FX1_FY1(gpu, x, y, w, h, u, v, color, clut); \
         break; \
   }

/* Pick the right SPR_DISPATCH_TM<n> based on TM_LIT.  C preprocessor
 * has no `if`, so use indirection through a pasted helper name. */
#define SPR_DISPATCH_DRAW_(TM_LIT, BM_TAG, MO_LIT, ME_LIT, T_LIT) SPR_DISPATCH_DRAW_TM##TM_LIT(BM_TAG, MO_LIT, ME_LIT, T_LIT)
#define SPR_DISPATCH_DRAW(TM_LIT, BM_TAG, MO_LIT, ME_LIT, T_LIT)  SPR_DISPATCH_DRAW_(TM_LIT, BM_TAG, MO_LIT, ME_LIT, T_LIT)
#define SPR_DISPATCH_DRAW_TM0(BM_TAG, MO_LIT, ME_LIT, T_LIT)      SPR_DISPATCH_TM0(BM_TAG, MO_LIT, ME_LIT, T_LIT)
#define SPR_DISPATCH_DRAW_TM1(BM_TAG, MO_LIT, ME_LIT, T_LIT)      SPR_DISPATCH_TM1(BM_TAG, MO_LIT, ME_LIT) /* T_LIT must be 1 */

#define DEFINE_Command_DrawSprite(SUFFIX, S_LIT, T_LIT, BM_VAL, BM_TAG, TM_LIT, MO_LIT, ME_LIT) \
static void Command_DrawSprite_##SUFFIX(PS_GPU *gpu, const uint32_t *cb) \
{ \
   int32_t x, y; \
   int32_t w, h; \
   uint8_t  u    = 0, v = 0; \
   uint32_t color = 0; \
   uint32_t clut  = 0; \
   gpu->DrawTimeAvail -= 16;                /* FIXME, correct time. */ \
   color = *cb & 0x00FFFFFF; \
   cb++; \
   x = sign_x_to_s32(11, (*cb & 0xFFFF)); \
   y = sign_x_to_s32(11, (*cb >> 16)); \
   cb++; \
   if (T_LIT) \
   { \
      u    = *cb & 0xFF; \
      v    = (*cb >> 8) & 0xFF; \
      clut = ((*cb >> 16) & 0xFFFF) << 4; \
      Update_CLUT_Cache_TM##MO_LIT(gpu, (*cb >> 16) & 0xFFFF); \
      cb++; \
   } \
   switch (S_LIT) \
   { \
      default: \
      case 0:  w = (*cb & 0x3FF); h = (*cb >> 16) & 0x1FF; cb++; break; \
      case 1:  w = 1;             h = 1;                          break; \
      case 2:  w = 8;             h = 8;                          break; \
      case 3:  w = 16;            h = 16;                         break; \
   } \
   x = sign_x_to_s32(11, x + gpu->OffsX); \
   y = sign_x_to_s32(11, y + gpu->OffsY); \
   GPU_SPR_RHI_PUSH_HOOK(gpu, x, y, w, h, u, v, color, clut, T_LIT, TM_LIT, MO_LIT, BM_VAL, ME_LIT); \
   if (!rhi_intf_has_software_renderer()) \
      return; \
   SPR_DISPATCH_DRAW(TM_LIT, BM_TAG, MO_LIT, ME_LIT, T_LIT) \
}

/* CMD_DRAWSPRITE_T0_BMGROUP and CMD_DRAWSPRITE_T1_BMGROUP emit the
 * 10 (BlendMode * MaskEval) specs for given outer parameters.
 * S is the numeric raw_size literal (0..3); the tag fragment in the
 * mangled name is S##S. */

#define CMD_DRAWSPRITE_T0_BMGROUP(RAW) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMopaque_TM0_MO0_ME0, RAW, 0, -1, BMopaque, 0, 0, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMopaque_TM0_MO0_ME1, RAW, 0, -1, BMopaque, 0, 0, 1) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMavg_TM0_MO0_ME0,    RAW, 0,  0, BMavg,    0, 0, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMavg_TM0_MO0_ME1,    RAW, 0,  0, BMavg,    0, 0, 1) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMadd_TM0_MO0_ME0,    RAW, 0,  1, BMadd,    0, 0, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMadd_TM0_MO0_ME1,    RAW, 0,  1, BMadd,    0, 0, 1) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMsub_TM0_MO0_ME0,    RAW, 0,  2, BMsub,    0, 0, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMsub_TM0_MO0_ME1,    RAW, 0,  2, BMsub,    0, 0, 1) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMaddq_TM0_MO0_ME0,   RAW, 0,  3, BMaddq,   0, 0, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T0_BMaddq_TM0_MO0_ME1,   RAW, 0,  3, BMaddq,   0, 0, 1)

#define CMD_DRAWSPRITE_T1_BMGROUP(RAW, TM, MO) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMopaque_TM##TM##_MO##MO##_ME0, RAW, 1, -1, BMopaque, TM, MO, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMopaque_TM##TM##_MO##MO##_ME1, RAW, 1, -1, BMopaque, TM, MO, 1) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMavg_TM##TM##_MO##MO##_ME0,    RAW, 1,  0, BMavg,    TM, MO, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMavg_TM##TM##_MO##MO##_ME1,    RAW, 1,  0, BMavg,    TM, MO, 1) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMadd_TM##TM##_MO##MO##_ME0,    RAW, 1,  1, BMadd,    TM, MO, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMadd_TM##TM##_MO##MO##_ME1,    RAW, 1,  1, BMadd,    TM, MO, 1) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMsub_TM##TM##_MO##MO##_ME0,    RAW, 1,  2, BMsub,    TM, MO, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMsub_TM##TM##_MO##MO##_ME1,    RAW, 1,  2, BMsub,    TM, MO, 1) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMaddq_TM##TM##_MO##MO##_ME0,   RAW, 1,  3, BMaddq,   TM, MO, 0) \
   DEFINE_Command_DrawSprite(S##RAW##_T1_BMaddq_TM##TM##_MO##MO##_ME1,   RAW, 1,  3, BMaddq,   TM, MO, 1)

/* Emit all 280 Command_DrawSprite specialisations: 4 raw_size *
 * (10 non-textured + 60 textured) = 4 * 70 = 280. */

CMD_DRAWSPRITE_T0_BMGROUP(0)
CMD_DRAWSPRITE_T1_BMGROUP(0, 0, 0)
CMD_DRAWSPRITE_T1_BMGROUP(0, 0, 1)
CMD_DRAWSPRITE_T1_BMGROUP(0, 0, 2)
CMD_DRAWSPRITE_T1_BMGROUP(0, 1, 0)
CMD_DRAWSPRITE_T1_BMGROUP(0, 1, 1)
CMD_DRAWSPRITE_T1_BMGROUP(0, 1, 2)

CMD_DRAWSPRITE_T0_BMGROUP(1)
CMD_DRAWSPRITE_T1_BMGROUP(1, 0, 0)
CMD_DRAWSPRITE_T1_BMGROUP(1, 0, 1)
CMD_DRAWSPRITE_T1_BMGROUP(1, 0, 2)
CMD_DRAWSPRITE_T1_BMGROUP(1, 1, 0)
CMD_DRAWSPRITE_T1_BMGROUP(1, 1, 1)
CMD_DRAWSPRITE_T1_BMGROUP(1, 1, 2)

CMD_DRAWSPRITE_T0_BMGROUP(2)
CMD_DRAWSPRITE_T1_BMGROUP(2, 0, 0)
CMD_DRAWSPRITE_T1_BMGROUP(2, 0, 1)
CMD_DRAWSPRITE_T1_BMGROUP(2, 0, 2)
CMD_DRAWSPRITE_T1_BMGROUP(2, 1, 0)
CMD_DRAWSPRITE_T1_BMGROUP(2, 1, 1)
CMD_DRAWSPRITE_T1_BMGROUP(2, 1, 2)

CMD_DRAWSPRITE_T0_BMGROUP(3)
CMD_DRAWSPRITE_T1_BMGROUP(3, 0, 0)
CMD_DRAWSPRITE_T1_BMGROUP(3, 0, 1)
CMD_DRAWSPRITE_T1_BMGROUP(3, 0, 2)
CMD_DRAWSPRITE_T1_BMGROUP(3, 1, 0)
CMD_DRAWSPRITE_T1_BMGROUP(3, 1, 1)
CMD_DRAWSPRITE_T1_BMGROUP(3, 1, 2)
