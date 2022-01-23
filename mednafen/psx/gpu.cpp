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

#include "psx.h"
#include "timer.h"
#include "FastFIFO.h"

#include "../math_ops.h"
#include "../state_helpers.h"
#include "../../rsx/rsx_intf.h"

#include "../pgxp/pgxp_main.h"
#include "../pgxp/pgxp_gpu.h"
#include "../pgxp/pgxp_mem.h"

#include "gpu_common.h"

#include "gpu_polygon.cpp"
#include "gpu_sprite.cpp"
#include "gpu_line.cpp"

extern int crop_overscan;
extern bool is_monkey_hero;

/*
   GPU display timing master clock is nominally 53.693182 MHz for NTSC PlayStations, and 53.203425 MHz for PAL PlayStations.

   Non-interlaced NTSC mode line timing notes(real-world times calculated via PS1 timer and math with nominal CPU clock value):

   263 lines per frame

   ~16714.85 us per frame, average.
   ~63.55456 us per line, average.

   Multiplying the results of counter 0 in pixel clock mode by the clock divider of the current dot clock mode/width gives a result that's slightly less
   than expected; the dot clock divider is probably being reset each scanline.

   Non-interlaced PAL mode(but with an NTSC source clock in an NTSC PS1; calculated same way as NTSC values):

   314 lines per frame

   ~19912.27 us per frame, average.
   ~63.41486 us per line, average.

   FB X and Y display positions can be changed during active display; and Y display position appears to be treated as an offset to the current Y readout
   position that gets reset around vblank time.

*/

extern bool fast_pal;

/*
   November 29, 2012 notes:

   PAL mode can be turned on, and then off again, mid-frame(creates a neat effect).

   Pixel clock can be changed mid-frame with effect(the effect is either instantaneous, or cached at some point in the scanline, not tested to see which);
   interestingly, alignment is off on a PS1 when going 5MHz->10MHz>5MHz with a grid image.

   Vertical start and end can be changed during active display, with effect(though it needs to be vs0->ve0->vs1->ve1->..., vs0->vs1->ve0 doesn't apparently do anything
   different from vs0->ve0.
   */
extern int32 EventCycles;

static const int8 dither_table[4][4] =
{
   { -4,  0, -3,  1 },
   {  2, -2,  3, -1 },
   { -3,  1, -4,  0 },
   {  3, -1,  2, -2 },
};

static FastFIFO<uint32, 0x20> GPU_BlitterFIFO; // 0x10 on an actual PS1 GPU, 0x20 here (see comment at top of gpu.h)

struct CTEntry
{
   void (*func[4][8])(PS_GPU* g, const uint32 *cb);
   uint8_t len;
   uint8_t fifo_fb_len;
   bool ss_cmd;
};

PS_GPU GPU;

/* Buffers used to hold data during upscale operations */
uint32 TexCache_Tag[256];
uint16 TexCache_Data[256][4];
uint16 *vram_new = NULL;

static INLINE void InvalidateTexCache(PS_GPU *gpu)
{
   unsigned i;
   for (i = 0; i < 256; i++)
      gpu->TexCache[i].Tag = ~0U;
}

static INLINE void InvalidateCache(PS_GPU *gpu)
{
   gpu->CLUT_Cache_VB = ~0U;
   InvalidateTexCache(gpu);
}

static void SetTPage(PS_GPU *gpu, const uint32_t cmdw)
{
   const unsigned NewTexPageX = (cmdw & 0xF) * 64;
   const unsigned NewTexPageY = (cmdw & 0x10) * 16;
   const unsigned NewTexMode  = (cmdw >> 7) & 0x3;

   gpu->abr = (cmdw >> 5) & 0x3;

   if(!NewTexMode != !gpu->TexMode || NewTexPageX != gpu->TexPageX || NewTexPageY != gpu->TexPageY)
      InvalidateTexCache(gpu);

   if(gpu->TexDisableAllowChange)
   {
      bool NewTexDisable = (cmdw >> 11) & 1;

      if (NewTexDisable != gpu->TexDisable)
         InvalidateTexCache(gpu);

      gpu->TexDisable = NewTexDisable;
   }

   gpu->TexPageX = NewTexPageX;
   gpu->TexPageY = NewTexPageY;
   gpu->TexMode  = NewTexMode;

   RecalcTexWindowStuff(gpu);
}

/* C-style function wrappers so our command table isn't so ginormous(in memory usage). */
template<int numvertices, bool shaded, bool textured,
    int BlendMode, bool TexMult, uint32 TexMode_TA, bool MaskEval_TA>
static void G_Command_DrawPolygon(PS_GPU* g, const uint32 *cb)
{
  if (PGXP_enabled())
    Command_DrawPolygon<numvertices, shaded, textured,
            BlendMode, TexMult, TexMode_TA, MaskEval_TA, true>(g, cb);
  else
    Command_DrawPolygon<numvertices, shaded, textured,
            BlendMode, TexMult, TexMode_TA, MaskEval_TA, false>(g, cb);
}


static void Command_ClearCache(PS_GPU* g, const uint32 *cb)
{
   InvalidateCache(g);
}

static void Command_IRQ(PS_GPU* g, const uint32 *cb)
{
   g->IRQPending = true;
   IRQ_Assert(IRQ_GPU, g->IRQPending);
}

// Special RAM write mode(16 pixels at a time),
// does *not* appear to use mask drawing environment settings.
static void Command_FBFill(PS_GPU* gpu, const uint32 *cb)
{
   unsigned y;
   int32_t r                 = cb[0] & 0xFF;
   int32_t g                 = (cb[0] >> 8) & 0xFF;
   int32_t b                 = (cb[0] >> 16) & 0xFF;
   const uint16_t fill_value = ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10);
   int32_t destX             = (cb[1] >>  0) & 0x3F0;
   int32_t destY             = (cb[1] >> 16) & 0x3FF;
   int32_t width             = (((cb[2] >> 0) & 0x3FF) + 0xF) & ~0xF;
   int32_t height            = (cb[2] >> 16) & 0x1FF;

   gpu->DrawTimeAvail       -= 46; // Approximate

   for(y = 0; y < height; y++)
   {
      unsigned x;
      const int32 d_y = (y + destY) & 511;

      if(LineSkipTest(gpu, d_y))
         continue;

      gpu->DrawTimeAvail -= (width >> 3) + 9;

      for(x = 0; x < width; x++)
      {
         const int32 d_x = (x + destX) & 1023;

         texel_put(d_x, d_y, fill_value);
      }
   }

   rsx_intf_fill_rect(cb[0], destX, destY, width, height);
}

static void Command_FBCopy(PS_GPU* g, const uint32 *cb)
{
   unsigned y;
   int32_t sourceX = (cb[1] >> 0) & 0x3FF;
   int32_t sourceY = (cb[1] >> 16) & 0x3FF;
   int32_t destX   = (cb[2] >> 0) & 0x3FF;
   int32_t destY   = (cb[2] >> 16) & 0x3FF;
   int32_t width   = (cb[3] >> 0) & 0x3FF;
   int32_t height  = (cb[3] >> 16) & 0x1FF;

   if(!width)
      width = 0x400;

   if(!height)
      height = 0x200;

   InvalidateTexCache(g);

   g->DrawTimeAvail -= (width * height) * 2;

   for(y = 0; y < height; y++)
   {
      unsigned x;

      for(x = 0; x < width; x += 128)
      {
         const int32 chunk_x_max = std::min<int32>(width - x, 128);
         uint16 tmpbuf[128]; // TODO: Check and see if the GPU is actually (ab)using the CLUT or texture cache.

         for(int32 chunk_x = 0; chunk_x < chunk_x_max; chunk_x++)
         {
            int32 s_y = (y + sourceY) & 511;
            int32 s_x = (x + chunk_x + sourceX) & 1023;

            // XXX make upscaling-friendly, as it is we copy at 1x
            tmpbuf[chunk_x] = texel_fetch(g, s_x, s_y);
         }

         for(int32 chunk_x = 0; chunk_x < chunk_x_max; chunk_x++)
         {
            int32 d_y = (y + destY) & 511;
            int32 d_x = (x + chunk_x + destX) & 1023;

            if(!(texel_fetch(g, d_x, d_y) & g->MaskEvalAND))
               texel_put(d_x, d_y, tmpbuf[chunk_x] | g->MaskSetOR);
         }
      }
   }

   rsx_intf_copy_rect(sourceX, sourceY, destX, destY, width, height, g->MaskEvalAND, g->MaskSetOR);
}

static void Command_FBWrite(PS_GPU* g, const uint32 *cb)
{
   //assert(InCmd == INCMD_NONE);

   g->FBRW_X = (cb[1] >>  0) & 0x3FF;
   g->FBRW_Y = (cb[1] >> 16) & 0x3FF;

   g->FBRW_W = (cb[2] >>  0) & 0x3FF;
   g->FBRW_H = (cb[2] >> 16) & 0x1FF;

   if(!g->FBRW_W)
      g->FBRW_W = 0x400;

   if(!g->FBRW_H)
      g->FBRW_H = 0x200;

   g->FBRW_CurX = g->FBRW_X;
   g->FBRW_CurY = g->FBRW_Y;

   InvalidateTexCache(g);

   if(g->FBRW_W != 0 && g->FBRW_H != 0)
      g->InCmd = INCMD_FBWRITE;
}

/* FBRead: PS1 GPU in SCPH-5501 gives odd, inconsistent results when
 * raw_height == 0, or raw_height != 0x200 && (raw_height & 0x1FF) == 0
 */

static void Command_FBRead(PS_GPU* g, const uint32 *cb)
{
   //assert(g->InCmd == INCMD_NONE);

   g->FBRW_X = (cb[1] >>  0) & 0x3FF;
   g->FBRW_Y = (cb[1] >> 16) & 0x3FF;

   g->FBRW_W = (cb[2] >>  0) & 0x3FF;
   g->FBRW_H = (cb[2] >> 16) & 0x3FF;

   if(!g->FBRW_W)
      g->FBRW_W = 0x400;

   if(g->FBRW_H > 0x200)
      g->FBRW_H &= 0x1FF;

   g->FBRW_CurX = g->FBRW_X;
   g->FBRW_CurY = g->FBRW_Y;

   InvalidateTexCache(g);

   if(g->FBRW_W != 0 && g->FBRW_H != 0)
      g->InCmd = INCMD_FBREAD;

   if (!rsx_intf_has_software_renderer())
   {
       /* Need a hard readback from GPU renderer. */
       bool supported = rsx_intf_read_vram(
               g->FBRW_X, g->FBRW_Y,
               g->FBRW_W, g->FBRW_H,
               g->vram);
   }
}

static void Command_DrawMode(PS_GPU* g, const uint32 *cb)
{
   const uint32 cmdw = *cb;

   SetTPage(g, cmdw);

   g->SpriteFlip = (cmdw & 0x3000);
   g->dtd =        (cmdw >> 9) & 1;
   g->dfe =        (cmdw >> 10) & 1;

   if (g->dfe)
      GPU.display_possibly_dirty = true;
}

static void Command_TexWindow(PS_GPU* g, const uint32 *cb)
{
   g->tww = (*cb & 0x1F);
   g->twh = ((*cb >> 5) & 0x1F);
   g->twx = ((*cb >> 10) & 0x1F);
   g->twy = ((*cb >> 15) & 0x1F);

   RecalcTexWindowStuff(g);
   rsx_intf_set_tex_window(g->tww, g->twh, g->twx, g->twy);
}

static void Command_Clip0(PS_GPU* g, const uint32 *cb)
{
   g->ClipX0 = *cb & 1023;
   g->ClipY0 = (*cb >> 10) & 1023;
   rsx_intf_set_draw_area(g->ClipX0, g->ClipY0,
           g->ClipX1, g->ClipY1);
}

static void Command_Clip1(PS_GPU* g, const uint32 *cb)
{
   g->ClipX1 = *cb & 1023;
   g->ClipY1 = (*cb >> 10) & 1023;
   rsx_intf_set_draw_area(g->ClipX0, g->ClipY0,
         g->ClipX1, g->ClipY1);
}

static void Command_DrawingOffset(PS_GPU* g, const uint32 *cb)
{
   g->OffsX = sign_x_to_s32(11, (*cb & 2047));
   g->OffsY = sign_x_to_s32(11, ((*cb >> 11) & 2047));
}

static void Command_MaskSetting(PS_GPU* g, const uint32 *cb)
{
   g->MaskSetOR   = (*cb & 1) ? 0x8000 : 0x0000;
   g->MaskEvalAND = (*cb & 2) ? 0x8000 : 0x0000;

   rsx_intf_set_mask_setting(g->MaskSetOR, g->MaskEvalAND);
}


static CTEntry Commands[256] =
{
   /* 0x00 */
   NULLCMD(),
   OTHER_HELPER(1, 2, false, Command_ClearCache),
   OTHER_HELPER(3, 3, false, Command_FBFill),

   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

   /* 0x10 */
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

   OTHER_HELPER(1, 1, false,  Command_IRQ),

   /* 0x20 */
   POLY_HELPER(0x20),
   POLY_HELPER(0x21),
   POLY_HELPER(0x22),
   POLY_HELPER(0x23),
   POLY_HELPER(0x24),
   POLY_HELPER(0x25),
   POLY_HELPER(0x26),
   POLY_HELPER(0x27),
   POLY_HELPER(0x28),
   POLY_HELPER(0x29),
   POLY_HELPER(0x2a),
   POLY_HELPER(0x2b),
   POLY_HELPER(0x2c),
   POLY_HELPER(0x2d),
   POLY_HELPER(0x2e),
   POLY_HELPER(0x2f),
   POLY_HELPER(0x30),
   POLY_HELPER(0x31),
   POLY_HELPER(0x32),
   POLY_HELPER(0x33),
   POLY_HELPER(0x34),
   POLY_HELPER(0x35),
   POLY_HELPER(0x36),
   POLY_HELPER(0x37),
   POLY_HELPER(0x38),
   POLY_HELPER(0x39),
   POLY_HELPER(0x3a),
   POLY_HELPER(0x3b),
   POLY_HELPER(0x3c),
   POLY_HELPER(0x3d),
   POLY_HELPER(0x3e),
   POLY_HELPER(0x3f),

   LINE_HELPER(0x40),
   LINE_HELPER(0x41),
   LINE_HELPER(0x42),
   LINE_HELPER(0x43),
   LINE_HELPER(0x44),
   LINE_HELPER(0x45),
   LINE_HELPER(0x46),
   LINE_HELPER(0x47),
   LINE_HELPER(0x48),
   LINE_HELPER(0x49),
   LINE_HELPER(0x4a),
   LINE_HELPER(0x4b),
   LINE_HELPER(0x4c),
   LINE_HELPER(0x4d),
   LINE_HELPER(0x4e),
   LINE_HELPER(0x4f),
   LINE_HELPER(0x50),
   LINE_HELPER(0x51),
   LINE_HELPER(0x52),
   LINE_HELPER(0x53),
   LINE_HELPER(0x54),
   LINE_HELPER(0x55),
   LINE_HELPER(0x56),
   LINE_HELPER(0x57),
   LINE_HELPER(0x58),
   LINE_HELPER(0x59),
   LINE_HELPER(0x5a),
   LINE_HELPER(0x5b),
   LINE_HELPER(0x5c),
   LINE_HELPER(0x5d),
   LINE_HELPER(0x5e),
   LINE_HELPER(0x5f),

   SPR_HELPER(0x60),
   SPR_HELPER(0x61),
   SPR_HELPER(0x62),
   SPR_HELPER(0x63),
   SPR_HELPER(0x64),
   SPR_HELPER(0x65),
   SPR_HELPER(0x66),
   SPR_HELPER(0x67),
   SPR_HELPER(0x68),
   SPR_HELPER(0x69),
   SPR_HELPER(0x6a),
   SPR_HELPER(0x6b),
   SPR_HELPER(0x6c),
   SPR_HELPER(0x6d),
   SPR_HELPER(0x6e),
   SPR_HELPER(0x6f),
   SPR_HELPER(0x70),
   SPR_HELPER(0x71),
   SPR_HELPER(0x72),
   SPR_HELPER(0x73),
   SPR_HELPER(0x74),
   SPR_HELPER(0x75),
   SPR_HELPER(0x76),
   SPR_HELPER(0x77),
   SPR_HELPER(0x78),
   SPR_HELPER(0x79),
   SPR_HELPER(0x7a),
   SPR_HELPER(0x7b),
   SPR_HELPER(0x7c),
   SPR_HELPER(0x7d),
   SPR_HELPER(0x7e),
   SPR_HELPER(0x7f),

   /* 0x80 ... 0x9F */
   OTHER_HELPER_X32(4, 2, false, Command_FBCopy),

   /* 0xA0 ... 0xBF */
   OTHER_HELPER_X32(3, 2, false, Command_FBWrite),

   /* 0xC0 ... 0xDF */
   OTHER_HELPER_X32(3, 2, false, Command_FBRead),

   /* 0xE0 */

   NULLCMD(),
   OTHER_HELPER(1, 2, false, Command_DrawMode),
   OTHER_HELPER(1, 2, false, Command_TexWindow),
   OTHER_HELPER(1, 1, true,  Command_Clip0),
   OTHER_HELPER(1, 1, true,  Command_Clip1),
   OTHER_HELPER(1, 1, true,  Command_DrawingOffset),
   OTHER_HELPER(1, 2, false, Command_MaskSetting),

   NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

   /* 0xF0 */
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

};

static INLINE bool CalcFIFOReadyBit(void)
{
   uint32_t ctcommand = (GPU_BlitterFIFO.Peek() >> 24);

   if(GPU.InCmd & (INCMD_PLINE | INCMD_QUAD))
      return(false);

   if(GPU_BlitterFIFO.in_count == 0)
      return(true);

   if(GPU.InCmd & (INCMD_FBREAD | INCMD_FBWRITE))
      return(false);

   // Change fifo_fb_len from 2 to 3 for Command_FBWrite when running Monkey Hero.
   if(GPU_BlitterFIFO.in_count >= Commands[GPU_BlitterFIFO.Peek() >> 24].fifo_fb_len + ((ctcommand >= 0xA0) && (ctcommand <= 0xBF) && is_monkey_hero ? 1 : 0))
      return(false);

   return(true);
}

static void RSX_UpdateDisplayMode(void)
{
   bool depth_24bpp = !!(GPU.DisplayMode & 0x10);

   //uint16_t yres = GPU.VertEnd - GPU.VertStart;

   bool is_pal_mode = false;
   if((GPU.DisplayMode & DISP_PAL) == DISP_PAL)
      is_pal_mode = true;

   // Both 2nd bit and 5th bit have to be enabled to use interlacing properly.
   bool is_480i_mode = false;
   if((GPU.DisplayMode & (DISP_INTERLACED | DISP_VERT480)) == (DISP_INTERLACED | DISP_VERT480))
   {
      //yres *= 2;
      is_480i_mode = true;
   }

   //unsigned pixelclock_divider;

   enum width_modes curr_width_mode;

   if ((GPU.DisplayMode >> 6) & 1)
   {
      // HRes ~ 368pixels
      //pixelclock_divider = 7;
      curr_width_mode = WIDTH_MODE_368;
   }
   else
   {
      switch (GPU.DisplayMode & 3)
      {
         case 0:
            // Hres ~ 256pixels
            //pixelclock_divider = 10;
            curr_width_mode = WIDTH_MODE_256;
            break;
         case 1:
            // Hres ~ 320pixels
            //pixelclock_divider = 8;
            curr_width_mode = WIDTH_MODE_320;
            break;
         case 2:
            // Hres ~ 512pixels
            //pixelclock_divider = 5;
            curr_width_mode = WIDTH_MODE_512;
            break;
         default:
            // Hres ~ 640pixels
            //pixelclock_divider = 4;
            curr_width_mode = WIDTH_MODE_640;
            break;
      }
   }

#if 0
   // First we get the horizontal range in number of pixel clock period
   uint16_t xres = (GPU.HorizEnd - GPU.HorizStart);

   // Then we apply the divider
   xres /= pixelclock_divider;

   // Then the rounding formula straight outta No$
   xres = (xres + 2) & ~3;
#endif

   rsx_intf_set_display_mode(
         depth_24bpp,
         is_pal_mode, 
         is_480i_mode,
         curr_width_mode);
}

/* Forward decls */
void GPU_RestoreStateP1(bool);
void GPU_RestoreStateP2(bool);
void GPU_RestoreStateP3();

/* Return a ptr to memory with enough space
 * for the VRAM, taking upscaling into account */
static uint16_t *VRAM_Alloc(uint8 upscale_shift)
{
   unsigned width  = 1024 << upscale_shift;
   unsigned height =  512 << upscale_shift;
   unsigned size   = width * height;

   uint16_t *vram    = new uint16_t[size];
   memset(vram, 0, size * sizeof(*vram));

   return vram;
}

void GPU_Init(bool pal_clock_and_tv,
      int sls, int sle, uint8 upscale_shift)
{
   
   GPU.vram = VRAM_Alloc(upscale_shift);

   int x, y, v;

   GPU.HardwarePALType = pal_clock_and_tv;

   for(y = 0; y < 4; y++)
   {
      for(x = 0; x < 4; x++)
      {
         for(v = 0; v < 512; v++)
         {
            int value = v;

            value += dither_table[y][x];

            value >>= 3;

            if(value < 0)
               value = 0;

            if(value > 0x1F)
               value = 0x1F;

            GPU.DitherLUT[y][x][v] = value;
         }
      }
   }

   GPU_RecalcClockRatio();

   memset(GPU.RGB8SAT_Under, 0, sizeof(GPU.RGB8SAT_Under));

   for(int i = 0; i < 256; i++)
      GPU.RGB8SAT[i] = i;

   memset(GPU.RGB8SAT_Over, 0xFF, sizeof(GPU.RGB8SAT_Over));

   GPU.LineVisFirst = sls;
   GPU.LineVisLast = sle;

   GPU.display_possibly_dirty = false;
   GPU.display_change_count = 0;

   GPU.upscale_shift = upscale_shift;
   GPU.dither_upscale_shift = 0;

   GPU.killQuadPart = 0;
}

void GPU_RecalcClockRatio(void) {
   if(GPU.HardwarePALType == false)  // NTSC clock
      GPU.GPUClockRatio = 103896; // 65536 * 53693181.818 / (44100 * 768)
   else  // PAL clock
      GPU.GPUClockRatio = 102948; // 65536 * 53203425 / (44100 * 768)

   overclock_cpu_to_device(GPU.GPUClockRatio);
}

void GPU_Destroy(void)
{
   delete [] GPU.vram;
}

/* Rescale the GPU with a different upscale_shift 
 * 
 * We copy, if necessary, the current VRAM (GPU.vram) at 1x
 * to a buffer (vram_new). 
 * We allocate enough space for the rescaled VRAM 
 * and copy the buffer to it, taking the upscale factor into account 
 * 
 */
void GPU_Rescale(uint8 ushift)
{
   if (GPU.upscale_shift == 0) 
   {
      /* VRAM is already at 1x, make the buffer point to the old VRAM
       * to avoid copying it */
      vram_new = GPU.vram;  
   }

   else
   {
      /* Copy current VRAM to temp buffer at 1x */
      vram_new = VRAM_Alloc(0);

      for (unsigned y = 0; y < 512; y++)
      {
         for (unsigned x = 0; x < 1024; x++)
            vram_new[y * 1024 + x] = texel_fetch(&GPU, x, y);
      }

      /* Cleanup the old VRAM */
      delete [] GPU.vram;
   }

   GPU.vram = NULL;

   /* Change the state of the upscale shift right now
    * or else texel_put won't use the new scaling factor
    * resulting in corrupted VRAM */
   GPU_set_upscale_shift(ushift);
   
   GPU.vram = VRAM_Alloc(ushift);

   /* Copy the temp buffer to the rescaled VRAM, taking the
    * upscale factor into account (nearest neighbor upscaling) */
   for (unsigned y = 0; y < 512; y++)
   {
      for (unsigned x = 0; x < 1024; x++)
         texel_put(x, y, vram_new[y * 1024 + x]);
   }

   /* Cleanup the temporary buffer */
   if (vram_new)
      delete [] vram_new;
   vram_new = NULL;
}

void GPU_FillVideoParams(MDFNGI* gi)
{
   if(GPU.HardwarePALType)
   {
      gi->lcm_width = 2800;
      gi->lcm_height = (GPU.LineVisLast + 1 - GPU.LineVisFirst) * 2; //576;

      gi->nominal_width = 384;   // Dunno. :(
      gi->nominal_height = GPU.LineVisLast + 1 - GPU.LineVisFirst; //288;

      gi->fb_width = 768;
      gi->fb_height = 576;

      gi->fps = 836203078; // 49.842

      gi->VideoSystem = VIDSYS_PAL;
   }
   else
   {
      gi->lcm_width = 2800;
      gi->lcm_height = (GPU.LineVisLast + 1 - GPU.LineVisFirst) * 2; //480;

      gi->nominal_width = 320;
      gi->nominal_height = GPU.LineVisLast + 1 - GPU.LineVisFirst; //240;

      gi->fb_width = 768;
      gi->fb_height = 480;

      gi->fps = 1005643085; // 59.941

      gi->VideoSystem = VIDSYS_NTSC;
   }

   //
   // For Justifier and Guncon.
   //
   gi->mouse_scale_x = (float)gi->lcm_width / gi->nominal_width;
   gi->mouse_offs_x = (float)(2800 - gi->lcm_width) / 2;

   gi->mouse_scale_y = 1.0;
   gi->mouse_offs_y = GPU.LineVisFirst;
}

void GPU_SoftReset(void) // Control command 0x00
{
   GPU.IRQPending = false;
   IRQ_Assert(IRQ_GPU, GPU.IRQPending);

   InvalidateCache(&GPU);
   GPU.DMAControl = 0;

   if(GPU.DrawTimeAvail < 0)
      GPU.DrawTimeAvail = 0;

   GPU_BlitterFIFO.Flush();
   GPU.DataReadBufferEx = 0;
   GPU.InCmd = INCMD_NONE;

   GPU.DisplayOff = 1;
   GPU.DisplayFB_XStart = 0;
   GPU.DisplayFB_YStart = 0;

   GPU.DisplayMode = 0;

   GPU.HorizStart = 0x200;
   GPU.HorizEnd = 0xC00;

   GPU.VertStart = 0x10;
   GPU.VertEnd = 0x100;


   //
   GPU.TexPageX = 0;
   GPU.TexPageY = 0;

   GPU.SpriteFlip = 0;

   GPU.abr = 0;
   GPU.TexMode = 0;

   GPU.dtd = 0;
   GPU.dfe = 0;

   //
   GPU.tww = 0;
   GPU.twh = 0;
   GPU.twx = 0;
   GPU.twy = 0;

   RecalcTexWindowStuff(&GPU);

   //
   GPU.ClipX0 = 0;
   GPU.ClipY0 = 0;

   //
   GPU.ClipX1 = 0;
   GPU.ClipY1 = 0;

   //
   GPU.OffsX = 0;
   GPU.OffsY = 0;

   //
   GPU.MaskSetOR = 0;
   GPU.MaskEvalAND = 0;

   GPU.TexDisable = false;
   GPU.TexDisableAllowChange = false;
}

void GPU_Power(void)
{
   memset(GPU.vram, 0, 512 * 1024 * UPSCALE(&GPU) * UPSCALE(&GPU) * sizeof(*GPU.vram));

   memset(GPU.CLUT_Cache, 0, sizeof(GPU.CLUT_Cache));
   GPU.CLUT_Cache_VB = ~0U;

   memset(GPU.TexCache, 0xFF, sizeof(GPU.TexCache));

   GPU.DMAControl    = 0;
   GPU.ClipX0        = 0;
   GPU.ClipY0        = 0;
   GPU.ClipX1        = 0;
   GPU.ClipY1        = 0;

   GPU.OffsX         = 0;
   GPU.OffsY         = 0;

   GPU.dtd           = false;
   GPU.dfe           = false;

   GPU.MaskSetOR     = 0;
   GPU.MaskEvalAND   = 0;

   GPU.TexDisable            = false;
   GPU.TexDisableAllowChange = false;

   GPU.tww = 0;
   GPU.twh = 0;
   GPU.twx = 0;
   GPU.twy = 0;

   RecalcTexWindowStuff(&GPU);

   GPU.TexPageX = 0;
   GPU.TexPageY = 0;
   GPU.SpriteFlip = 0;

   GPU.abr = 0;
   GPU.TexMode = 0;

   GPU_BlitterFIFO.Flush();

   GPU.DataReadBuffer = 0; // Don't reset in SoftReset()
   GPU.DataReadBufferEx = 0;
   GPU.InCmd = INCMD_NONE;
   GPU.FBRW_X = 0;
   GPU.FBRW_Y = 0;
   GPU.FBRW_W = 0;
   GPU.FBRW_H = 0;
   GPU.FBRW_CurY = 0;
   GPU.FBRW_CurX = 0;

   GPU.DisplayMode = 0;
   GPU.DisplayOff = 1;
   GPU.DisplayFB_XStart = 0;
   GPU.DisplayFB_YStart = 0;

   GPU.HorizStart = 0;
   GPU.HorizEnd = 0;

   GPU.VertStart = 0;
   GPU.VertEnd = 0;

   //
   //
   //
   GPU.DisplayFB_CurYOffset = 0;
   GPU.DisplayFB_CurLineYReadout = 0;
   GPU.InVBlank = true;

   // TODO: factor out in a separate function.
   GPU.LinesPerField = 263;

   //
   //
   //
   GPU.scanline = 0;
   GPU.field = 0;
   GPU.field_ram_readout = 0;
   GPU.PhaseChange = 0;

   //
   //
   //
   GPU.DotClockCounter = 0;
   GPU.GPUClockCounter = 0;
   GPU.LineClockCounter = 3412 - 200;
   GPU.LinePhase = 0;

   GPU.DrawTimeAvail = 0;

   GPU.lastts = 0;

   GPU_SoftReset();

   IRQ_Assert(IRQ_VBLANK, GPU.InVBlank);
   TIMER_SetVBlank(GPU.InVBlank);
}

void GPU_ResetTS(void)
{
   GPU.lastts = 0;
}


static void ProcessFIFO(uint32_t in_count)
{
   uint32_t CB[0x10], InData;
   unsigned i;
   unsigned command_len;
   uint32_t cc            = GPU.InCmd_CC;
   const CTEntry *command = &Commands[cc];
   bool read_fifo         = false;
   bool sw                = rsx_intf_has_software_renderer();

   switch (GPU.InCmd)
   {
      default:
      case INCMD_NONE:
         break;
      case INCMD_FBWRITE:
         InData = GPU_BlitterFIFO.Read();

         for(i = 0; i < 2; i++)
         {
            /* Cannot rely on mask bit if we don't have SW renderer, HW renderer will
             * perform masking. */
            bool fetch = false;
            if (sw)
                fetch = texel_fetch(&GPU, GPU.FBRW_CurX & 1023, GPU.FBRW_CurY & 511) & GPU.MaskEvalAND;

            if (!fetch)
               texel_put(GPU.FBRW_CurX & 1023, GPU.FBRW_CurY & 511, InData | GPU.MaskSetOR);

            GPU.FBRW_CurX++;
            if(GPU.FBRW_CurX == (GPU.FBRW_X + GPU.FBRW_W))
            {
               GPU.FBRW_CurX = GPU.FBRW_X;
               GPU.FBRW_CurY++;
               if(GPU.FBRW_CurY == (GPU.FBRW_Y + GPU.FBRW_H))
               {
                  /* Upload complete, send over to RSX */
                  rsx_intf_load_image(
                        GPU.FBRW_X, GPU.FBRW_Y,
                        GPU.FBRW_W, GPU.FBRW_H,
                        GPU.vram,
                        GPU.MaskEvalAND,
                        GPU.MaskSetOR);
                  GPU.InCmd = INCMD_NONE;
                  break;   // Break out of the for() loop.
               }
            }
            InData >>= 16;
         }
         return;

      case INCMD_QUAD:
         if(GPU.DrawTimeAvail < 0)
            return;

         command_len      = 1 + (bool)(cc & 0x4) + (bool)(cc & 0x10);
         read_fifo = true;
         break;
      case INCMD_PLINE:
         if(GPU.DrawTimeAvail < 0)
            return;

         command_len        = 1 + (bool)(GPU.InCmd_CC & 0x10);

         if((GPU_BlitterFIFO.Peek() & 0xF000F000) == 0x50005000)
         {
            GPU_BlitterFIFO.Read();
            GPU.InCmd = INCMD_NONE;
            return;
         }

         read_fifo = true;
         break;
   }

   if (!read_fifo)
   {
      cc          = GPU_BlitterFIFO.Peek() >> 24;
      command     = &Commands[cc];
      command_len = command->len;

      if(GPU.DrawTimeAvail < 0 && !command->ss_cmd)
         return;
   }

   if(in_count < command_len)
      return;

   for (i = 0; i < command_len; i++)
   {
      if(PGXP_enabled())
         PGXP_WriteCB(PGXP_ReadFIFO(GPU_BlitterFIFO.read_pos), i);
      CB[i] = GPU_BlitterFIFO.Read();
   }

   if (!read_fifo)
   {
      if(!command->ss_cmd)
         GPU.DrawTimeAvail -= 2;

      // A very very ugly kludge to support
      // texture mode specialization.
      // fixme/cleanup/SOMETHING in the future.
      
      /* Don't alter SpriteFlip here. */
      if(cc >= 0x20 && cc <= 0x3F && (cc & 0x4))
         SetTPage(&GPU, CB[4 + ((cc >> 4) & 0x1)] >> 16);
   }

   if ((cc >= 0x80) && (cc <= 0x9F))
      Command_FBCopy(&GPU, CB);
   else if ((cc >= 0xA0) && (cc <= 0xBF))
      Command_FBWrite(&GPU, CB);
   else if ((cc >= 0xC0) && (cc <= 0xDF))
      Command_FBRead(&GPU, CB);
   else
   {
      if (command->func[GPU.abr][GPU.TexMode])
         command->func[GPU.abr][GPU.TexMode | (GPU.MaskEvalAND ? 0x4 : 0x0)](&GPU, CB);
   }
}

static INLINE void GPU_WriteCB(uint32_t InData, uint32_t addr)
{
   if(GPU_BlitterFIFO.in_count >= 0x10
      && (GPU.InCmd != INCMD_NONE || 
      (GPU_BlitterFIFO.in_count - 0x10) >= Commands[GPU_BlitterFIFO.Peek() >> 24].fifo_fb_len))
      return;

   if(PGXP_enabled())
      PGXP_WriteFIFO(ReadMem(addr), GPU_BlitterFIFO.write_pos);
   GPU_BlitterFIFO.Write(InData);

   if(GPU_BlitterFIFO.in_count && GPU.InCmd != INCMD_FBREAD)
      ProcessFIFO(GPU_BlitterFIFO.in_count);
}

void GPU_Write(const int32_t timestamp, uint32_t A, uint32_t V)
{
   V <<= (A & 3) * 8;

   if(A & 4)   // GP1 ("Control")
   {
      uint32_t command = V >> 24;

      V &= 0x00FFFFFF;

      //PSX_WARNING("[GPU] Control command: %02x %06x %d", command, V, scanline);

      switch(command)
      {
         /*
            0x40-0xFF do NOT appear to be mirrors, at least not on my PS1's GPU.
            */
         default:
            PSX_WARNING("[GPU] Unknown control command %02x - %06x", command, V);
            break;
         case 0x00:  // Reset GPU
            GPU_SoftReset();
            rsx_intf_set_draw_area(GPU.ClipX0, GPU.ClipY0,
                                   GPU.ClipX1, GPU.ClipY1);
            rsx_intf_toggle_display(GPU.DisplayOff); // `true` set by GPU_SoftReset()
            rsx_intf_set_vram_framebuffer_coords(GPU.DisplayFB_XStart, GPU.DisplayFB_YStart); // (0, 0) set by GPU_SoftReset()
            rsx_intf_set_horizontal_display_range(GPU.HorizStart, GPU.HorizEnd); // 0x200, 0xC00 set by GPU_SoftReset()
            rsx_intf_set_vertical_display_range(GPU.VertStart, GPU.VertEnd); // 0x10, 0x100 set by GPU_SoftReset()
            RSX_UpdateDisplayMode();
            break;

         case 0x01:  // Reset command buffer
            if(GPU.DrawTimeAvail < 0)
               GPU.DrawTimeAvail = 0;
            GPU_BlitterFIFO.Flush();
            GPU.InCmd = INCMD_NONE;
            break;

         case 0x02:  // Acknowledge IRQ
            GPU.IRQPending = false;
            IRQ_Assert(IRQ_GPU, GPU.IRQPending);
            break;

         case 0x03:  // Display enable
            GPU.DisplayOff = V & 1;
            rsx_intf_toggle_display(GPU.DisplayOff);
            break;

         case 0x04:  // DMA Setup
            GPU.DMAControl = V & 0x3;
            break;

         case 0x05:  // Start of display area in framebuffer
            GPU.DisplayFB_XStart = V & 0x3FE; // Lower bit is apparently ignored.
            GPU.DisplayFB_YStart = (V >> 10) & 0x1FF;
            GPU.display_change_count++;
            rsx_intf_set_vram_framebuffer_coords(GPU.DisplayFB_XStart, GPU.DisplayFB_YStart);
            break;

         case 0x06:  // Horizontal display range
            GPU.HorizStart = V & 0xFFF;
            GPU.HorizEnd = (V >> 12) & 0xFFF;
            rsx_intf_set_horizontal_display_range(GPU.HorizStart, GPU.HorizEnd);
            break;

         case 0x07:
            GPU.VertStart = V & 0x3FF;
            GPU.VertEnd = (V >> 10) & 0x3FF;
            rsx_intf_set_vertical_display_range(GPU.VertStart, GPU.VertEnd);
            break;

         case 0x08:
            GPU.DisplayMode = V & 0xFF;
            RSX_UpdateDisplayMode();
            break;

         case 0x09:
            GPU.TexDisableAllowChange = V & 1;
            break;

         case 0x10:  // GPU info(?)
            switch(V & 0xF)
            {
               // DataReadBuffer must remain unchanged for any unhandled GPU info index.
               default:
                  break;
               case 0x2:
                  GPU.DataReadBufferEx &= 0xFFF00000;
                  GPU.DataReadBufferEx |= (GPU.tww << 0) | (GPU.twh << 5) | (GPU.twx << 10) | (GPU.twy << 15);
                  GPU.DataReadBuffer    = GPU.DataReadBufferEx;
                  break;
               case 0x3:
                  GPU.DataReadBufferEx &= 0xFFF00000;
                  GPU.DataReadBufferEx |= (GPU.ClipY0 << 10) | GPU.ClipX0;
                  GPU.DataReadBuffer = GPU.DataReadBufferEx;
                  break;

               case 0x4:
                  GPU.DataReadBufferEx &= 0xFFF00000;
                  GPU.DataReadBufferEx |= (GPU.ClipY1 << 10) | GPU.ClipX1;
                  GPU.DataReadBuffer = GPU.DataReadBufferEx;
                  break;

               case 0x5:
                  GPU.DataReadBufferEx &= 0xFFC00000;
                  GPU.DataReadBufferEx |= (GPU.OffsX & 2047) | ((GPU.OffsY & 2047) << 11);
                  GPU.DataReadBuffer = GPU.DataReadBufferEx;
                  break;

               case 0x7:
                  GPU.DataReadBufferEx = 2;
                  GPU.DataReadBuffer = GPU.DataReadBufferEx;
                  break;

               case 0x8:
                  GPU.DataReadBufferEx = 0;
                  GPU.DataReadBuffer = GPU.DataReadBufferEx;
                  break;
            }
            break;

      }
   }
   else     // GP0 ("Data")
   {
      GPU_WriteCB(V, A);
   }
}

void GPU_WriteDMA(uint32_t V, uint32 addr)
{
   GPU_WriteCB(V, addr);
}

static INLINE uint32_t GPU_ReadData(void)
{
   unsigned i;

   GPU.DataReadBufferEx = 0;

   for(i = 0; i < 2; i++)
   {
      GPU.DataReadBufferEx |=
         texel_fetch(&GPU,
                     GPU.FBRW_CurX & 1023,
                     GPU.FBRW_CurY & 511) << (i * 16);

      GPU.FBRW_CurX++;
      if(GPU.FBRW_CurX == (GPU.FBRW_X + GPU.FBRW_W))
      {
         if((GPU.FBRW_CurY + 1) == (GPU.FBRW_Y + GPU.FBRW_H))
            GPU.InCmd = INCMD_NONE;
         else
         {
            GPU.FBRW_CurY++;
            GPU.FBRW_CurX = GPU.FBRW_X;
         }
      }
   }

   return GPU.DataReadBufferEx;
}

uint32_t GPU_ReadDMA(void)
{
   if(GPU.InCmd != INCMD_FBREAD)
      return GPU.DataReadBuffer;
   return GPU_ReadData();
}

uint32_t GPU_Read(const int32_t timestamp, uint32_t A)
{
   uint32_t ret = 0;


   if(A & 4)   // Status
   {
      ret = (((GPU.DisplayMode << 1) & 0x7F) | ((GPU.DisplayMode >> 6) & 1)) << 16;

      ret |= (GPU.DisplayMode & 0x80) << 7;

      ret |= GPU.DMAControl << 29;

      ret |= (GPU.DisplayFB_CurLineYReadout & 1) << 31;

      ret |= (!GPU.field) << 13;

      if(GPU.DMAControl & 0x02)
         ret |= 1 << 25;

      ret |= GPU.IRQPending << 24;

      ret |= GPU.DisplayOff << 23;

      /* GPU idle bit */
      if(GPU.InCmd == INCMD_NONE && GPU.DrawTimeAvail >= 0
            && GPU_BlitterFIFO.in_count == 0x00)
         ret |= 1 << 26;

      if(GPU.InCmd == INCMD_FBREAD) // Might want to more accurately emulate this in the future?
         ret |= (1 << 27);

      ret |= CalcFIFOReadyBit() << 28;    // FIFO has room bit? (kinda).

      //
      //
      ret |= GPU.TexPageX >> 6;
      ret |= GPU.TexPageY >> 4;
      ret |= GPU.abr << 5;
      ret |= GPU.TexMode << 7;

      ret |= GPU.dtd << 9;
      ret |= GPU.dfe << 10;

      if(GPU.MaskSetOR)
         ret |= 1 << 11;

      if(GPU.MaskEvalAND)
         ret |= 1 << 12;

      ret |= GPU.TexDisable << 15;
   }
   else     // "Data"
   {
      if(GPU.InCmd == INCMD_FBREAD)
         ret = GPU_ReadData();
      else
         ret = GPU.DataReadBuffer;
   }

#if 0
   if(GPU.DMAControl & 2)
   {
      //PSX_WARNING("[GPU READ WHEN (DMACONTROL&2)] 0x%08x - ret=0x%08x, scanline=%d", A, ret, scanline);
   }
#endif

   return(ret >> ((A & 3) * 8));
}

static INLINE void ReorderRGB_Var(uint32_t out_Rshift,
      uint32_t out_Gshift, uint32_t out_Bshift,
      bool bpp24, const uint16_t *src, uint32_t *dest,
      const int32 dx_start, const int32 dx_end, int32 fb_x,
      unsigned upscale_shift, unsigned upscale)
{
  int32_t fb_mask = ((0x7FF << upscale_shift) + upscale - 1);

   if(bpp24)   // 24bpp
   {
      for(int32 x = dx_start; x < dx_end; x+= upscale)
      {
         int i;
         uint32_t color;
         uint32_t srcpix = src[(fb_x >> 1) + 0]
            | (src[((fb_x >> 1) + (1 << upscale_shift)) & fb_mask] << 16);
         srcpix >>= ((fb_x >> upscale_shift) & 1) * 8;

         color =   (((srcpix >> 0) << RED_SHIFT)   & (0xFF << RED_SHIFT))
            | (((srcpix >> 8) << GREEN_SHIFT) & (0xFF << GREEN_SHIFT))
            | (((srcpix >> 16) << BLUE_SHIFT) & (0xFF << BLUE_SHIFT));

         for (i = 0; i < upscale; i++)
            dest[x + i] = color;

         fb_x = (fb_x + (3 << upscale_shift)) & fb_mask;
      }
   }           // 15bpp
   else
   {
      for(int32 x = dx_start; x < dx_end; x++)
      {
         uint32_t srcpix = src[(fb_x >> 1)];
         dest[x] = MAKECOLOR(
               (((srcpix >> 0) & 0x1F) << 3),
               (((srcpix >> 5) & 0x1F) << 3),
               (((srcpix >> 10) & 0x1F) << 3),
               0);

         fb_x = (fb_x + 2) & fb_mask;
      }
   }
}

int32_t GPU_Update(const int32_t sys_timestamp)
{
   int32 gpu_clocks;
   static const uint32_t DotClockRatios[5] = { 10, 8, 5, 4, 7 };
   const uint32_t dmc = (GPU.DisplayMode & 0x40) ? 4 : (GPU.DisplayMode & 0x3);
   const uint32_t dmw = 2800 / DotClockRatios[dmc];   // Must be <= 768
   int32_t sys_clocks = sys_timestamp - GPU.lastts;

   if(!sys_clocks)
      goto TheEnd;

   GPU.DrawTimeAvail += sys_clocks << (1 + psx_gpu_overclock_shift);

   if(GPU.DrawTimeAvail > (2*EventCycles << psx_gpu_overclock_shift))
      GPU.DrawTimeAvail = (2*EventCycles << psx_gpu_overclock_shift);

   if(GPU_BlitterFIFO.in_count && GPU.InCmd != INCMD_FBREAD)
      ProcessFIFO(GPU_BlitterFIFO.in_count);

   //puts("GPU Update Start");

   GPU.GPUClockCounter += (uint64)sys_clocks * GPU.GPUClockRatio;

   gpu_clocks       = GPU.GPUClockCounter >> 16;
   GPU.GPUClockCounter -= gpu_clocks << 16;

   while(gpu_clocks > 0)
   {
      int32 chunk_clocks = gpu_clocks;
      int32 dot_clocks;

      if(chunk_clocks > GPU.LineClockCounter)
         chunk_clocks = GPU.LineClockCounter;

      gpu_clocks -= chunk_clocks;
      GPU.LineClockCounter -= chunk_clocks;

      GPU.DotClockCounter += chunk_clocks;
      dot_clocks = GPU.DotClockCounter / DotClockRatios[GPU.DisplayMode & 0x3];
      GPU.DotClockCounter -= dot_clocks * DotClockRatios[GPU.DisplayMode & 0x3];

      TIMER_AddDotClocks(dot_clocks);


      if(!GPU.LineClockCounter)
      {
         // We could just call this at the top of GPU_Update(), but
         // do it here for slightly less CPU usage(presumably).
         PSX_SetEventNT(PSX_EVENT_TIMER, TIMER_Update(sys_timestamp));

         GPU.LinePhase = (GPU.LinePhase + 1) & 1;

         if(GPU.LinePhase)
         {
            TIMER_SetHRetrace(true);
            if((GPU.DisplayMode & DISP_PAL) && fast_pal) {
               GPU.LineClockCounter = (200 * 50) / 59.94;
            } else {
               GPU.LineClockCounter = 200;
            }
            TIMER_ClockHRetrace();
         }
         else
         {
            unsigned int FirstVisibleLineTemp =
               GPU.LineVisFirst + (crop_overscan == 2 ? GPU.VertStart : (GPU.HardwarePALType ? 20 : 16));
            unsigned int VisibleLineCountTemp =
               (crop_overscan == 2 ? (GPU.VertEnd - GPU.VertStart) - ((GPU.HardwarePALType ? 287 : 239) - GPU.LineVisLast) - GPU.LineVisFirst : GPU.LineVisLast + 1 - GPU.LineVisFirst); //HardwarePALType ? 288 : 240;

            if (VisibleLineCountTemp > (GPU.HardwarePALType ? 288 : 240))
            {
               FirstVisibleLineTemp =
                  GPU.LineVisFirst + (GPU.HardwarePALType ? 20 : 16);
               VisibleLineCountTemp =
                  GPU.LineVisLast + 1 - GPU.LineVisFirst; //HardwarePALType ? 288 : 240;
            }
            const unsigned int FirstVisibleLine = FirstVisibleLineTemp;
            const unsigned int VisibleLineCount = VisibleLineCountTemp;

            TIMER_SetHRetrace(false);

            if(GPU.DisplayMode & DISP_PAL)
            {
               if (fast_pal) {
                  GPU.LineClockCounter = ((3405 - 200) * 50) / 59.94;
               } else {
                  GPU.LineClockCounter = 3405 - 200;
               }
            }
            else
               GPU.LineClockCounter = 3412 + GPU.PhaseChange - 200;

            GPU.scanline = (GPU.scanline + 1) % GPU.LinesPerField;
            GPU.PhaseChange = !GPU.PhaseChange;

#ifdef WANT_DEBUGGER
            DBG_GPUScanlineHook(GPU.scanline);
#endif

            //
            //
            //
            if(GPU.scanline == (GPU.HardwarePALType ? 308 : 256)) // Will need to be redone if we ever allow for visible vertical overscan with NTSC.
            {
               if(GPU.sl_zero_reached)
                  PSX_RequestMLExit();
            }

            if(GPU.scanline == (GPU.LinesPerField - 1))
            {
               if(GPU.sl_zero_reached)
                  PSX_RequestMLExit();

               if(GPU.DisplayMode & DISP_INTERLACED)
                  GPU.field = !GPU.field;
               else
                  GPU.field = 0;
            }

            if(GPU.scanline == 0)
            {
               assert(GPU.sl_zero_reached == false);
               GPU.sl_zero_reached = true;

               if(GPU.DisplayMode & DISP_INTERLACED)
               {
                  if(GPU.DisplayMode & DISP_PAL)
                     GPU.LinesPerField = 313 - GPU.field;
                  else                   // NTSC
                     GPU.LinesPerField = 263 - GPU.field;
               }
               else
               {
                  GPU.field = 0;  // May not be the correct place for this?

                  if(GPU.DisplayMode & DISP_PAL)
                     GPU.LinesPerField = 314;
                  else        // NTSC
                     GPU.LinesPerField = 263;
               }


               if (rsx_intf_is_type() == RSX_SOFTWARE && GPU.espec)
               {
                  if((bool)(GPU.DisplayMode & DISP_PAL) != GPU.HardwarePALType)
                  {
                     GPU.DisplayRect->x = 0;
                     GPU.DisplayRect->y = 0;
                     GPU.DisplayRect->w = 384;
                     GPU.DisplayRect->h = VisibleLineCount;

                     for(int32 y = 0; y < GPU.DisplayRect->h; y++)
                     {
                        uint32_t *dest = GPU.surface->pixels + y * GPU.surface->pitch32;

                        GPU.LineWidths[y] = 384;

                        memset(dest, 0, 384 * sizeof(int32));
                     }
                  }
                  else
                  {
                     GPU.espec->InterlaceOn = (bool)(GPU.DisplayMode & DISP_INTERLACED);
                     GPU.espec->InterlaceField = (bool)(GPU.DisplayMode & DISP_INTERLACED) && GPU.field;

                     GPU.DisplayRect->x = 0;
                     GPU.DisplayRect->y = 0;
                     GPU.DisplayRect->w = 0;
                     GPU.DisplayRect->h = VisibleLineCount << (bool)(GPU.DisplayMode & DISP_INTERLACED);

                     // Clear ~0 state.
                     GPU.LineWidths[0] = 0;

                     for(int i = 0; i < (GPU.DisplayRect->y + GPU.DisplayRect->h); i++)
                     {
                        GPU.surface->pixels[i * GPU.surface->pitch32 + 0] =
                           GPU.surface->pixels[i * GPU.surface->pitch32 + 1] = 0;
                        GPU.LineWidths[i] = 2;
                     }
                  }
               }
            }

            //
            // Don't mess with the order of evaluation of
            // these scanline == VertXXX && (InVblankwhatever) if statements
            // and the following IRQ/timer vblank stuff
            // unless you know what you're doing!!!
            // (IE you've run further tests to refine the behavior)
            if(GPU.scanline == GPU.VertEnd && !GPU.InVBlank)
            {
               if(GPU.sl_zero_reached)
               {
                  // Gameplay in Descent(NTSC) has vblank at scanline 236
                  //
                  // Mikagura Shoujo Tanteidan has vblank at scanline 192 during intro
                  //  FMV(which we don't handle here because low-latency in that case is not so important).
                  //
                  if(GPU.scanline >= (GPU.HardwarePALType ? 260 : 232))
                     PSX_RequestMLExit();
               }

               GPU.InVBlank = true;

               GPU.DisplayFB_CurYOffset = 0;

               if((GPU.DisplayMode & 0x24) == 0x24)
                  GPU.field_ram_readout = !GPU.field;
               else
                  GPU.field_ram_readout = 0;
            }

            if(GPU.scanline == GPU.VertStart && GPU.InVBlank)
            {
               GPU.InVBlank = false;

               // Note to self: X-Men Mutant Academy
               // relies on this being set on the proper
               // scanline in 480i mode(otherwise it locks up on startup).
               //if(HeightMode)
               // DisplayFB_CurYOffset = field;
            }

            IRQ_Assert(IRQ_VBLANK, GPU.InVBlank);
            TIMER_SetVBlank(GPU.InVBlank);

            unsigned displayfb_yoffset = GPU.DisplayFB_CurYOffset;

            // Needs to occur even in vblank.
            // Not particularly confident about the timing
            // of this in regards to vblank and the
            // upper bit(ODE) of the GPU status port, though the
            // test that showed an oddity was pathological in
            // that VertEnd < VertStart in it.
            if((GPU.DisplayMode & 0x24) == 0x24)
               displayfb_yoffset = (GPU.DisplayFB_CurYOffset << 1) + (GPU.InVBlank ? 0 : GPU.field_ram_readout);

            GPU.DisplayFB_CurLineYReadout = (GPU.DisplayFB_YStart + displayfb_yoffset) & 0x1FF;

            unsigned dmw_width = 0;
            unsigned pix_clock_offset = 0;
            unsigned pix_clock = 0;
            unsigned pix_clock_div = 0;
            uint32_t *dest = NULL;

            if((bool)(GPU.DisplayMode & DISP_PAL) == GPU.HardwarePALType
                  && GPU.scanline >= FirstVisibleLine
                  && GPU.scanline < (FirstVisibleLine + VisibleLineCount))
            {
               int32 fb_x      = GPU.DisplayFB_XStart * 2;
               // Restore old center behaviour if GPU.HorizStart is intentionally very high.
               // 938 fixes Gunbird (1008) and Mobile Light Force (EU release of Gunbird),
               // but this value should be lowered in the future if necessary.
               // Additionally cut off everything after GPU.HorizEnd that shouldn't be
               // in the viewport (the hardware renderers already takes care of this).
               int32 dx_start  = (crop_overscan == 2 && GPU.HorizStart < 938 ? 608 : GPU.HorizStart), dx_end = (crop_overscan == 2 && GPU.HorizStart < 938 ? GPU.HorizEnd - GPU.HorizStart + 608 : GPU.HorizEnd - (GPU.HorizStart < 938 ? 0 : 1));
               int32 dest_line =
                  ((GPU.scanline - FirstVisibleLine) << GPU.espec->InterlaceOn)
                  + GPU.espec->InterlaceField;

               if(dx_end < dx_start)
                  dx_end = dx_start;

               dx_start = dx_start / DotClockRatios[dmc];
               dx_end = dx_end / DotClockRatios[dmc];

               dx_start -= 488 / DotClockRatios[dmc];
               dx_end -= 488 / DotClockRatios[dmc];

               if(dx_start < 0)
               {
                  fb_x -= dx_start * ((GPU.DisplayMode & DISP_RGB24) ? 3 : 2);
                  fb_x &= 0x7FF; //0x3FF;
                  dx_start = 0;
               }

               if((uint32)dx_end > dmw)
                  dx_end = dmw;

               if(GPU.InVBlank || GPU.DisplayOff)
                  dx_start = dx_end = 0;

               GPU.LineWidths[dest_line] = dmw;

               if (rsx_intf_is_type() == RSX_SOFTWARE)
               {
                  // Convert the necessary variables to the upscaled version
                  uint32_t x;
                  uint32_t y        = GPU.DisplayFB_CurLineYReadout << GPU.upscale_shift;
                  uint32_t udmw     = dmw      << GPU.upscale_shift;
                  int32 udx_start   = dx_start << GPU.upscale_shift;
                  int32 udx_end     = dx_end   << GPU.upscale_shift;
                  int32 ufb_x       = fb_x     << GPU.upscale_shift;
                  unsigned _upscale = UPSCALE(&GPU);

                  for (uint32_t i = 0; i < _upscale; i++)
                  {
                     const uint16_t *src = GPU.vram +
                        ((y + i) << (10 + GPU.upscale_shift));

                     dest = GPU.surface->pixels +
                        ((dest_line << GPU.upscale_shift) + i) * GPU.surface->pitch32;
                     memset(dest, 0, udx_start * sizeof(int32));

                     ReorderRGB_Var(
                           RED_SHIFT,
                           GREEN_SHIFT,
                           BLUE_SHIFT,
                           GPU.DisplayMode & DISP_RGB24,
                           src,
                           dest,
                           udx_start,
                           udx_end,
                           ufb_x,
                           GPU.upscale_shift,
                           _upscale);

                     for(x = udx_end; x < udmw; x++)
                        dest[x] = 0;
                  }

                  //reset dest back to i=0 for PSX_GPULineHook call
                  dest = GPU.surface->pixels + ((dest_line << GPU.upscale_shift) * GPU.surface->pitch32);
               }

               dmw_width = dmw;
               pix_clock_offset = (488 - 146) / DotClockRatios[dmc];
               pix_clock = (GPU.HardwarePALType ? 53203425 : 53693182) / DotClockRatios[dmc];
               pix_clock_div = DotClockRatios[dmc];

               PSX_GPULineHook(sys_timestamp,
                               sys_timestamp - ((uint64)gpu_clocks * 65536) / GPU.GPUClockRatio,
                               GPU.scanline == 0,
                               dest,
                               &GPU.surface->format,
                               dmw_width,
                               pix_clock_offset,
                               pix_clock,
                               pix_clock_div,
                               GPU.surface->pitch32,
                               (1 << GPU.upscale_shift));
            }
            else
            {
               PSX_GPULineHook(sys_timestamp,
                               sys_timestamp - ((uint64)gpu_clocks * 65536) / GPU.GPUClockRatio,
                               GPU.scanline == 0,
                               NULL,
                               NULL,
                               0, 0, 0, 0,
                               GPU.surface->pitch32,
                               (1 << GPU.upscale_shift));
            }

            if(!GPU.InVBlank)
               GPU.DisplayFB_CurYOffset = (GPU.DisplayFB_CurYOffset + 1) & 0x1FF;
         }

         // Mostly so the next event time gets
         // recalculated properly in regards to our calls
         PSX_SetEventNT(PSX_EVENT_TIMER, TIMER_Update(sys_timestamp));

         // to TIMER_SetVBlank() and TIMER_SetHRetrace().
      }  // end if(!LineClockCounter)
   }  // end while(gpu_clocks > 0)

   //puts("GPU Update End");

TheEnd:
   GPU.lastts = sys_timestamp;

   int32 next_dt = GPU.LineClockCounter;

   next_dt = (((int64)next_dt << 16) - GPU.GPUClockCounter + GPU.GPUClockRatio - 1) / GPU.GPUClockRatio;

   next_dt = std::max<int32>(1, next_dt);
   next_dt = std::min<int32>(EventCycles, next_dt);

   return(sys_timestamp + next_dt);
}

void GPU_StartFrame(EmulateSpecStruct *espec_arg)
{
   GPU.sl_zero_reached = false;
   GPU.espec           = espec_arg;
   GPU.surface         = GPU.espec->surface;
   GPU.DisplayRect     = &GPU.espec->DisplayRect;
   GPU.LineWidths      = GPU.espec->LineWidths;
}


void GPU_RestoreStateP1(bool load)
{
   if (GPU.upscale_shift == 0)
   {
      // No upscaling, we can dump the VRAM contents directly
      vram_new = GPU.vram;
   }
   else
   {
      // We have increased internal resolution, savestates are always
      // made at 1x for compatibility
      vram_new = new uint16[1024 * 512];

      if (!load)
      {
         // We must downscale the current VRAM contents back to 1x
         for (unsigned y = 0; y < 512; y++)
         {
            for (unsigned x = 0; x < 1024; x++)
               vram_new[y * 1024 + x] = texel_fetch(&GPU, x, y);
         }
      }
   }

   for(unsigned i = 0; i < 256; i++)
   {
      TexCache_Tag[i] = GPU.TexCache[i].Tag;

      for(unsigned j = 0; j < 4; j++)
         TexCache_Data[i][j] = GPU.TexCache[i].Data[j];

   }
}

void GPU_RestoreStateP2(bool load)
{
   if (GPU.upscale_shift > 0)
   {
      if (load)
      {
         // Restore upscaled VRAM from savestate
         for (unsigned y = 0; y < 512; y++)
         {
            for (unsigned x = 0; x < 1024; x++)
               texel_put(x, y, vram_new[y * 1024 + x]);
         }
      }

      delete [] vram_new;
      vram_new = NULL;
   }
}

void GPU_RestoreStateP3(void)
{
   for(unsigned i = 0; i < 256; i++)
   {
      GPU.TexCache[i].Tag = TexCache_Tag[i];

      for(unsigned j = 0; j < 4; j++)
         GPU.TexCache[i].Data[j] = TexCache_Data[i][j];
   }
   RecalcTexWindowStuff(&GPU);
   rsx_intf_set_tex_window(GPU.tww, GPU.twh, GPU.twx, GPU.twy);

   GPU_BlitterFIFO.SaveStatePostLoad();

   GPU.HorizStart &= 0xFFF;
   GPU.HorizEnd &= 0xFFF;

   GPU.DisplayFB_CurYOffset &= 0x1FF;
   GPU.DisplayFB_CurLineYReadout &= 0x1FF;

   GPU.TexPageX &= 0xF * 64;
   GPU.TexPageY &= 0x10 * 16;
   GPU.TexMode &= 0x3;
   GPU.abr &= 0x3;

   GPU.ClipX0 &= 1023;
   GPU.ClipY0 &= 1023;
   GPU.ClipX1 &= 1023;
   GPU.ClipY1 &= 1023;

   GPU.OffsX = sign_x_to_s32(11, GPU.OffsX);
   GPU.OffsY = sign_x_to_s32(11, GPU.OffsY);

   IRQ_Assert(IRQ_GPU, GPU.IRQPending);

   rsx_intf_toggle_display(GPU.DisplayOff);
   rsx_intf_set_draw_area( GPU.ClipX0, GPU.ClipY0,
                           GPU.ClipX1, GPU.ClipY1);

   rsx_intf_load_image( 0,    0,
                        1024, 512,
                        GPU.vram, false, false);

   rsx_intf_set_vram_framebuffer_coords(GPU.DisplayFB_XStart, GPU.DisplayFB_YStart);
   rsx_intf_set_horizontal_display_range(GPU.HorizStart, GPU.HorizEnd);
   rsx_intf_set_vertical_display_range(GPU.VertStart, GPU.VertEnd);

   RSX_UpdateDisplayMode();
}

int GPU_StateAction(StateMem *sm, int load, int data_only)
{
   GPU_RestoreStateP1(load);

   SFORMAT StateRegs[] =
   {
      // Hardcode entry name to remain backward compatible with the
      // previous fixed internal resolution code
      SFARRAY16N(vram_new, 1024 * 512, "&GPURAM[0][0]"),

      SFVARN(GPU.DMAControl, "DMAControl"),

      SFVARN(GPU.ClipX0, "ClipX0"),
      SFVARN(GPU.ClipY0, "ClipY0"),
      SFVARN(GPU.ClipX1, "ClipX1"),
      SFVARN(GPU.ClipY1, "ClipY1"),

      SFVARN(GPU.OffsX, "OffsX"),
      SFVARN(GPU.OffsY, "OffsY"),

      SFVARN(GPU.dtd, "dtd"),
      SFVARN(GPU.dfe, "dfe"),

      SFVARN(GPU.MaskSetOR, "MaskSetOR"),
      SFVARN(GPU.MaskEvalAND, "MaskEvalAND"),

      SFVARN(GPU.TexDisable, "TexDisable"),
      SFVARN(GPU.TexDisableAllowChange, "TexDisableAllowChange"),

      SFVARN(GPU.tww, "tww"),
      SFVARN(GPU.twh, "twh"),
      SFVARN(GPU.twx, "twx"),
      SFVARN(GPU.twy, "twy"),

      SFVARN(GPU.TexPageX, "TexPageX"),
      SFVARN(GPU.TexPageY, "TexPageY"),

      SFVARN(GPU.SpriteFlip, "SpriteFlip"),

      SFVARN(GPU.abr, "abr"),
      SFVARN(GPU.TexMode, "TexMode"),

      SFARRAY32N(&GPU_BlitterFIFO.data[0], sizeof(GPU_BlitterFIFO.data) / sizeof(GPU_BlitterFIFO.data[0]), "&BlitterFIFO.data[0]"),
      SFVARN(GPU_BlitterFIFO.read_pos, "BlitterFIFO.read_pos"),
      SFVARN(GPU_BlitterFIFO.write_pos, "BlitterFIFO.write_pos"),
      SFVARN(GPU_BlitterFIFO.in_count, "BlitterFIFO.in_count"),

      SFVARN(GPU.DataReadBuffer, "DataReadBuffer"),
      SFVARN(GPU.DataReadBufferEx, "DataReadBufferEx"),

      SFVARN(GPU.IRQPending, "IRQPending"),

      SFVARN(GPU.InCmd, "InCmd"),
      SFVARN(GPU.InCmd_CC, "InCmd_CC"),

      SFVARN(GPU.InQuad_F3Vertices[0].x, "InQuad_F3Vertices[0].x"),
      SFVARN(GPU.InQuad_F3Vertices[0].y, "InQuad_F3Vertices[0].y"),
      SFVARN(GPU.InQuad_F3Vertices[0].u, "InQuad_F3Vertices[0].u"),
      SFVARN(GPU.InQuad_F3Vertices[0].v, "InQuad_F3Vertices[0].v"),
      SFVARN(GPU.InQuad_F3Vertices[0].r, "InQuad_F3Vertices[0].r"),
      SFVARN(GPU.InQuad_F3Vertices[0].g, "InQuad_F3Vertices[0].g"),
      SFVARN(GPU.InQuad_F3Vertices[0].b, "InQuad_F3Vertices[0].b"),

      SFVARN(GPU.InQuad_F3Vertices[1].x, "InQuad_F3Vertices[1].x"),
      SFVARN(GPU.InQuad_F3Vertices[1].y, "InQuad_F3Vertices[1].y"),
      SFVARN(GPU.InQuad_F3Vertices[1].u, "InQuad_F3Vertices[1].u"),
      SFVARN(GPU.InQuad_F3Vertices[1].v, "InQuad_F3Vertices[1].v"),
      SFVARN(GPU.InQuad_F3Vertices[1].r, "InQuad_F3Vertices[1].r"),
      SFVARN(GPU.InQuad_F3Vertices[1].g, "InQuad_F3Vertices[1].g"),
      SFVARN(GPU.InQuad_F3Vertices[1].b, "InQuad_F3Vertices[1].b"),

      SFVARN(GPU.InQuad_F3Vertices[2].x, "InQuad_F3Vertices[2].x"),
      SFVARN(GPU.InQuad_F3Vertices[2].y, "InQuad_F3Vertices[2].y"),
      SFVARN(GPU.InQuad_F3Vertices[2].u, "InQuad_F3Vertices[2].u"),
      SFVARN(GPU.InQuad_F3Vertices[2].v, "InQuad_F3Vertices[2].v"),
      SFVARN(GPU.InQuad_F3Vertices[2].r, "InQuad_F3Vertices[2].r"),
      SFVARN(GPU.InQuad_F3Vertices[2].g, "InQuad_F3Vertices[2].g"),
      SFVARN(GPU.InQuad_F3Vertices[2].b, "InQuad_F3Vertices[2].b"),

      SFVARN(GPU.InPLine_PrevPoint.x, "InPLine_PrevPoint.x"),
      SFVARN(GPU.InPLine_PrevPoint.y, "InPLine_PrevPoint.y"),
      SFVARN(GPU.InPLine_PrevPoint.r, "InPLine_PrevPoint.r"),
      SFVARN(GPU.InPLine_PrevPoint.g, "InPLine_PrevPoint.g"),
      SFVARN(GPU.InPLine_PrevPoint.b, "InPLine_PrevPoint.b"),

      SFVARN(GPU.FBRW_X, "FBRW_X"),
      SFVARN(GPU.FBRW_Y, "FBRW_Y"),
      SFVARN(GPU.FBRW_W, "FBRW_W"),
      SFVARN(GPU.FBRW_H, "FBRW_H"),
      SFVARN(GPU.FBRW_CurY, "FBRW_CurY"),
      SFVARN(GPU.FBRW_CurX, "FBRW_CurX"),

      SFVARN(GPU.DisplayMode, "DisplayMode"),
      SFVARN(GPU.DisplayOff, "DisplayOff"),
      SFVARN(GPU.DisplayFB_XStart, "DisplayFB_XStart"),
      SFVARN(GPU.DisplayFB_YStart, "DisplayFB_YStart"),

      SFVARN(GPU.HorizStart, "HorizStart"),
      SFVARN(GPU.HorizEnd, "HorizEnd"),

      SFVARN(GPU.VertStart, "VertStart"),
      SFVARN(GPU.VertEnd, "VertEnd"),

      SFVARN(GPU.DisplayFB_CurYOffset, "DisplayFB_CurYOffset"),
      SFVARN(GPU.DisplayFB_CurLineYReadout, "DisplayFB_CurLineYReadout"),

      SFVARN(GPU.InVBlank, "InVBlank"),

      SFVARN(GPU.LinesPerField, "LinesPerField"),
      SFVARN(GPU.scanline, "scanline"),
      SFVARN(GPU.field, "field"),
      SFVARN(GPU.field_ram_readout, "field_ram_readout"),
      SFVARN(GPU.PhaseChange, "PhaseChange"),

      SFVARN(GPU.DotClockCounter, "DotClockCounter"),

      SFVARN(GPU.GPUClockCounter, "GPUClockCounter"),
      SFVARN(GPU.LineClockCounter, "LineClockCounter"),
      SFVARN(GPU.LinePhase, "LinePhase"),

      SFVARN(GPU.DrawTimeAvail, "DrawTimeAvail"),

      SFEND
   };

   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "GPU");

   GPU_RestoreStateP2(load);

   if(load)
      GPU_RestoreStateP3();

   return(ret);
}

bool GPU_get_display_possibly_dirty(void)
{
   return GPU.display_possibly_dirty;
}

void GPU_set_display_possibly_dirty(bool dirty)
{
   GPU.display_possibly_dirty = dirty;
}

void GPU_set_display_change_count(unsigned a)
{
   GPU.display_change_count = a;
}

unsigned GPU_get_display_change_count(void)
{
   return GPU.display_change_count;
}

void GPU_set_dither_upscale_shift(uint8 factor)
{
   GPU.dither_upscale_shift = factor;
}

uint8 GPU_get_dither_upscale_shift(void)
{
   return GPU.dither_upscale_shift;
}

void GPU_set_upscale_shift(uint8 factor)
{
   GPU.upscale_shift = factor;
}

uint8 GPU_get_upscale_shift(void)
{
   return GPU.upscale_shift;
}

bool GPU_DMACanWrite(void)
{
   return CalcFIFOReadyBit();
}

uint16 *GPU_get_vram(void)
{
   return GPU.vram;
}

uint16 GPU_PeekRAM(uint32 A)
{
   return texel_fetch(&GPU, A & 0x3FF, (A >> 10) & 0x1FF);
}

void GPU_PokeRAM(uint32 A, uint16 V)
{
   texel_put(A & 0x3FF, (A >> 10) & 0x1FF, V);
}

/* Set a pixel in VRAM, upscaling it if necessary */
void texel_put(uint32 x, uint32 y, uint16 v)
{
   uint32_t dy, dx;
   x <<= GPU.upscale_shift;
   y <<= GPU.upscale_shift;

   /* Duplicate the pixel as many times as necessary (nearest
    * neighbour upscaling) */
   for (dy = 0; dy < UPSCALE(&GPU); dy++)
   {
      for (dx = 0; dx < UPSCALE(&GPU); dx++)
         vram_put(&GPU, x + dx, y + dy, v);
   }
}

int32_t GPU_GetScanlineNum(void)
{
   return GPU.scanline;
}

/* Beetle PSX addition, allows runtime configuration of visible scanlines in software renderer */
void GPU_set_visible_scanlines(int sls, int sle)
{
   GPU.LineVisFirst = sls;
   GPU.LineVisLast = sle;
}
