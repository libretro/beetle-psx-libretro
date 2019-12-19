// WARNING WARNING WARNING:  ONLY use CanRead() method of BlitterFIFO, and NOT CanWrite(), since the FIFO is larger than the actual PS1 GPU FIFO to accommodate
// our lack of fancy superscalarish command sequencer.

#ifndef __MDFN_PSX_GPU_H
#define __MDFN_PSX_GPU_H

#include <map>
#include <queue>
#include <cmath>
#include <math.h>
#include "FastFIFO.h"

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
#include <glsm/glsmsym.h>
#endif

#define INCMD_NONE     0
#define INCMD_PLINE    1
#define INCMD_QUAD     2
#define INCMD_FBWRITE  4
#define INCMD_FBREAD   8

#define DISP_VERT480    0x04
#define DISP_PAL        0x08
#define DISP_RGB24      0x10
#define DISP_INTERLACED 0x20

enum dither_mode
{
   DITHER_NATIVE   = 0,
   DITHER_UPSCALED,
   DITHER_OFF
};

struct tri_vertex
{
   int32 x, y;
   int32 u, v;
   int32 r, g, b;
   // Precise x, y, and w coordinates using PGXP (if available)
   float precise[3];
};

struct i_group;
struct i_deltas;

struct line_point
{
   int32 x, y;
   uint8 r, g, b;
};

#define vertex_swap(_type, _a, _b) \
{                           \
   _type tmp = _a;          \
   _a = _b;                 \
   _b = tmp;                \
}                           \

struct PS_GPU
{
   uint16 CLUT_Cache[256];

   uint32 CLUT_Cache_VB;   // Don't try to be clever and reduce it to 16 bits... ~0U is value for invalidated state.

   struct   // Speedup-cache varibles, derived from other variables; shouldn't be saved in save states.
   {
      // TW*_* variables derived from tww, twh, twx, twy, TexPageX, TexPageY
      uint32 TWX_AND;
      uint32 TWX_ADD;

      uint32 TWY_AND;
      uint32 TWY_ADD;
   } SUCV;

   struct TexCache_t
   {
      uint16 Data[4];
      uint32 Tag;
   } TexCache[256];

   uint32 DMAControl;

   /* Beetle-psx upscaling vars */
   uint8 upscale_shift;
   uint8 dither_upscale_shift;

   // Drawing stuff
   int32 ClipX0;
   int32 ClipY0;
   int32 ClipX1;
   int32 ClipY1;

   int32 OffsX;
   int32 OffsY;

   bool dtd;            // Dithering enable 
   bool dfe;

   uint32 MaskSetOR;
   uint32 MaskEvalAND;

   bool TexDisable;
   bool TexDisableAllowChange;

   uint8_t tww, twh, twx, twy;
   struct
   {
      uint8 TexWindowXLUT_Pre[16];
      uint8 TexWindowXLUT[256];
      uint8 TexWindowXLUT_Post[16];
   };

   struct
   {
      uint8 TexWindowYLUT_Pre[16];
      uint8 TexWindowYLUT[256];
      uint8 TexWindowYLUT_Post[16];
   };

   uint32_t TexPageX; // 0, 64, 128, 192, etc up to 960
   uint32_t TexPageY; // 0 or 256

   uint32 SpriteFlip;

   uint32 abr;        // Semi-transparency mode(0~3)
   uint32 TexMode;

   struct
   {
      uint8 RGB8SAT_Under[256];
      uint8 RGB8SAT[256];
      uint8 RGB8SAT_Over[256];
   };

   uint32 DataReadBuffer;
   uint32 DataReadBufferEx;

   bool IRQPending;

   // Powers of 2 for faster multiple equality testing(just for multi-testing; InCmd itself will only contain 0, or a power of 2).
   uint8 InCmd;
   uint8 InCmd_CC;

   tri_vertex InQuad_F3Vertices[3];
   uint32 InQuad_clut;
   bool InQuad_invalidW;
   uint32 killQuadPart;	// bit flags for tris in quad that are to be culled

   // primitive UV offsets (used to correct flipped sprites)
   uint16_t off_u, off_v;
   // primitive UV limits (used to clamp texture sampling)
   uint16_t min_u, min_v, max_u, max_v;

   line_point InPLine_PrevPoint;

   uint32 FBRW_X;
   uint32 FBRW_Y;
   uint32 FBRW_W;
   uint32 FBRW_H;
   uint32 FBRW_CurY;
   uint32 FBRW_CurX;

   //
   // Display Parameters
   //
   uint32 DisplayMode;

   bool DisplayOff;
   uint32 DisplayFB_XStart;
   uint32 DisplayFB_YStart;

   unsigned display_change_count;

   uint32 HorizStart;
   uint32 HorizEnd;

   uint32 VertStart;
   uint32 VertEnd;

   //
   // Display work vars
   //
   uint32 DisplayFB_CurYOffset;
   uint32 DisplayFB_CurLineYReadout;

   bool InVBlank;

   //
   //
   //
   uint32 LinesPerField;
   uint32 scanline;
   bool field;
   bool field_ram_readout;
   bool PhaseChange;

   uint32 DotClockCounter;

   uint64 GPUClockCounter;
   int32 GPUClockRatio;
   int32 LineClockCounter;
   int32 LinePhase;

   int32 DrawTimeAvail;

   int32_t lastts;

   bool sl_zero_reached;

   EmulateSpecStruct *espec;
   MDFN_Surface *surface;
   MDFN_Rect *DisplayRect;
   int32 *LineWidths;
   bool HardwarePALType;
   int LineVisFirst, LineVisLast;

   uint8_t DitherLUT[4][4][512]; // Y, X, 8-bit source value(256 extra for saturation)

   /*
   VRAM has to be a ptr type or else we have to rely on smartcode void* shenanigans to
   wrestle a variable-sized struct.
   */
   uint16 *vram;
};



uint16 *GPU_get_vram(void);

void GPU_WriteDMA(uint32 V, uint32 addr);

uint32_t GPU_ReadDMA(void);

bool GPU_DMACanWrite(void);

uint8 GPU_get_dither_upscale_shift(void);

void GPU_set_dither_upscale_shift(uint8 factor);

uint8 GPU_get_upscale_shift(void);

void GPU_set_upscale_shift(uint8 factor);

void GPU_set_display_change_count(unsigned a);

unsigned GPU_get_display_change_count(void);

void GPU_Init(bool pal_clock_and_tv,
      int sls, int sle, uint8 upscale_shift);

void GPU_SoftReset(void);

void GPU_RecalcClockRatio(void);

void GPU_Destroy(void);

void GPU_Rescale(uint8 ushift);

int32_t GPU_Update(const int32_t sys_timestamp);

void GPU_FillVideoParams(MDFNGI* gi);

void GPU_Power(void);

void GPU_ResetTS(void);

void GPU_Write(const int32_t timestamp, uint32_t A, uint32_t V);

uint32_t GPU_Read(const int32_t timestamp, uint32_t A);

void GPU_StartFrame(EmulateSpecStruct *espec_arg);

int GPU_StateAction(StateMem *sm, int load, int data_only);

uint16 GPU_PeekRAM(uint32 A);

void GPU_PokeRAM(uint32 A, uint16 V);

int32_t GPU_GetScanlineNum(void);

void texel_put(uint32 x, uint32 y, uint16 v);

#endif
