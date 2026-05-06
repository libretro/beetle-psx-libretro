extern enum dither_mode psx_gpu_dither_mode;

/* Return a pixel from VRAM */
#define vram_fetch(gpu, x, y)  ((gpu)->vram[((y) << (10 + (gpu)->upscale_shift)) | (x)])

/* Return a pixel from VRAM, ignoring the internal upscaling */
#define texel_fetch(gpu, x, y) vram_fetch((gpu), (x) << (gpu)->upscale_shift, (y) << (gpu)->upscale_shift)

/* Set a pixel in VRAM */
#define vram_put(gpu, x, y, v) (gpu)->vram[((y) << (10 + (gpu)->upscale_shift)) | (x)] = (v)

#define DitherEnabled(gpu)    (psx_gpu_dither_mode != DITHER_OFF && (gpu)->dtd)

#define UPSCALE(gpu)          (1U << (gpu)->upscale_shift)

/*
 * Apply one of the PS1 hardware semi-transparency blend modes to
 * `*fore_pix`, using `bg_pix` as the previously-stored framebuffer
 * value. `BlendMode` is one of the BLEND_MODE_* values from
 * rsx_intf.h (BLEND_MODE_AVERAGE / _ADD / _SUBTRACT / _ADD_FOURTH);
 * BLEND_MODE_OPAQUE is -1 and never reaches this function -
 * callers gate on `BlendMode >= 0` first.
 *
 *   AVERAGE     (mode 0):   out = 0.5 * bg + 0.5 * fore
 *   ADD         (mode 1):   out = 1.0 * bg + 1.0 * fore
 *   SUBTRACT    (mode 2):   out = 1.0 * bg - 1.0 * fore
 *   ADD_FOURTH  (mode 3):   out = 1.0 * bg + 0.25 * fore
 *
 * The bit-twiddling implementations are blargg's standard
 * 15bpp pixel-math sequences.  Each does its own clamping and
 * leaves the mask bit (0x8000) cleared/set per the PS1 spec.
 *
 * Naming: bg_pix is the back/destination pixel (already in VRAM)
 * and fore_pix is the new fragment being drawn. The variable
 * names in the body retain their historical meaning even though
 * "fore"/"bg" are inverted from the typical graphics convention.
 *
 * Specialised on BlendMode at every call site so the switch
 * collapses to a single arm at compile time.
 */
/*
 * Generator macro for one PlotPixelBlend specialisation.  See the
 * banner comment above for the per-mode pixel math.  The switch on
 * BLENDMODE_VAL is a C switch over a literal integer, so the C
 * compiler reduces it to the single matching arm.
 */
#define DEFINE_PlotPixelBlend(SUFFIX, BLENDMODE_VAL) \
static INLINE void PlotPixelBlend_##SUFFIX(uint16_t bg_pix, uint16_t *fore_pix) \
{ \
   switch (BLENDMODE_VAL) \
   { \
      case BLEND_MODE_AVERAGE: \
         bg_pix   |= 0x8000; \
         *fore_pix = ((*fore_pix + bg_pix) - ((*fore_pix ^ bg_pix) & 0x0421)) >> 1; \
         break; \
      case BLEND_MODE_ADD: \
         { \
            uint32_t sum, carry; \
            bg_pix   &= ~0x8000; \
            sum       = *fore_pix + bg_pix; \
            carry     = (sum - ((*fore_pix ^ bg_pix) & 0x8421)) & 0x8420; \
            *fore_pix = (sum - carry) | (carry - (carry >> 5)); \
         } \
         break; \
      case BLEND_MODE_SUBTRACT: \
         { \
            uint32_t diff; \
            uint32_t borrow; \
            bg_pix    |= 0x8000; \
            *fore_pix &= ~0x8000; \
            diff       = bg_pix - *fore_pix + 0x108420; \
            borrow     = (diff  - ((bg_pix ^ *fore_pix) & 0x108420)) & 0x108420; \
            *fore_pix  = (diff  - borrow) & (borrow - (borrow >> 5)); \
         } \
         break; \
      case BLEND_MODE_ADD_FOURTH: \
         { \
            uint32_t sum, carry; \
            bg_pix   &= ~0x8000; \
            *fore_pix = ((*fore_pix >> 2) & 0x1CE7) | 0x8000; \
            sum       = *fore_pix + bg_pix; \
            carry     = (sum - ((*fore_pix ^ bg_pix) & 0x8421)) & 0x8420; \
            *fore_pix = (sum - carry) | (carry - (carry >> 5)); \
         } \
         break; \
      case BLEND_MODE_OPAQUE: \
         break; \
   } \
}

DEFINE_PlotPixelBlend(BMavg,  BLEND_MODE_AVERAGE)
DEFINE_PlotPixelBlend(BMadd,  BLEND_MODE_ADD)
DEFINE_PlotPixelBlend(BMsub,  BLEND_MODE_SUBTRACT)
DEFINE_PlotPixelBlend(BMaddq, BLEND_MODE_ADD_FOURTH)
/* No PlotPixelBlend_BMopaque function - opaque path is gated out by the
 * caller (BlendMode >= 0 check) before any blend call is made.
 * Define a no-op macro that still touches its arguments so callers
 * compiling at -Wunused-variable don't see the bg_pix decl as unused. */
#define PlotPixelBlend_BMopaque(bg, fp) ((void)(bg), (void)(fp))

#ifdef __cplusplus
/* Thin C++ template wrapper that switch-dispatches to the right
 * specialisation by literal blend-mode tag.  When BlendMode is a
 * compile-time constant (which it always is at the call sites),
 * the optimiser collapses the switch to a single direct call.  The
 * wrapper exists so we can keep the rest of this header (still
 * templated) compiling unchanged during the staged conversion. */
template<int BlendMode>
static INLINE void PlotPixelBlend(uint16_t bg_pix, uint16_t *fore_pix)
{
   switch (BlendMode)
   {
      case BLEND_MODE_AVERAGE:    PlotPixelBlend_BMavg (bg_pix, fore_pix); break;
      case BLEND_MODE_ADD:        PlotPixelBlend_BMadd (bg_pix, fore_pix); break;
      case BLEND_MODE_SUBTRACT:   PlotPixelBlend_BMsub (bg_pix, fore_pix); break;
      case BLEND_MODE_ADD_FOURTH: PlotPixelBlend_BMaddq(bg_pix, fore_pix); break;
      case BLEND_MODE_OPAQUE:     break;
   }
}
#endif

/*
 * Plot a single pixel into VRAM at the current upscale, applying
 * blend, mask-evaluation, and mask-set semantics.
 *
 * Template parameters:
 *   BlendMode    - BLEND_MODE_OPAQUE (-1) skips the blend step;
 *                  any other BLEND_MODE_* value in [0..3] feeds
 *                  PlotPixelBlend (see above).
 *   MaskEval_TA  - When true, the destination pixel's mask bit
 *                  (0x8000) is consulted; if set, the write is
 *                  skipped. Models the PS1 "check mask before
 *                  draw" GP0(0xE6) bit. Per-call literal so the
 *                  fetch+test is elided when false.
 *   textured    -  When true, the incoming `fore_pix` carries a
 *                  texture-derived mask bit that should be
 *                  preserved. When false, this is a flat-shaded
 *                  fragment and the mask bit must be forced to
 *                  zero before write so MaskSetOR can set it
 *                  cleanly.
 *
 * MaskSetOR is the runtime "force mask bit on output" setting
 * from GP0(0xE6); it's OR'd into every write regardless of
 * MaskEval_TA.
 *
 * `vram_fetch` and `vram_put` are upscale-aware. For the
 * non-upscaled equivalent used by the line and sprite
 * rasterisers, see PlotNativePixel below.
 */
/*
 * Generator macro for one PlotPixel specialisation.  BLENDMODE_VAL
 * is a literal int constant from the BLEND_MODE_* enum, BLENDMODE_TAG
 * is the matching specialisation suffix used by PlotPixelBlend_<TAG>
 * (BMavg/BMadd/BMsub/BMaddq/BMopaque).  MASKEVAL and TEXTURED are
 * literal 0/1.  The if(0)/(1) blocks compile away.
 */
#define DEFINE_PlotPixel(SUFFIX, BLENDMODE_VAL, BLENDMODE_TAG, MASKEVAL, TEXTURED) \
static INLINE void PlotPixel_##SUFFIX(PS_GPU *gpu, int32_t x, int32_t y, uint16_t fore_pix) \
{ \
   y &= (512 << gpu->upscale_shift) - 1; \
   if ((BLENDMODE_VAL) >= 0 && (fore_pix & 0x8000)) \
   { \
      uint16_t bg_pix = vram_fetch(gpu, x, y); \
      PlotPixelBlend_##BLENDMODE_TAG(bg_pix, &fore_pix); \
   } \
   if (!(MASKEVAL) || !(vram_fetch(gpu, x, y) & 0x8000)) \
   { \
      if (TEXTURED) \
         vram_put(gpu, x, y, fore_pix | gpu->MaskSetOR); \
      else \
         vram_put(gpu, x, y, (fore_pix & 0x7FFF) | gpu->MaskSetOR); \
   } \
}

/* All combinations of (blend, maskeval, textured) the dispatch table
 * may produce.  Opaque + textured/non-textured + maskeval/no = 4 combos;
 * each blend mode * 2 maskeval * 2 textured = 16 more.  20 total. */
DEFINE_PlotPixel(BMopaque_ME0_T0, BLEND_MODE_OPAQUE,     BMopaque, 0, 0)
DEFINE_PlotPixel(BMopaque_ME0_T1, BLEND_MODE_OPAQUE,     BMopaque, 0, 1)
DEFINE_PlotPixel(BMopaque_ME1_T0, BLEND_MODE_OPAQUE,     BMopaque, 1, 0)
DEFINE_PlotPixel(BMopaque_ME1_T1, BLEND_MODE_OPAQUE,     BMopaque, 1, 1)
DEFINE_PlotPixel(BMavg_ME0_T0,    BLEND_MODE_AVERAGE,    BMavg,    0, 0)
DEFINE_PlotPixel(BMavg_ME0_T1,    BLEND_MODE_AVERAGE,    BMavg,    0, 1)
DEFINE_PlotPixel(BMavg_ME1_T0,    BLEND_MODE_AVERAGE,    BMavg,    1, 0)
DEFINE_PlotPixel(BMavg_ME1_T1,    BLEND_MODE_AVERAGE,    BMavg,    1, 1)
DEFINE_PlotPixel(BMadd_ME0_T0,    BLEND_MODE_ADD,        BMadd,    0, 0)
DEFINE_PlotPixel(BMadd_ME0_T1,    BLEND_MODE_ADD,        BMadd,    0, 1)
DEFINE_PlotPixel(BMadd_ME1_T0,    BLEND_MODE_ADD,        BMadd,    1, 0)
DEFINE_PlotPixel(BMadd_ME1_T1,    BLEND_MODE_ADD,        BMadd,    1, 1)
DEFINE_PlotPixel(BMsub_ME0_T0,    BLEND_MODE_SUBTRACT,   BMsub,    0, 0)
DEFINE_PlotPixel(BMsub_ME0_T1,    BLEND_MODE_SUBTRACT,   BMsub,    0, 1)
DEFINE_PlotPixel(BMsub_ME1_T0,    BLEND_MODE_SUBTRACT,   BMsub,    1, 0)
DEFINE_PlotPixel(BMsub_ME1_T1,    BLEND_MODE_SUBTRACT,   BMsub,    1, 1)
DEFINE_PlotPixel(BMaddq_ME0_T0,   BLEND_MODE_ADD_FOURTH, BMaddq,   0, 0)
DEFINE_PlotPixel(BMaddq_ME0_T1,   BLEND_MODE_ADD_FOURTH, BMaddq,   0, 1)
DEFINE_PlotPixel(BMaddq_ME1_T0,   BLEND_MODE_ADD_FOURTH, BMaddq,   1, 0)
DEFINE_PlotPixel(BMaddq_ME1_T1,   BLEND_MODE_ADD_FOURTH, BMaddq,   1, 1)

#ifdef __cplusplus
/* Thin C++ template wrapper - dispatches on literal template params. */
template<int BlendMode, bool MaskEval_TA, bool textured>
static INLINE void PlotPixel(PS_GPU *gpu, int32_t x, int32_t y, uint16_t fore_pix)
{
   if (textured)
   {
      if (MaskEval_TA)
      {
         switch (BlendMode)
         {
            case BLEND_MODE_OPAQUE:     PlotPixel_BMopaque_ME1_T1(gpu, x, y, fore_pix); break;
            case BLEND_MODE_AVERAGE:    PlotPixel_BMavg_ME1_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD:        PlotPixel_BMadd_ME1_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_SUBTRACT:   PlotPixel_BMsub_ME1_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD_FOURTH: PlotPixel_BMaddq_ME1_T1  (gpu, x, y, fore_pix); break;
         }
      }
      else
      {
         switch (BlendMode)
         {
            case BLEND_MODE_OPAQUE:     PlotPixel_BMopaque_ME0_T1(gpu, x, y, fore_pix); break;
            case BLEND_MODE_AVERAGE:    PlotPixel_BMavg_ME0_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD:        PlotPixel_BMadd_ME0_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_SUBTRACT:   PlotPixel_BMsub_ME0_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD_FOURTH: PlotPixel_BMaddq_ME0_T1  (gpu, x, y, fore_pix); break;
         }
      }
   }
   else
   {
      if (MaskEval_TA)
      {
         switch (BlendMode)
         {
            case BLEND_MODE_OPAQUE:     PlotPixel_BMopaque_ME1_T0(gpu, x, y, fore_pix); break;
            case BLEND_MODE_AVERAGE:    PlotPixel_BMavg_ME1_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD:        PlotPixel_BMadd_ME1_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_SUBTRACT:   PlotPixel_BMsub_ME1_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD_FOURTH: PlotPixel_BMaddq_ME1_T0  (gpu, x, y, fore_pix); break;
         }
      }
      else
      {
         switch (BlendMode)
         {
            case BLEND_MODE_OPAQUE:     PlotPixel_BMopaque_ME0_T0(gpu, x, y, fore_pix); break;
            case BLEND_MODE_AVERAGE:    PlotPixel_BMavg_ME0_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD:        PlotPixel_BMadd_ME0_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_SUBTRACT:   PlotPixel_BMsub_ME0_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD_FOURTH: PlotPixel_BMaddq_ME0_T0  (gpu, x, y, fore_pix); break;
         }
      }
   }
}
#endif

/*
 * Plot a single pixel without any internal upscaling (1x VRAM
 * coordinate space). Used by the line and sprite rasterisers
 * which compute coordinates at native resolution. Polygon
 * rasterisation uses PlotPixel above, which is upscale-aware.
 *
 * Template parameters mirror PlotPixel exactly:
 *   BlendMode    - see PlotPixelBlend for the BLEND_MODE_*
 *                  semantics; -1 skips blending.
 *   MaskEval_TA  - if true, gates the write on the destination
 *                  mask bit being clear.
 *   textured     - if true, preserve the source mask bit;
 *                  otherwise force it to zero so MaskSetOR
 *                  cleanly sets the final mask state.
 *
 * Uses texel_fetch / texel_put which round both dimensions down
 * to the nearest 1x texel of the upscaled buffer.
 */
/* Same shape as PlotPixel but uses texel_fetch/texel_put (1x VRAM
 * coords) - see banner comment.  Note: the original had an
 * unused 'output' decl which is dropped. */
#define DEFINE_PlotNativePixel(SUFFIX, BLENDMODE_VAL, BLENDMODE_TAG, MASKEVAL, TEXTURED) \
static INLINE void PlotNativePixel_##SUFFIX(PS_GPU *gpu, int32_t x, int32_t y, uint16_t fore_pix) \
{ \
   y &= 511; \
   if ((BLENDMODE_VAL) >= 0 && (fore_pix & 0x8000)) \
   { \
      uint16_t bg_pix = texel_fetch(gpu, x, y); \
      PlotPixelBlend_##BLENDMODE_TAG(bg_pix, &fore_pix); \
   } \
   if (!(MASKEVAL) || !(texel_fetch(gpu, x, y) & 0x8000)) \
      texel_put(x, y, ((TEXTURED) ? fore_pix : (fore_pix & 0x7FFF)) | gpu->MaskSetOR); \
}

DEFINE_PlotNativePixel(BMopaque_ME0_T0, BLEND_MODE_OPAQUE,     BMopaque, 0, 0)
DEFINE_PlotNativePixel(BMopaque_ME0_T1, BLEND_MODE_OPAQUE,     BMopaque, 0, 1)
DEFINE_PlotNativePixel(BMopaque_ME1_T0, BLEND_MODE_OPAQUE,     BMopaque, 1, 0)
DEFINE_PlotNativePixel(BMopaque_ME1_T1, BLEND_MODE_OPAQUE,     BMopaque, 1, 1)
DEFINE_PlotNativePixel(BMavg_ME0_T0,    BLEND_MODE_AVERAGE,    BMavg,    0, 0)
DEFINE_PlotNativePixel(BMavg_ME0_T1,    BLEND_MODE_AVERAGE,    BMavg,    0, 1)
DEFINE_PlotNativePixel(BMavg_ME1_T0,    BLEND_MODE_AVERAGE,    BMavg,    1, 0)
DEFINE_PlotNativePixel(BMavg_ME1_T1,    BLEND_MODE_AVERAGE,    BMavg,    1, 1)
DEFINE_PlotNativePixel(BMadd_ME0_T0,    BLEND_MODE_ADD,        BMadd,    0, 0)
DEFINE_PlotNativePixel(BMadd_ME0_T1,    BLEND_MODE_ADD,        BMadd,    0, 1)
DEFINE_PlotNativePixel(BMadd_ME1_T0,    BLEND_MODE_ADD,        BMadd,    1, 0)
DEFINE_PlotNativePixel(BMadd_ME1_T1,    BLEND_MODE_ADD,        BMadd,    1, 1)
DEFINE_PlotNativePixel(BMsub_ME0_T0,    BLEND_MODE_SUBTRACT,   BMsub,    0, 0)
DEFINE_PlotNativePixel(BMsub_ME0_T1,    BLEND_MODE_SUBTRACT,   BMsub,    0, 1)
DEFINE_PlotNativePixel(BMsub_ME1_T0,    BLEND_MODE_SUBTRACT,   BMsub,    1, 0)
DEFINE_PlotNativePixel(BMsub_ME1_T1,    BLEND_MODE_SUBTRACT,   BMsub,    1, 1)
DEFINE_PlotNativePixel(BMaddq_ME0_T0,   BLEND_MODE_ADD_FOURTH, BMaddq,   0, 0)
DEFINE_PlotNativePixel(BMaddq_ME0_T1,   BLEND_MODE_ADD_FOURTH, BMaddq,   0, 1)
DEFINE_PlotNativePixel(BMaddq_ME1_T0,   BLEND_MODE_ADD_FOURTH, BMaddq,   1, 0)
DEFINE_PlotNativePixel(BMaddq_ME1_T1,   BLEND_MODE_ADD_FOURTH, BMaddq,   1, 1)

#ifdef __cplusplus
template<int BlendMode, bool MaskEval_TA, bool textured>
static INLINE void PlotNativePixel(PS_GPU *gpu, int32_t x, int32_t y, uint16_t fore_pix)
{
   if (textured)
   {
      if (MaskEval_TA)
      {
         switch (BlendMode)
         {
            case BLEND_MODE_OPAQUE:     PlotNativePixel_BMopaque_ME1_T1(gpu, x, y, fore_pix); break;
            case BLEND_MODE_AVERAGE:    PlotNativePixel_BMavg_ME1_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD:        PlotNativePixel_BMadd_ME1_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_SUBTRACT:   PlotNativePixel_BMsub_ME1_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD_FOURTH: PlotNativePixel_BMaddq_ME1_T1  (gpu, x, y, fore_pix); break;
         }
      }
      else
      {
         switch (BlendMode)
         {
            case BLEND_MODE_OPAQUE:     PlotNativePixel_BMopaque_ME0_T1(gpu, x, y, fore_pix); break;
            case BLEND_MODE_AVERAGE:    PlotNativePixel_BMavg_ME0_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD:        PlotNativePixel_BMadd_ME0_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_SUBTRACT:   PlotNativePixel_BMsub_ME0_T1   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD_FOURTH: PlotNativePixel_BMaddq_ME0_T1  (gpu, x, y, fore_pix); break;
         }
      }
   }
   else
   {
      if (MaskEval_TA)
      {
         switch (BlendMode)
         {
            case BLEND_MODE_OPAQUE:     PlotNativePixel_BMopaque_ME1_T0(gpu, x, y, fore_pix); break;
            case BLEND_MODE_AVERAGE:    PlotNativePixel_BMavg_ME1_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD:        PlotNativePixel_BMadd_ME1_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_SUBTRACT:   PlotNativePixel_BMsub_ME1_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD_FOURTH: PlotNativePixel_BMaddq_ME1_T0  (gpu, x, y, fore_pix); break;
         }
      }
      else
      {
         switch (BlendMode)
         {
            case BLEND_MODE_OPAQUE:     PlotNativePixel_BMopaque_ME0_T0(gpu, x, y, fore_pix); break;
            case BLEND_MODE_AVERAGE:    PlotNativePixel_BMavg_ME0_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD:        PlotNativePixel_BMadd_ME0_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_SUBTRACT:   PlotNativePixel_BMsub_ME0_T0   (gpu, x, y, fore_pix); break;
            case BLEND_MODE_ADD_FOURTH: PlotNativePixel_BMaddq_ME0_T0  (gpu, x, y, fore_pix); break;
         }
      }
   }
}
#endif

#define ModTexel(dither_offset, texel, r, g, b) ((texel & 0x8000) | (dither_offset[(((texel & 0x1F)  * (r))   >> (5 - 1))] << 0) | (dither_offset[(((texel & 0x3E0)  * (g))  >> (10 - 1))] << 5) | (dither_offset[(((texel & 0x7C00) * (b)) >> (15 - 1))] << 10))

/*
 * Refresh the per-PS_GPU CLUT (palette) cache if the new texture
 * page descriptor has changed since last update.
 *
 * TexMode_TA selects the texture format used by the *upcoming*
 * draw, so the CLUT cache must be sized accordingly. Modes:
 *   0 = 4bpp indexed   - palette is 16 entries
 *   1 = 8bpp indexed   - palette is 256 entries
 *   2 = 15bpp direct   - no palette; this function is a no-op
 *
 * Specialising on TexMode_TA lets the compiler drop the runtime
 * mode==2 path and elide the palette-size branch when the caller
 * knows the format at compile time. The `if(TexMode_TA < 2)`
 * outer gate exists so 15bpp draws skip the cache check entirely.
 *
 * raw_clut is the GP0 CLUT word (a 15-bit VRAM Y address in the
 * upper bits, X position in the low 6); the high bit is masked
 * because the GPU silicon ignores it (verified on SCPH-5501).
 */
/* Generator macro for one Update_CLUT_Cache specialisation.  Three
 * texture-mode flavours: 0 (4bpp), 1 (8bpp), 2 (15bpp; no-op).  The
 * if (TM_VAL < 2) test is a literal-compare, so the entire body
 * vanishes for TM2. */
#define DEFINE_Update_CLUT_Cache(SUFFIX, TM_VAL) \
static INLINE void Update_CLUT_Cache_##SUFFIX(PS_GPU *g, uint16 raw_clut) \
{ \
   if ((TM_VAL) < 2) \
   { \
      const uint32 new_ccvb = ((raw_clut & 0x7FFF) | ((TM_VAL) << 16)); \
      if (g->CLUT_Cache_VB != new_ccvb) \
      { \
         uint16_t y = (raw_clut >> 6) & 0x1FF; \
         const uint32 cxo = (raw_clut & 0x3F) << 4; \
         const uint32 count = ((TM_VAL) ? 256 : 16); \
         unsigned i; \
         g->DrawTimeAvail -= count; \
         for (i = 0; i < count; i++) \
         { \
            uint16_t x = (cxo + i) & 0x3FF; \
            g->CLUT_Cache[i] = texel_fetch(g, x, y); \
         } \
         g->CLUT_Cache_VB = new_ccvb; \
      } \
   } \
}

DEFINE_Update_CLUT_Cache(TM0, 0)
DEFINE_Update_CLUT_Cache(TM1, 1)
DEFINE_Update_CLUT_Cache(TM2, 2)

#ifdef __cplusplus
template<uint32 TexMode_TA>
static INLINE void Update_CLUT_Cache(PS_GPU *g, uint16 raw_clut)
{
   switch (TexMode_TA)
   {
      case 0: Update_CLUT_Cache_TM0(g, raw_clut); break;
      case 1: Update_CLUT_Cache_TM1(g, raw_clut); break;
      case 2: Update_CLUT_Cache_TM2(g, raw_clut); break;
   }
}
#endif

static INLINE void RecalcTexWindowStuff(PS_GPU *g)
{
   uint8_t tww = g->tww;
   uint8_t twh = g->twh;
   uint8_t twx = g->twx;
   uint8_t twy = g->twy;

   g->SUCV.TWX_AND = ~(tww << 3);
   g->SUCV.TWX_ADD = ((twx & tww) << 3) + (g->TexPageX << (2 - MIN(2u, g->TexMode)));

   g->SUCV.TWY_AND = ~(twh << 3);
   g->SUCV.TWY_ADD = ((twy & twh) << 3) + g->TexPageY;
}

/*
 * Sample one texel from the currently-bound texture page,
 * applying the texture-window mask/offset and the format-specific
 * CLUT lookup.
 *
 * TexMode_TA selects the texel format (matching Update_CLUT_Cache
 * above):
 *   0 = 4bpp  - low 4 bits of the looked-up halfword index the
 *               16-entry CLUT cache
 *   1 = 8bpp  - low 8 bits index the 256-entry CLUT cache
 *   2 = 15bpp - the halfword IS the texel; CLUT bypassed
 *
 * Returns 0x0000 for transparent texels (PS1 hardware treats an
 * all-zero texel as transparent regardless of mask bit; callers
 * must check the return value before plotting).
 *
 * The TexCache is a small (256-entry) read-around cache of
 * recently-fetched VRAM blocks; modes 0 and 1 hit it for
 * spatial-locality wins, mode 2 reads VRAM directly per texel.
 *
 * The static_assert guards against accidental TexMode_TA values
 * outside the hardware-defined range; only the C++ build sees it
 * (HAS_CXX11), the C build will rely on the dispatch table to
 * never produce an out-of-range value.
 */
/* Generator macro for one GetTexel specialisation.  TM_VAL is the
 * literal texture-mode (0/1/2).  The switch on TM_VAL collapses to
 * the matching arm; the if (TM_VAL != 2) and if (TM_VAL == 0) tests
 * are literal so dead arms vanish. */
#define DEFINE_GetTexel(SUFFIX, TM_VAL) \
static INLINE uint16_t GetTexel_##SUFFIX(PS_GPU *g, int32_t u_arg, int32_t v_arg) \
{ \
   uint32_t u_ext   = ((u_arg & g->SUCV.TWX_AND) + g->SUCV.TWX_ADD); \
   uint32_t fbtex_x = ((u_ext >> (2 - (TM_VAL)))) & 1023; \
   uint32_t fbtex_y = (v_arg & g->SUCV.TWY_AND) + g->SUCV.TWY_ADD; \
   uint32_t gro     = fbtex_y * 1024U + fbtex_x; \
   PS_GPU_TexCache_t *TexCache = &g->TexCache[0]; \
   PS_GPU_TexCache_t *c = NULL; \
   uint16 fbw; \
   switch (TM_VAL) \
   { \
      case 0: c = &TexCache[((gro >> 2) & 0x3) | ((gro >> 8) & 0xFC)]; break; \
      case 1: c = &TexCache[((gro >> 2) & 0x7) | ((gro >> 7) & 0xF8)]; break; \
      case 2: c = &TexCache[((gro >> 2) & 0x7) | ((gro >> 7) & 0xF8)]; break; \
   } \
   if (MDFN_UNLIKELY(c->Tag != (gro &~ 0x3))) \
   { \
      uint32_t cache_x = fbtex_x & ~3; \
      g->DrawTimeAvail -= 4; \
      c->Data[0] = texel_fetch(g, cache_x + 0, fbtex_y); \
      c->Data[1] = texel_fetch(g, cache_x + 1, fbtex_y); \
      c->Data[2] = texel_fetch(g, cache_x + 2, fbtex_y); \
      c->Data[3] = texel_fetch(g, cache_x + 3, fbtex_y); \
      c->Tag = (gro &~ 0x3); \
   } \
   fbw = c->Data[gro & 0x3]; \
   if ((TM_VAL) != 2) \
   { \
      if ((TM_VAL) == 0) \
         fbw = (fbw >> ((u_ext & 3) * 4)) & 0xF; \
      else \
         fbw = (fbw >> ((u_ext & 1) * 8)) & 0xFF; \
      fbw = g->CLUT_Cache[fbw]; \
   } \
   return fbw; \
}

#ifdef __cplusplus
typedef PS_GPU::TexCache_t PS_GPU_TexCache_t;
#else
typedef struct PS_GPU_TexCache_t {
   uint16 Data[4];
   uint32 Tag;
} PS_GPU_TexCache_t;
#endif

DEFINE_GetTexel(TM0, 0)
DEFINE_GetTexel(TM1, 1)
DEFINE_GetTexel(TM2, 2)

#ifdef __cplusplus
template<uint32_t TexMode_TA>
static INLINE uint16_t GetTexel(PS_GPU *g, int32_t u_arg, int32_t v_arg)
{
#ifdef HAS_CXX11
   static_assert(TexMode_TA <= 2, "TexMode_TA must be <= 2");
#endif
   switch (TexMode_TA)
   {
      case 0:  return GetTexel_TM0(g, u_arg, v_arg);
      case 1:  return GetTexel_TM1(g, u_arg, v_arg);
      default: return GetTexel_TM2(g, u_arg, v_arg);
   }
}
#endif

static INLINE bool LineSkipTest(PS_GPU* g, unsigned y)
{
   if((g->DisplayMode & 0x24) != 0x24)
      return false;

   if(!g->dfe && ((y & 1) == ((g->DisplayFB_YStart + g->field_ram_readout) & 1))/* && !DisplayOff*/) //&& (y >> 1) >= DisplayFB_YStart && (y >> 1) < (DisplayFB_YStart + (VertEnd - VertStart)))
      return true;

   return false;
}

/*
 * ===========================================================================
 *  Command table generation macros
 * ===========================================================================
 *
 * The Commands[256] dispatch table at the top of gpu.cpp is a
 * fully-instantiated lookup of every GP0 command opcode the GPU
 * may receive. Each table entry (CTEntry) has:
 *
 *   func[abr][slot] : 4*8 = 32 function pointers, one per dynamic
 *                     dispatch combination of:
 *                       abr  (0..3)  - semi-transparency mode
 *                       slot (0..7)  - low 2 bits = TexMode_TA
 *                                      (0..2; 3 reserved/duplicated)
 *                                    - bit 2     = MaskEval_TA
 *   len             : Number of FIFO words this command consumes
 *                     before drawing begins.
 *   fifo_fb_len     : Frame-buffer-related extra length, used by
 *                     framebuffer copy/write/read variants.
 *   ss_cmd          : true  for "simple" / state-setting commands
 *                     that don't touch VRAM (ClearCache, IRQ,
 *                     SetMask, etc); allows the dispatcher to
 *                     short-circuit drawtime accounting.
 *
 * Each row of the func[][] matrix is generated at compile time by
 * a HELPER macro family, one per primitive class (POLY, SPR,
 * LINE, OTHER). The macros take `cv` - the GP0 command opcode -
 * and decode the relevant bits to select the matching template
 * specialisation:
 *
 *   POLY  (0x20..0x3F):  bit 0   = raw (unblended)
 *                        bit 1   = blended (semi-transparent)
 *                        bit 2   = textured
 *                        bit 3   = quad (1 = 4 vertices, 0 = 3)
 *                        bit 4   = gouraud (1 = per-vertex colour)
 *
 *   LINE  (0x40..0x5F):  bit 0   = raw
 *                        bit 1   = blended
 *                        bit 3   = polyline (1 = N-vertex line strip)
 *                        bit 4   = gouraud
 *
 *   SPR   (0x60..0x7F):  bit 0   = raw
 *                        bit 1   = blended
 *                        bit 2   = textured
 *                        bits 3-4 = size class
 *                                   (00=variable, 01=1x1, 10=8x8, 11=16x16)
 *
 *   OTHER (0x00, 0x01, 0x02, 0x1F, 0x80-0xDF, 0xE1-0xE6, 0xFF):
 *                        Single function pointer, replicated
 *                        across all 32 dispatch slots since the
 *                        rasteriser parameters don't apply.
 *
 * The row dimension (`bm` / abr) and inner indexing
 * (TexMode_TA + MaskEval_TA) are common to all draw helpers and
 * are dispatched at the func[][] index level rather than baked
 * into each entry.
 *
 * BlendMode in the template arguments resolves to:
 *   - The runtime `bm` value (0..3) when `cv` indicates blended,
 *     i.e. (cv & 0x2) == 0x2.
 *   - BLEND_MODE_OPAQUE (-1) otherwise, telling the rasteriser
 *     to skip the blend pipeline entirely.
 *
 * TexMult collapses to 0 when not textured (since per-vertex
 * colour modulation is meaningless without a texture).
 *
 * The duplicated entries within POLY/SPR/LINE _FG (slots 2&3,
 * 6&7) cover the unused TexMode_TA == 3 dispatch index. The
 * hardware never produces it, but the table needs an entry to
 * keep the dispatch math branchless.
 *
 * Same-arity HELPER macros for non-textured draw classes
 * (LINE_HELPER_FG) leave all 8 slots routed through the same
 * non-texture-mode-dependent specialisation.
 */

/*
 * POLY_HELPER_SUB - emit one G_Command_DrawPolygon mangled-name
 * reference. Decodes `cv` as detailed above and combines with the
 * row-level `bm` / `tm` / `mam` to produce the full eight-parameter
 * specialisation entry. Also threads the PGXP runtime gate via
 * G_Command_DrawPolygon (gpu.cpp), which dispatches to
 * Command_DrawPolygon_..._PG0 / _PG1 based on PGXP_enabled().
 *
 * Per-cv macros decode polyline / quad / gouraud / textured /
 * blended / texmult bits at preprocessor time, in the same style
 * as SPR_HELPER and LINE_HELPER above.  bm-tag rewrite reuses the
 * LINE_BMTAG_BM_<n> chain.
 */

/*  cv         numvertices  gouraud  textured  blended  texmult  */
#define POLY_HELPER_NV_0x20  NV3
#define POLY_HELPER_G_0x20   G0
#define POLY_HELPER_T_0x20   T0
#define POLY_HELPER_TM_0x20  TM0
#define POLY_HELPER_BM_0x20(bm) BMopaque
#define POLY_HELPER_NV_0x21  NV3
#define POLY_HELPER_G_0x21   G0
#define POLY_HELPER_T_0x21   T0
#define POLY_HELPER_TM_0x21  TM0
#define POLY_HELPER_BM_0x21(bm) BMopaque
#define POLY_HELPER_NV_0x22  NV3
#define POLY_HELPER_G_0x22   G0
#define POLY_HELPER_T_0x22   T0
#define POLY_HELPER_TM_0x22  TM0
#define POLY_HELPER_BM_0x22(bm) BM_##bm
#define POLY_HELPER_NV_0x23  NV3
#define POLY_HELPER_G_0x23   G0
#define POLY_HELPER_T_0x23   T0
#define POLY_HELPER_TM_0x23  TM0
#define POLY_HELPER_BM_0x23(bm) BM_##bm
#define POLY_HELPER_NV_0x24  NV3
#define POLY_HELPER_G_0x24   G0
#define POLY_HELPER_T_0x24   T1
#define POLY_HELPER_TM_0x24  TM1
#define POLY_HELPER_BM_0x24(bm) BMopaque
#define POLY_HELPER_NV_0x25  NV3
#define POLY_HELPER_G_0x25   G0
#define POLY_HELPER_T_0x25   T1
#define POLY_HELPER_TM_0x25  TM0
#define POLY_HELPER_BM_0x25(bm) BMopaque
#define POLY_HELPER_NV_0x26  NV3
#define POLY_HELPER_G_0x26   G0
#define POLY_HELPER_T_0x26   T1
#define POLY_HELPER_TM_0x26  TM1
#define POLY_HELPER_BM_0x26(bm) BM_##bm
#define POLY_HELPER_NV_0x27  NV3
#define POLY_HELPER_G_0x27   G0
#define POLY_HELPER_T_0x27   T1
#define POLY_HELPER_TM_0x27  TM0
#define POLY_HELPER_BM_0x27(bm) BM_##bm
#define POLY_HELPER_NV_0x28  NV4
#define POLY_HELPER_G_0x28   G0
#define POLY_HELPER_T_0x28   T0
#define POLY_HELPER_TM_0x28  TM0
#define POLY_HELPER_BM_0x28(bm) BMopaque
#define POLY_HELPER_NV_0x29  NV4
#define POLY_HELPER_G_0x29   G0
#define POLY_HELPER_T_0x29   T0
#define POLY_HELPER_TM_0x29  TM0
#define POLY_HELPER_BM_0x29(bm) BMopaque
#define POLY_HELPER_NV_0x2a  NV4
#define POLY_HELPER_G_0x2a   G0
#define POLY_HELPER_T_0x2a   T0
#define POLY_HELPER_TM_0x2a  TM0
#define POLY_HELPER_BM_0x2a(bm) BM_##bm
#define POLY_HELPER_NV_0x2b  NV4
#define POLY_HELPER_G_0x2b   G0
#define POLY_HELPER_T_0x2b   T0
#define POLY_HELPER_TM_0x2b  TM0
#define POLY_HELPER_BM_0x2b(bm) BM_##bm
#define POLY_HELPER_NV_0x2c  NV4
#define POLY_HELPER_G_0x2c   G0
#define POLY_HELPER_T_0x2c   T1
#define POLY_HELPER_TM_0x2c  TM1
#define POLY_HELPER_BM_0x2c(bm) BMopaque
#define POLY_HELPER_NV_0x2d  NV4
#define POLY_HELPER_G_0x2d   G0
#define POLY_HELPER_T_0x2d   T1
#define POLY_HELPER_TM_0x2d  TM0
#define POLY_HELPER_BM_0x2d(bm) BMopaque
#define POLY_HELPER_NV_0x2e  NV4
#define POLY_HELPER_G_0x2e   G0
#define POLY_HELPER_T_0x2e   T1
#define POLY_HELPER_TM_0x2e  TM1
#define POLY_HELPER_BM_0x2e(bm) BM_##bm
#define POLY_HELPER_NV_0x2f  NV4
#define POLY_HELPER_G_0x2f   G0
#define POLY_HELPER_T_0x2f   T1
#define POLY_HELPER_TM_0x2f  TM0
#define POLY_HELPER_BM_0x2f(bm) BM_##bm
#define POLY_HELPER_NV_0x30  NV3
#define POLY_HELPER_G_0x30   G1
#define POLY_HELPER_T_0x30   T0
#define POLY_HELPER_TM_0x30  TM0
#define POLY_HELPER_BM_0x30(bm) BMopaque
#define POLY_HELPER_NV_0x31  NV3
#define POLY_HELPER_G_0x31   G1
#define POLY_HELPER_T_0x31   T0
#define POLY_HELPER_TM_0x31  TM0
#define POLY_HELPER_BM_0x31(bm) BMopaque
#define POLY_HELPER_NV_0x32  NV3
#define POLY_HELPER_G_0x32   G1
#define POLY_HELPER_T_0x32   T0
#define POLY_HELPER_TM_0x32  TM0
#define POLY_HELPER_BM_0x32(bm) BM_##bm
#define POLY_HELPER_NV_0x33  NV3
#define POLY_HELPER_G_0x33   G1
#define POLY_HELPER_T_0x33   T0
#define POLY_HELPER_TM_0x33  TM0
#define POLY_HELPER_BM_0x33(bm) BM_##bm
#define POLY_HELPER_NV_0x34  NV3
#define POLY_HELPER_G_0x34   G1
#define POLY_HELPER_T_0x34   T1
#define POLY_HELPER_TM_0x34  TM1
#define POLY_HELPER_BM_0x34(bm) BMopaque
#define POLY_HELPER_NV_0x35  NV3
#define POLY_HELPER_G_0x35   G1
#define POLY_HELPER_T_0x35   T1
#define POLY_HELPER_TM_0x35  TM0
#define POLY_HELPER_BM_0x35(bm) BMopaque
#define POLY_HELPER_NV_0x36  NV3
#define POLY_HELPER_G_0x36   G1
#define POLY_HELPER_T_0x36   T1
#define POLY_HELPER_TM_0x36  TM1
#define POLY_HELPER_BM_0x36(bm) BM_##bm
#define POLY_HELPER_NV_0x37  NV3
#define POLY_HELPER_G_0x37   G1
#define POLY_HELPER_T_0x37   T1
#define POLY_HELPER_TM_0x37  TM0
#define POLY_HELPER_BM_0x37(bm) BM_##bm
#define POLY_HELPER_NV_0x38  NV4
#define POLY_HELPER_G_0x38   G1
#define POLY_HELPER_T_0x38   T0
#define POLY_HELPER_TM_0x38  TM0
#define POLY_HELPER_BM_0x38(bm) BMopaque
#define POLY_HELPER_NV_0x39  NV4
#define POLY_HELPER_G_0x39   G1
#define POLY_HELPER_T_0x39   T0
#define POLY_HELPER_TM_0x39  TM0
#define POLY_HELPER_BM_0x39(bm) BMopaque
#define POLY_HELPER_NV_0x3a  NV4
#define POLY_HELPER_G_0x3a   G1
#define POLY_HELPER_T_0x3a   T0
#define POLY_HELPER_TM_0x3a  TM0
#define POLY_HELPER_BM_0x3a(bm) BM_##bm
#define POLY_HELPER_NV_0x3b  NV4
#define POLY_HELPER_G_0x3b   G1
#define POLY_HELPER_T_0x3b   T0
#define POLY_HELPER_TM_0x3b  TM0
#define POLY_HELPER_BM_0x3b(bm) BM_##bm
#define POLY_HELPER_NV_0x3c  NV4
#define POLY_HELPER_G_0x3c   G1
#define POLY_HELPER_T_0x3c   T1
#define POLY_HELPER_TM_0x3c  TM1
#define POLY_HELPER_BM_0x3c(bm) BMopaque
#define POLY_HELPER_NV_0x3d  NV4
#define POLY_HELPER_G_0x3d   G1
#define POLY_HELPER_T_0x3d   T1
#define POLY_HELPER_TM_0x3d  TM0
#define POLY_HELPER_BM_0x3d(bm) BMopaque
#define POLY_HELPER_NV_0x3e  NV4
#define POLY_HELPER_G_0x3e   G1
#define POLY_HELPER_T_0x3e   T1
#define POLY_HELPER_TM_0x3e  TM1
#define POLY_HELPER_BM_0x3e(bm) BM_##bm
#define POLY_HELPER_NV_0x3f  NV4
#define POLY_HELPER_G_0x3f   G1
#define POLY_HELPER_T_0x3f   T1
#define POLY_HELPER_TM_0x3f  TM0
#define POLY_HELPER_BM_0x3f(bm) BM_##bm

/* MO selector per cv: non-textured cv always wants MO=0 (since
 * G_Command_DrawPolygon is only defined with MO0 when T=0); textured
 * cv passes through the MO slot value. */
#define POLY_HELPER_MO_0x20(mo) 0
#define POLY_HELPER_MO_0x21(mo) 0
#define POLY_HELPER_MO_0x22(mo) 0
#define POLY_HELPER_MO_0x23(mo) 0
#define POLY_HELPER_MO_0x24(mo) mo
#define POLY_HELPER_MO_0x25(mo) mo
#define POLY_HELPER_MO_0x26(mo) mo
#define POLY_HELPER_MO_0x27(mo) mo
#define POLY_HELPER_MO_0x28(mo) 0
#define POLY_HELPER_MO_0x29(mo) 0
#define POLY_HELPER_MO_0x2a(mo) 0
#define POLY_HELPER_MO_0x2b(mo) 0
#define POLY_HELPER_MO_0x2c(mo) mo
#define POLY_HELPER_MO_0x2d(mo) mo
#define POLY_HELPER_MO_0x2e(mo) mo
#define POLY_HELPER_MO_0x2f(mo) mo
#define POLY_HELPER_MO_0x30(mo) 0
#define POLY_HELPER_MO_0x31(mo) 0
#define POLY_HELPER_MO_0x32(mo) 0
#define POLY_HELPER_MO_0x33(mo) 0
#define POLY_HELPER_MO_0x34(mo) mo
#define POLY_HELPER_MO_0x35(mo) mo
#define POLY_HELPER_MO_0x36(mo) mo
#define POLY_HELPER_MO_0x37(mo) mo
#define POLY_HELPER_MO_0x38(mo) 0
#define POLY_HELPER_MO_0x39(mo) 0
#define POLY_HELPER_MO_0x3a(mo) 0
#define POLY_HELPER_MO_0x3b(mo) 0
#define POLY_HELPER_MO_0x3c(mo) mo
#define POLY_HELPER_MO_0x3d(mo) mo
#define POLY_HELPER_MO_0x3e(mo) mo
#define POLY_HELPER_MO_0x3f(mo) mo

/* Three-level paste indirection so all bm/tm/mam fragments expand
 * fully before token-pasting into the function name. */
#define POLY_HELPER_NAME(nv, g, t, bmtag, tm, mo, mam)        POLY_HELPER_NAME_(nv, g, t, bmtag, tm, mo, mam)
#define POLY_HELPER_NAME_(nv, g, t, bmtag, tm, mo, mam)       POLY_HELPER_NAME__(nv, g, t, LINE_BMTAG_##bmtag, tm, mo, mam)
#define POLY_HELPER_NAME__(nv, g, t, finaltag, tm, mo, mam)   POLY_HELPER_NAME___(nv, g, t, finaltag, tm, mo, mam)
#define POLY_HELPER_NAME___(nv, g, t, finaltag, tm, mo, mam)  G_Command_DrawPolygon_##nv##_##g##_##t##_##finaltag##_##tm##_MO##mo##_ME##mam

#define POLY_HELPER_SUB(bm, cv, mo, mam) \
   POLY_HELPER_NAME(POLY_HELPER_NV_##cv, POLY_HELPER_G_##cv, POLY_HELPER_T_##cv, POLY_HELPER_BM_##cv(bm), POLY_HELPER_TM_##cv, POLY_HELPER_MO_##cv(mo), mam)

/*
 * POLY_HELPER_FG - emit one row (8 entries) of the func[][]
 * matrix for a polygon command. The eight slots cover (TexMode,
 * MaskEval) in (slot & 3, slot >> 2) order; for non-textured
 * primitives the TexMode dimension collapses to 0 via the
 * `((cv & 0x4) ? N : 0)` ternary.
 */
#define POLY_HELPER_FG(bm, cv) \
   { \
      POLY_HELPER_SUB(bm, cv, 0, 0), \
      POLY_HELPER_SUB(bm, cv, 1, 0), \
      POLY_HELPER_SUB(bm, cv, 2, 0), \
      POLY_HELPER_SUB(bm, cv, 2, 0), \
      POLY_HELPER_SUB(bm, cv, 0, 1), \
      POLY_HELPER_SUB(bm, cv, 1, 1), \
      POLY_HELPER_SUB(bm, cv, 2, 1), \
      POLY_HELPER_SUB(bm, cv, 2, 1)  \
   }

/*
 * POLY_HELPER - top-level entry for one polygon opcode. Emits a
 * full CTEntry: 4 BlendMode rows of 8 (TexMode, MaskEval) slots
 * each, plus the GP0 word counts.
 *
 *   len = 1 + 3 * (1 + textured + gouraud) - gouraud
 *       = base 1 word (command+colour) + 3 vertices, each
 *         carrying 1 word for x/y plus optional uv (textured)
 *         and optional rgb (gouraud); first vertex's rgb is
 *         already in word 0, hence the trailing `- gouraud`.
 *
 *   fifo_fb_len = 1 (the command word itself counts)
 *
 * Quads (cv & 0x8) are handled by the rasteriser as two tris;
 * the table doesn't need to multiply the count.
 */
#define POLY_HELPER(cv)														\
	{ 															\
	 { POLY_HELPER_FG(0, cv), POLY_HELPER_FG(1, cv), POLY_HELPER_FG(2, cv), POLY_HELPER_FG(3, cv) },			\
	 1 + (3 /*+ ((cv & 0x8) >> 3)*/) * ( 1 + ((cv & 0x4) >> 2) + ((cv & 0x10) >> 4) ) - ((cv & 0x10) >> 4),			\
	 1,															\
 	 false															\
	}

/*
 * SPR_HELPER_SUB / _FG / SPR_HELPER - sprite analogue of the
 * POLY_HELPER family. raw_size encoding (cv bits 3-4):
 *   00 = variable - dimensions in extra GP0 word
 *   01 = 1x1
 *   10 = 8x8
 *   11 = 16x16
 *
 * SPR_HELPER's `len` and `fifo_fb_len` calculations:
 *   len         = 2 + textured + variable_size
 *                 (1 word command, 1 vertex, optional uv/clut,
 *                  optional w/h)
 *   fifo_fb_len = bitmask of (texture, variable_size) since the
 *                 frame-buffer-cost word count differs from len
 *                 for textured non-variable sprites
 *                 (`|, not +, for this` annotation in original).
 *
 * The Command_DrawSprite mangled name is built from cv's bits at
 * preprocessor time:
 *
 *    SPR_HELPER_S_<cv>     yields S<n>           (raw_size 0..3)
 *    SPR_HELPER_T_<cv>     yields T<n>           (textured 0/1)
 *    SPR_HELPER_BM_<cv>(bm) yields BMopaque
 *                           or BM_##bm           (per blended bit)
 *    SPR_HELPER_TM_<cv>    yields TM<n>          (texmult, derived
 *                                                 from bits 0+2)
 *
 * The MaskEval (mam) and TexMode_TA (mo) dimensions are pasted
 * directly as literals at the FG level.  For non-textured cv the
 * MO dimension collapses (TexMode_TA fixed at 0); SPR_HELPER_FG
 * still emits 4 slots of the same name to fill the dispatch row.
 *
 * The bm-tag rewrite goes through the LINE_BMTAG_* family from the
 * line block above (BMavg/BMadd/BMsub/BMaddq tags - same layout). */

/*  cv         raw_size  textured  blended  texmult  */
#define SPR_HELPER_S_0x60   S0
#define SPR_HELPER_T_0x60   T0
#define SPR_HELPER_TM_0x60  TM0
#define SPR_HELPER_BM_0x60(bm) BMopaque
#define SPR_HELPER_S_0x61   S0
#define SPR_HELPER_T_0x61   T0
#define SPR_HELPER_TM_0x61  TM0
#define SPR_HELPER_BM_0x61(bm) BMopaque
#define SPR_HELPER_S_0x62   S0
#define SPR_HELPER_T_0x62   T0
#define SPR_HELPER_TM_0x62  TM0
#define SPR_HELPER_BM_0x62(bm) BM_##bm
#define SPR_HELPER_S_0x63   S0
#define SPR_HELPER_T_0x63   T0
#define SPR_HELPER_TM_0x63  TM0
#define SPR_HELPER_BM_0x63(bm) BM_##bm
#define SPR_HELPER_S_0x64   S0
#define SPR_HELPER_T_0x64   T1
#define SPR_HELPER_TM_0x64  TM1
#define SPR_HELPER_BM_0x64(bm) BMopaque
#define SPR_HELPER_S_0x65   S0
#define SPR_HELPER_T_0x65   T1
#define SPR_HELPER_TM_0x65  TM0
#define SPR_HELPER_BM_0x65(bm) BMopaque
#define SPR_HELPER_S_0x66   S0
#define SPR_HELPER_T_0x66   T1
#define SPR_HELPER_TM_0x66  TM1
#define SPR_HELPER_BM_0x66(bm) BM_##bm
#define SPR_HELPER_S_0x67   S0
#define SPR_HELPER_T_0x67   T1
#define SPR_HELPER_TM_0x67  TM0
#define SPR_HELPER_BM_0x67(bm) BM_##bm
#define SPR_HELPER_S_0x68   S1
#define SPR_HELPER_T_0x68   T0
#define SPR_HELPER_TM_0x68  TM0
#define SPR_HELPER_BM_0x68(bm) BMopaque
#define SPR_HELPER_S_0x69   S1
#define SPR_HELPER_T_0x69   T0
#define SPR_HELPER_TM_0x69  TM0
#define SPR_HELPER_BM_0x69(bm) BMopaque
#define SPR_HELPER_S_0x6a   S1
#define SPR_HELPER_T_0x6a   T0
#define SPR_HELPER_TM_0x6a  TM0
#define SPR_HELPER_BM_0x6a(bm) BM_##bm
#define SPR_HELPER_S_0x6b   S1
#define SPR_HELPER_T_0x6b   T0
#define SPR_HELPER_TM_0x6b  TM0
#define SPR_HELPER_BM_0x6b(bm) BM_##bm
#define SPR_HELPER_S_0x6c   S1
#define SPR_HELPER_T_0x6c   T1
#define SPR_HELPER_TM_0x6c  TM1
#define SPR_HELPER_BM_0x6c(bm) BMopaque
#define SPR_HELPER_S_0x6d   S1
#define SPR_HELPER_T_0x6d   T1
#define SPR_HELPER_TM_0x6d  TM0
#define SPR_HELPER_BM_0x6d(bm) BMopaque
#define SPR_HELPER_S_0x6e   S1
#define SPR_HELPER_T_0x6e   T1
#define SPR_HELPER_TM_0x6e  TM1
#define SPR_HELPER_BM_0x6e(bm) BM_##bm
#define SPR_HELPER_S_0x6f   S1
#define SPR_HELPER_T_0x6f   T1
#define SPR_HELPER_TM_0x6f  TM0
#define SPR_HELPER_BM_0x6f(bm) BM_##bm
#define SPR_HELPER_S_0x70   S2
#define SPR_HELPER_T_0x70   T0
#define SPR_HELPER_TM_0x70  TM0
#define SPR_HELPER_BM_0x70(bm) BMopaque
#define SPR_HELPER_S_0x71   S2
#define SPR_HELPER_T_0x71   T0
#define SPR_HELPER_TM_0x71  TM0
#define SPR_HELPER_BM_0x71(bm) BMopaque
#define SPR_HELPER_S_0x72   S2
#define SPR_HELPER_T_0x72   T0
#define SPR_HELPER_TM_0x72  TM0
#define SPR_HELPER_BM_0x72(bm) BM_##bm
#define SPR_HELPER_S_0x73   S2
#define SPR_HELPER_T_0x73   T0
#define SPR_HELPER_TM_0x73  TM0
#define SPR_HELPER_BM_0x73(bm) BM_##bm
#define SPR_HELPER_S_0x74   S2
#define SPR_HELPER_T_0x74   T1
#define SPR_HELPER_TM_0x74  TM1
#define SPR_HELPER_BM_0x74(bm) BMopaque
#define SPR_HELPER_S_0x75   S2
#define SPR_HELPER_T_0x75   T1
#define SPR_HELPER_TM_0x75  TM0
#define SPR_HELPER_BM_0x75(bm) BMopaque
#define SPR_HELPER_S_0x76   S2
#define SPR_HELPER_T_0x76   T1
#define SPR_HELPER_TM_0x76  TM1
#define SPR_HELPER_BM_0x76(bm) BM_##bm
#define SPR_HELPER_S_0x77   S2
#define SPR_HELPER_T_0x77   T1
#define SPR_HELPER_TM_0x77  TM0
#define SPR_HELPER_BM_0x77(bm) BM_##bm
#define SPR_HELPER_S_0x78   S3
#define SPR_HELPER_T_0x78   T0
#define SPR_HELPER_TM_0x78  TM0
#define SPR_HELPER_BM_0x78(bm) BMopaque
#define SPR_HELPER_S_0x79   S3
#define SPR_HELPER_T_0x79   T0
#define SPR_HELPER_TM_0x79  TM0
#define SPR_HELPER_BM_0x79(bm) BMopaque
#define SPR_HELPER_S_0x7a   S3
#define SPR_HELPER_T_0x7a   T0
#define SPR_HELPER_TM_0x7a  TM0
#define SPR_HELPER_BM_0x7a(bm) BM_##bm
#define SPR_HELPER_S_0x7b   S3
#define SPR_HELPER_T_0x7b   T0
#define SPR_HELPER_TM_0x7b  TM0
#define SPR_HELPER_BM_0x7b(bm) BM_##bm
#define SPR_HELPER_S_0x7c   S3
#define SPR_HELPER_T_0x7c   T1
#define SPR_HELPER_TM_0x7c  TM1
#define SPR_HELPER_BM_0x7c(bm) BMopaque
#define SPR_HELPER_S_0x7d   S3
#define SPR_HELPER_T_0x7d   T1
#define SPR_HELPER_TM_0x7d  TM0
#define SPR_HELPER_BM_0x7d(bm) BMopaque
#define SPR_HELPER_S_0x7e   S3
#define SPR_HELPER_T_0x7e   T1
#define SPR_HELPER_TM_0x7e  TM1
#define SPR_HELPER_BM_0x7e(bm) BM_##bm
#define SPR_HELPER_S_0x7f   S3
#define SPR_HELPER_T_0x7f   T1
#define SPR_HELPER_TM_0x7f  TM0
#define SPR_HELPER_BM_0x7f(bm) BM_##bm

/* MO selector per cv: non-textured cv always wants MO=0 (since
 * Command_DrawSprite is only defined with MO0 when T=0); textured
 * cv passes through the MO slot value. */
#define SPR_HELPER_MO_0x60(mo) 0
#define SPR_HELPER_MO_0x61(mo) 0
#define SPR_HELPER_MO_0x62(mo) 0
#define SPR_HELPER_MO_0x63(mo) 0
#define SPR_HELPER_MO_0x64(mo) mo
#define SPR_HELPER_MO_0x65(mo) mo
#define SPR_HELPER_MO_0x66(mo) mo
#define SPR_HELPER_MO_0x67(mo) mo
#define SPR_HELPER_MO_0x68(mo) 0
#define SPR_HELPER_MO_0x69(mo) 0
#define SPR_HELPER_MO_0x6a(mo) 0
#define SPR_HELPER_MO_0x6b(mo) 0
#define SPR_HELPER_MO_0x6c(mo) mo
#define SPR_HELPER_MO_0x6d(mo) mo
#define SPR_HELPER_MO_0x6e(mo) mo
#define SPR_HELPER_MO_0x6f(mo) mo
#define SPR_HELPER_MO_0x70(mo) 0
#define SPR_HELPER_MO_0x71(mo) 0
#define SPR_HELPER_MO_0x72(mo) 0
#define SPR_HELPER_MO_0x73(mo) 0
#define SPR_HELPER_MO_0x74(mo) mo
#define SPR_HELPER_MO_0x75(mo) mo
#define SPR_HELPER_MO_0x76(mo) mo
#define SPR_HELPER_MO_0x77(mo) mo
#define SPR_HELPER_MO_0x78(mo) 0
#define SPR_HELPER_MO_0x79(mo) 0
#define SPR_HELPER_MO_0x7a(mo) 0
#define SPR_HELPER_MO_0x7b(mo) 0
#define SPR_HELPER_MO_0x7c(mo) mo
#define SPR_HELPER_MO_0x7d(mo) mo
#define SPR_HELPER_MO_0x7e(mo) mo
#define SPR_HELPER_MO_0x7f(mo) mo

/* Three-level paste indirection so all bm/tm/mam fragments expand
 * fully before token-pasting into the function name. */
#define SPR_HELPER_NAME(s, t, bmtag, tm, mo, mam)        SPR_HELPER_NAME_(s, t, bmtag, tm, mo, mam)
#define SPR_HELPER_NAME_(s, t, bmtag, tm, mo, mam)       SPR_HELPER_NAME__(s, t, LINE_BMTAG_##bmtag, tm, mo, mam)
#define SPR_HELPER_NAME__(s, t, finaltag, tm, mo, mam)   SPR_HELPER_NAME___(s, t, finaltag, tm, mo, mam)
#define SPR_HELPER_NAME___(s, t, finaltag, tm, mo, mam)  Command_DrawSprite_##s##_##t##_##finaltag##_##tm##_MO##mo##_ME##mam

#define SPR_HELPER_SUB(bm, cv, mo, mam) \
   SPR_HELPER_NAME(SPR_HELPER_S_##cv, SPR_HELPER_T_##cv, SPR_HELPER_BM_##cv(bm), SPR_HELPER_TM_##cv, SPR_HELPER_MO_##cv(mo), mam)

#define SPR_HELPER_FG(bm, cv) \
   { \
      SPR_HELPER_SUB(bm, cv, 0, 0), \
      SPR_HELPER_SUB(bm, cv, 1, 0), \
      SPR_HELPER_SUB(bm, cv, 2, 0), \
      SPR_HELPER_SUB(bm, cv, 2, 0), \
      SPR_HELPER_SUB(bm, cv, 0, 1), \
      SPR_HELPER_SUB(bm, cv, 1, 1), \
      SPR_HELPER_SUB(bm, cv, 2, 1), \
      SPR_HELPER_SUB(bm, cv, 2, 1)  \
   }


#define SPR_HELPER(cv)												\
	{													\
	 { SPR_HELPER_FG(0, cv), SPR_HELPER_FG(1, cv), SPR_HELPER_FG(2, cv), SPR_HELPER_FG(3, cv) },		\
	 2 + ((cv & 0x4) >> 2) + ((cv & 0x18) ? 0 : 1),								\
	 2 | ((cv & 0x4) >> 2) | ((cv & 0x18) ? 0 : 1),		/* |, not +, for this */			\
	 false													\
	}

/*
 * LINE_HELPER_SUB / _FG / LINE_HELPER - line analogue. Lines are
 * never textured, so TexMode/TexMult are absent from the argument
 * list and all 8 dispatch slots within a row carry the same
 * MaskEval pair (0/0/0/0/1/1/1/1).
 *
 * LINE_HELPER's `len`:
 *   3 + polyline
 *      = command + start vertex + end vertex (2 words each
 *        when gouraud, 1 word otherwise; the rasteriser handles
 *        the gouraud read inline). Polyline mode adds 1 word for
 *        the terminator-checking phase the rasteriser performs.
 *
 *   fifo_fb_len = 1
 *
 * Each LINE_HELPER_PG_<cv> resolves the polyline / gouraud bits of
 * the opcode at preprocessor time, yielding the correct P<n>_G<n>
 * fragment for the Command_DrawLine_<...> mangled name in
 * gpu_line.cpp.  The blend-mode dimension is dispatched by selecting
 * between the BMopaque and BM<bm> tags via the LINE_BMTAG_<bm>_<cv>
 * macro family below.
 */

/*  cv         polyline  gouraud  blended  */
#define LINE_HELPER_PG_0x40 P0_G0  /*    0       0        0      */
#define LINE_HELPER_PG_0x41 P0_G0  /*    0       0        0      */
#define LINE_HELPER_PG_0x42 P0_G0  /*    0       0        1      */
#define LINE_HELPER_PG_0x43 P0_G0  /*    0       0        1      */
#define LINE_HELPER_PG_0x44 P0_G0  /*    0       0        0      */
#define LINE_HELPER_PG_0x45 P0_G0  /*    0       0        0      */
#define LINE_HELPER_PG_0x46 P0_G0  /*    0       0        1      */
#define LINE_HELPER_PG_0x47 P0_G0  /*    0       0        1      */
#define LINE_HELPER_PG_0x48 P1_G0  /*    1       0        0      */
#define LINE_HELPER_PG_0x49 P1_G0  /*    1       0        0      */
#define LINE_HELPER_PG_0x4a P1_G0  /*    1       0        1      */
#define LINE_HELPER_PG_0x4b P1_G0  /*    1       0        1      */
#define LINE_HELPER_PG_0x4c P1_G0  /*    1       0        0      */
#define LINE_HELPER_PG_0x4d P1_G0  /*    1       0        0      */
#define LINE_HELPER_PG_0x4e P1_G0  /*    1       0        1      */
#define LINE_HELPER_PG_0x4f P1_G0  /*    1       0        1      */
#define LINE_HELPER_PG_0x50 P0_G1  /*    0       1        0      */
#define LINE_HELPER_PG_0x51 P0_G1  /*    0       1        0      */
#define LINE_HELPER_PG_0x52 P0_G1  /*    0       1        1      */
#define LINE_HELPER_PG_0x53 P0_G1  /*    0       1        1      */
#define LINE_HELPER_PG_0x54 P0_G1  /*    0       1        0      */
#define LINE_HELPER_PG_0x55 P0_G1  /*    0       1        0      */
#define LINE_HELPER_PG_0x56 P0_G1  /*    0       1        1      */
#define LINE_HELPER_PG_0x57 P0_G1  /*    0       1        1      */
#define LINE_HELPER_PG_0x58 P1_G1  /*    1       1        0      */
#define LINE_HELPER_PG_0x59 P1_G1  /*    1       1        0      */
#define LINE_HELPER_PG_0x5a P1_G1  /*    1       1        1      */
#define LINE_HELPER_PG_0x5b P1_G1  /*    1       1        1      */
#define LINE_HELPER_PG_0x5c P1_G1  /*    1       1        0      */
#define LINE_HELPER_PG_0x5d P1_G1  /*    1       1        0      */
#define LINE_HELPER_PG_0x5e P1_G1  /*    1       1        1      */
#define LINE_HELPER_PG_0x5f P1_G1  /*    1       1        1      */

/* For non-blended cv (bit 1 = 0) all four bm rows collapse to BMopaque.
 * For blended cv (bit 1 = 1) each bm picks the matching BM<bm> tag.
 * Per-cv LINE_HELPER_BM_<cv>(bm) yields either BMopaque or BM<bm>. */
#define LINE_HELPER_BM_0x40(bm) BMopaque
#define LINE_HELPER_BM_0x41(bm) BMopaque
#define LINE_HELPER_BM_0x42(bm) BM_##bm
#define LINE_HELPER_BM_0x43(bm) BM_##bm
#define LINE_HELPER_BM_0x44(bm) BMopaque
#define LINE_HELPER_BM_0x45(bm) BMopaque
#define LINE_HELPER_BM_0x46(bm) BM_##bm
#define LINE_HELPER_BM_0x47(bm) BM_##bm
#define LINE_HELPER_BM_0x48(bm) BMopaque
#define LINE_HELPER_BM_0x49(bm) BMopaque
#define LINE_HELPER_BM_0x4a(bm) BM_##bm
#define LINE_HELPER_BM_0x4b(bm) BM_##bm
#define LINE_HELPER_BM_0x4c(bm) BMopaque
#define LINE_HELPER_BM_0x4d(bm) BMopaque
#define LINE_HELPER_BM_0x4e(bm) BM_##bm
#define LINE_HELPER_BM_0x4f(bm) BM_##bm
#define LINE_HELPER_BM_0x50(bm) BMopaque
#define LINE_HELPER_BM_0x51(bm) BMopaque
#define LINE_HELPER_BM_0x52(bm) BM_##bm
#define LINE_HELPER_BM_0x53(bm) BM_##bm
#define LINE_HELPER_BM_0x54(bm) BMopaque
#define LINE_HELPER_BM_0x55(bm) BMopaque
#define LINE_HELPER_BM_0x56(bm) BM_##bm
#define LINE_HELPER_BM_0x57(bm) BM_##bm
#define LINE_HELPER_BM_0x58(bm) BMopaque
#define LINE_HELPER_BM_0x59(bm) BMopaque
#define LINE_HELPER_BM_0x5a(bm) BM_##bm
#define LINE_HELPER_BM_0x5b(bm) BM_##bm
#define LINE_HELPER_BM_0x5c(bm) BMopaque
#define LINE_HELPER_BM_0x5d(bm) BMopaque
#define LINE_HELPER_BM_0x5e(bm) BM_##bm
#define LINE_HELPER_BM_0x5f(bm) BM_##bm

/* The bm dimension is named by tag in the function suffix.  The
 * trailing _##bm in BM_##bm produces 'BM_0', 'BM_1' etc; we rewrite
 * those to the actual specialisation tags via the LINE_BMTAG family. */
#define LINE_BMTAG_BM_0   BMavg
#define LINE_BMTAG_BM_1   BMadd
#define LINE_BMTAG_BM_2   BMsub
#define LINE_BMTAG_BM_3   BMaddq
#define LINE_BMTAG_BMopaque BMopaque

/* Three-level paste indirection so the bm fragment expands fully
 * before token-pasting into the function name. */
#define LINE_HELPER_NAME(pg, bmtag, mam)         LINE_HELPER_NAME_(pg, bmtag, mam)
#define LINE_HELPER_NAME_(pg, bmtag, mam)        LINE_HELPER_NAME__(pg, LINE_BMTAG_##bmtag, mam)
#define LINE_HELPER_NAME__(pg, finaltag, mam)    LINE_HELPER_NAME___(pg, finaltag, mam)
#define LINE_HELPER_NAME___(pg, finaltag, mam)   Command_DrawLine_##pg##_##finaltag##_ME##mam

#define LINE_HELPER_SUB(bm, cv, mam) \
   LINE_HELPER_NAME(LINE_HELPER_PG_##cv, LINE_HELPER_BM_##cv(bm), mam)

#define LINE_HELPER_FG(bm, cv)											\
	 {													\
		LINE_HELPER_SUB(bm, cv, 0),									\
		LINE_HELPER_SUB(bm, cv, 0),									\
		LINE_HELPER_SUB(bm, cv, 0),									\
		LINE_HELPER_SUB(bm, cv, 0),									\
		LINE_HELPER_SUB(bm, cv, 1),									\
		LINE_HELPER_SUB(bm, cv, 1),									\
		LINE_HELPER_SUB(bm, cv, 1),									\
		LINE_HELPER_SUB(bm, cv, 1)									\
	 }

#define LINE_HELPER(cv)												\
	{ 													\
	 { LINE_HELPER_FG(0, cv), LINE_HELPER_FG(1, cv), LINE_HELPER_FG(2, cv), LINE_HELPER_FG(3, cv) },	\
	 3 + ((cv & 0x10) >> 4),										\
	 1,													\
	 false													\
	}

/*
 * OTHER_HELPER family - state-setting and framebuffer-management
 * commands that don't go through the rasteriser. The same
 * function pointer is replicated across every dispatch slot since
 * BlendMode/TexMode/MaskEval don't apply.
 *
 * OTHER_HELPER_X{2,4,8,16,32} produce the same entry repeated N
 * times for opcode ranges that all map to the same handler
 * (e.g. 0x80..0x9F all do FBCopy regardless of low-nibble bits).
 */
#define OTHER_HELPER_FG(bm, arg_ptr) { arg_ptr, arg_ptr, arg_ptr, arg_ptr, arg_ptr, arg_ptr, arg_ptr, arg_ptr }
#define OTHER_HELPER(arg_cs, arg_fbcs, arg_ss, arg_ptr) { { OTHER_HELPER_FG(0, arg_ptr), OTHER_HELPER_FG(1, arg_ptr), OTHER_HELPER_FG(2, arg_ptr), OTHER_HELPER_FG(3, arg_ptr) }, arg_cs, arg_fbcs, arg_ss }
#define OTHER_HELPER_X2(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER(arg_cs, arg_fbcs, arg_ss, arg_ptr)
#define OTHER_HELPER_X4(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER_X2(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER_X2(arg_cs, arg_fbcs, arg_ss, arg_ptr)
#define OTHER_HELPER_X8(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER_X4(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER_X4(arg_cs, arg_fbcs, arg_ss, arg_ptr)
#define OTHER_HELPER_X16(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER_X8(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER_X8(arg_cs, arg_fbcs, arg_ss, arg_ptr)
#define OTHER_HELPER_X32(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER_X16(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER_X16(arg_cs, arg_fbcs, arg_ss, arg_ptr)

/*
 * NULLCMD - placeholder for unused opcodes. NULL function pointer
 * across all 32 slots; the dispatcher checks for NULL before
 * calling. ss_cmd=true so drawtime accounting is bypassed.
 */
#define NULLCMD_FG(bm) { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
#define NULLCMD() { { NULLCMD_FG(0), NULLCMD_FG(1), NULLCMD_FG(2), NULLCMD_FG(3) }, 1, 1, true }
