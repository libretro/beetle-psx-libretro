// WARNING WARNING WARNING:  ONLY use CanRead() method of BlitterFIFO, and NOT CanWrite(), since the FIFO is larger than the actual PS1 GPU FIFO to accommodate
// our lack of fancy superscalarish command sequencer.

#ifndef __MDFN_PSX_GPU_H
#define __MDFN_PSX_GPU_H

#include <math.h>

#include "../git.h"

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
   int32_t x, y;
   int32_t u, v;
   int32_t r, g, b;
   // Precise x, y, and w coordinates using PGXP (if available)
   float precise[3];
};
typedef struct tri_vertex tri_vertex;

struct i_group;
struct i_deltas;

struct line_point
{
   int32_t x, y;
   uint8_t r, g, b;
};
typedef struct line_point line_point;

#define vertex_swap(_type, _a, _b) \
{                           \
   _type tmp = _a;          \
   _a = _b;                 \
   _b = tmp;                \
}                           \

struct PS_GPU
{
   uint16_t CLUT_Cache[256];

   uint32_t CLUT_Cache_VB;   // Don't try to be clever and reduce it to 16 bits... ~0U is value for invalidated state.

   struct   // Speedup-cache varibles, derived from other variables; shouldn't be saved in save states.
   {
      // TW*_* variables derived from tww, twh, twx, twy, TexPageX, TexPageY
      uint32_t TWX_AND;
      uint32_t TWX_ADD;

      uint32_t TWY_AND;
      uint32_t TWY_ADD;
   } SUCV;

   struct TexCache_t
   {
      uint16_t Data[4];
      uint32_t Tag;
   } TexCache[256];

   uint32_t DMAControl;

   /* Beetle-psx upscaling vars */
   uint8_t upscale_shift;
   uint8_t dither_upscale_shift;

   // Drawing stuff
   int32_t ClipX0;
   int32_t ClipY0;
   int32_t ClipX1;
   int32_t ClipY1;

   int32_t OffsX;
   int32_t OffsY;

   bool dtd;            // Dithering enable 
   bool dfe;

   uint32_t MaskSetOR;
   uint32_t MaskEvalAND;

   bool TexDisable;
   bool TexDisableAllowChange;

   uint8_t tww, twh, twx, twy;
   struct
   {
      uint8_t TexWindowXLUT_Pre[16];
      uint8_t TexWindowXLUT[256];
      uint8_t TexWindowXLUT_Post[16];
   };

   struct
   {
      uint8_t TexWindowYLUT_Pre[16];
      uint8_t TexWindowYLUT[256];
      uint8_t TexWindowYLUT_Post[16];
   };

   uint32_t TexPageX; // 0, 64, 128, 192, etc up to 960
   uint32_t TexPageY; // 0 or 256

   uint32_t SpriteFlip;

   uint32_t abr;        // Semi-transparency mode(0~3)
   uint32_t TexMode;

   struct
   {
      uint8_t RGB8SAT_Under[256];
      uint8_t RGB8SAT[256];
      uint8_t RGB8SAT_Over[256];
   };

   uint32_t DataReadBuffer;
   uint32_t DataReadBufferEx;

   bool IRQPending;

   // Powers of 2 for faster multiple equality testing(just for multi-testing; InCmd itself will only contain 0, or a power of 2).
   uint8_t InCmd;
   uint8_t InCmd_CC;

   tri_vertex InQuad_F3Vertices[3];
   uint32_t InQuad_clut;
   bool InQuad_invalidW;
   uint32_t killQuadPart;	// bit flags for tris in quad that are to be culled

   // primitive UV offsets (used to correct flipped sprites)
   uint16_t off_u, off_v;
   // primitive UV limits (used to clamp texture sampling)
   uint16_t min_u, min_v, max_u, max_v;
   bool may_be_2d;

   line_point InPLine_PrevPoint;

   uint32_t FBRW_X;
   uint32_t FBRW_Y;
   uint32_t FBRW_W;
   uint32_t FBRW_H;
   uint32_t FBRW_CurY;
   uint32_t FBRW_CurX;

   //
   // Display Parameters
   //
   uint32_t DisplayMode;

   bool DisplayOff;
   uint32_t DisplayFB_XStart;
   uint32_t DisplayFB_YStart;

   bool display_possibly_dirty;
   unsigned display_change_count;

   uint32_t HorizStart;
   uint32_t HorizEnd;

   uint32_t VertStart;
   uint32_t VertEnd;

   //
   // Display work vars
   //
   uint32_t DisplayFB_CurYOffset;
   uint32_t DisplayFB_CurLineYReadout;

   bool InVBlank;

   //
   //
   //
   uint32_t LinesPerField;
   uint32_t scanline;
   bool field;
   bool field_ram_readout;
   bool PhaseChange;

   uint32_t DotClockCounter;

   uint64_t GPUClockCounter;
   int32_t GPUClockRatio;
   int32_t LineClockCounter;
   int32_t LinePhase;

   int32_t DrawTimeAvail;

   int32_t lastts;

   bool sl_zero_reached;

   EmulateSpecStruct *espec;
   MDFN_Surface *surface;
   MDFN_Rect *DisplayRect;
   int32_t *LineWidths;
   bool HardwarePALType;
   int LineVisFirst, LineVisLast;

   uint8_t DitherLUT[4][4][512]; // Y, X, 8-bit source value(256 extra for saturation)

   /*
   VRAM has to be a ptr type or else we have to rely on smartcode void* shenanigans to
   wrestle a variable-sized struct.
   */
   uint16_t *vram;
};
typedef struct PS_GPU PS_GPU;

#include "gpu_c.h"

#ifdef __cplusplus
extern "C" {
#endif

void GPU_set_dither_upscale_shift(uint8_t factor);

uint8_t GPU_get_upscale_shift(void);

bool GPU_get_display_possibly_dirty(void);

void GPU_set_display_possibly_dirty(bool dirty);

void GPU_set_display_change_count(unsigned a);

unsigned GPU_get_display_change_count(void);

bool GPU_Init(bool pal_clock_and_tv,
      int sls, int sle, uint8_t upscale_shift);

void GPU_RecalcClockRatio(void);

void GPU_Destroy(void);

bool GPU_Rescale(uint8_t ushift);

void GPU_Power(void);

void GPU_ResetTS(void);

void GPU_Write(const int32_t timestamp, uint32_t A, uint32_t V);

uint32_t GPU_Read(const int32_t timestamp, uint32_t A);

void GPU_StartFrame(EmulateSpecStruct *espec_arg);
void GPU_FlushDeferredScanout(void);

/* Resets the per-dest_line cache that lets the SW renderer skip
 * re-zeroing margin pixels each frame when geometry is unchanged.
 * Must be called whenever the surface storage has been replaced
 * (alloc_surface, GPU_Rescale) or when external code may have
 * dirtied the margin pixels (Deinterlacer_Process in WEAVE mode). */
void GPU_InvalidateScanoutCache(void);

int GPU_StateAction(StateMem *sm, int load, int data_only);

void GPU_set_visible_scanlines(int sls, int sle); // Beetle PSX addition

#ifdef __cplusplus
}
#endif

#endif
