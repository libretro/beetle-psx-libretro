#include <retro_atomic.h>

/* Whether the GPU thread and its band lanes are built (gpu_thread.c) */
#if defined(HAVE_THREADS) && defined(RETRO_ATOMIC_LOCK_FREE)
#define GPU_HAVE_THREAD 1
#endif

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
 * rhi_intf.h (BLEND_MODE_AVERAGE / _ADD / _SUBTRACT / _ADD_FOURTH);
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

/* The template-style PlotPixelBlend dispatcher that previously sat
 * here under #ifdef __cplusplus is gone in stage 5: every caller
 * now references the right PlotPixelBlend_<TAG> specialisation by
 * mangled name through macro literal pasting, so the runtime switch
 * (which the optimiser collapsed at -O2) is no longer needed. */

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

/* The template-style PlotPixel dispatcher previously here is gone
 * in stage 5; callers now reference the right PlotPixel_<TAG>_<...>
 * specialisation by mangled name. */

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
      texel_put(gpu, x, y, ((TEXTURED) ? fore_pix : (fore_pix & 0x7FFF)) | gpu->MaskSetOR); \
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

/* The template-style PlotNativePixel dispatcher previously here is
 * gone in stage 5; callers now reference the right
 * PlotNativePixel_<TAG>_<...> specialisation by mangled name. */

/* dither_offset points at &DitherLUT[y][0][x]; the reordered LUT
 * ([y][value][x], see gpu.h) indexes source values at stride 4, which
 * folds into scaled addressing. */
#define ModTexelIndexR(texel, r) ((((texel) & 0x1F)   * (r)) >> (5 - 1))
#define ModTexelIndexG(texel, g) ((((texel) & 0x3E0)  * (g)) >> (10 - 1))
#define ModTexelIndexB(texel, b) ((((texel) & 0x7C00) * (b)) >> (15 - 1))

#define ModTexelRaw(dither_offset, texel, r, g, b) \
   ((texel & 0x8000) \
    | (dither_offset[ModTexelIndexR(texel, r) * 4] << 0) \
    | (dither_offset[ModTexelIndexG(texel, g) * 4] << 5) \
    | (dither_offset[ModTexelIndexB(texel, b) * 4] << 10))

#ifdef PSX_MEASURE_MODULATE
/* Opt-in measurement, off by default and free when off -- this is a
 * per-pixel inner loop and the counters are not worth paying for in a
 * shipping build.
 *
 * Texture modulation multiplies the texel by the vertex colour with the
 * vertex colour's 0x80 as unity, so the product carries up to about 2x of
 * headroom above the 5-bit channel ceiling: 0x1F * 0xFF >> 4 = 494 against a
 * DitherLUT input of 255 for full scale. Everything above that is clamped
 * away by the table (value >>= 3 then clamp to 0x1F), and clamped identically
 * on real hardware.
 *
 * Whether an HDR path should keep that headroom instead of clamping it is a
 * question about content, not about the pipeline: if games rarely drive the
 * product past the ceiling there is nothing to keep, and if they drive it
 * past constantly then lifting the clamp would push ordinary lit geometry
 * above reference white rather than producing highlights. Build with
 * -DPSX_MEASURE_MODULATE and read the counters before deciding either way. */
void GPU_NoteModulate(int idx_r, int idx_g, int idx_b);

#define ModTexel(dither_offset, texel, r, g, b) \
   (GPU_NoteModulate(ModTexelIndexR(texel, r), \
                     ModTexelIndexG(texel, g), \
                     ModTexelIndexB(texel, b)), \
    ModTexelRaw(dither_offset, texel, r, g, b))
#else
#define ModTexel(dither_offset, texel, r, g, b) \
   ModTexelRaw(dither_offset, texel, r, g, b)
#endif

/* The rasterizer charges DrawTimeAvail itself only inline at 1x, drawing
 * what the hardware draws.  Inline at an upscale, or drawing PGXP's
 * sub-pixel vertices or perspective-correct texels, the native timing walk
 * charges instead, of the vertices and texels the hardware gets; with the
 * GPU thread the emulation thread's walk does, so the GPU thread's drawing
 * never charges. */
#define GPU_TIMING_NONE   0   /* FRONT walks, BACK draws without timing */
#define GPU_TIMING_RASTER 1   /* inline at 1x, drawing as the hardware */
#define GPU_TIMING_WALK   2   /* inline otherwise */
#define GPU_RASTER_CHARGES(g) ((g)->timing == GPU_TIMING_RASTER)
#define GPU_RASTER_WALKS(g)   ((g)->timing == GPU_TIMING_WALK)

/* Whether the software rasterizer draws other vertices or texels than the
 * hardware: PGXP's under the software renderer, or perspective-correct */
static INLINE bool gpu_draws_enhanced(void)
{
   return PGXP_enabled() && (rhi_intf_is_type() == RHI_SOFTWARE
         || PGXP_texture_correction_enabled());
}

/* Role and, from it, the scale and PGXP, who charges timing.  The walk
 * takes over the texture cache's tags as its timing tags when the
 * rasterizer stops charging. */
static INLINE void gpu_set_role(PS_GPU *g, uint8_t role)
{
   const uint8_t timing = (role != GPU_ROLE_INLINE) ? GPU_TIMING_NONE
      : (g->upscale_shift || gpu_draws_enhanced()) ? GPU_TIMING_WALK
      : GPU_TIMING_RASTER;
   if (g->timing == GPU_TIMING_RASTER && timing != GPU_TIMING_RASTER)
   {
      unsigned i;
      for (i = 0; i < 256; i++)
         g->TexCacheTimingTag[i] = g->TexCache[i].Tag;
      g->last_timing_line = ~0U;
   }
   g->role   = role;
   g->timing = timing;
}
/* Whether a software renderer or framebuffer keeps VRAM.  The GPU thread
 * runs only when one does, and does not read the renderer's settings,
 * which the emulation thread may rewrite while it draws. */
#define GPU_HAS_SW(g) ((g)->role == GPU_ROLE_BACK || rhi_intf_has_software_renderer())
/* Whether this instance draws native VRAM row y (gpu.h, band_lanes) */
#define GPU_BAND_NONE 0xFF
#define GPU_ROW_MINE(g, y) (!(g)->band_lanes \
      || ((g)->band_lanes != GPU_BAND_NONE \
         && ((((unsigned)(y) & 511) >> 3) % (g)->band_lanes) == (g)->band_lane))

/* For a lane drawing bands: whether it draws native row y, stamping its
 * texture cache reads with the row if it does (gpu.h, tc_stamp), the
 * row's first included */
static INLINE bool gpu_band_row(PS_GPU *g, int32_t y)
{
   if (!GPU_ROW_MINE(g, y))
      return false;
   g->tc_stamp      = g->tc_draw | ((uint32_t)y & 0xFFFF);
   g->last_tex_line = ~0U;
   return true;
}
/* The emulation thread notes what each command writes and what texture
 * it reads, while the lanes draw bands (gpu_thread_cmd_end). */
#ifdef GPU_HAVE_THREAD
#define GPU_FP(g) ((g)->role == GPU_ROLE_FRONT && (g)->tc_track)
#else
#define GPU_FP(g) 0
#endif

/* Native rectangle [x0, x1] x [y0, y1] written, clipped to the draw area
 * when clip, added to the command's. */
static NO_INLINE void gpu_fp_write(PS_GPU *g, int32_t x0, int32_t y0,
      int32_t x1, int32_t y1, bool clip)
{
   if (clip)
   {
      if (x0 < g->ClipX0) x0 = g->ClipX0;
      if (y0 < g->ClipY0) y0 = g->ClipY0;
      if (x1 > g->ClipX1) x1 = g->ClipX1;
      if (y1 > g->ClipY1) y1 = g->ClipY1;
   }
   if (x1 < x0 || y1 < y0)
      return;
   if (g->fp_write[1])
   {
      int32_t ox0 = (int32_t)(g->fp_write[0] & 0xFFFF);
      int32_t oy0 = (int32_t)(g->fp_write[0] >> 16);
      int32_t ox1 = ox0 + (int32_t)(g->fp_write[1] & 0xFFFF) - 1;
      int32_t oy1 = oy0 + (int32_t)(g->fp_write[1] >> 16) - 1;
      if (ox0 < x0) x0 = ox0;
      if (oy0 < y0) y0 = oy0;
      if (ox1 > x1) x1 = ox1;
      if (oy1 > y1) y1 = oy1;
   }
   g->fp_write[0] = (uint32_t)x0 | ((uint32_t)y0 << 16);
   g->fp_write[1] = (uint32_t)(x1 - x0 + 1) | ((uint32_t)(y1 - y0 + 1) << 16);
}

/* A textured command: the texture page it reads and, below 15 bits, its
 * CLUT */
static NO_INLINE void gpu_fp_texture(PS_GPU *g, uint16_t raw_clut, int tm)
{
   g->fp_textured = true;
   if (tm < 2)
   {
      g->fp_clut[0] = ((uint32_t)(raw_clut & 0x3F) << 4)
         | ((uint32_t)((raw_clut >> 6) & 0x1FF) << 16);
      g->fp_clut[1] = (tm ? 256u : 16u) | (1u << 16);
   }
}

/* Renderer calls and display bookkeeping happen on the emulation thread. */
#define GPU_ON_EMU_THREAD(g)  ((g)->role != GPU_ROLE_BACK)
/* VRAM is read and written by the instance that draws. */
#define GPU_TOUCHES_VRAM(g)   ((g)->role != GPU_ROLE_FRONT)

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
static INLINE void Update_CLUT_Cache_##SUFFIX(PS_GPU *g, uint16_t raw_clut) \
{ \
   if (GPU_FP(g)) \
      gpu_fp_texture(g, raw_clut, (TM_VAL)); \
   if ((TM_VAL) < 2) \
   { \
      const uint32_t new_ccvb = ((raw_clut & 0x7FFF) | ((TM_VAL) << 16)); \
      if (g->CLUT_Cache_VB != new_ccvb) \
      { \
         uint16_t y = (raw_clut >> 6) & 0x1FF; \
         const uint32_t cxo = (raw_clut & 0x3F) << 4; \
         const uint32_t count = ((TM_VAL) ? 256 : 16); \
         unsigned i; \
         g->DrawTimeAvail -= count; \
         /* not by a lane drawing nothing: lane 0's is copied when the \
          * lanes draw bands again (gpu_thread.c) */ \
         if (GPU_TOUCHES_VRAM(g) && g->band_lanes != GPU_BAND_NONE) \
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

/* The template-style Update_CLUT_Cache dispatcher previously here
 * is gone in stage 5; callers now reference the right
 * Update_CLUT_Cache_TM<n> specialisation by mangled name. */

static INLINE void RecalcTexWindowStuff(PS_GPU *g)
{
   uint8_t tww = g->tww;
   uint8_t twh = g->twh;
   uint8_t twx = g->twx;
   uint8_t twy = g->twy;

   g->SUCV.TWX_AND = ~(tww << 3);
   {
      /* PSX TexMode field is 2 bits; value 3 ("reserved") is folded
       * to 2 here so the shift below stays in range. */
      uint8_t _tm = g->TexMode;
      if (_tm > 2) _tm = 2;
      g->SUCV.TWX_ADD = ((twx & tww) << 3) + (g->TexPageX << (2 - _tm));
   }

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
 */

/* Generator macro for one GetTexel specialisation.  TM_VAL is the
 * literal texture-mode (0/1/2).  The switch on TM_VAL collapses to
 * the matching arm; the if (TM_VAL != 2) and if (TM_VAL == 0) tests
 * are literal so dead arms vanish.  TRACK_LIT stamps each cache read
 * for a lane drawing bands (gpu.h, tc_pos). */
#define DEFINE_GetTexel(SUFFIX, TM_VAL, TRACK_LIT) \
static INLINE uint16_t GetTexel_##SUFFIX(PS_GPU *g, int32_t u_arg, int32_t v_arg) \
{ \
   uint16_t fbw; \
   uint32_t u_ext   = ((u_arg & g->SUCV.TWX_AND) + g->SUCV.TWX_ADD); \
   uint32_t fbtex_x = ((u_ext >> (2 - (TM_VAL)))) & 1023; \
   uint32_t fbtex_y = (v_arg & g->SUCV.TWY_AND) + g->SUCV.TWY_ADD; \
   uint32_t gro     = fbtex_y * 1024U + fbtex_x; \
   uint32_t gro_line = gro & ~0x3U; \
   PS_GPU_TexCache_t *c; \
   if (g->last_tex_line == gro_line && g->last_tex_c) { \
      /* same cache line as previous pixel: skip index shuffle + tag check */ \
      c = (PS_GPU_TexCache_t *)g->last_tex_c; \
   } else { \
      PS_GPU_TexCache_t *TexCache = &g->TexCache[0]; \
      c = NULL; \
      switch (TM_VAL) \
      { \
         case 0: c = &TexCache[((gro >> 2) & 0x3) | ((gro >> 8) & 0xFC)]; break; \
         case 1: c = &TexCache[((gro >> 2) & 0x7) | ((gro >> 7) & 0xF8)]; break; \
         case 2: c = &TexCache[((gro >> 2) & 0x7) | ((gro >> 7) & 0xF8)]; break; \
      } \
      if (TRACK_LIT) \
         g->tc_pos[c - TexCache] = g->tc_stamp; \
      if (MDFN_UNLIKELY(c->Tag != gro_line)) \
      { \
         uint32_t cache_x = fbtex_x & ~3; \
         /* inline at 1x only: see GPU_RASTER_CHARGES */ \
         if (GPU_RASTER_CHARGES(g)) \
            g->DrawTimeAvail -= 4; \
         c->Data[0] = texel_fetch(g, cache_x + 0, fbtex_y); \
         c->Data[1] = texel_fetch(g, cache_x + 1, fbtex_y); \
         c->Data[2] = texel_fetch(g, cache_x + 2, fbtex_y); \
         c->Data[3] = texel_fetch(g, cache_x + 3, fbtex_y); \
         c->Tag     = gro_line; \
      } \
      g->last_tex_line = gro_line; \
      g->last_tex_c = c; \
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

/* The texture cache's timing, apart from the drawing: the same tags and
 * index as GetTexel, in TexCacheTimingTag, charging DrawTimeAvail on a
 * miss.  The native timing walk and sprites charge through these, so
 * emulated GPU timing is the same at every internal resolution and
 * whichever thread draws.  TimingLine is one fetch from the cache line at
 * gro_line. */
/* The texture cache entry a cache line maps to */
#define TIMING_LINE_IDX(line, TM_VAL) (((TM_VAL) == 0) \
   ? ((((line) >> 2) & 0x3) | (((line) >> 8) & 0xFC)) \
   : ((((line) >> 2) & 0x7) | (((line) >> 7) & 0xF8)))

#define DEFINE_TimingLine(SUFFIX, TM_VAL) \
static INLINE void TimingLine_##SUFFIX(PS_GPU *g, uint32_t gro_line) \
{ \
   uint32_t idx; \
   if (g->last_timing_line == gro_line) \
      return; \
   g->last_timing_line = gro_line; \
   idx = TIMING_LINE_IDX(gro_line, TM_VAL); \
   if (g->TexCacheTimingTag[idx] != gro_line) \
   { \
      g->DrawTimeAvail -= 4; \
      g->TexCacheTimingTag[idx] = gro_line; \
   } \
}

DEFINE_TimingLine(TM0, 0)
DEFINE_TimingLine(TM1, 1)
DEFINE_TimingLine(TM2, 2)

#define TIMING_GRO_LINE(g, u_arg, v_arg, TM_VAL) \
   (((((uint32_t)(v_arg) & (g)->SUCV.TWY_AND) + (g)->SUCV.TWY_ADD) * 1024U \
     + (((((uint32_t)(u_arg) & (g)->SUCV.TWX_AND) + (g)->SUCV.TWX_ADD) \
          >> (2 - (TM_VAL))) & 1023)) & ~0x3U)

#define DEFINE_TimingTexel(SUFFIX, TM_VAL) \
static INLINE void TimingTexel_##SUFFIX(PS_GPU *g, int32_t u_arg, int32_t v_arg) \
{ \
   TimingLine_##SUFFIX(g, TIMING_GRO_LINE(g, u_arg, v_arg, TM_VAL)); \
}

DEFINE_TimingTexel(TM0, 0)
DEFINE_TimingTexel(TM1, 1)
DEFINE_TimingTexel(TM2, 2)

/* The texture cache's timing along a run of w texels whose coordinates
 * step linearly in 8.24 fixed point: texel k is at ((u + k * du) >> 24,
 * (v + k * dv) >> 24), as an affine polygon span or a sprite row steps.
 * The cache lines are worked out eight at a time and the tags checked
 * where the line changes, which is what TimingTexel per texel does. */
#if defined(__SSE2__)
#define TIMING_SPAN_LINES(g, u, v, du, dv, TM_VAL, lines) \
   do { \
      const __m128i du4_ = _mm_set1_epi32((int)((du) * 4u)); \
      const __m128i dv4_ = _mm_set1_epi32((int)((dv) * 4u)); \
      const __m128i xa_  = _mm_set1_epi32((int)(g)->SUCV.TWX_AND); \
      const __m128i xd_  = _mm_set1_epi32((int)(g)->SUCV.TWX_ADD); \
      const __m128i ya_  = _mm_set1_epi32((int)(g)->SUCV.TWY_AND); \
      const __m128i yd_  = _mm_set1_epi32((int)(g)->SUCV.TWY_ADD); \
      const __m128i m10_ = _mm_set1_epi32(1023); \
      const __m128i m4_  = _mm_set1_epi32(~3); \
      __m128i uu_ = _mm_add_epi32(_mm_set1_epi32((int)(u)), \
            _mm_set_epi32((int)((du) * 3u), (int)((du) * 2u), (int)(du), 0)); \
      __m128i vv_ = _mm_add_epi32(_mm_set1_epi32((int)(v)), \
            _mm_set_epi32((int)((dv) * 3u), (int)((dv) * 2u), (int)(dv), 0)); \
      int h_; \
      for (h_ = 0; h_ < 2; h_++) \
      { \
         __m128i fx_ = _mm_add_epi32(_mm_and_si128(_mm_srli_epi32(uu_, 24), xa_), xd_); \
         __m128i fy_ = _mm_add_epi32(_mm_and_si128(_mm_srli_epi32(vv_, 24), ya_), yd_); \
         fx_ = _mm_and_si128(_mm_srli_epi32(fx_, 2 - (TM_VAL)), m10_); \
         _mm_storeu_si128((__m128i*)&(lines)[h_ * 4], \
               _mm_and_si128(_mm_add_epi32(_mm_slli_epi32(fy_, 10), fx_), m4_)); \
         uu_ = _mm_add_epi32(uu_, du4_); \
         vv_ = _mm_add_epi32(vv_, dv4_); \
      } \
   } while (0)
#elif defined(GPU_HAVE_NEON)
#define TIMING_SPAN_LINES(g, u, v, du, dv, TM_VAL, lines) \
   do { \
      static const uint32_t k_lane_[4] = { 0, 1, 2, 3 }; \
      const uint32x4_t k_   = vld1q_u32(k_lane_); \
      const uint32x4_t xa_  = vdupq_n_u32((g)->SUCV.TWX_AND); \
      const uint32x4_t xd_  = vdupq_n_u32((g)->SUCV.TWX_ADD); \
      const uint32x4_t ya_  = vdupq_n_u32((g)->SUCV.TWY_AND); \
      const uint32x4_t yd_  = vdupq_n_u32((g)->SUCV.TWY_ADD); \
      const uint32x4_t m10_ = vdupq_n_u32(1023); \
      const uint32x4_t m4_  = vdupq_n_u32(~3u); \
      uint32x4_t uu_ = vmlaq_n_u32(vdupq_n_u32(u), k_, (du)); \
      uint32x4_t vv_ = vmlaq_n_u32(vdupq_n_u32(v), k_, (dv)); \
      int h_; \
      for (h_ = 0; h_ < 2; h_++) \
      { \
         uint32x4_t fx_ = vaddq_u32(vandq_u32(vshrq_n_u32(uu_, 24), xa_), xd_); \
         uint32x4_t fy_ = vaddq_u32(vandq_u32(vshrq_n_u32(vv_, 24), ya_), yd_); \
         fx_ = vandq_u32(vshlq_u32(fx_, vdupq_n_s32(-(2 - (TM_VAL)))), m10_); \
         vst1q_u32(&(lines)[h_ * 4], \
               vandq_u32(vaddq_u32(vshlq_n_u32(fy_, 10), fx_), m4_)); \
         uu_ = vaddq_u32(uu_, vdupq_n_u32((du) * 4u)); \
         vv_ = vaddq_u32(vv_, vdupq_n_u32((dv) * 4u)); \
      } \
   } while (0)
#else
#define TIMING_SPAN_LINES(g, u, v, du, dv, TM_VAL, lines) \
   do { \
      int h_; \
      for (h_ = 0; h_ < 8; h_++) \
         (lines)[h_] = TIMING_GRO_LINE(g, ((u) + (uint32_t)h_ * (du)) >> 24, \
               ((v) + (uint32_t)h_ * (dv)) >> 24, TM_VAL); \
   } while (0)
#endif

/* Bit k of the result is set where lines[k] differs from the line before
 * it, prev for k = 0. */
#if defined(__SSE2__)
#define TIMING_SPAN_CHANGES(lines, prev, mask) \
   do { \
      const __m128i l0_ = _mm_loadu_si128((const __m128i*)&(lines)[0]); \
      const __m128i l1_ = _mm_loadu_si128((const __m128i*)&(lines)[4]); \
      const __m128i p0_ = _mm_or_si128(_mm_slli_si128(l0_, 4), \
            _mm_cvtsi32_si128((int)(prev))); \
      const __m128i p1_ = _mm_or_si128(_mm_slli_si128(l1_, 4), \
            _mm_srli_si128(l0_, 12)); \
      (mask) = 0xFFu ^ (uint32_t)(_mm_movemask_ps(_mm_castsi128_ps( \
               _mm_cmpeq_epi32(l0_, p0_))) \
            | (_mm_movemask_ps(_mm_castsi128_ps( \
               _mm_cmpeq_epi32(l1_, p1_))) << 4)); \
   } while (0)
#elif defined(GPU_HAVE_NEON)
#define TIMING_SPAN_CHANGES(lines, prev, mask) \
   do { \
      static const uint32_t b0_[4] = { 1, 2, 4, 8 }; \
      static const uint32_t b1_[4] = { 16, 32, 64, 128 }; \
      const uint32x4_t l0_ = vld1q_u32(&(lines)[0]); \
      const uint32x4_t l1_ = vld1q_u32(&(lines)[4]); \
      const uint32x4_t e0_ = vceqq_u32(l0_, \
            vextq_u32(vdupq_n_u32(prev), l0_, 3)); \
      const uint32x4_t e1_ = vceqq_u32(l1_, vextq_u32(l0_, l1_, 3)); \
      const uint32x4_t b_  = vorrq_u32(vandq_u32(e0_, vld1q_u32(b0_)), \
            vandq_u32(e1_, vld1q_u32(b1_))); \
      const uint32x2_t s_  = vorr_u32(vget_low_u32(b_), vget_high_u32(b_)); \
      (mask) = 0xFFu ^ (vget_lane_u32(s_, 0) | vget_lane_u32(s_, 1)); \
   } while (0)
#else
#define TIMING_SPAN_CHANGES(lines, prev, mask) \
   do { \
      uint32_t p_ = (prev); \
      int k_; \
      (mask) = 0; \
      for (k_ = 0; k_ < 8; k_++) \
      { \
         (mask) |= (uint32_t)((lines)[k_] != p_) << k_; \
         p_ = (lines)[k_]; \
      } \
   } while (0)
#endif

/* The lowest set bit's index, for a bit of an 8-bit mask */
#define TIMING_LOW_BIT(m) \
   ("\0\1\2\4\7\3\6\5"[((((m) & (0u - (m))) * 0x17u) & 0xFFu) >> 5])

/* Only the texels whose line differs from the one before look the tag up;
 * misses are counted and charged once at the end of the span.  A span
 * stepping half a texel down or half a cache line across per pixel changes
 * line on most texels, so it checks every changed lane without branching;
 * other spans visit the changed lanes only. */
#define DEFINE_TimingSpan(SUFFIX, TM_VAL) \
static INLINE void TimingSpan_##SUFFIX(PS_GPU *g, uint32_t u, uint32_t v, \
      uint32_t du, uint32_t dv, int32_t w) \
{ \
   uint32_t lines[8]; \
   uint32_t last   = g->last_timing_line; \
   int32_t  misses = 0; \
   const uint32_t adu = ((int32_t)du < 0) ? 0u - du : du; \
   const uint32_t adv = ((int32_t)dv < 0) ? 0u - dv : dv; \
   const int dense = adv >= (1u << 23) || adu >= (1u << (27 - (TM_VAL))); \
   while (w > 0) \
   { \
      uint32_t mask; \
      int32_t  n = (w < 8) ? w : 8; \
      TIMING_SPAN_LINES(g, u, v, du, dv, TM_VAL, lines); \
      TIMING_SPAN_CHANGES(lines, last, mask); \
      mask &= (1u << n) - 1u; \
      if (dense) \
      { \
         int32_t k; \
         for (k = 0; k < n; k++) \
         { \
            const uint32_t line = lines[k]; \
            const uint32_t idx  = TIMING_LINE_IDX(line, TM_VAL); \
            const uint32_t t    = g->TexCacheTimingTag[idx]; \
            const uint32_t d    = (t ^ line) & (0u - ((mask >> k) & 1u)); \
            misses += (d != 0); \
            g->TexCacheTimingTag[idx] = t ^ d; \
         } \
      } \
      else \
      { \
         while (mask) \
         { \
            const uint32_t line = lines[(int)TIMING_LOW_BIT(mask)]; \
            const uint32_t idx  = TIMING_LINE_IDX(line, TM_VAL); \
            mask &= mask - 1u; \
            if (g->TexCacheTimingTag[idx] != line) \
            { \
               misses++; \
               g->TexCacheTimingTag[idx] = line; \
            } \
         } \
      } \
      last = lines[n - 1]; \
      u += du * 8u; \
      v += dv * 8u; \
      w -= 8; \
   } \
   g->last_timing_line  = last; \
   g->DrawTimeAvail    -= misses * 4; \
}

DEFINE_TimingSpan(TM0, 0)
DEFINE_TimingSpan(TM1, 1)
DEFINE_TimingSpan(TM2, 2)

/* PS_GPU_TexCache_t aliases the nested TexCache_t struct from
 * PS_GPU.  The nested struct is hoisted to file scope in C, so we
 * can reference it as `struct TexCache_t` directly. */
typedef struct TexCache_t PS_GPU_TexCache_t;

DEFINE_GetTexel(TM0, 0, 0)
DEFINE_GetTexel(TM1, 1, 0)
DEFINE_GetTexel(TM2, 2, 0)
#ifdef GPU_HAVE_THREAD
DEFINE_GetTexel(TM0_tk, 0, 1)
DEFINE_GetTexel(TM1_tk, 1, 1)
DEFINE_GetTexel(TM2_tk, 2, 1)
#endif
/* GetTexel_TM<TM>, or its tracked variant for TK 1; both literals */
#define GPU_GETTEXEL(TM, TK)   GPU_GETTEXEL_##TK(TM)
#define GPU_GETTEXEL_0(TM)     GetTexel_TM##TM
#define GPU_GETTEXEL_1(TM)     GetTexel_TM##TM##_tk

/* The template-style GetTexel dispatcher previously here is gone
 * in stage 5; callers now reference the right GetTexel_TM<n>
 * specialisation by mangled name. */

/* See psx_gpu_rasterize_both_fields in beetle_psx_globals.h. */
extern bool psx_gpu_rasterize_both_fields;

/*
 * Returns true if the real PS1 GPU's dfe-skip would have rejected
 * native-row y for the current display mode.  Identical to
 * LineSkipTest when psx_gpu_rasterize_both_fields is false; when
 * the bypass is active, it still reports what the real hardware
 * would have done so DrawTimeAvail bookkeeping in the rasteriser
 * stays anchored to the half-density real-PSX cost rather than the
 * full-density cost we actually pay.  Without this, complex 480i
 * scenes drop polygons mid-frame because DrawTimeAvail underflows.
 */
static INLINE bool DfeWouldSkip(PS_GPU *g, unsigned y)
{
   if ((g->DisplayMode & 0x24) != 0x24)
      return false;
   if (g->dfe)
      return false;
   return (y & 1) == ((g->DisplayFB_YStart + g->field_ram_readout) & 1);
}

static INLINE bool LineSkipTest(PS_GPU* g, unsigned y)
{
   if((g->DisplayMode & 0x24) != 0x24)
      return false;

   /* "Off" deinterlace mode: rasterise polygons to every VRAM line
    * WITHIN THE DISPLAYED Y RANGE, regardless of the game's dfe
    * flag.  Scoping the bypass to the display rectangle preserves
    * real-PSX dfe-skip semantics for back-buffer / offscreen VRAM
    * regions that some games (e.g. Pro Pinball) populate from CPU
    * and read back through GPUREAD - trampling those rows by
    * drawing on-screen-intended polygons there would hang those
    * games on boot. */
   if (psx_gpu_rasterize_both_fields)
   {
      const unsigned dy_start = g->DisplayFB_YStart;
      const unsigned dy_end   = (dy_start + 480) & 0x1FF;
      if (dy_start <= dy_end
            ? (y >= dy_start && y < dy_end)
            : (y >= dy_start || y < dy_end))
         return false;
   }

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
