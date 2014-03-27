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
#include "mdec.h"

#include "../cdrom/SimpleFIFO.h"
#include <math.h>

#if defined(__SSE2__)
#include <xmmintrin.h>
#include <emmintrin.h>
#endif

#if defined(ARCH_POWERPC_ALTIVEC) && defined(HAVE_ALTIVEC_H)
 #include <altivec.h>
#endif

namespace MDFN_IEN_PSX
{


static bool block_ready;
static int16 block_y[2][2][8][8];
static int16 block_cb[8][8];	// [y >> 1][x >> 1]
static int16 block_cr[8][8];	// [y >> 1][x >> 1]

static int32 run_time;
static uint32 Command;

static uint8 QMatrix[2][64];
static uint32 QMIndex;

static int16 IDCTMatrix[64] MDFN_ALIGN(16);
static uint32 IDCTMIndex;

static uint8 QScale;

static int16 Coeff[6][64] MDFN_ALIGN(16);
static uint32 CoeffIndex;
static uint32 DecodeWB;

static SimpleFIFO<uint16> InputBuffer(65536);
static SimpleFIFO<uint16> OutBuffer(384);

static uint32 InCounter;
static bool BlockEnd;
static bool DecodeEnd;

static const uint8 ZigZag[64] =
{
 0x00, 0x08, 0x01, 0x02, 0x09, 0x10, 0x18, 0x11, 
 0x0a, 0x03, 0x04, 0x0b, 0x12, 0x19, 0x20, 0x28, 
 0x21, 0x1a, 0x13, 0x0c, 0x05, 0x06, 0x0d, 0x14, 
 0x1b, 0x22, 0x29, 0x30, 0x38, 0x31, 0x2a, 0x23, 
 0x1c, 0x15, 0x0e, 0x07, 0x0f, 0x16, 0x1d, 0x24, 
 0x2b, 0x32, 0x39, 0x3a, 0x33, 0x2c, 0x25, 0x1e, 
 0x17, 0x1f, 0x26, 0x2d, 0x34, 0x3b, 0x3c, 0x35, 
 0x2e, 0x27, 0x2f, 0x36, 0x3d, 0x3e, 0x37, 0x3f, 
};

void MDEC_Power(void)
{
#if 0
 for(int i = 0; i < 64; i++)
 {
  int d = ((ZigZag[i] & 0x7) << 3) | ((ZigZag[i] >> 3) & 0x7);
  printf("0x%02x, ", d);

  if((i & 0x7) == 7)
   printf("\n");
 }
#endif

 run_time = 0;
 block_ready = false;

 Command = 0;
 memset(QMatrix, 0, sizeof(QMatrix));
 QMIndex = 0;

 memset(IDCTMatrix, 0, sizeof(IDCTMatrix));
 IDCTMIndex = 0;

 QScale = 0;

 memset(Coeff, 0, sizeof(Coeff));
 CoeffIndex = 0;
 DecodeWB = 0;

 OutBuffer.Flush();

 InCounter = 0;
 BlockEnd = 0;
 DecodeEnd = 0;
}

int MDEC_StateAction(StateMem *sm, int load, int data_only)
{
 SFORMAT StateRegs[] =
 {
  SFVAR(block_ready),

  SFARRAY16(&block_y[0][0][0][0], sizeof(block_y) / sizeof(block_y[0][0][0][0])),
  SFARRAY16(&block_cb[0][0], sizeof(block_cb) / sizeof(block_cb[0][0])),
  SFARRAY16(&block_cr[0][0], sizeof(block_cr) / sizeof(block_cr[0][0])),

  SFVAR(run_time),
  SFVAR(Command),

  SFARRAY(&QMatrix[0][0], sizeof(QMatrix) / sizeof(QMatrix[0][0])),
  SFVAR(QMIndex),

  SFARRAY16(&IDCTMatrix[0], sizeof(IDCTMatrix) / sizeof(IDCTMatrix[0])),
  SFVAR(IDCTMIndex),

  SFVAR(QScale),

  SFARRAY16(&Coeff[0][0], sizeof(Coeff) / sizeof(Coeff[0][0])),
  SFVAR(CoeffIndex),
  SFVAR(DecodeWB),

#define SFFIFO16(fifoobj)  SFARRAY16(&fifoobj.data[0], fifoobj.data.size()),	\
			 SFVAR(fifoobj.read_pos),				\
			 SFVAR(fifoobj.write_pos),				\
			 SFVAR(fifoobj.in_count)

  SFFIFO16(InputBuffer),
  SFFIFO16(OutBuffer),
#undef SFFIFO

  SFVAR(InCounter),
  SFVAR(BlockEnd),
  SFVAR(DecodeEnd),

  SFEND
 };

 int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "MDEC");

 if(load)
 {

 }

 return(ret);
}


static void DecodeImage(void);
static INLINE void WriteImageData(uint16 V)
{
 const uint32 qmw = (bool)(DecodeWB < 2);

  //printf("MDEC DMA SubWrite: %04x\n", V);

  if(!CoeffIndex)
  {
   if(DecodeWB == 0 && V == 0xFE00)
   {
    InputBuffer.Flush();
    return;
   }
   QScale = V >> 10;

   {
    int q = QMatrix[qmw][0];	// No QScale here!
    int ci = sign_10_to_s16(V & 0x3FF);
    int tmp;

    if(q != 0)
     tmp = ((ci * q) << 4) + (ci ? ((ci < 0) ? 8 : -8) : 0);
    else
     tmp = (ci * 2) << 4;

    // Not sure if it should be 0x3FFF or 0x3FF0 or maybe 0x3FF8?
    Coeff[DecodeWB][ZigZag[0]] = std::min<int>(0x3FFF, std::max<int>(-0x4000, tmp));
    CoeffIndex++;
   }
  }
  else
  {
   if(V == 0xFE00)
   {
    BlockEnd = true;
    while(CoeffIndex < 64)
     Coeff[DecodeWB][ZigZag[CoeffIndex++]] = 0;
   }
   else
   {
    uint32 rlcount = V >> 10;

    for(uint32 i = 0; i < rlcount && CoeffIndex < 64; i++)
    {
     Coeff[DecodeWB][ZigZag[CoeffIndex]] = 0;
     CoeffIndex++;
    }

    if(CoeffIndex < 64)
    {
     int q = QScale * QMatrix[qmw][CoeffIndex];
     int ci = sign_10_to_s16(V & 0x3FF);
     int tmp;

     if(q != 0)
      tmp = (((ci * q) >> 3) << 4) + (ci ? ((ci < 0) ? 8 : -8) : 0);
     else
      tmp = (ci * 2) << 4;

     // Not sure if it should be 0x3FFF or 0x3FF0 or maybe 0x3FF8?
     Coeff[DecodeWB][ZigZag[CoeffIndex]] = std::min<int>(0x3FFF, std::max<int>(-0x4000, tmp));
     CoeffIndex++;
    }
   }
  }

  if(CoeffIndex == 64 && BlockEnd)
  {
   BlockEnd = false;
   CoeffIndex = 0;

   //printf("Block %d finished\n", DecodeWB);

   DecodeWB++;
   if(DecodeWB == (((Command >> 27) & 2) ? 6 : 3))
   {
    DecodeWB = 0;

    DecodeImage();
   }
  }
}

template<bool phase>
static void IDCT_1D_Multi(int16 *in_coeff, int16 *out_coeff)
{
#if defined(__SSE2__)
{
 for(unsigned col = 0; col < 8; col++)
 {
  __m128i c =  _mm_load_si128((__m128i *)&in_coeff[(col * 8)]);

  for(unsigned x = 0; x < 8; x++)
  {
   __m128i sum;
   __m128i m;
   int32 tmp[4] MDFN_ALIGN(16);

   m = _mm_load_si128((__m128i *)&IDCTMatrix[(x * 8)]);
   sum = _mm_madd_epi16(m, c);
   sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, (3 << 0) | (2 << 2) | (1 << 4) | (0 << 6)));
   sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, (1 << 0) | (0 << 2)));

   //_mm_store_ss((float *)&tmp[0], (__m128)sum);
   _mm_store_si128((__m128i*)tmp, sum);

   if(phase)
    out_coeff[(col * 8) + x] = (tmp[0] + 0x4000) >> 15;
   else
    out_coeff[(x * 8) + col] = (tmp[0] + 0x4000) >> 15;
  }
 }
}
#else
 for(unsigned col = 0; col < 8; col++)
 {
  for(unsigned x = 0; x < 8; x++)
  {
   int32 sum = 0;

   for(unsigned u = 0; u < 8; u++)
   {
    sum += (in_coeff[(col * 8) + u] * IDCTMatrix[(x * 8) + u]);
   }

   if(phase)
    out_coeff[(col * 8) + x] = (sum + 0x4000) >> 15;
   else
    out_coeff[(x * 8) + col] = (sum + 0x4000) >> 15;
  }
 }
#endif
}

static void IDCT(int16 *in_coeff, int16 *out_coeff) NO_INLINE;
static void IDCT(int16 *in_coeff, int16 *out_coeff)
{
 int16 tmpbuf[64] MDFN_ALIGN(16);

 IDCT_1D_Multi<0>(in_coeff, tmpbuf);
 IDCT_1D_Multi<1>(tmpbuf, out_coeff);
}

static void YCbCr_to_RGB(const int32 y, const int32 cb, const int32 cr, uint8 &r, uint8 &g, uint8 &b)
{
 int rt = (y + 128) + ((91881 * cr) >> 16);
 int gt = (y + 128) - ((22525 * cb) >> 16) - ((46812 * cr) >> 16);
 int bt = (y + 128) + ((116130 * cb) >> 16);

 r = std::max<int>(std::min<int>(rt, 255), 0);
 g = std::max<int>(std::min<int>(gt, 255), 0);
 b = std::max<int>(std::min<int>(bt, 255), 0);
}

static void DecodeImage(void)
{
 //puts("DECODE");

 if((Command >> 27) & 0x2)
 {
  run_time -= 2048;

  IDCT(Coeff[0], &block_cr[0][0]);
  IDCT(Coeff[1], &block_cb[0][0]);
  IDCT(Coeff[2], &block_y[0][0][0][0]);
  IDCT(Coeff[3], &block_y[0][1][0][0]);
  IDCT(Coeff[4], &block_y[1][0][0][0]);
  IDCT(Coeff[5], &block_y[1][1][0][0]);
 }
 else
 {
  run_time -= 341;
  IDCT(Coeff[2], &block_y[0][0][0][0]);
 }

 block_ready = true;
}

static void EncodeImage(void)
{
 //printf("ENCODE, %d\n", (Command & 0x08000000) ? 256 : 384);

 block_ready = false;

 switch((Command >> 27) & 0x3)
 {
  case 0:	// 4bpp, TODO

  break;


  case 1:	// 8bpp
  {
   uint8 us_xor = (Command & (1U << 26)) ? 0x00 : 0x80;

   for(int y = 0; y < 8; y++)
   {
    uint32 qb = 0;

    for(int x = 0; x < 8; x++)
    {
     int yv = block_y[0][0][y][x];

     if(yv < -128)
      yv = -128;

     if(yv > 127)
      yv = 127;

     qb |= ((uint8)yv ^ us_xor) << ((x & 1) * 8);
     if((x & 1) == 1)
     {
      if(OutBuffer.CanWrite())
       OutBuffer.WriteUnit(qb);

      qb = 0;
     }

#if 0
     qb |= yv << ((x & 3) * 8);
     if((x & 3) == 3)
     {
      if(OutBuffer.CanWrite())
      {
       printf("0x%08x\n", qb);
       OutBuffer.WriteUnit(qb);
      }
      qb = 0;
     }
#endif
    }
   }
  }
  break;

  case 2:	// 24bpp
  {
   uint8 output[16][16][3];	// [y][x][cc]

   for(int y = 0; y < 16; y++)
   {
    for(int x = 0; x < 16; x++)
    {
     uint8 r, g, b;

     YCbCr_to_RGB(block_y[(y >> 3) & 1][(x >> 3) & 1][y & 7][x & 7], block_cb[y >> 1][x >> 1], block_cr[y >> 1][x >> 1], r, g, b);

     output[y][x][0] = r;
     output[y][x][1] = g;
     output[y][x][2] = b;
    }
   }

   for(int i = 0; i < 384; i++)
   {
    if(OutBuffer.CanWrite())
     OutBuffer.WriteUnit((&output[0][0][0])[i * 2 + 0] | ((&output[0][0][0])[i * 2 + 1] << 8));
   }
  }
  break;

  case 3:	// 16bpp
  {
   uint16 pixel_or = (Command & 0x02000000) ? 0x8000 : 0x0000;

   for(int y = 0; y < 16; y++)
   {
    for(int x = 0; x < 16; x++)
    {
     uint8 r, g, b;

     YCbCr_to_RGB(block_y[(y >> 3) & 1][(x >> 3) & 1][y & 7][x & 7], block_cb[y >> 1][x >> 1], block_cr[y >> 1][x >> 1], r, g, b);

     if(OutBuffer.CanWrite())
      OutBuffer.WriteUnit(pixel_or | ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10));
    }
   }
  }
  break;

 }
}

void MDEC_DMAWrite(uint32 V)
{
 if(InCounter > 0)
 {
  InCounter--;

  switch((Command >> 29) & 0x7)
  {
   case 1:
	for(int vi = 0; vi < 2; vi++)
	{
	 if(InputBuffer.CanWrite())
	  InputBuffer.WriteUnit(V);

	 V >>= 16;
	}
	break;

   case 2:
	for(int i = 0; i < 4; i++)
	{
	 QMatrix[QMIndex >> 6][QMIndex & 0x3F] = (uint8)V;
	 QMIndex = (QMIndex + 1) & 0x7F;
	 V >>= 8;
	}
	break;

   case 3:
  	for(unsigned i = 0; i < 2; i++)
	{
	 IDCTMatrix[((IDCTMIndex & 0x7) << 3) | ((IDCTMIndex >> 3) & 0x7)] = (int16)(V & 0xFFFF) >> 3;
	 IDCTMIndex = (IDCTMIndex + 1) & 0x3F;

	 V >>= 16;
	}
	break;

   default:
	PSX_DBG(PSX_DBG_WARNING, "MYSTERY1: %08x\n", V);
	break;
  }
 }
}

uint32 MDEC_DMARead(void)
{
 uint32 V = 0;

 if(((Command >> 29) & 0x7) == 0x1 && OutBuffer.CanRead() >= 2)
 {
  V = OutBuffer.ReadUnit() | (OutBuffer.ReadUnit() << 16);
 }
 else
 {
  PSX_DBG(PSX_DBG_WARNING, "[MDEC] BONUS GNOMES\n");
  V = rand();
 }
 return(V);
}

// Test case related to this: GameShark Version 4.0 intro movie(coupled with (clever) abuse of DMA channel 0).
//			also: SimCity 2000 startup.
bool MDEC_DMACanWrite(void)
{
 return(InCounter > 0 && ((Command >> 29) & 0x7) >= 1 && ((Command >> 29) & 0x7) <= 3);
}

bool MDEC_DMACanRead(void)
{
 return(OutBuffer.CanRead() >= 2);
}

void MDEC_Write(const pscpu_timestamp_t timestamp, uint32 A, uint32 V)
{
 //PSX_WARNING("[MDEC] Write: 0x%08x 0x%08x, %d", A, V, timestamp);
 if(A & 4)
 {
  if(V & 0x80000000) // Reset?
  {
   Command = 0;

   block_ready = false;
   run_time = 0;
   QMIndex = 0;
   IDCTMIndex = 0;

   QScale = 0;

   memset(Coeff, 0, sizeof(Coeff));
   CoeffIndex = 0;
   DecodeWB = 0;

   InputBuffer.Flush();
   OutBuffer.Flush();

   InCounter = 0;
   BlockEnd = false;
  }
 }
 else
 {
  Command = V;

  switch((Command >> 29) & 0x7)
  {
   case 1:
	InputBuffer.Flush();
	OutBuffer.Flush();

	block_ready = false;
	BlockEnd = false;
	CoeffIndex = 0;

	if((Command >> 27) & 2)
	 DecodeWB = 0;
	else
	 DecodeWB = 2;

	InCounter = V & 0xFFFF;
	break;

   case 2:
   	QMIndex = 0;
	InCounter = 0x10 + ((Command & 0x1) ? 0x10 : 0x00);
	break;

   case 3:
	IDCTMIndex = 0;
	InCounter = 0x20;
	break;

   default:
	InCounter = V & 0xFFFF;
	break;
  }
 }
}

uint32 MDEC_Read(const pscpu_timestamp_t timestamp, uint32 A)
{
 uint32 ret = 0;

 if(A & 4)
 {
  ret = 0;

  if(InputBuffer.CanRead())
   ret |= 0x20000000;

  ret |= ((Command >> 25) & 0xF) << 23;
 }
 else
 {
  ret = Command;
 }

 //PSX_WARNING("[MDEC] Read: 0x%08x 0x%08x -- %d %d", A, ret, InputBuffer.CanRead(), InCounter);

 return(ret);
}

void MDEC_Run(int32 clocks)
{
 run_time += clocks;

 while(run_time > 0)
 {
  run_time--;

  if(block_ready && !OutBuffer.CanRead())
   EncodeImage();

  if(block_ready && OutBuffer.CanRead())
   break;

  if(!InputBuffer.CanRead())
   break;

  WriteImageData(InputBuffer.ReadUnit());
 }

 if(run_time > 0)
  run_time = 0;
}

#if 0
// Maybe we should just use libco....
#define MDEC_WRITE_FIFO(n) case __COUNTER__: if(!InFIFO.CanRead()) { MDRPhase = __COUNTER__ - 1; return; } OutFIFO.Write(n); 
#define MDEC_READ_FIFO(n) case __COUNTER__: if(!InFIFO.CanRead()) { MDRPhase = __COUNTER__ - 1; return; } n = InFIFO.Read();
#define MDEC_EAT_CLOCKS(n) ClockCounter -= clocks;  case __COUNTER__: if(ClockCounter <= 0) { MDRPhase = __COUNTER__ - 1; return; }
void MDEC_Run2(int32 clocks)
{
 ClockCounter += clocks;

 if(ClockCounter > 128)
  ClockCounter = 128;

 switch(MDRPhase)
 {
  MDEC_READ_FIFO(Command);

  //
  //
  //
  if(((Command >> 29) & 0x7) == 1)
  {
   InCounter = Command & 0xFFFF;
   OutBuffer.Flush();

   block_ready = false;
   BlockEnd = false;
   CoeffIndex = 0;

   if((Command >> 27) & 2)
    DecodeWB = 0;
   else
    DecodeWB = 2;

   InCounter--;
   while(InCounter != 0xFFFF)
   {
	uint32 tfr;
	bool need_encode;

	MDEC_READ_FIFO(tfr);

	need_encode = WriteImageData(tfr);
	need_encode |= WriteImageData(tfr >> 16);

	if(need_encode)
	{


	}
   }
  }
  //
  //
  //
  else if(((Command >> 29) & 0x7) == 2)
  {
   QMIndex = 0;
   InCounter = 0x10 + ((Command & 0x1) ? 0x10 : 0x00);

   InCounter--;
   while(InCounter != 0xFFFF)
   {
	uint32 tfr;
    
	MDEC_READ_FIFO(tfr);

	for(int i = 0; i < 4; i++)
	{
         QMatrix[QMIndex >> 6][QMIndex & 0x3F] = (uint8)tfr;
	 QMIndex = (QMIndex + 1) & 0x7F;
	 tfr >>= 8;
	}
   }
  }
  //
  //
  //
  else if(((Command >> 29) & 0x7) == 3)
  {
   IDCTMIndex = 0;
   InCounter = 0x20;

   InCounter--;
   while(InCounter != 0xFFFF)
   {
    uint32 tfr;

    MDEC_READ_FIFO(tfr);

    for(unsigned i = 0; i < 2; i++)
    {
     int tmp = (int16)(tfr & 0xFFFF) >> 3;

     IDCTMatrix[((IDCTMIndex & 0x7) << 3) | ((IDCTMIndex >> 3) & 0x7)] = (int16)(V & 0xFFFF) >> 3;
     IDCTMIndex = (IDCTMIndex + 1) & 0x3F;

     tfr >>= 16;
    }
   }
  }
  else
  {
   InCounter = Command & 0xFFFF;
  }
 }
}

#endif
}
