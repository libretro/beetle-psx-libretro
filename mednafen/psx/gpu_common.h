extern enum dither_mode psx_gpu_dither_mode;

/* Return a pixel from VRAM */
#define vram_fetch(gpu, x, y)  ((gpu)->vram[((y) << (10 + (gpu)->upscale_shift)) | (x)])

/* Return a pixel from VRAM, ignoring the internal upscaling */
#define texel_fetch(gpu, x, y) vram_fetch((gpu), (x) << (gpu)->upscale_shift, (y) << (gpu)->upscale_shift)

/* Set a pixel in VRAM */
#define vram_put(gpu, x, y, v) (gpu)->vram[((y) << (10 + (gpu)->upscale_shift)) | (x)] = (v)

#define DitherEnabled(gpu)    (psx_gpu_dither_mode != DITHER_OFF && (gpu)->dtd)

#define UPSCALE(gpu)          (1U << (gpu)->upscale_shift)

template<int BlendMode>
static INLINE void PlotPixelBlend(uint16_t bg_pix, uint16_t *fore_pix)
{
   /*
    * fore_pix - foreground -  the screen
    * bg_pix   - background  - the texture
    */

   uint32_t sum, carry;

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
         bg_pix   &= ~0x8000;
         sum       = *fore_pix + bg_pix;
         carry     = (sum - ((*fore_pix ^ bg_pix) & 0x8421)) & 0x8420;
         *fore_pix = (sum - carry) | (carry - (carry >> 5));
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
         bg_pix   &= ~0x8000;
         *fore_pix = ((*fore_pix >> 2) & 0x1CE7) | 0x8000;
         sum       = *fore_pix + bg_pix;
         carry     = (sum - ((*fore_pix ^ bg_pix) & 0x8421)) & 0x8420;
         *fore_pix = (sum - carry) | (carry - (carry >> 5));
         break;
   }

}

template<int BlendMode, bool textured>
static INLINE void PlotPixel(PS_GPU *gpu, int32_t x, int32_t y, uint16_t fore_pix, bool MaskEval_TA)
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

/// Copy of PlotPixel without internal upscaling, used to draw lines and sprites
template<int BlendMode, bool textured>
static INLINE void PlotNativePixel(PS_GPU *gpu, int32_t x, int32_t y, uint16_t fore_pix, bool MaskEval_TA)
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

#if 0
 TexWindowX_AND = ~(tww << 3);
 TexWindowX_ADD = ((twx & tww) << 3;

 TexWindowY_AND = ~(twh << 3);
 TexWindowY_OR = (twy & twh) << 3;

     uint32_t u = (u_arg & TexWindowX_AND)  TexWindowX_OR;
     uint32_t v = (v_arg & TexWindowY_AND) | TexWindowY_OR;
     uint32_t fbtex_x = TexPageX + (u >> (2 - TexMode_TA));
     uint32_t fbtex_y = TexPageY + v;
     uint16 fbw = GPURAM[fbtex_y][fbtex_x & 1023];

     if(TexMode_TA != 2)
     {
      if(TexMode_TA == 0)
       fbw = (fbw >> ((u & 3) * 4)) & 0xF;
      else
       fbw = (fbw >> ((u & 1) * 8)) & 0xFF;

      fbw = CLUT_Cache[fbw];
     }
#endif

static INLINE void RecalcTexWindowStuff(PS_GPU *g)
{
   uint8_t tww = g->tww;
   uint8_t twh = g->twh;
   uint8_t twx = g->twx;
   uint8_t twy = g->twy;

   g->SUCV.TWX_AND = ~(tww << 3);
   g->SUCV.TWX_ADD = ((twx & tww) << 3) + (g->TexPageX << (2 - std::min<uint32>(2, g->TexMode)));

   g->SUCV.TWY_AND = ~(twh << 3);
   g->SUCV.TWY_ADD = ((twy & twh) << 3) + g->TexPageY;
}

struct TexCache_t
{
      uint16 Data[4];
      uint32 Tag;
};

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

// Command table generation macros follow:

//#define BM_HELPER(fg) { fg(0), fg(1), fg(2), fg(3) }

#define POLY_HELPER_SUB(bm, cv, tm)	\
	 G_Command_DrawPolygon<3 + ((cv & 0x8) >> 3), ((cv & 0x10) >> 4), ((cv & 0x4) >> 2), ((cv & 0x2) >> 1) ? bm : -1, ((cv & 1) ^ 1) & ((cv & 0x4) >> 2), tm>

#define POLY_HELPER_FG(bm, cv)					\
	 {							\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 0 : 0)),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 1 : 0)),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0)),	\
		POLY_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0)),	\
	 }

#define POLY_HELPER(cv)														\
	{ 															\
	 { POLY_HELPER_FG(0, cv), POLY_HELPER_FG(1, cv), POLY_HELPER_FG(2, cv), POLY_HELPER_FG(3, cv) },			\
	 1 + (3 /*+ ((cv & 0x8) >> 3)*/) * ( 1 + ((cv & 0x4) >> 2) + ((cv & 0x10) >> 4) ) - ((cv & 0x10) >> 4),			\
	 1,															\
 	 false															\
	}

#define SPR_HELPER_SUB(bm, cv, tm) Command_DrawSprite<(cv >> 3) & 0x3,	((cv & 0x4) >> 2), ((cv & 0x2) >> 1) ? bm : -1, ((cv & 1) ^ 1) & ((cv & 0x4) >> 2), tm>

#define SPR_HELPER_FG(bm, cv)					\
	 {							\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 0 : 0)),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 1 : 0)),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0)),	\
		SPR_HELPER_SUB(bm, cv, ((cv & 0x4) ? 2 : 0)),	\
	 }


#define SPR_HELPER(cv)												\
	{													\
	 { SPR_HELPER_FG(0, cv), SPR_HELPER_FG(1, cv), SPR_HELPER_FG(2, cv), SPR_HELPER_FG(3, cv) },		\
	 2 + ((cv & 0x4) >> 2) + ((cv & 0x18) ? 0 : 1),								\
	 2 | ((cv & 0x4) >> 2) | ((cv & 0x18) ? 0 : 1),		/* |, not +, for this */			\
	 false													\
	}

#define LINE_HELPER_SUB(bm, cv) Command_DrawLine<((cv & 0x08) >> 3), ((cv & 0x10) >> 4), ((cv & 0x2) >> 1) ? bm : -1>

#define LINE_HELPER_FG(bm, cv)											\
	 {													\
		LINE_HELPER_SUB(bm, cv),									\
		LINE_HELPER_SUB(bm, cv),									\
		LINE_HELPER_SUB(bm, cv),									\
		LINE_HELPER_SUB(bm, cv),									\
	 }

#define LINE_HELPER(cv)												\
	{ 													\
	 { LINE_HELPER_FG(0, cv), LINE_HELPER_FG(1, cv), LINE_HELPER_FG(2, cv), LINE_HELPER_FG(3, cv) },	\
	 3 + ((cv & 0x10) >> 4),										\
	 1,													\
	 false													\
	}

#define OTHER_HELPER_FG(bm, arg_ptr) { arg_ptr, arg_ptr, arg_ptr, arg_ptr }
#define OTHER_HELPER(arg_cs, arg_fbcs, arg_ss, arg_ptr) { { OTHER_HELPER_FG(0, arg_ptr), OTHER_HELPER_FG(1, arg_ptr), OTHER_HELPER_FG(2, arg_ptr), OTHER_HELPER_FG(3, arg_ptr) }, arg_cs, arg_fbcs, arg_ss }
#define OTHER_HELPER_X2(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER(arg_cs, arg_fbcs, arg_ss, arg_ptr)
#define OTHER_HELPER_X4(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER_X2(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER_X2(arg_cs, arg_fbcs, arg_ss, arg_ptr)
#define OTHER_HELPER_X8(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER_X4(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER_X4(arg_cs, arg_fbcs, arg_ss, arg_ptr)
#define OTHER_HELPER_X16(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER_X8(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER_X8(arg_cs, arg_fbcs, arg_ss, arg_ptr)
#define OTHER_HELPER_X32(arg_cs, arg_fbcs, arg_ss, arg_ptr)	OTHER_HELPER_X16(arg_cs, arg_fbcs, arg_ss, arg_ptr), OTHER_HELPER_X16(arg_cs, arg_fbcs, arg_ss, arg_ptr)

#define NULLCMD_FG(bm) { NULL, NULL, NULL, NULL }
#define NULLCMD() { { NULLCMD_FG(0), NULLCMD_FG(1), NULLCMD_FG(2), NULLCMD_FG(3) }, 1, 1, true }
