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
template<int BlendMode>
static INLINE void PlotPixelBlend(uint16_t bg_pix, uint16_t *fore_pix)
{
   /*
    * fore_pix - foreground -  the screen
    * bg_pix   - background  - the texture
    */

   /* Efficient 15bpp pixel math algorithms from blargg */
   switch(BlendMode)
   {
      /* 0.5 x B + 0.5 x F */
      case BLEND_MODE_AVERAGE:
         bg_pix   |= 0x8000;
         *fore_pix = ((*fore_pix + bg_pix) - ((*fore_pix ^ bg_pix) & 0x0421)) >> 1;
         break;

         /* 1.0 x B + 1.0 x F */
      case BLEND_MODE_ADD:
         {
            uint32_t sum, carry;
            bg_pix   &= ~0x8000;
            sum       = *fore_pix + bg_pix;
            carry     = (sum - ((*fore_pix ^ bg_pix) & 0x8421)) & 0x8420;
            *fore_pix = (sum - carry) | (carry - (carry >> 5));
         }
         break;

         /* 1.0 x B - 1.0 x F */

      case BLEND_MODE_SUBTRACT:
         {
            uint32_t diff;
            uint32_t borrow;

            bg_pix    |= 0x8000;
            *fore_pix &= ~0x8000;
            diff       = bg_pix - *fore_pix + 0x108420;
            borrow     = (diff  - ((bg_pix ^ *fore_pix) & 0x108420)) & 0x108420;
            *fore_pix  = (diff  - borrow) & (borrow - (borrow >> 5));
         }
         break;

         /* 1.0 x B + 0.25 * F */

      case BLEND_MODE_ADD_FOURTH:
         {
            uint32_t sum, carry;
            bg_pix   &= ~0x8000;
            *fore_pix = ((*fore_pix >> 2) & 0x1CE7) | 0x8000;
            sum       = *fore_pix + bg_pix;
            carry     = (sum - ((*fore_pix ^ bg_pix) & 0x8421)) & 0x8420;
            *fore_pix = (sum - carry) | (carry - (carry >> 5));
         }
         break;
      case BLEND_MODE_OPAQUE:
         break;
   }

}

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
template<int BlendMode, bool MaskEval_TA, bool textured>
static INLINE void PlotPixel(PS_GPU *gpu, int32_t x, int32_t y, uint16_t fore_pix)
{
   // More Y precision bits than GPU RAM installed in (non-arcade, at least) Playstation hardware.
   y &= (512 << gpu->upscale_shift) - 1;

   if(BlendMode >= 0 && (fore_pix & 0x8000))
   {
      // Don't use bg_pix for mask evaluation, it's modified in blending code paths.
      uint16_t bg_pix = vram_fetch(gpu, x, y);
      PlotPixelBlend<BlendMode>(bg_pix, &fore_pix);
   }

   if(!MaskEval_TA || !(vram_fetch(gpu, x, y) & 0x8000))
   {
      if (textured)
         vram_put(gpu, x, y, fore_pix | gpu->MaskSetOR);
      else
         vram_put(gpu, x, y, (fore_pix & 0x7FFF) | gpu->MaskSetOR);
   }
}

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
template<int BlendMode, bool MaskEval_TA, bool textured>
static INLINE void PlotNativePixel(PS_GPU *gpu, int32_t x, int32_t y, uint16_t fore_pix)
{
   uint16_t output;
   y &= 511;	// More Y precision bits than GPU RAM installed in (non-arcade, at least) Playstation hardware.

   if(BlendMode >= 0 && (fore_pix & 0x8000))
   {
      uint16_t bg_pix = texel_fetch(gpu, x, y);	// Don't use bg_pix for mask evaluation, it's modified in blending code paths.
      PlotPixelBlend<BlendMode>(bg_pix, &fore_pix);
   }

   if(!MaskEval_TA || !(texel_fetch(gpu, x, y) & 0x8000))
      texel_put(x, y, (textured ? fore_pix : (fore_pix & 0x7FFF)) | gpu->MaskSetOR);
}

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
template<uint32 TexMode_TA>
static INLINE void Update_CLUT_Cache(PS_GPU *g, uint16 raw_clut)
{
 if(TexMode_TA < 2)
 {
  const uint32 new_ccvb = ((raw_clut & 0x7FFF) | (TexMode_TA << 16));	// Confirmed upper bit of raw_clut is ignored(at least on SCPH-5501's GPU).

  if(g->CLUT_Cache_VB != new_ccvb)
  {
     uint16_t y = (raw_clut >> 6) & 0x1FF;

     //uint16* const gpulp = GPURAM[(raw_clut >> 6) & 0x1FF];
     const uint32 cxo = (raw_clut & 0x3F) << 4;
     const uint32 count = (TexMode_TA ? 256 : 16);

     g->DrawTimeAvail -= count;

     for(unsigned i = 0; i < count; i++)
        {
           uint16_t x = (cxo + i) & 0x3FF;
           g->CLUT_Cache[i] = texel_fetch(g, x, y);
        }

   g->CLUT_Cache_VB = new_ccvb;
  }
 }
}

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
template<uint32_t TexMode_TA>
static INLINE uint16_t GetTexel(PS_GPU *g, int32_t u_arg, int32_t v_arg)
{
#ifdef HAS_CXX11
     static_assert(TexMode_TA <= 2, "TexMode_TA must be <= 2");
#endif

     uint32_t u_ext = ((u_arg & g->SUCV.TWX_AND) + g->SUCV.TWX_ADD);
     uint32_t fbtex_x = ((u_ext >> (2 - TexMode_TA))) & 1023;
     uint32_t fbtex_y = (v_arg & g->SUCV.TWY_AND) + g->SUCV.TWY_ADD;
     uint32_t gro = fbtex_y * 1024U + fbtex_x;

     PS_GPU::TexCache_t *TexCache = &g->TexCache[0];
     PS_GPU::TexCache_t *c = NULL;

     switch(TexMode_TA)
     {
      case 0: c = &TexCache[((gro >> 2) & 0x3) | ((gro >> 8) & 0xFC)]; break;	// 64x64
      case 1: c = &TexCache[((gro >> 2) & 0x7) | ((gro >> 7) & 0xF8)]; break;	// 64x32 (NOT 32x64!)
      case 2: c = &TexCache[((gro >> 2) & 0x7) | ((gro >> 7) & 0xF8)]; break;	// 32x32
     }

     if(MDFN_UNLIKELY(c->Tag != (gro &~ 0x3)))
     {
      // SCPH-1001 old revision GPU is like(for sprites at least): (20 + 4)
      // SCPH-5501 new revision GPU is like(for sprites at least): (12 + 4)
      //
      // We'll be conservative and just go with 4 for now, until we can run some tests with triangles too.
      //
      g->DrawTimeAvail -= 4;

      uint32_t cache_x= fbtex_x & ~3;

      c->Data[0] = texel_fetch(g, cache_x + 0, fbtex_y);
      c->Data[1] = texel_fetch(g, cache_x + 1, fbtex_y);
      c->Data[2] = texel_fetch(g, cache_x + 2, fbtex_y);
      c->Data[3] = texel_fetch(g, cache_x + 3, fbtex_y);
      c->Tag = (gro &~ 0x3);
     }

     uint16 fbw = c->Data[gro & 0x3];

     if(TexMode_TA != 2)
     {
      if(TexMode_TA == 0)
       fbw = (fbw >> ((u_ext & 3) * 4)) & 0xF;
      else
       fbw = (fbw >> ((u_ext & 1) * 8)) & 0xFF;

      fbw = g->CLUT_Cache[fbw];
     }

     return(fbw);
}

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
 * POLY_HELPER_SUB - emit one Command_DrawPolygon template
 * specialisation. Decodes `cv` as detailed above and combines
 * with the row-level `bm` / `tm` / `mam` to produce the full
 * eight-parameter template instantiation. Also threads the PGXP
 * runtime gate via G_Command_DrawPolygon (gpu.cpp).
 */
#define POLY_HELPER_SUB(bm, cv, tm, mam)	\
	 G_Command_DrawPolygon<3 + ((cv & 0x8) >> 3), ((cv & 0x10) >> 4), ((cv & 0x4) >> 2), ((cv & 0x2) >> 1) ? bm : -1, ((cv & 1) ^ 1) & ((cv & 0x4) >> 2), tm, mam >

/*
 * POLY_HELPER_FG - emit one row (8 entries) of the func[][]
 * matrix for a polygon command. The eight slots cover (TexMode,
 * MaskEval) in (slot & 3, slot >> 2) order; for non-textured
 * primitives the TexMode dimension collapses to 0 via the
 * `((cv & 0x4) ? N : 0)` ternary.
 */
#define POLY_HELPER_FG(bm, cv)						\
	 {								\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 0 : 0), 0),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 1 : 0), 0),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0), 0),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0), 0),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 0 : 0), 1),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 1 : 0), 1),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0), 1),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0), 1),	\
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
 */
#define SPR_HELPER_SUB(bm, cv, tm, mam) Command_DrawSprite<(cv >> 3) & 0x3,	((cv & 0x4) >> 2), ((cv & 0x2) >> 1) ? bm : -1, ((cv & 1) ^ 1) & ((cv & 0x4) >> 2), tm, mam>

#define SPR_HELPER_FG(bm, cv)						\
	 {								\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 0 : 0), 0),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 1 : 0), 0),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0), 0),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0), 0),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 0 : 0), 1),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 1 : 0), 1),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0), 1),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0), 1),	\
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
 * never textured, so TexMode/TexMult are absent from the template
 * argument list and all 8 dispatch slots within a row carry the
 * same MaskEval pair (0/0/0/0/1/1/1/1).
 *
 * LINE_HELPER's `len`:
 *   3 + polyline
 *      = command + start vertex + end vertex (2 words each
 *        when gouraud, 1 word otherwise; the rasteriser handles
 *        the gouraud read inline). Polyline mode adds 1 word for
 *        the terminator-checking phase the rasteriser performs.
 *
 *   fifo_fb_len = 1
 */
#define LINE_HELPER_SUB(bm, cv, mam) Command_DrawLine<((cv & 0x08) >> 3), ((cv & 0x10) >> 4), ((cv & 0x2) >> 1) ? bm : -1, mam>

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
