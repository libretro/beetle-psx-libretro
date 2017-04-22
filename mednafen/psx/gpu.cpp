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
#include "../../rsx/rsx_intf.h"

#include "../pgxp/pgxp_main.h"
#include "../pgxp/pgxp_gpu.h"
#include "../pgxp/pgxp_mem.h"

#include "gpu_common.h"

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

/*
   November 29, 2012 notes:

   PAL mode can be turned on, and then off again, mid-frame(creates a neat effect).

   Pixel clock can be changed mid-frame with effect(the effect is either instantaneous, or cached at some point in the scanline, not tested to see which);
   interestingly, alignment is off on a PS1 when going 5MHz->10MHz>5MHz with a grid image.

   Vertical start and end can be changed during active display, with effect(though it needs to be vs0->ve0->vs1->ve1->..., vs0->vs1->ve0 doesn't apparently do anything
   different from vs0->ve0.
   */
static const int8 dither_table[4][4] =
{
   { -4,  0, -3,  1 },
   {  2, -2,  3, -1 },
   { -3,  1, -4,  0 },
   {  3, -1,  2, -2 },
};

PS_GPU *GPU = NULL;

static INLINE bool CalcFIFOReadyBit(void)
{
   if(GPU->InCmd & (INCMD_PLINE | INCMD_QUAD))
      return(false);

   if(GPU->BlitterFIFO.in_count == 0)
      return(true);

   if(GPU->InCmd & (INCMD_FBREAD | INCMD_FBWRITE))
      return(false);

   if(GPU->BlitterFIFO.in_count >= GPU->Commands[GPU->BlitterFIFO.Peek() >> 24].fifo_fb_len)
      return(false);

   return(true);
}

PS_GPU::PS_GPU(bool pal_clock_and_tv, int sls, int sle, uint8_t upscale_shift)
{
   HardwarePALType = pal_clock_and_tv;

   BuildDitherTable();

   if(HardwarePALType == false)	// NTSC clock
      GPUClockRatio = 103896; // 65536 * 53693181.818 / (44100 * 768)
   else	// PAL clock
      GPUClockRatio = 102948; // 65536 * 53203425 / (44100 * 768)

   memset(RGB8SAT_Under, 0, sizeof(RGB8SAT_Under));

   for(int i = 0; i < 256; i++)
      RGB8SAT[i] = i;

   memset(RGB8SAT_Over, 0xFF, sizeof(RGB8SAT_Over));

   LineVisFirst = sls;
   LineVisLast = sle;

   display_change_count = 0;

   this->upscale_shift = upscale_shift;
   this->dither_upscale_shift = 0;
}

PS_GPU::PS_GPU(const PS_GPU &g, uint8 ushift)
{
   // Recopy the GPU state in the new buffer
   *this = g;

   // Override the upscaling factor
   upscale_shift = ushift;

   //For simplicity we do the transfer at 1x internal resolution.
   for (unsigned y = 0; y < 512; y++)
   {
      for (unsigned x = 0; x < 1024; x++)
         texel_put(x, y, texel_fetch(GPU, x, y));
   }
}

PS_GPU::~PS_GPU()
{
}

void PS_GPU::BuildDitherTable()
{
  int x, y, v;

  for(y = 0; y < 4; y++)
    for(x = 0; x < 4; x++)
      for(v = 0; v < 512; v++)
	{
	  int value = v;

	  value += dither_table[y][x];

	  value >>= 3;

	  if(value < 0)
	    value = 0;

	  if(value > 0x1F)
	    value = 0x1F;

	  DitherLUT[y][x][v] = value;
	}
}

// Allocate enough room for the PS_GPU class and VRAM
void *PS_GPU::Alloc(uint8 upscale_shift)
{
  unsigned width = 1024 << upscale_shift;
  unsigned height = 512 << upscale_shift;

  unsigned size   = sizeof(PS_GPU) + width * height * sizeof(uint16_t);

  char *buffer    = new char[size];

  memset(buffer, 0, size);

  return (void*)buffer;
}


PS_GPU *PS_GPU::Build(bool pal_clock_and_tv,
      int sls, int sle, uint8 upscale_shift)
{
  void *buffer = PS_GPU::Alloc(upscale_shift);

  // Place the new GPU inside the buffer
  return new (buffer) PS_GPU(pal_clock_and_tv, sls, sle, upscale_shift);
}

void GPU_Init(bool pal_clock_and_tv,
      int sls, int sle, uint8 upscale_shift)
{
   GPU = PS_GPU::Build(pal_clock_and_tv, sls, sle, upscale_shift);
}

void GPU_Destroy(void)
{
   if(GPU)
   {
      GPU->~PS_GPU();
      delete [] (char*)GPU;
   }
   GPU = NULL;
}

// Build a new GPU with a different upscale_shift
PS_GPU *PS_GPU::Rescale(uint8 ushift)
{
   void *buffer = PS_GPU::Alloc(ushift);

   return new (buffer) PS_GPU(*this, ushift);
}

void GPU_Reinit(uint8 ushift)
{
   // We successfully changed the frontend's resolution, we can
   // apply the change immediately
   PS_GPU *new_gpu = GPU->Rescale(ushift);
   GPU_Destroy();
   GPU = new_gpu;
}

void GPU_FillVideoParams(MDFNGI* gi)
{
   PS_GPU *gpu = (PS_GPU*)GPU;

   if(gpu->HardwarePALType)
   {
      gi->lcm_width = 2800;
      gi->lcm_height = (gpu->LineVisLast + 1 - gpu->LineVisFirst) * 2; //576;

      gi->nominal_width = 384;	// Dunno. :(
      gi->nominal_height = gpu->LineVisLast + 1 - gpu->LineVisFirst; //288;

      gi->fb_width = 768;
      gi->fb_height = 576;

      gi->fps = 836203078; // 49.842

      gi->VideoSystem = VIDSYS_PAL;
   }
   else
   {
      gi->lcm_width = 2800;
      gi->lcm_height = (gpu->LineVisLast + 1 - gpu->LineVisFirst) * 2; //480;

      gi->nominal_width = 320;
      gi->nominal_height = gpu->LineVisLast + 1 - gpu->LineVisFirst; //240;

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
   gi->mouse_offs_y = gpu->LineVisFirst;
}

void GPU_SoftReset(void) // Control command 0x00
{
   PS_GPU *gpu = (PS_GPU*)GPU;

   gpu->IRQPending = false;
   IRQ_Assert(IRQ_GPU, gpu->IRQPending);

   gpu->InvalidateCache();
   gpu->DMAControl = 0;

   if(gpu->DrawTimeAvail < 0)
      gpu->DrawTimeAvail = 0;

   gpu->BlitterFIFO.Flush();
   gpu->DataReadBufferEx = 0;
   gpu->InCmd = INCMD_NONE;

   gpu->DisplayOff = 1;
   gpu->DisplayFB_XStart = 0;
   gpu->DisplayFB_YStart = 0;

   gpu->DisplayMode = 0;

   gpu->HorizStart = 0x200;
   gpu->HorizEnd = 0xC00;

   gpu->VertStart = 0x10;
   gpu->VertEnd = 0x100;


   //
   gpu->TexPageX = 0;
   gpu->TexPageY = 0;

   gpu->SpriteFlip = 0;

   gpu->abr = 0;
   gpu->TexMode = 0;

   gpu->dtd = 0;
   gpu->dfe = 0;

   //
   gpu->tww = 0;
   gpu->twh = 0;
   gpu->twx = 0;
   gpu->twy = 0;

   gpu->RecalcTexWindowStuff();

   //
   gpu->ClipX0 = 0;
   gpu->ClipY0 = 0;

   //
   gpu->ClipX1 = 0;
   gpu->ClipY1 = 0;

   //
   gpu->OffsX = 0;
   gpu->OffsY = 0;

   //
   gpu->MaskSetOR = 0;
   gpu->MaskEvalAND = 0;

   gpu->TexDisable = false;
   gpu->TexDisableAllowChange = false;
}

void GPU_Power(void)
{
   PS_GPU *gpu = (PS_GPU*)GPU;

   memset(gpu->vram, 0, gpu->vram_npixels() * sizeof(*gpu->vram));

   memset(gpu->CLUT_Cache, 0, sizeof(gpu->CLUT_Cache));
   gpu->CLUT_Cache_VB = ~0U;

   memset(gpu->TexCache, 0xFF, sizeof(gpu->TexCache));

   gpu->DMAControl = 0;
   gpu->ClipX0     = 0;
   gpu->ClipY0     = 0;
   gpu->ClipX1     = 0;
   gpu->ClipY1     = 0;

   gpu->OffsX      = 0;
   gpu->OffsY      = 0;

   gpu->dtd        = false;
   gpu->dfe        = false;

   gpu->MaskSetOR  = 0;
   gpu->MaskEvalAND= 0;

   gpu->TexDisable = false;
   gpu->TexDisableAllowChange = false;

   gpu->tww = 0;
   gpu->twh = 0;
   gpu->twx = 0;
   gpu->twy = 0;

   gpu->RecalcTexWindowStuff();

   gpu->TexPageX = 0;
   gpu->TexPageY = 0;
   gpu->SpriteFlip = 0;

   gpu->abr = 0;
   gpu->TexMode = 0;

   gpu->BlitterFIFO.Flush();

   gpu->DataReadBuffer = 0; // Don't reset in SoftReset()
   gpu->DataReadBufferEx = 0;
   gpu->InCmd = INCMD_NONE;
   gpu->FBRW_X = 0;
   gpu->FBRW_Y = 0;
   gpu->FBRW_W = 0;
   gpu->FBRW_H = 0;
   gpu->FBRW_CurY = 0;
   gpu->FBRW_CurX = 0;

   gpu->DisplayMode = 0;
   gpu->DisplayOff = 1;
   gpu->DisplayFB_XStart = 0;
   gpu->DisplayFB_YStart = 0;

   gpu->HorizStart = 0;
   gpu->HorizEnd = 0;

   gpu->VertStart = 0;
   gpu->VertEnd = 0;

   //
   //
   //
   gpu->DisplayFB_CurYOffset = 0;
   gpu->DisplayFB_CurLineYReadout = 0;
   gpu->InVBlank = true;

   // TODO: factor out in a separate function.
   gpu->LinesPerField = 263;

   //
   //
   //
   gpu->scanline = 0;
   gpu->field = 0;
   gpu->field_ram_readout = 0;
   gpu->PhaseChange = 0;

   //
   //
   //
   gpu->DotClockCounter = 0;
   gpu->GPUClockCounter = 0;
   gpu->LineClockCounter = 3412 - 200;
   gpu->LinePhase = 0;

   gpu->DrawTimeAvail = 0;

   gpu->lastts = 0;

   GPU_SoftReset();

   IRQ_Assert(IRQ_VBLANK, gpu->InVBlank);
   TIMER_SetVBlank(gpu->InVBlank);
}

void GPU_ResetTS(void)
{
   PS_GPU *gpu = (PS_GPU*)GPU;
   gpu->lastts = 0;
}

#include "gpu_polygon.cpp"
#include "gpu_sprite.cpp"
#include "gpu_line.cpp"

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

void PS_GPU::InvalidateTexCache()
{
   unsigned i;
   for (i = 0; i < 256; i++)
      TexCache[i].Tag = ~0U;
}

void PS_GPU::InvalidateCache()
{
   CLUT_Cache_VB = ~0U;
   InvalidateTexCache();
}

static void Command_ClearCache(PS_GPU* g, const uint32 *cb)
{
   g->InvalidateCache();
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

   //printf("[GPU] FB Fill %d:%d w=%d, h=%d\n", destX, destY, width, height);
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

   g->InvalidateTexCache();
   //printf("FB Copy: %d %d %d %d %d %d\n", sourceX, sourceY, destX, destY, width, height);

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

   rsx_intf_copy_rect(sourceX, sourceY, destX, destY, width, height, g->MaskEvalAND != 0, g->MaskSetOR != 0);
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

   g->InvalidateTexCache();

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

   g->InvalidateTexCache();

   if(g->FBRW_W != 0 && g->FBRW_H != 0)
      g->InCmd = INCMD_FBREAD;
}

static void Command_DrawMode(PS_GPU* g, const uint32 *cb)
{
   const uint32 cmdw = *cb;

   g->SetTPage(cmdw);

   g->SpriteFlip = (cmdw & 0x3000);
   g->dtd =        (cmdw >> 9) & 1;
   g->dfe =        (cmdw >> 10) & 1;

   //printf("*******************DFE: %d -- scanline=%d\n", dfe, scanline);
}

static void Command_TexWindow(PS_GPU* g, const uint32 *cb)
{
   g->tww = (*cb & 0x1F);
   g->twh = ((*cb >> 5) & 0x1F);
   g->twx = ((*cb >> 10) & 0x1F);
   g->twy = ((*cb >> 15) & 0x1F);

   g->RecalcTexWindowStuff();
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

   //fprintf(stderr, "[GPU] Drawing offset: %d(raw=%d) %d(raw=%d) -- %d\n", OffsX, *cb, OffsY, *cb >> 11, scanline);
}

static void Command_MaskSetting(PS_GPU* g, const uint32 *cb)
{
   //printf("Mask setting: %08x\n", *cb);
   g->MaskSetOR   = (*cb & 1) ? 0x8000 : 0x0000;
   g->MaskEvalAND = (*cb & 2) ? 0x8000 : 0x0000;

   rsx_intf_set_mask_setting(g->MaskSetOR, g->MaskEvalAND);
}

CTEntry PS_GPU::Commands[256] =
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

static void ProcessFIFO(uint32_t in_count)
{
   uint32_t CB[0x10], InData;
   unsigned i;
   unsigned command_len;
   uint32_t cc            = GPU->InCmd_CC;
   const CTEntry *command = &GPU->Commands[cc];
   bool read_fifo         = false;

   switch (GPU->InCmd)
   {
      default:
      case INCMD_NONE:
         break;
      case INCMD_FBWRITE:
         InData = GPU->BlitterFIFO.Read();

         for(i = 0; i < 2; i++)
         {
            bool fetch = texel_fetch(GPU, GPU->FBRW_CurX & 1023, GPU->FBRW_CurY & 511) & GPU->MaskEvalAND;

            if (!fetch)
               texel_put(GPU->FBRW_CurX & 1023, GPU->FBRW_CurY & 511, InData | GPU->MaskSetOR);

            GPU->FBRW_CurX++;
            if(GPU->FBRW_CurX == (GPU->FBRW_X + GPU->FBRW_W))
            {
               GPU->FBRW_CurX = GPU->FBRW_X;
               GPU->FBRW_CurY++;
               if(GPU->FBRW_CurY == (GPU->FBRW_Y + GPU->FBRW_H))
               {
                  /* Upload complete, send over to RSX */
                  rsx_intf_load_image(
                        GPU->FBRW_X, GPU->FBRW_Y,
                        GPU->FBRW_W, GPU->FBRW_H,
                        GPU->vram,
                        GPU->MaskEvalAND != 0,
                        GPU->MaskSetOR != 0);
                  GPU->InCmd = INCMD_NONE;
                  break;	// Break out of the for() loop.
               }
            }
            InData >>= 16;
         }
         return;

      case INCMD_QUAD:
         if(GPU->DrawTimeAvail < 0)
            return;

         command_len      = 1 + (bool)(cc & 0x4) + (bool)(cc & 0x10);
         read_fifo = true;
         break;
      case INCMD_PLINE:
         if(GPU->DrawTimeAvail < 0)
            return;

         command_len        = 1 + (bool)(GPU->InCmd_CC & 0x10);

         if((GPU->BlitterFIFO.Peek() & 0xF000F000) == 0x50005000)
         {
            GPU->BlitterFIFO.Read();
            GPU->InCmd = INCMD_NONE;
            return;
         }

         read_fifo = true;
         break;
   }

   if (!read_fifo)
   {
      cc          = GPU->BlitterFIFO.Peek() >> 24;
      command     = &GPU->Commands[cc];
      command_len = command->len;

      if(GPU->DrawTimeAvail < 0 && !command->ss_cmd)
         return;
   }

   if(in_count < command_len)
      return;

   for (i = 0; i < command_len; i++)
   {
	   PGXP_WriteCB(PGXP_ReadFIFO(GPU->BlitterFIFO.read_pos), i);
	   CB[i] = GPU->BlitterFIFO.Read();
   }

   if (!read_fifo)
   {
      if(!command->ss_cmd)
         GPU->DrawTimeAvail -= 2;

      // A very very ugly kludge to support
      // texture mode specialization.
      // fixme/cleanup/SOMETHING in the future.
      if(cc >= 0x20 && cc <= 0x3F && (cc & 0x4))
      {
         /* Don't alter SpriteFlip here. */
         GPU->SetTPage(CB[4 + ((cc >> 4) & 0x1)] >> 16);
      }
   }

   if ((cc >= 0x80) && (cc <= 0x9F))
      Command_FBCopy(GPU, CB);
   else if ((cc >= 0xA0) && (cc <= 0xBF))
      Command_FBWrite(GPU, CB);
   else if ((cc >= 0xC0) && (cc <= 0xDF))
      Command_FBRead(GPU, CB);
   else
   {
	   if (command->func[GPU->abr][GPU->TexMode])
		   command->func[GPU->abr][GPU->TexMode | (GPU->MaskEvalAND ? 0x4 : 0x0)](GPU, CB);
   }
}

static INLINE void GPU_WriteCB(uint32_t InData, uint32_t addr)
{
   if(GPU->BlitterFIFO.in_count >= 0x10
         && 
         ( GPU->InCmd != INCMD_NONE || 
          (GPU->BlitterFIFO.in_count - 0x10) >= GPU->Commands[GPU->BlitterFIFO.Peek() >> 24].fifo_fb_len))
   {
      PSX_DBG(PSX_DBG_WARNING, "GPU FIFO overflow!!!\n");
      return;
   }

   PGXP_WriteFIFO(ReadMem(addr), GPU->BlitterFIFO.write_pos);
   GPU->BlitterFIFO.Write(InData);

   if(GPU->BlitterFIFO.in_count && GPU->InCmd != INCMD_FBREAD)
      ProcessFIFO(GPU->BlitterFIFO.in_count);
}

void PS_GPU::SetTPage(const uint32_t cmdw)
{
   const unsigned NewTexPageX = (cmdw & 0xF) * 64;
   const unsigned NewTexPageY = (cmdw & 0x10) * 16;
   const unsigned NewTexMode  = (cmdw >> 7) & 0x3;

   this->abr = (cmdw >> 5) & 0x3;

   if(!NewTexMode != !TexMode || NewTexPageX != TexPageX || NewTexPageY != TexPageY)
      InvalidateTexCache();

   if(TexDisableAllowChange)
   {
      bool NewTexDisable = (cmdw >> 11) & 1;

      if (NewTexDisable != TexDisable)
         InvalidateTexCache();

      TexDisable = NewTexDisable;
      //printf("TexDisable: %02x\n", TexDisable);
   }

   TexPageX = NewTexPageX;
   TexPageY = NewTexPageY;
   TexMode  = NewTexMode;
}

static void UpdateDisplayMode(void)
{
   bool depth_24bpp = !!(GPU->DisplayMode & 0x10);

   uint16_t yres = GPU->VertEnd - GPU->VertStart;

   // Both 2nd bit and 5th bit have to be enabled to use interlacing properly.
   if((GPU->DisplayMode & (DISP_INTERLACED | DISP_VERT480)) == (DISP_INTERLACED | DISP_VERT480))
      yres *= 2;

   unsigned pixelclock_divider;

   if ((GPU->DisplayMode >> 6) & 1)
   {
      // HRes ~ 368pixels
      pixelclock_divider = 7;
   }
   else
   {
      switch (GPU->DisplayMode & 3)
      {
         case 0:
            // Hres ~ 256pixels
            pixelclock_divider = 10;
            break;
         case 1:
            // Hres ~ 320pixels
            pixelclock_divider = 8;
            break;
         case 2:
            // Hres ~ 512pixels
            pixelclock_divider = 5;
            break;
         default:
            // Hres ~ 640pixels
            pixelclock_divider = 4;
            break;
      }
   }

   // First we get the horizontal range in number of pixel clock period
   uint16_t xres = (GPU->HorizEnd - GPU->HorizStart);

   // Then we apply the divider
   xres /= pixelclock_divider;

   // Then the rounding formula straight outta No$
   xres = (xres + 2) & ~3;

   rsx_intf_set_display_mode(
         GPU->DisplayFB_XStart,
         GPU->DisplayFB_YStart,
         xres, yres,
         depth_24bpp);
}

void GPU_Write(const int32_t timestamp, uint32_t A, uint32_t V)
{
   PS_GPU *gpu = (PS_GPU*)GPU;

   V <<= (A & 3) * 8;

   if(A & 4)	// GP1 ("Control")
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
         case 0x00:	// Reset GPU
            //printf("\n\n************ Soft Reset %u ********* \n\n", scanline);
            GPU_SoftReset();
             rsx_intf_set_draw_area(gpu->ClipX0, gpu->ClipY0,
                   gpu->ClipX1, gpu->ClipY1);
             UpdateDisplayMode();
            break;

         case 0x01:	// Reset command buffer
            if(gpu->DrawTimeAvail < 0)
               gpu->DrawTimeAvail = 0;
            gpu->BlitterFIFO.Flush();
            gpu->InCmd = INCMD_NONE;
            break;

         case 0x02: 	// Acknowledge IRQ
            gpu->IRQPending = false;
            IRQ_Assert(IRQ_GPU, gpu->IRQPending);
            break;

         case 0x03:	// Display enable
            gpu->DisplayOff = V & 1;
            rsx_intf_toggle_display(gpu->DisplayOff);
            break;

         case 0x04:	// DMA Setup
            gpu->DMAControl = V & 0x3;
            break;

         case 0x05:	// Start of display area in framebuffer
            gpu->DisplayFB_XStart = V & 0x3FE; // Lower bit is apparently ignored.
            gpu->DisplayFB_YStart = (V >> 10) & 0x1FF;
            gpu->display_change_count++;
            break;

         case 0x06:	// Horizontal display range
            gpu->HorizStart = V & 0xFFF;
            gpu->HorizEnd = (V >> 12) & 0xFFF;
            break;

         case 0x07:
            gpu->VertStart = V & 0x3FF;
            gpu->VertEnd = (V >> 10) & 0x3FF;
            break;

         case 0x08:
            //printf("\n\nDISPLAYMODE SET: 0x%02x, %u *************************\n\n\n", V & 0xFF, scanline);
            gpu->DisplayMode = V & 0xFF;
            UpdateDisplayMode();
            break;

         case 0x09:
            gpu->TexDisableAllowChange = V & 1;
            break;

         case 0x10:	// GPU info(?)
            switch(V & 0xF)
            {
               // DataReadBuffer must remain unchanged for any unhandled GPU info index.
               default:
                  break;
               case 0x2:
                  gpu->DataReadBufferEx &= 0xFFF00000;
                  gpu->DataReadBufferEx |= (gpu->tww << 0) | (gpu->twh << 5) | (gpu->twx << 10) | (gpu->twy << 15);
                  gpu->DataReadBuffer    = gpu->DataReadBufferEx;
                  break;
               case 0x3:
                  gpu->DataReadBufferEx &= 0xFFF00000;
                  gpu->DataReadBufferEx |= (gpu->ClipY0 << 10) | gpu->ClipX0;
                  gpu->DataReadBuffer = gpu->DataReadBufferEx;
                  break;

               case 0x4:
                  gpu->DataReadBufferEx &= 0xFFF00000;
                  gpu->DataReadBufferEx |= (gpu->ClipY1 << 10) | gpu->ClipX1;
                  gpu->DataReadBuffer = gpu->DataReadBufferEx;
                  break;

               case 0x5:
                  gpu->DataReadBufferEx &= 0xFFC00000;
                  gpu->DataReadBufferEx |= (gpu->OffsX & 2047) | ((gpu->OffsY & 2047) << 11);
                  gpu->DataReadBuffer = gpu->DataReadBufferEx;
                  break;

               case 0x7:
                  gpu->DataReadBufferEx = 2;
                  gpu->DataReadBuffer = gpu->DataReadBufferEx;
                  break;

               case 0x8:
                  gpu->DataReadBufferEx = 0;
                  gpu->DataReadBuffer = gpu->DataReadBufferEx;
                  break;
            }
            break;

      }
   }
   else		// GP0 ("Data")
   {
      //uint32_t command = V >> 24;
      //printf("Meow command: %02x\n", command);
      //assert(!(gpu->DMAControl & 2));
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

   GPU->DataReadBufferEx = 0;

   for(i = 0; i < 2; i++)
   {
      GPU->DataReadBufferEx |=
         texel_fetch(GPU,
               GPU->FBRW_CurX & 1023,
               GPU->FBRW_CurY & 511) << (i * 16);

      GPU->FBRW_CurX++;
      if(GPU->FBRW_CurX == (GPU->FBRW_X + GPU->FBRW_W))
      {
         if((GPU->FBRW_CurY + 1) == (GPU->FBRW_Y + GPU->FBRW_H))
            GPU->InCmd = INCMD_NONE;
         else
         {
            GPU->FBRW_CurY++;
            GPU->FBRW_CurX = GPU->FBRW_X;
         }
      }
   }

   return GPU->DataReadBufferEx;
}

uint32_t GPU_ReadDMA(void)
{
   if(GPU->InCmd != INCMD_FBREAD)
      return GPU->DataReadBuffer;
   return GPU_ReadData();
}

uint32_t GPU_Read(const int32_t timestamp, uint32_t A)
{
   uint32_t ret = 0;
   PS_GPU *gpu  = (PS_GPU*)GPU;

   if(A & 4)	// Status
   {
      ret = (((gpu->DisplayMode << 1) & 0x7F) | ((gpu->DisplayMode >> 6) & 1)) << 16;

      ret |= (gpu->DisplayMode & 0x80) << 7;

      ret |= gpu->DMAControl << 29;

      ret |= (gpu->DisplayFB_CurLineYReadout & 1) << 31;

      ret |= (!gpu->field) << 13;

      if(gpu->DMAControl & 0x02)
         ret |= 1 << 25;

      ret |= gpu->IRQPending << 24;

      ret |= gpu->DisplayOff << 23;

      /* GPU idle bit */
      if(gpu->InCmd == INCMD_NONE && gpu->DrawTimeAvail >= 0
            && gpu->BlitterFIFO.in_count == 0x00)
         ret |= 1 << 26;

      if(gpu->InCmd == INCMD_FBREAD)	// Might want to more accurately emulate this in the future?
         ret |= (1 << 27);

      ret |= CalcFIFOReadyBit() << 28;		// FIFO has room bit? (kinda).

      //
      //
      ret |= gpu->TexPageX >> 6;
      ret |= gpu->TexPageY >> 4;
      ret |= gpu->abr << 5;
      ret |= gpu->TexMode << 7;

      ret |= gpu->dtd << 9;
      ret |= gpu->dfe << 10;

      if(gpu->MaskSetOR)
         ret |= 1 << 11;

      if(gpu->MaskEvalAND)
         ret |= 1 << 12;

      ret |= gpu->TexDisable << 15;
   }
   else		// "Data"
   {
      if(gpu->InCmd == INCMD_FBREAD)
         ret = GPU_ReadData();
      else
         ret = gpu->DataReadBuffer;
   }

#if 0
   if(gpu->DMAControl & 2)
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

   if(bpp24)	// 24bpp
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
   }				// 15bpp
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
   PS_GPU *gpu = (PS_GPU*)GPU;
   static const uint32_t DotClockRatios[5] = { 10, 8, 5, 4, 7 };
   const uint32_t dmc = (gpu->DisplayMode & 0x40) ? 4 : (gpu->DisplayMode & 0x3);
   const uint32_t dmw = 2800 / DotClockRatios[dmc];	// Must be <= 768
   int32_t sys_clocks = sys_timestamp - gpu->lastts;

   //printf("GPUISH: %d\n", sys_timestamp - gpu->lastts);

   if(!sys_clocks)
      goto TheEnd;

   gpu->DrawTimeAvail += sys_clocks << 1;

   if(gpu->DrawTimeAvail > 256)
      gpu->DrawTimeAvail = 256;

   if(gpu->BlitterFIFO.in_count && GPU->InCmd != INCMD_FBREAD)
      ProcessFIFO(gpu->BlitterFIFO.in_count);

   //puts("GPU Update Start");

   gpu->GPUClockCounter += (uint64)sys_clocks * gpu->GPUClockRatio;

   gpu_clocks       = gpu->GPUClockCounter >> 16;
   gpu->GPUClockCounter -= gpu_clocks << 16;

   while(gpu_clocks > 0)
   {
      int32 chunk_clocks = gpu_clocks;
      int32 dot_clocks;

      if(chunk_clocks > gpu->LineClockCounter)
      {
         //printf("Chunk: %u, LCC: %u\n", chunk_clocks, LineClockCounter);
         chunk_clocks = gpu->LineClockCounter;
      }

      gpu_clocks -= chunk_clocks;
      gpu->LineClockCounter -= chunk_clocks;

      gpu->DotClockCounter += chunk_clocks;
      dot_clocks = gpu->DotClockCounter / DotClockRatios[gpu->DisplayMode & 0x3];
      gpu->DotClockCounter -= dot_clocks * DotClockRatios[gpu->DisplayMode & 0x3];

      TIMER_AddDotClocks(dot_clocks);


      if(!gpu->LineClockCounter)
      {
         // We could just call this at the top of GPU_Update(), but
         // do it here for slightly less CPU usage(presumably).
         PSX_SetEventNT(PSX_EVENT_TIMER, TIMER_Update(sys_timestamp));

         gpu->LinePhase = (gpu->LinePhase + 1) & 1;

         if(gpu->LinePhase)
         {
            TIMER_SetHRetrace(true);
            gpu->LineClockCounter = 200;
            TIMER_ClockHRetrace();
         }
         else
         {
            const unsigned int FirstVisibleLine =
               gpu->LineVisFirst + (gpu->HardwarePALType ? 20 : 16);
            const unsigned int VisibleLineCount =
               gpu->LineVisLast + 1 - gpu->LineVisFirst; //HardwarePALType ? 288 : 240;

            TIMER_SetHRetrace(false);

            if(gpu->DisplayMode & DISP_PAL)
               gpu->LineClockCounter = 3405 - 200;
            else
               gpu->LineClockCounter = 3412 + gpu->PhaseChange - 200;

            gpu->scanline = (gpu->scanline + 1) % gpu->LinesPerField;
            gpu->PhaseChange = !gpu->PhaseChange;

#ifdef WANT_DEBUGGER
            DBG_GPUScanlineHook(gpu->scanline);
#endif

            //
            //
            //
            if(gpu->scanline == (gpu->HardwarePALType ? 308 : 256))	// Will need to be redone if we ever allow for visible vertical overscan with NTSC.
            {
               if(gpu->sl_zero_reached)
               {
                  //printf("Req Exit(visible fallthrough case): %u\n", gpu->scanline);
                  PSX_RequestMLExit();
               }
            }

            if(gpu->scanline == (gpu->LinesPerField - 1))
            {
               if(gpu->sl_zero_reached)
               {
                  //printf("Req Exit(final fallthrough case): %u\n", gpu->scanline);
                  PSX_RequestMLExit();
               }

               if(gpu->DisplayMode & DISP_INTERLACED)
                  gpu->field = !gpu->field;
               else
                  gpu->field = 0;
            }

            if(gpu->scanline == 0)
            {
               assert(gpu->sl_zero_reached == false);
               gpu->sl_zero_reached = true;

               if(gpu->DisplayMode & DISP_INTERLACED)
               {
                  if(gpu->DisplayMode & DISP_PAL)
                     gpu->LinesPerField = 313 - gpu->field;
                  else                   // NTSC
                     gpu->LinesPerField = 263 - gpu->field;
               }
               else
               {
                  gpu->field = 0;  // May not be the correct place for this?

                  if(gpu->DisplayMode & DISP_PAL)
                     gpu->LinesPerField = 314;
                  else			// NTSC
                     gpu->LinesPerField = 263;
               }


               if (rsx_intf_is_type() == RSX_SOFTWARE && gpu->espec)
               {
                  if((bool)(gpu->DisplayMode & DISP_PAL) != gpu->HardwarePALType)
                  {
                     gpu->DisplayRect->x = 0;
                     gpu->DisplayRect->y = 0;
                     gpu->DisplayRect->w = 384;
                     gpu->DisplayRect->h = VisibleLineCount;

                     for(int32 y = 0; y < gpu->DisplayRect->h; y++)
                     {
                        uint32_t *dest = gpu->surface->pixels + y * gpu->surface->pitch32;

                        gpu->LineWidths[y] = 384;

                        memset(dest, 0, 384 * sizeof(int32));
                     }

                     //char buffer[256];
                     //snprintf(buffer, sizeof(buffer), _("VIDEO STANDARD MISMATCH"));
                     //DrawTextTrans(surface->pixels + ((DisplayRect->h / 2) - (13 / 2)) * surface->pitch32, surface->pitch32 << 2, DisplayRect->w, (UTF8*)buffer,
                     //MAKECOLOR(0x00, 0xFF, 0x00), true, MDFN_FONT_6x13_12x13, 0);
                  }
                  else
                  {
                     gpu->espec->InterlaceOn = (bool)(gpu->DisplayMode & DISP_INTERLACED);
                     gpu->espec->InterlaceField = (bool)(gpu->DisplayMode & DISP_INTERLACED) && gpu->field;

                     gpu->DisplayRect->x = 0;
                     gpu->DisplayRect->y = 0;
                     gpu->DisplayRect->w = 0;
                     gpu->DisplayRect->h = VisibleLineCount << (bool)(gpu->DisplayMode & DISP_INTERLACED);

                     // Clear ~0 state.
                     gpu->LineWidths[0] = 0;

                     for(int i = 0; i < (gpu->DisplayRect->y + gpu->DisplayRect->h); i++)
                     {
                        gpu->surface->pixels[i * gpu->surface->pitch32 + 0] =
                           gpu->surface->pixels[i * gpu->surface->pitch32 + 1] = 0;
                        gpu->LineWidths[i] = 2;
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
            if(gpu->scanline == gpu->VertEnd && !gpu->InVBlank)
            {
               if(gpu->sl_zero_reached)
               {
                  // Gameplay in Descent(NTSC) has vblank at scanline 236
                  //
                  // Mikagura Shoujo Tanteidan has vblank at scanline 192 during intro
                  //  FMV(which we don't handle here because low-latency in that case is not so important).
                  //
                  if(gpu->scanline >= (gpu->HardwarePALType ? 260 : 232))
                  {
                     //printf("Req Exit(vblank case): %u\n", gpu->scanline);
                     PSX_RequestMLExit();
                  }
#if 0
                  else
                  {
                     //printf("VBlank too early, chickening out early exit: %u!\n", gpu->scanline);
                  }
#endif
               }

               //printf("VBLANK: %u\n", gpu->scanline);
               gpu->InVBlank = true;

               gpu->DisplayFB_CurYOffset = 0;

               if((gpu->DisplayMode & 0x24) == 0x24)
                  gpu->field_ram_readout = !gpu->field;
               else
                  gpu->field_ram_readout = 0;
            }

            if(gpu->scanline == gpu->VertStart && gpu->InVBlank)
            {
               gpu->InVBlank = false;

               // Note to self: X-Men Mutant Academy
               // relies on this being set on the proper
               // scanline in 480i mode(otherwise it locks up on startup).
               //if(HeightMode)
               // DisplayFB_CurYOffset = field;
            }

            IRQ_Assert(IRQ_VBLANK, gpu->InVBlank);
            TIMER_SetVBlank(gpu->InVBlank);

            unsigned displayfb_yoffset = gpu->DisplayFB_CurYOffset;

            // Needs to occur even in vblank.
            // Not particularly confident about the timing
            // of this in regards to vblank and the
            // upper bit(ODE) of the GPU status port, though the
            // test that showed an oddity was pathological in
            // that VertEnd < VertStart in it.
            if((gpu->DisplayMode & 0x24) == 0x24)
               displayfb_yoffset = (gpu->DisplayFB_CurYOffset << 1) + (gpu->InVBlank ? 0 : gpu->field_ram_readout);

            gpu->DisplayFB_CurLineYReadout = (gpu->DisplayFB_YStart + displayfb_yoffset) & 0x1FF;

            unsigned dmw_width = 0;
            unsigned pix_clock_offset = 0;
            unsigned pix_clock = 0;
            unsigned pix_clock_div = 0;
            uint32_t *dest = NULL;

            if(      (bool)(gpu->DisplayMode & DISP_PAL) == gpu->HardwarePALType
                  && gpu->scanline >= FirstVisibleLine
                  && gpu->scanline < (FirstVisibleLine + VisibleLineCount))
            {
               int32 fb_x      = gpu->DisplayFB_XStart * 2;
               int32 dx_start  = gpu->HorizStart, dx_end = gpu->HorizEnd;
               int32 dest_line =
                  ((gpu->scanline - FirstVisibleLine) << gpu->espec->InterlaceOn)
                  + gpu->espec->InterlaceField;

               if(dx_end < dx_start)
                  dx_end = dx_start;

               dx_start = dx_start / DotClockRatios[dmc];
               dx_end = dx_end / DotClockRatios[dmc];

               dx_start -= 488 / DotClockRatios[dmc];
               dx_end -= 488 / DotClockRatios[dmc];

               if(dx_start < 0)
               {
                  fb_x -= dx_start * ((gpu->DisplayMode & DISP_RGB24) ? 3 : 2);
                  fb_x &= 0x7FF; //0x3FF;
                  dx_start = 0;
               }

               if((uint32)dx_end > dmw)
                  dx_end = dmw;

               if(gpu->InVBlank || gpu->DisplayOff)
                  dx_start = dx_end = 0;

               gpu->LineWidths[dest_line] = dmw;

               //printf("dx_start base: %d, dmw: %d\n", dx_start, dmw);

               if (rsx_intf_is_type() == RSX_SOFTWARE)
               {
                  // Convert the necessary variables to the upscaled version
                  uint32_t x;
                  uint32_t y        = gpu->DisplayFB_CurLineYReadout << gpu->upscale_shift;
                  uint32_t udmw     = dmw      << gpu->upscale_shift;
                  int32 udx_start   = dx_start << gpu->upscale_shift;
                  int32 udx_end     = dx_end   << gpu->upscale_shift;
                  int32 ufb_x       = fb_x     << gpu->upscale_shift;
                  unsigned _upscale = gpu->upscale();

                  for (uint32_t i = 0; i < _upscale; i++)
                  {
                     const uint16_t *src = gpu->vram +
                        ((y + i) << (10 + gpu->upscale_shift));

                     // printf("surface: %dx%d (%d) %u %u + %u\n",
                     // 	   surface->w, surface->h, surface->pitchinpix,
                     // 	   dest_line, y, i);

                     dest = gpu->surface->pixels +
                        ((dest_line << gpu->upscale_shift) + i) * gpu->surface->pitch32;
                     memset(dest, 0, udx_start * sizeof(int32));

                     //printf("%d %d %d - %d %d\n", scanline, dx_start, dx_end, HorizStart, HorizEnd);
                     ReorderRGB_Var(
                           RED_SHIFT,
                           GREEN_SHIFT,
                           BLUE_SHIFT,
                           gpu->DisplayMode & DISP_RGB24,
                           src,
                           dest,
                           udx_start,
                           udx_end,
                           ufb_x,
                           gpu->upscale_shift,
                           _upscale);

                     //printf("dx_end: %d, dmw: %d\n", udx_end, udmw);
                     //
                     for(x = udx_end; x < udmw; x++)
                        dest[x] = 0;
                  }
               }

               //if(gpu->scanline == 64)
               // printf("%u\n", sys_timestamp - ((uint64)gpu_clocks * 65536) / gpu->GPUClockRatio);

               dmw_width = dmw;
               pix_clock_offset = (488 - 146) / DotClockRatios[dmc];
               pix_clock = (gpu->HardwarePALType ? 53203425 : 53693182) / DotClockRatios[dmc];
               pix_clock_div = DotClockRatios[dmc];
            }
            // XXX fixme when upscaling is active
            PSX_GPULineHook(sys_timestamp,
                  sys_timestamp - ((uint64)gpu_clocks * 65536) / gpu->GPUClockRatio, gpu->scanline == 0,
                  dest,
                  &gpu->surface->format,
                  dmw_width,
                  pix_clock_offset,
                  pix_clock,
                  pix_clock_div);

            if(!gpu->InVBlank)
               gpu->DisplayFB_CurYOffset = (gpu->DisplayFB_CurYOffset + 1) & 0x1FF;
         }

         // Mostly so the next event time gets
         // recalculated properly in regards to our calls
         PSX_SetEventNT(PSX_EVENT_TIMER, TIMER_Update(sys_timestamp));

         // to TIMER_SetVBlank() and TIMER_SetHRetrace().
      }	// end if(!LineClockCounter)
   }	// end while(gpu_clocks > 0)

   //puts("GPU Update End");

TheEnd:
   gpu->lastts = sys_timestamp;

   int32 next_dt = gpu->LineClockCounter;

   next_dt = (((int64)next_dt << 16) - gpu->GPUClockCounter + gpu->GPUClockRatio - 1) / gpu->GPUClockRatio;

   next_dt = std::max<int32>(1, next_dt);
   next_dt = std::min<int32>(128, next_dt);

   //printf("%d\n", next_dt);

   return(sys_timestamp + next_dt);
}

void GPU_StartFrame(EmulateSpecStruct *espec_arg)
{
   PS_GPU *gpu = (PS_GPU*)GPU;

   gpu->sl_zero_reached = false;
   gpu->espec           = espec_arg;
   gpu->surface         = gpu->espec->surface;
   gpu->DisplayRect     = &gpu->espec->DisplayRect;
   gpu->LineWidths      = gpu->espec->LineWidths;
}

int GPU_StateAction(StateMem *sm, int load, int data_only)
{
   uint32 TexCache_Tag[256];
   uint16 TexCache_Data[256][4];
   uint16 *vram_new = NULL;
   PS_GPU *gpu      = (PS_GPU*)GPU;

   if (gpu->upscale_shift == 0)
   {
      // No upscaling, we can dump the VRAM contents directly
      vram_new = gpu->vram;
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
               vram_new[y * 1024 + x] = texel_fetch(gpu, x, y);
         }
      }
   }

   for(unsigned i = 0; i < 256; i++)
   {
      TexCache_Tag[i] = gpu->TexCache[i].Tag;

      for(unsigned j = 0; j < 4; j++)
         TexCache_Data[i][j] = gpu->TexCache[i].Data[j];

   }
   SFORMAT StateRegs[] =
   {
      // Hardcode entry name to remain backward compatible with the
      // previous fixed internal resolution code
      SFARRAY16N(vram_new, 1024 * 512, "&GPURAM[0][0]"),

      SFVARN(gpu->DMAControl, "DMAControl"),

      SFVARN(gpu->ClipX0, "ClipX0"),
      SFVARN(gpu->ClipY0, "ClipY0"),
      SFVARN(gpu->ClipX1, "ClipX1"),
      SFVARN(gpu->ClipY1, "ClipY1"),

      SFVARN(gpu->OffsX, "OffsX"),
      SFVARN(gpu->OffsY, "OffsY"),

      SFVARN(gpu->dtd, "dtd"),
      SFVARN(gpu->dfe, "dfe"),

      SFVARN(gpu->MaskSetOR, "MaskSetOR"),
      SFVARN(gpu->MaskEvalAND, "MaskEvalAND"),

      SFVARN(gpu->TexDisable, "TexDisable"),
      SFVARN(gpu->TexDisableAllowChange, "TexDisableAllowChange"),

      SFVARN(gpu->tww, "tww"),
      SFVARN(gpu->twh, "twh"),
      SFVARN(gpu->twx, "twx"),
      SFVARN(gpu->twy, "twy"),

      SFVARN(gpu->TexPageX, "TexPageX"),
      SFVARN(gpu->TexPageY, "TexPageY"),

      SFVARN(gpu->SpriteFlip, "SpriteFlip"),

      SFVARN(gpu->abr, "abr"),
      SFVARN(gpu->TexMode, "TexMode"),

      SFARRAY32N(&gpu->BlitterFIFO.data[0], sizeof(gpu->BlitterFIFO.data) / sizeof(gpu->BlitterFIFO.data[0]), "&BlitterFIFO.data[0]"),
      SFVARN(gpu->BlitterFIFO.read_pos, "BlitterFIFO.read_pos"),
      SFVARN(gpu->BlitterFIFO.write_pos, "BlitterFIFO.write_pos"),
      SFVARN(gpu->BlitterFIFO.in_count, "BlitterFIFO.in_count"),

      SFVARN(gpu->DataReadBuffer, "DataReadBuffer"),
      SFVARN(gpu->DataReadBufferEx, "DataReadBufferEx"),

      SFVARN(gpu->IRQPending, "IRQPending"),

      SFVARN(gpu->InCmd, "InCmd"),
      SFVARN(gpu->InCmd_CC, "InCmd_CC"),

      SFVARN(gpu->InQuad_F3Vertices[0].x, "InQuad_F3Vertices[0].x"),
      SFVARN(gpu->InQuad_F3Vertices[0].y, "InQuad_F3Vertices[0].y"),
      SFVARN(gpu->InQuad_F3Vertices[0].u, "InQuad_F3Vertices[0].u"),
      SFVARN(gpu->InQuad_F3Vertices[0].v, "InQuad_F3Vertices[0].v"),
      SFVARN(gpu->InQuad_F3Vertices[0].r, "InQuad_F3Vertices[0].r"),
      SFVARN(gpu->InQuad_F3Vertices[0].g, "InQuad_F3Vertices[0].g"),
      SFVARN(gpu->InQuad_F3Vertices[0].b, "InQuad_F3Vertices[0].b"),

      SFVARN(gpu->InQuad_F3Vertices[1].x, "InQuad_F3Vertices[1].x"),
      SFVARN(gpu->InQuad_F3Vertices[1].y, "InQuad_F3Vertices[1].y"),
      SFVARN(gpu->InQuad_F3Vertices[1].u, "InQuad_F3Vertices[1].u"),
      SFVARN(gpu->InQuad_F3Vertices[1].v, "InQuad_F3Vertices[1].v"),
      SFVARN(gpu->InQuad_F3Vertices[1].r, "InQuad_F3Vertices[1].r"),
      SFVARN(gpu->InQuad_F3Vertices[1].g, "InQuad_F3Vertices[1].g"),
      SFVARN(gpu->InQuad_F3Vertices[1].b, "InQuad_F3Vertices[1].b"),

      SFVARN(gpu->InQuad_F3Vertices[2].x, "InQuad_F3Vertices[2].x"),
      SFVARN(gpu->InQuad_F3Vertices[2].y, "InQuad_F3Vertices[2].y"),
      SFVARN(gpu->InQuad_F3Vertices[2].u, "InQuad_F3Vertices[2].u"),
      SFVARN(gpu->InQuad_F3Vertices[2].v, "InQuad_F3Vertices[2].v"),
      SFVARN(gpu->InQuad_F3Vertices[2].r, "InQuad_F3Vertices[2].r"),
      SFVARN(gpu->InQuad_F3Vertices[2].g, "InQuad_F3Vertices[2].g"),
      SFVARN(gpu->InQuad_F3Vertices[2].b, "InQuad_F3Vertices[2].b"),

      SFVARN(gpu->InPLine_PrevPoint.x, "InPLine_PrevPoint.x"),
      SFVARN(gpu->InPLine_PrevPoint.y, "InPLine_PrevPoint.y"),
      SFVARN(gpu->InPLine_PrevPoint.r, "InPLine_PrevPoint.r"),
      SFVARN(gpu->InPLine_PrevPoint.g, "InPLine_PrevPoint.g"),
      SFVARN(gpu->InPLine_PrevPoint.b, "InPLine_PrevPoint.b"),

      SFVARN(gpu->FBRW_X, "FBRW_X"),
      SFVARN(gpu->FBRW_Y, "FBRW_Y"),
      SFVARN(gpu->FBRW_W, "FBRW_W"),
      SFVARN(gpu->FBRW_H, "FBRW_H"),
      SFVARN(gpu->FBRW_CurY, "FBRW_CurY"),
      SFVARN(gpu->FBRW_CurX, "FBRW_CurX"),

      SFVARN(gpu->DisplayMode, "DisplayMode"),
      SFVARN(gpu->DisplayOff, "DisplayOff"),
      SFVARN(gpu->DisplayFB_XStart, "DisplayFB_XStart"),
      SFVARN(gpu->DisplayFB_YStart, "DisplayFB_YStart"),

      SFVARN(gpu->HorizStart, "HorizStart"),
      SFVARN(gpu->HorizEnd, "HorizEnd"),

      SFVARN(gpu->VertStart, "VertStart"),
      SFVARN(gpu->VertEnd, "VertEnd"),

      SFVARN(gpu->DisplayFB_CurYOffset, "DisplayFB_CurYOffset"),
      SFVARN(gpu->DisplayFB_CurLineYReadout, "DisplayFB_CurLineYReadout"),

      SFVARN(gpu->InVBlank, "InVBlank"),

      SFVARN(gpu->LinesPerField, "LinesPerField"),
      SFVARN(gpu->scanline, "scanline"),
      SFVARN(gpu->field, "field"),
      SFVARN(gpu->field_ram_readout, "field_ram_readout"),
      SFVARN(gpu->PhaseChange, "PhaseChange"),

      SFVARN(gpu->DotClockCounter, "DotClockCounter"),

      SFVARN(gpu->GPUClockCounter, "GPUClockCounter"),
      SFVARN(gpu->LineClockCounter, "LineClockCounter"),
      SFVARN(gpu->LinePhase, "LinePhase"),

      SFVARN(gpu->DrawTimeAvail, "DrawTimeAvail"),

      SFEND
   };

   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "GPU");

   if (gpu->upscale_shift > 0)
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

   if(load)
   {
      for(unsigned i = 0; i < 256; i++)
      {
         gpu->TexCache[i].Tag = TexCache_Tag[i];

         for(unsigned j = 0; j < 4; j++)
            gpu->TexCache[i].Data[j] = TexCache_Data[i][j];
      }
      gpu->RecalcTexWindowStuff();
      rsx_intf_set_tex_window(gpu->tww, gpu->twh, gpu->twx, gpu->twy);

      gpu->BlitterFIFO.SaveStatePostLoad();

      gpu->HorizStart &= 0xFFF;
      gpu->HorizEnd &= 0xFFF;

	  gpu->DisplayFB_CurYOffset &= 0x1FF;
	  gpu->DisplayFB_CurLineYReadout &= 0x1FF;

	  gpu->TexPageX &= 0xF * 64;
	  gpu->TexPageY &= 0x10 * 16;
	  gpu->TexMode &= 0x3;
	  gpu->abr &= 0x3;

	  gpu->ClipX0 &= 1023;
	  gpu->ClipY0 &= 1023;
	  gpu->ClipX1 &= 1023;
	  gpu->ClipY1 &= 1023;

	  gpu->OffsX = sign_x_to_s32(11, gpu->OffsX);
	  gpu->OffsY = sign_x_to_s32(11, gpu->OffsY);

	  IRQ_Assert(IRQ_GPU, gpu->IRQPending);

	  rsx_intf_toggle_display(gpu->DisplayOff);
	  rsx_intf_set_draw_area(gpu->ClipX0, gpu->ClipY0,
				 gpu->ClipX1, gpu->ClipY1);

	  rsx_intf_load_image(0, 0,
			      1024, 512,
			      gpu->vram, false, false);

	  UpdateDisplayMode();
   }

   return(ret);
}


void GPU_set_dither_upscale_shift(uint8 upscale_shift)
{
   PS_GPU *gpu = (PS_GPU*)GPU;
   gpu->dither_upscale_shift = upscale_shift;
}

void GPU_set_display_change_count(unsigned a)
{
   PS_GPU *gpu = (PS_GPU*)GPU;
   gpu->display_change_count = a;
}

unsigned GPU_get_display_change_count(void)
{
   PS_GPU *gpu = (PS_GPU*)GPU;
   return gpu->display_change_count;
}

uint8 GPU_get_dither_upscale_shift(void)
{
   PS_GPU *gpu = (PS_GPU*)GPU;
   return gpu->dither_upscale_shift;
}

bool GPU_DMACanWrite(void)
{
   return CalcFIFOReadyBit();
}

uint16 *GPU_get_vram(void)
{
   PS_GPU *gpu = (PS_GPU*)GPU;
   return gpu->vram;
}

uint16 GPU_PeekRAM(uint32 A)
{
   return texel_fetch(GPU, A & 0x3FF, (A >> 10) & 0x1FF);
}

void GPU_PokeRAM(uint32 A, uint16 V)
{
   texel_put(A & 0x3FF, (A >> 10) & 0x1FF, V);
}

/* Set a pixel in VRAM, upscaling it if necessary */
void texel_put(uint32 x, uint32 y, uint16 v)
{
   uint32_t dy, dx;
   x <<= GPU->upscale_shift;
   y <<= GPU->upscale_shift;

   /* Duplicate the pixel as many times as necessary (nearest
    * neighbour upscaling) */
   for (dy = 0; dy < GPU->upscale(); dy++)
   {
      for (dx = 0; dx < GPU->upscale(); dx++)
         vram_put(GPU, x + dx, y + dy, v);
   }
}
