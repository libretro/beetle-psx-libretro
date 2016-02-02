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


PS_GPU::PS_GPU(bool pal_clock_and_tv, int sls, int sle, uint8_t upscale_shift)
{
   int x, y, v;
   HardwarePALType = pal_clock_and_tv;

   for(y = 0; y < 4; y++)
      for(x = 0; x < 4; x++)
         for(v = 0; v < 512; v++)
         {
            int value = v + dither_table[y][x];

            value >>= 3;

            if(value < 0)
               value = 0;

            if(value > 0x1F)
               value = 0x1F;

            DitherLUT[y][x][v] = value;
         }

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

   this->upscale_shift = upscale_shift;
}

PS_GPU::~PS_GPU()
{
}

// Allocate enough room for the PS_GPU class and VRAM
void *PS_GPU::Alloc(uint8 upscale_shift) {
  unsigned width = 1024 << upscale_shift;
  unsigned height = 512 << upscale_shift;

  unsigned size = sizeof(PS_GPU) + width * height * sizeof(uint16_t);

  char *buffer = new char[size];

  memset(buffer, 0, size);

  return (void*)buffer;
}

PS_GPU *PS_GPU::Build(bool pal_clock_and_tv, int sls, int sle, uint8 upscale_shift)
{
  void *buffer = PS_GPU::Alloc(upscale_shift);

  // Place the new GPU inside the buffer
  return new (buffer) PS_GPU(pal_clock_and_tv, sls, sle, upscale_shift);
}

void PS_GPU::Destroy(PS_GPU *gpu) {
  gpu->~PS_GPU();
  delete [] (char*)gpu;
}

// Build a new GPU with a different upscale_shift
PS_GPU *PS_GPU::Rescale(uint8 ushift) {
  // This is all very unsafe but it should work since PS_GPU doesn't
  // contain anything that can't be copied using memcpy. If this ever
  // changes a copy constructor should be created and called from here
  // instead.
  void *buffer = PS_GPU::Alloc(ushift);

  // Recopy the GPU state in the new buffer
  memcpy(buffer, (void*)this, sizeof(*this));

  PS_GPU *gpu = (PS_GPU *)buffer;

  // Override the upscaling factor
  gpu->upscale_shift = ushift;

  // Rescale the VRAM for the new upscaling ratio
  uint16_t *vram_new = gpu->vram;

  //For simplicity we do the transfer at 1x internal resolution.
  for (unsigned y = 0; y < 512; y++) {
    for (unsigned x = 0; x < 1024; x++) {
      gpu->texel_put(x, y, texel_fetch(x, y));
    }
  }

  return gpu;
}

void PS_GPU::FillVideoParams(MDFNGI* gi)
{
   if(HardwarePALType)
   {
      gi->lcm_width = 2800;
      gi->lcm_height = (LineVisLast + 1 - LineVisFirst) * 2; //576;

      gi->nominal_width = 384;	// Dunno. :(
      gi->nominal_height = LineVisLast + 1 - LineVisFirst; //288;

      gi->fb_width = 768;
      gi->fb_height = 576;

      gi->fps = 836203078; // 49.842

      gi->VideoSystem = VIDSYS_PAL;
   }
   else
   {
      gi->lcm_width = 2800;
      gi->lcm_height = (LineVisLast + 1 - LineVisFirst) * 2; //480;

      gi->nominal_width = 320;
      gi->nominal_height = LineVisLast + 1 - LineVisFirst; //240;

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
   gi->mouse_offs_y = LineVisFirst;
}

void PS_GPU::SoftReset(void) // Control command 0x00
{
   IRQPending = false;
   IRQ_Assert(IRQ_GPU, IRQPending);

   InvalidateCache();
   DMAControl = 0;

   if(DrawTimeAvail < 0)
      DrawTimeAvail = 0;

   BlitterFIFO.Flush();
   DataReadBufferEx = 0;
   InCmd = INCMD_NONE;

   DisplayOff = 1;
   DisplayFB_XStart = 0;
   DisplayFB_YStart = 0;

   DisplayMode = 0;

   HorizStart = 0x200;
   HorizEnd = 0xC00;

   VertStart = 0x10;
   VertEnd = 0x100;


   //
   TexPageX = 0;
   TexPageY = 0;

   SpriteFlip = 0;

   abr = 0;
   TexMode = 0;

   dtd = 0;
   dfe = 0;

   //
   tww = 0; 
   twh = 0; 
   twx = 0;
   twy = 0;

   RecalcTexWindowStuff();

   //
   ClipX0 = 0;
   ClipY0 = 0;

   //
   ClipX1 = 0;
   ClipY1 = 0;

   //
   OffsX = 0;
   OffsY = 0;

   //
   MaskSetOR = 0;
   MaskEvalAND = 0;

   TexDisable = false;
   TexDisableAllowChange = false;
}

void PS_GPU::Power(void)
{
   memset(vram, 0, vram_npixels() * sizeof(*vram));

   memset(CLUT_Cache, 0, sizeof(CLUT_Cache));
   CLUT_Cache_VB = ~0U;

   memset(TexCache, 0xFF, sizeof(TexCache));

   DMAControl = 0;

   ClipX0 = 0;
   ClipY0 = 0;
   ClipX1 = 0;
   ClipY1 = 0;

   OffsX = 0;
   OffsY = 0;

   dtd = false;
   dfe = false;

   MaskSetOR = 0;
   MaskEvalAND = 0;

   TexDisable = false;
   TexDisableAllowChange = false;

   tww = 0;
   twh = 0;
   twx = 0;
   twy = 0;

   RecalcTexWindowStuff();

   TexPageX = 0;
   TexPageY = 0;
   SpriteFlip = 0;

   abr = 0;
   TexMode = 0;

   BlitterFIFO.Flush();

   DataReadBuffer = 0; // Don't reset in SoftReset()
   DataReadBufferEx = 0;
   InCmd = INCMD_NONE;
   FBRW_X = 0;
   FBRW_Y = 0;
   FBRW_W = 0;
   FBRW_H = 0;
   FBRW_CurY = 0;
   FBRW_CurX = 0;

   DisplayMode = 0;
   DisplayOff = 1;
   DisplayFB_XStart = 0;
   DisplayFB_YStart = 0;

   HorizStart = 0;
   HorizEnd = 0;

   VertStart = 0;
   VertEnd = 0;

   //
   //
   //
   DisplayFB_CurYOffset = 0;
   DisplayFB_CurLineYReadout = 0;
   InVBlank = true;

   // TODO: factor out in a separate function.
   LinesPerField = 263;

   //
   //
   //
   scanline = 0;
   field = 0;
   field_ram_readout = 0;
   PhaseChange = 0;

   //
   //
   //
   DotClockCounter = 0;
   GPUClockCounter = 0;
   LineClockCounter = 3412 - 200;
   LinePhase = 0;

   DrawTimeAvail = 0;

   lastts = 0;

   SoftReset();

   IRQ_Assert(IRQ_VBLANK, InVBlank);
   TIMER_SetVBlank(InVBlank);
}

void PS_GPU::ResetTS(void)
{
   lastts = 0;
}

#include "gpu_common.cpp"
#include "gpu_polygon.cpp"
#include "gpu_sprite.cpp"
#include "gpu_line.cpp"

//
// C-style function wrappers so our command table isn't so ginormous(in memory usage).
//
   template<int numvertices, bool shaded, bool textured, int BlendMode, bool TexMult, uint32 TexMode_TA, bool MaskEval_TA>
static void G_Command_DrawPolygon(PS_GPU* g, const uint32 *cb)
{
   g->Command_DrawPolygon<numvertices, shaded, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(cb);
}

   template<uint8 raw_size, bool textured, int BlendMode, bool TexMult, uint32 TexMode_TA, bool MaskEval_TA>
static void G_Command_DrawSprite(PS_GPU* g, const uint32 *cb)
{
   g->Command_DrawSprite<raw_size, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(cb);
}

   template<bool polyline, bool goraud, int BlendMode, bool MaskEval_TA>
static void G_Command_DrawLine(PS_GPU* g, const uint32 *cb)
{
   g->Command_DrawLine<polyline, goraud, BlendMode, MaskEval_TA>(cb);
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

static void G_Command_ClearCache(PS_GPU* g, const uint32 *cb)
{
   g->InvalidateCache();
}

static void G_Command_IRQ(PS_GPU* g, const uint32 *cb)
{
   g->IRQPending = true;
   IRQ_Assert(IRQ_GPU, g->IRQPending);
}

// Special RAM write mode(16 pixels at a time), does *not* appear to use mask drawing environment settings.
//
static void G_Command_FBFill(PS_GPU* gpu, const uint32 *cb)
{
   int32_t x, y, r, g, b, destX, destY, width, height;
   r = cb[0] & 0xFF;
   g = (cb[0] >> 8) & 0xFF;
   b = (cb[0] >> 16) & 0xFF;
   const uint16_t fill_value = ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10);

   destX = (cb[1] >>  0) & 0x3F0;
   destY = (cb[1] >> 16) & 0x3FF;

   width =  (((cb[2] >> 0) & 0x3FF) + 0xF) & ~0xF;
   height = (cb[2] >> 16) & 0x1FF;

   //printf("[GPU] FB Fill %d:%d w=%d, h=%d\n", destX, destY, width, height);
   gpu->DrawTimeAvail -= 46;	// Approximate

   for(y = 0; y < height; y++)
   {
      const int32 d_y = (y + destY) & 511;

      if(LineSkipTest(gpu, d_y))
         continue;

      gpu->DrawTimeAvail -= (width >> 3) + 9;

      for(x = 0; x < width; x++)
      {
         const int32 d_x = (x + destX) & 1023;

         gpu->texel_put(d_x, d_y, fill_value);
      }
   }
}

static void G_Command_FBCopy(PS_GPU* g, const uint32 *cb)
{
   int32 sourceX = (cb[1] >> 0) & 0x3FF;
   int32 sourceY = (cb[1] >> 16) & 0x3FF;
   int32 destX = (cb[2] >> 0) & 0x3FF;
   int32 destY = (cb[2] >> 16) & 0x3FF;

   int32 width = (cb[3] >> 0) & 0x3FF;
   int32 height = (cb[3] >> 16) & 0x1FF;

   if(!width)
      width = 0x400;

   if(!height)
      height = 0x200;

   g->InvalidateTexCache();
   //printf("FB Copy: %d %d %d %d %d %d\n", sourceX, sourceY, destX, destY, width, height);

   g->DrawTimeAvail -= (width * height) * 2;

   for(int32 y = 0; y < height; y++)
   {
      for(int32 x = 0; x < width; x += 128)
      {
         const int32 chunk_x_max = std::min<int32>(width - x, 128);
         uint16 tmpbuf[128]; // TODO: Check and see if the GPU is actually (ab)using the CLUT or texture cache.

         for(int32 chunk_x = 0; chunk_x < chunk_x_max; chunk_x++)
         {
            int32 s_y = (y + sourceY) & 511;
            int32 s_x = (x + chunk_x + sourceX) & 1023;

            // XXX make upscaling-friendly, as it is we copy at 1x
            tmpbuf[chunk_x] = g->texel_fetch(s_x, s_y);
         }

         for(int32 chunk_x = 0; chunk_x < chunk_x_max; chunk_x++)
         {
            int32 d_y = (y + destY) & 511;
            int32 d_x = (x + chunk_x + destX) & 1023;

            if(!(g->texel_fetch(d_x, d_y) & g->MaskEvalAND))
               g->texel_put(d_x, d_y, tmpbuf[chunk_x] | g->MaskSetOR);
         }
      }
   }
}

static void G_Command_FBWrite(PS_GPU* g, const uint32 *cb)
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

static void G_Command_FBRead(PS_GPU* g, const uint32 *cb)
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

static void G_Command_DrawMode(PS_GPU* g, const uint32 *cb)
{
   const uint32 cmdw = *cb;

   g->SetTPage(cmdw);

   g->SpriteFlip = (cmdw & 0x3000);
   g->dtd =        (cmdw >> 9) & 1;
   g->dfe =        (cmdw >> 10) & 1;

   //printf("*******************DFE: %d -- scanline=%d\n", dfe, scanline);
}

static void G_Command_TexWindow(PS_GPU* g, const uint32 *cb)
{
   g->tww = (*cb & 0x1F);
   g->twh = ((*cb >> 5) & 0x1F);
   g->twx = ((*cb >> 10) & 0x1F);
   g->twy = ((*cb >> 15) & 0x1F);

   g->RecalcTexWindowStuff();
}

static void G_Command_Clip0(PS_GPU* g, const uint32 *cb)
{
   g->ClipX0 = *cb & 1023;
   g->ClipY0 = (*cb >> 10) & 1023;
}

static void G_Command_Clip1(PS_GPU* g, const uint32 *cb)
{
   g->ClipX1 = *cb & 1023;
   g->ClipY1 = (*cb >> 10) & 1023;
}

static void G_Command_DrawingOffset(PS_GPU* g, const uint32 *cb)
{
   g->OffsX = sign_x_to_s32(11, (*cb & 2047));
   g->OffsY = sign_x_to_s32(11, ((*cb >> 11) & 2047));

   //fprintf(stderr, "[GPU] Drawing offset: %d(raw=%d) %d(raw=%d) -- %d\n", OffsX, *cb, OffsY, *cb >> 11, scanline);
}

static void G_Command_MaskSetting(PS_GPU* g, const uint32 *cb)
{
   //printf("Mask setting: %08x\n", *cb);
   g->MaskSetOR = (*cb & 1) ? 0x8000 : 0x0000;
   g->MaskEvalAND = (*cb & 2) ? 0x8000 : 0x0000;
}


CTEntry PS_GPU::Commands[256] =
{
   /* 0x00 */
   NULLCMD(),
   OTHER_HELPER(1, 2, false, G_Command_ClearCache),
   OTHER_HELPER(3, 3, false, G_Command_FBFill),

   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

   /* 0x10 */
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

   OTHER_HELPER(1, 1, false,  G_Command_IRQ),

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
   OTHER_HELPER_X32(4, 2, false, G_Command_FBCopy),

   /* 0xA0 ... 0xBF */
   OTHER_HELPER_X32(3, 2, false, G_Command_FBWrite),

   /* 0xC0 ... 0xDF */
   OTHER_HELPER_X32(3, 2, false, G_Command_FBRead),

   /* 0xE0 */

   NULLCMD(),
   OTHER_HELPER(1, 2, false, G_Command_DrawMode),
   OTHER_HELPER(1, 2, false, G_Command_TexWindow),
   OTHER_HELPER(1, 1, true,  G_Command_Clip0),
   OTHER_HELPER(1, 1, true,  G_Command_Clip1),
   OTHER_HELPER(1, 1, true,  G_Command_DrawingOffset),
   OTHER_HELPER(1, 2, false, G_Command_MaskSetting),

   NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

   /* 0xF0 */
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),
   NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(), NULLCMD(),

};


void PS_GPU::ProcessFIFO(void)
{
   uint32_t CB[0x10], InData;
   unsigned i;
   uint32_t cc = InCmd_CC;
   unsigned command_len;
   const CTEntry *command = &Commands[cc];
   bool read_fifo = false;

   if(!BlitterFIFO.CanRead())
      return;

   switch(InCmd)
   {
      default:
      case INCMD_NONE:
         break;

      case INCMD_FBREAD:
         return;

      case INCMD_FBWRITE:
         InData = BlitterFIFO.Read();

         for(i = 0; i < 2; i++)
         {
            if(!(texel_fetch(FBRW_CurX & 1023, FBRW_CurY & 511) & MaskEvalAND))
               texel_put(FBRW_CurX & 1023, FBRW_CurY & 511, InData | MaskSetOR);

            FBRW_CurX++;
            if(FBRW_CurX == (FBRW_X + FBRW_W))
            {
               FBRW_CurX = FBRW_X;
               FBRW_CurY++;
               if(FBRW_CurY == (FBRW_Y + FBRW_H))
               {
                  InCmd = INCMD_NONE;
                  break;	// Break out of the for() loop.
               }
            }
            InData >>= 16;
         }
         return;

      case INCMD_QUAD:
         if(DrawTimeAvail < 0)
            return;

         command_len      = 1 + (bool)(cc & 0x4) + (bool)(cc & 0x10);
         read_fifo = true;
         break;
      case INCMD_PLINE:
         if(DrawTimeAvail < 0)
            return;

         command_len        = 1 + (bool)(InCmd_CC & 0x10);

         if((BlitterFIFO.Peek() & 0xF000F000) == 0x50005000)
         {
            BlitterFIFO.Read();
            InCmd = INCMD_NONE;
            return;
         }

         read_fifo = true;
         break;
   }

   if (!read_fifo)
   {
      cc          = BlitterFIFO.Peek() >> 24;
      command     = &Commands[cc];
      command_len = command->len;

      if(DrawTimeAvail < 0 && !command->ss_cmd)
         return;
   }

   if(BlitterFIFO.CanRead() < command_len)
      return;

   for(i = 0; i < command_len; i++)
      CB[i] = BlitterFIFO.Read();

   if (!read_fifo)
   {
      if(!command->ss_cmd)
         DrawTimeAvail -= 2;

      // A very very ugly kludge to support texture mode specialization. fixme/cleanup/SOMETHING in the future.
      if(cc >= 0x20 && cc <= 0x3F && (cc & 0x4))
      {
         /* Don't alter SpriteFlip here. */
         SetTPage(CB[4 + ((cc >> 4) & 0x1)] >> 16);
      }
   }

   if ((cc >= 0x80) && (cc <= 0x9F))
      G_Command_FBCopy(this, CB);
   else if ((cc >= 0xA0) && (cc <= 0xBF))
      G_Command_FBWrite(this, CB);
   else if ((cc >= 0xC0) && (cc <= 0xDF))
      G_Command_FBRead(this, CB);
   else switch (cc)
   {
      case 0x01:
         CLUT_Cache_VB = ~0U;
         InvalidateTexCache();
         break;
      case 0x02:
         G_Command_FBFill(this, CB);
         break;
      case 0x1F:
         this->IRQPending = true;
         IRQ_Assert(IRQ_GPU, this->IRQPending);
         break;
      case 0xe1:
         G_Command_DrawMode(this, CB);
         break;
      case 0xe2:
         G_Command_TexWindow(this, CB);
         break;
      case 0xe3: /* Clip 0 */
         this->ClipX0 = *CB & 1023;
         this->ClipY0 = (*CB >> 10) & 1023;
         break;
      case 0xe4: /* Clip 1 */
         this->ClipX1 = *CB & 1023;
         this->ClipY1 = (*CB >> 10) & 1023;
         break;
      case 0xe5: /* Drawing Offset */
         this->OffsX = sign_x_to_s32(11, (*CB & 2047));
         this->OffsY = sign_x_to_s32(11, ((*CB >> 11) & 2047));
         break;
      case 0xe6: /* Mask Setting */
         this->MaskSetOR = (*CB & 1) ? 0x8000 : 0x0000;
         this->MaskEvalAND = (*CB & 2) ? 0x8000 : 0x0000;
         break;
      default:
         if(command->func[abr][TexMode])
            command->func[abr][TexMode | (MaskEvalAND ? 0x4 : 0x0)](this, CB);
         break;
   }
}

INLINE void PS_GPU::WriteCB(uint32_t InData)
{
   if(BlitterFIFO.CanRead() >= 0x10 && (InCmd != INCMD_NONE || (BlitterFIFO.CanRead() - 0x10) >= Commands[BlitterFIFO.Peek() >> 24].fifo_fb_len))
   {
      PSX_DBG(PSX_DBG_WARNING, "GPU FIFO overflow!!!\n");
      return;
   }

   BlitterFIFO.Write(InData);
   ProcessFIFO();
}

void PS_GPU::SetTPage(const uint32_t cmdw)
{
   const unsigned NewTexPageX = (cmdw & 0xF) * 64;
   const unsigned NewTexPageY = (cmdw & 0x10) * 16;
   const unsigned NewTexMode = (cmdw >> 7) & 0x3;

   abr = (cmdw >> 5) & 0x3;

   if(!NewTexMode != !TexMode || NewTexPageX != TexPageX || NewTexPageY != TexPageY)
   {
      InvalidateTexCache();
   }

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

void PS_GPU::Write(const int32_t timestamp, uint32_t A, uint32_t V)
{
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
            SoftReset();
            break;

         case 0x01:	// Reset command buffer
            if(DrawTimeAvail < 0)
               DrawTimeAvail = 0;
            BlitterFIFO.Flush();
            InCmd = INCMD_NONE;
            break;

         case 0x02: 	// Acknowledge IRQ
            IRQPending = false;
            IRQ_Assert(IRQ_GPU, IRQPending);            
            break;

         case 0x03:	// Display enable
            DisplayOff = V & 1;
            break;

         case 0x04:	// DMA Setup
            DMAControl = V & 0x3;
            break;

         case 0x05:	// Start of display area in framebuffer
            DisplayFB_XStart = V & 0x3FE; // Lower bit is apparently ignored.
            DisplayFB_YStart = (V >> 10) & 0x1FF;
            break;

         case 0x06:	// Horizontal display range
            HorizStart = V & 0xFFF;
            HorizEnd = (V >> 12) & 0xFFF;
            break;

         case 0x07:
            VertStart = V & 0x3FF;
            VertEnd = (V >> 10) & 0x3FF;
            break;

         case 0x08:
            //printf("\n\nDISPLAYMODE SET: 0x%02x, %u *************************\n\n\n", V & 0xFF, scanline);
            DisplayMode = V & 0xFF;
            break;

         case 0x09:
            TexDisableAllowChange = V & 1;
            break;

         case 0x10:	// GPU info(?)
            switch(V & 0xF)
            {
               // DataReadBuffer must remain unchanged for any unhandled GPU info index.
               default:  break;

               case 0x2: 
                         DataReadBufferEx &= 0xFFF00000;
                         DataReadBufferEx |= (tww << 0) | (twh << 5) | (twx << 10) | (twy << 15);
                         DataReadBuffer    = DataReadBufferEx;
                         break;

               case 0x3:
                         DataReadBufferEx &= 0xFFF00000;
                         DataReadBufferEx |= (ClipY0 << 10) | ClipX0;
                         DataReadBuffer = DataReadBufferEx;
                         break;

               case 0x4:
                         DataReadBufferEx &= 0xFFF00000;
                         DataReadBufferEx |= (ClipY1 << 10) | ClipX1;
                         DataReadBuffer = DataReadBufferEx;
                         break;

               case 0x5: 
                         DataReadBufferEx &= 0xFFC00000;
                         DataReadBufferEx |= (OffsX & 2047) | ((OffsY & 2047) << 11);
                         DataReadBuffer = DataReadBufferEx;
                         break;

               case 0x7: 
                         DataReadBufferEx = 2;
                         DataReadBuffer = DataReadBufferEx;
                         break;

               case 0x8:
                         DataReadBufferEx = 0;
                         DataReadBuffer = DataReadBufferEx;
                         break;
            }
            break;

      }
   }
   else		// GP0 ("Data")
   {
      //uint32_t command = V >> 24;
      //printf("Meow command: %02x\n", command);
      //assert(!(DMAControl & 2));
      WriteCB(V);
   }
}


void PS_GPU::WriteDMA(uint32_t V)
{
   WriteCB(V);
}

INLINE uint32_t PS_GPU::ReadData(void)
{
   if(InCmd == INCMD_FBREAD)
   {
      DataReadBufferEx = 0;
      for(int i = 0; i < 2; i++)
      {
         DataReadBufferEx |= texel_fetch(FBRW_CurX & 1023, FBRW_CurY & 511) << (i * 16);

         FBRW_CurX++;
         if(FBRW_CurX == (FBRW_X + FBRW_W))
         {
            if((FBRW_CurY + 1) == (FBRW_Y + FBRW_H))
            {
               InCmd = INCMD_NONE;
            }
            else
            {
               FBRW_CurY++;
               FBRW_CurX = FBRW_X;
            }
         }
      }

      return DataReadBufferEx;
   }

   return DataReadBuffer;
}

uint32_t PS_GPU::ReadDMA(void)
{
   return ReadData();
}

uint32_t PS_GPU::Read(const int32_t timestamp, uint32_t A)
{
   uint32_t ret = 0;

   if(A & 4)	// Status
   {
      ret = (((DisplayMode << 1) & 0x7F) | ((DisplayMode >> 6) & 1)) << 16;

      ret |= (DisplayMode & 0x80) << 7;

      ret |= DMAControl << 29;

      ret |= (DisplayFB_CurLineYReadout & 1) << 31;

      ret |= (!field) << 13;

      if(DMAControl & 0x02)
         ret |= 1 << 25;

      ret |= IRQPending << 24;

      ret |= DisplayOff << 23;

      if(InCmd == INCMD_NONE && DrawTimeAvail >= 0 && BlitterFIFO.CanRead() == 0x00)	// GPU idle bit.
         ret |= 1 << 26;

      if(InCmd == INCMD_FBREAD)	// Might want to more accurately emulate this in the future?
         ret |= (1 << 27);

      ret |= CalcFIFOReadyBit() << 28;		// FIFO has room bit? (kinda).

      //
      //
      ret |= TexPageX >> 6;
      ret |= TexPageY >> 4;
      ret |= abr << 5;
      ret |= TexMode << 7;

      ret |= dtd << 9;
      ret |= dfe << 10;

      if(MaskSetOR)
         ret |= 1 << 11;

      if(MaskEvalAND)
         ret |= 1 << 12;

      ret |= TexDisable << 15;
   }
   else		// "Data"
      ret = ReadData();

   if(DMAControl & 2)
   {
      //PSX_WARNING("[GPU READ WHEN (DMACONTROL&2)] 0x%08x - ret=0x%08x, scanline=%d", A, ret, scanline);
   }

   return(ret >> ((A & 3) * 8));
}

INLINE void PS_GPU::ReorderRGB_Var(uint32_t out_Rshift, uint32_t out_Gshift, uint32_t out_Bshift, bool bpp24, const uint16_t *src, uint32_t *dest, const int32 dx_start, const int32 dx_end, int32 fb_x)
{
  int32_t fb_mask = ((0x7FF << upscale_shift) + upscale() - 1);

   if(bpp24)	// 24bpp
   {
     for(int32 x = dx_start; x < dx_end; x+= upscale())
      {
         uint32_t srcpix;

         srcpix = src[(fb_x >> 1) + 0] | (src[((fb_x >> 1) + (1 << upscale_shift)) & fb_mask] << 16);
         srcpix >>= ((fb_x >> upscale_shift) & 1) * 8;

         uint32_t color = (((srcpix >> 0) << RED_SHIFT) & (0xFF << RED_SHIFT)) | (((srcpix >> 8) << GREEN_SHIFT) & (0xFF << GREEN_SHIFT)) |
            (((srcpix >> 16) << BLUE_SHIFT) & (0xFF << BLUE_SHIFT));

         for (int i = 0; i < upscale(); i++) {
            dest[x + i] = color;
         }

         fb_x = (fb_x + (3 << upscale_shift)) & fb_mask;
      }
   }				// 15bpp
   else
   {
      for(int32 x = dx_start; x < dx_end; x++)
      {
         uint32_t srcpix = src[(fb_x >> 1)];
         dest[x] = MAKECOLOR((((srcpix >> 0) & 0x1F) << 3), (((srcpix >> 5) & 0x1F) << 3), (((srcpix >> 10) & 0x1F) << 3), 0);

         fb_x = (fb_x + 2) & fb_mask;
      }
   }

}

int32_t PS_GPU::Update(const int32_t sys_timestamp)
{
   static const uint32_t DotClockRatios[5] = { 10, 8, 5, 4, 7 };
   const uint32_t dmc = (DisplayMode & 0x40) ? 4 : (DisplayMode & 0x3);
   const uint32_t dmw = 2800 / DotClockRatios[dmc];	// Must be <= 768

   int32 sys_clocks = sys_timestamp - lastts;
   int32 gpu_clocks;

   //printf("GPUISH: %d\n", sys_timestamp - lastts);

   if(!sys_clocks)
      goto TheEnd;

   DrawTimeAvail += sys_clocks << 1;

   if(DrawTimeAvail > 256)
      DrawTimeAvail = 256;

   ProcessFIFO();

   //puts("GPU Update Start");

   GPUClockCounter += (uint64)sys_clocks * GPUClockRatio;

   gpu_clocks = GPUClockCounter >> 16;
   GPUClockCounter -= gpu_clocks << 16;

   while(gpu_clocks > 0)
   {
      int32 chunk_clocks = gpu_clocks;
      int32 dot_clocks;

      if(chunk_clocks > LineClockCounter)
      {
         //printf("Chunk: %u, LCC: %u\n", chunk_clocks, LineClockCounter);
         chunk_clocks = LineClockCounter;
      }

      gpu_clocks -= chunk_clocks;
      LineClockCounter -= chunk_clocks;

      DotClockCounter += chunk_clocks;
      dot_clocks = DotClockCounter / DotClockRatios[DisplayMode & 0x3];
      DotClockCounter -= dot_clocks * DotClockRatios[DisplayMode & 0x3];

      TIMER_AddDotClocks(dot_clocks);


      if(!LineClockCounter)
      {
         PSX_SetEventNT(PSX_EVENT_TIMER, TIMER_Update(sys_timestamp));  // We could just call this at the top of GPU_Update(), but do it here for slightly less CPU usage(presumably).

         LinePhase = (LinePhase + 1) & 1;

         if(LinePhase)
         {
            TIMER_SetHRetrace(true);
            LineClockCounter = 200;
            TIMER_ClockHRetrace();
         }
         else
         {
            const unsigned int FirstVisibleLine = LineVisFirst + (HardwarePALType ? 20 : 16);
            const unsigned int VisibleLineCount = LineVisLast + 1 - LineVisFirst; //HardwarePALType ? 288 : 240;

            TIMER_SetHRetrace(false);

            if(DisplayMode & DISP_PAL)
               LineClockCounter = 3405 - 200;
            else
               LineClockCounter = 3412 + PhaseChange - 200;

            scanline = (scanline + 1) % LinesPerField;
            PhaseChange = !PhaseChange;

#ifdef WANT_DEBUGGER
            DBG_GPUScanlineHook(scanline);
#endif

            //
            //
            //
            if(scanline == (HardwarePALType ? 308 : 256))	// Will need to be redone if we ever allow for visible vertical overscan with NTSC.
            {
               if(sl_zero_reached)
               {
                  //printf("Req Exit(visible fallthrough case): %u\n", scanline);
                  PSX_RequestMLExit();
               }
            }

            if(scanline == (LinesPerField - 1))
            {
               if(sl_zero_reached)
               {
                  //printf("Req Exit(final fallthrough case): %u\n", scanline);
                  PSX_RequestMLExit();
               }

               if(DisplayMode & DISP_INTERLACED)
                  field = !field;
               else
                  field = 0;
            }

            if(scanline == 0)
            {
               assert(sl_zero_reached == false);
               sl_zero_reached = true;

               if(DisplayMode & DISP_INTERLACED)
               {
                  if(DisplayMode & DISP_PAL)
                     LinesPerField = 313 - field;
                  else                   // NTSC
                     LinesPerField = 263 - field;
               }
               else
               {
                  field = 0;  // May not be the correct place for this?

                  if(DisplayMode & DISP_PAL)
                     LinesPerField = 314;
                  else			// NTSC
                     LinesPerField = 263;
               }


               if(espec)
               {
                  if((bool)(DisplayMode & DISP_PAL) != HardwarePALType)
                  {
                     DisplayRect->x = 0;
                     DisplayRect->y = 0;
                     DisplayRect->w = 384;
                     DisplayRect->h = VisibleLineCount;

                     for(int32 y = 0; y < DisplayRect->h; y++)
                     {
                        uint32_t *dest = surface->pixels + y * surface->pitch32;

                        LineWidths[y] = 384;

                        memset(dest, 0, 384 * sizeof(int32));
                     }

                     //char buffer[256];
                     //snprintf(buffer, sizeof(buffer), _("VIDEO STANDARD MISMATCH"));
                     //DrawTextTrans(surface->pixels + ((DisplayRect->h / 2) - (13 / 2)) * surface->pitch32, surface->pitch32 << 2, DisplayRect->w, (UTF8*)buffer,
                     //MAKECOLOR(0x00, 0xFF, 0x00), true, MDFN_FONT_6x13_12x13, 0);
                  }
                  else
                  {
                     espec->InterlaceOn = (bool)(DisplayMode & DISP_INTERLACED);
                     espec->InterlaceField = (bool)(DisplayMode & DISP_INTERLACED) && field;

                     DisplayRect->x = 0;
                     DisplayRect->y = 0;
                     DisplayRect->w = 0;
                     DisplayRect->h = VisibleLineCount << (bool)(DisplayMode & DISP_INTERLACED);

                     // Clear ~0 state.
                     LineWidths[0] = 0;

                     for(int i = 0; i < (DisplayRect->y + DisplayRect->h); i++)
                     {
                        surface->pixels[i * surface->pitch32 + 0] =
                           surface->pixels[i * surface->pitch32 + 1] = 0;
                        LineWidths[i] = 2;
                     }
                  }
               }
            }

            //
            // Don't mess with the order of evaluation of these scanline == VertXXX && (InVblankwhatever) if statements and the following IRQ/timer vblank stuff
            // unless you know what you're doing!!! (IE you've run further tests to refine the behavior)
            //
            if(scanline == VertEnd && !InVBlank)
            {
               if(sl_zero_reached)
               {
                  // Gameplay in Descent(NTSC) has vblank at scanline 236
                  // 
                  // Mikagura Shoujo Tanteidan has vblank at scanline 192 during intro
                  //  FMV(which we don't handle here because low-latency in that case is not so important).
                  //
                  if(scanline >= (HardwarePALType ? 260 : 232))
                  {
                     //printf("Req Exit(vblank case): %u\n", scanline);
                     PSX_RequestMLExit();
                  }
#if 0
                  else
                  {
                     //printf("VBlank too early, chickening out early exit: %u!\n", scanline);
                  }
#endif
               }

               //printf("VBLANK: %u\n", scanline);
               InVBlank = true;

               DisplayFB_CurYOffset = 0;

               if((DisplayMode & 0x24) == 0x24)
                  field_ram_readout = !field;
               else
                  field_ram_readout = 0;
            }

            if(scanline == VertStart && InVBlank)
            {
               InVBlank = false;

               // Note to self: X-Men Mutant Academy relies on this being set on the proper scanline in 480i mode(otherwise it locks up on startup).
               //if(HeightMode)
               // DisplayFB_CurYOffset = field;
            }

            IRQ_Assert(IRQ_VBLANK, InVBlank);
            TIMER_SetVBlank(InVBlank);
            //
            //
            //

            // Needs to occur even in vblank.
            // Not particularly confident about the timing of this in regards to vblank and the upper bit(ODE) of the GPU status port, though the test that
            // showed an oddity was pathological in that VertEnd < VertStart in it.
            if((DisplayMode & 0x24) == 0x24)
               DisplayFB_CurLineYReadout = (DisplayFB_YStart + (DisplayFB_CurYOffset << 1) + (InVBlank ? 0 : field_ram_readout)) & 0x1FF;
            else
               DisplayFB_CurLineYReadout = (DisplayFB_YStart + DisplayFB_CurYOffset) & 0x1FF;

            unsigned dmw_width = 0;
            unsigned pix_clock_offset = 0;
            unsigned pix_clock = 0;
            unsigned pix_clock_div = 0;
            uint32_t *dest = NULL;
            if((bool)(DisplayMode & DISP_PAL) == HardwarePALType && scanline >= FirstVisibleLine && scanline < (FirstVisibleLine + VisibleLineCount))
            {
               int32 dest_line;
               int32 fb_x = DisplayFB_XStart * 2;
               int32 dx_start = HorizStart, dx_end = HorizEnd;

               dest_line = ((scanline - FirstVisibleLine) << espec->InterlaceOn) + espec->InterlaceField;

               if(dx_end < dx_start)
                  dx_end = dx_start;

               dx_start = dx_start / DotClockRatios[dmc];
               dx_end = dx_end / DotClockRatios[dmc];

               dx_start -= 488 / DotClockRatios[dmc];
               dx_end -= 488 / DotClockRatios[dmc];

               if(dx_start < 0)
               {
                  fb_x -= dx_start * ((DisplayMode & DISP_RGB24) ? 3 : 2);
                  fb_x &= 0x7FF; //0x3FF;
                  dx_start = 0;
               }

               if((uint32)dx_end > dmw)
                  dx_end = dmw;

               if(InVBlank || DisplayOff)
                  dx_start = dx_end = 0;

               LineWidths[dest_line] = dmw;

               //printf("dx_start base: %d, dmw: %d\n", dx_start, dmw);

               {
                  // Convert the necessary variables to the upscaled version
                  uint32_t x;
                  uint32_t y      = DisplayFB_CurLineYReadout << upscale_shift;
                  uint32_t udmw   = dmw      << upscale_shift;
                  int32 udx_start = dx_start << upscale_shift;
                  int32 udx_end   = dx_end   << upscale_shift;
                  int32 ufb_x     = fb_x     << upscale_shift;

                  for (uint32_t i = 0; i < upscale(); i++)
                  {
		    const uint16_t *src = vram + ((y + i) << (10 + upscale_shift));

                     // printf("surface: %dx%d (%d) %u %u + %u\n",
                     // 	   surface->w, surface->h, surface->pitchinpix,
                     // 	   dest_line, y, i);

                     dest = surface->pixels + ((dest_line << upscale_shift) + i) * surface->pitch32;

                     memset(dest, 0, udx_start * sizeof(int32));

                     //printf("%d %d %d - %d %d\n", scanline, dx_start, dx_end, HorizStart, HorizEnd);
                     ReorderRGB_Var(RED_SHIFT, GREEN_SHIFT, BLUE_SHIFT, DisplayMode & DISP_RGB24, src, dest, udx_start, udx_end, ufb_x);

                     //printf("dx_end: %d, dmw: %d\n", udx_end, udmw);
                     //
                     for(x = udx_end; x < udmw; x++)
                        dest[x] = 0;
                  }
               }

               //if(scanline == 64)
               // printf("%u\n", sys_timestamp - ((uint64)gpu_clocks * 65536) / GPUClockRatio);

               dmw_width = dmw;
               pix_clock_offset = (488 - 146) / DotClockRatios[dmc];
               pix_clock = (HardwarePALType ? 53203425 : 53693182) / DotClockRatios[dmc];
               pix_clock_div = DotClockRatios[dmc];
            }
	    // XXX fixme when upscaling is active
            PSX_GPULineHook(sys_timestamp, sys_timestamp - ((uint64)gpu_clocks * 65536) / GPUClockRatio, scanline == 0, dest, &surface->format, dmw_width, pix_clock_offset, pix_clock, pix_clock_div);

            if(!InVBlank)
            {
               DisplayFB_CurYOffset = (DisplayFB_CurYOffset + 1) & 0x1FF;
            }
         }
         PSX_SetEventNT(PSX_EVENT_TIMER, TIMER_Update(sys_timestamp));  // Mostly so the next event time gets recalculated properly in regards to our calls
         // to TIMER_SetVBlank() and TIMER_SetHRetrace().
      }	// end if(!LineClockCounter)
   }	// end while(gpu_clocks > 0)

   //puts("GPU Update End");

TheEnd:
   lastts = sys_timestamp;

   {
      int32 next_dt = LineClockCounter;

      next_dt = (((int64)next_dt << 16) - GPUClockCounter + GPUClockRatio - 1) / GPUClockRatio;

      next_dt = std::max<int32>(1, next_dt);
      next_dt = std::min<int32>(128, next_dt);

      //printf("%d\n", next_dt);

      return(sys_timestamp + next_dt);
   }
}

void PS_GPU::StartFrame(EmulateSpecStruct *espec_arg)
{
   sl_zero_reached = false;

   espec = espec_arg;

   surface = espec->surface;
   DisplayRect = &espec->DisplayRect;
   LineWidths = espec->LineWidths;
}

int PS_GPU::StateAction(StateMem *sm, int load, int data_only)
{
   uint32 TexCache_Tag[256];
   uint16 TexCache_Data[256][4];

   uint16 *vram_new = NULL;

   if (upscale_shift == 0) {
     // No upscaling, we can dump the VRAM contents directly
     vram_new = vram;
   } else {
     // We have increased internal resolution, savestates are always
     // made at 1x for compatibility
     vram_new = new uint16[1024 * 512];

     if (!load) {
       // We must downscale the current VRAM contents back to 1x
       for (unsigned y = 0; y < 512; y++) {
	 for (unsigned x = 0; x < 1024; x++) {
	   vram_new[y * 1024 + x] = texel_fetch(x, y);
	 }
       }
     }
   }

   for(unsigned i = 0; i < 256; i++)
   {
      TexCache_Tag[i] = TexCache[i].Tag;

      for(unsigned j = 0; j < 4; j++)
         TexCache_Data[i][j] = TexCache[i].Data[j];

   }
   SFORMAT StateRegs[] =
   {
      // Hardcode entry name to remain backward compatible with the
      // previous fixed internal resolution code
      SFARRAY16N(vram_new, 1024 * 512, "&GPURAM[0][0]"),

      SFVAR(DMAControl),

      SFVAR(ClipX0),
      SFVAR(ClipY0),
      SFVAR(ClipX1),
      SFVAR(ClipY1),

      SFVAR(OffsX),
      SFVAR(OffsY),

      SFVAR(dtd),
      SFVAR(dfe),

      SFVAR(MaskSetOR),
      SFVAR(MaskEvalAND),

      SFVAR(TexDisable),
      SFVAR(TexDisableAllowChange),

      SFVAR(tww),
      SFVAR(twh),
      SFVAR(twx),
      SFVAR(twy),

      SFVAR(TexPageX),
      SFVAR(TexPageY),

      SFVAR(SpriteFlip),

      SFVAR(abr),
      SFVAR(TexMode),

      SFARRAY32(&BlitterFIFO.data[0], sizeof(BlitterFIFO.data) / sizeof(BlitterFIFO.data[0])),
      SFVAR(BlitterFIFO.read_pos),
      SFVAR(BlitterFIFO.write_pos),
      SFVAR(BlitterFIFO.in_count),

      SFVAR(DataReadBuffer),
      SFVAR(DataReadBufferEx),

      SFVAR(IRQPending),

      SFVAR(InCmd),
      SFVAR(InCmd_CC),

#define TVHELPER(n)	SFVAR(n.x), SFVAR(n.y), SFVAR(n.u), SFVAR(n.v), SFVAR(n.r), SFVAR(n.g), SFVAR(n.b)
      TVHELPER(InQuad_F3Vertices[0]),
      TVHELPER(InQuad_F3Vertices[1]),
      TVHELPER(InQuad_F3Vertices[2]),
#undef TVHELPER

      SFVAR(InPLine_PrevPoint.x),
      SFVAR(InPLine_PrevPoint.y),
      SFVAR(InPLine_PrevPoint.r),
      SFVAR(InPLine_PrevPoint.g),
      SFVAR(InPLine_PrevPoint.b),

      SFVAR(FBRW_X),
      SFVAR(FBRW_Y),
      SFVAR(FBRW_W),
      SFVAR(FBRW_H),
      SFVAR(FBRW_CurY),
      SFVAR(FBRW_CurX),

      SFVAR(DisplayMode),
      SFVAR(DisplayOff),
      SFVAR(DisplayFB_XStart),
      SFVAR(DisplayFB_YStart),

      SFVAR(HorizStart),
      SFVAR(HorizEnd),

      SFVAR(VertStart),
      SFVAR(VertEnd),

      SFVAR(DisplayFB_CurYOffset),
      SFVAR(DisplayFB_CurLineYReadout),

      SFVAR(InVBlank),

      SFVAR(LinesPerField),
      SFVAR(scanline),
      SFVAR(field),
      SFVAR(field_ram_readout),
      SFVAR(PhaseChange),

      SFVAR(DotClockCounter),

      SFVAR(GPUClockCounter),
      SFVAR(LineClockCounter),
      SFVAR(LinePhase),

      SFVAR(DrawTimeAvail),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "GPU");

   if (upscale_shift > 0) {
     if (load) {
       // Restore upscaled VRAM from savestate
       for (unsigned y = 0; y < 512; y++) {
	 for (unsigned x = 0; x < 1024; x++) {
	   texel_put(x, y, vram_new[y * 1024 + x]);
	 }
       }
     }

     delete [] vram_new;
     vram_new = NULL;
   }

   if(load)
   {
      for(unsigned i = 0; i < 256; i++)
      {
         TexCache[i].Tag = TexCache_Tag[i];

         for(unsigned j = 0; j < 4; j++)
            TexCache[i].Data[j] = TexCache_Data[i][j];
      }
      RecalcTexWindowStuff();
      BlitterFIFO.SaveStatePostLoad();

      HorizStart &= 0xFFF;
      HorizEnd &= 0xFFF;

      IRQ_Assert(IRQ_GPU, IRQPending);
   }

   return(ret);
}
