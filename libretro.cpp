#include "mednafen/mednafen.h"
#include "mednafen/mempatcher.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include "mednafen/md5.h"
#include "mednafen/msvc_compat.h"
#ifdef NEED_DEINTERLACER
#include	"mednafen/video/Deinterlacer.h"
#endif
#include "libretro.h"
#include <rthreads/rthreads.h>

struct retro_perf_callback perf_cb;
retro_get_cpu_features_t perf_get_cpu_features_cb = NULL;
retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_rumble_interface rumble;
static unsigned players = 2;

unsigned char widescreen_hack;
unsigned char widescreen_auto_ar;
unsigned char widescreen_auto_ar_old;

static bool is_pal;

/* start of Mednafen psx.cpp */

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

#include "mednafen/psx/psx.h"
#include "mednafen/psx/mdec.h"
#include "mednafen/psx/frontio.h"
#include "mednafen/psx/timer.h"
#include "mednafen/psx/sio.h"
#include "mednafen/psx/cdc.h"
#include "mednafen/psx/spu.h"
#include "mednafen/mempatcher.h"

#include <stdarg.h>
#include <ctype.h>

bool setting_apply_analog_toggle  = false;
bool use_mednafen_memcard0_method = false;

extern MDFNGI EmulatedPSX;
MDFNGI *MDFNGameInfo = &EmulatedPSX;

enum
{
 REGION_JP = 0,
 REGION_NA = 1,
 REGION_EU = 2,
};

#if PSX_DBGPRINT_ENABLE
static unsigned psx_dbg_level = 0;

void PSX_DBG(unsigned level, const char *format, ...)
{
   if(psx_dbg_level >= level)
   {
      va_list ap;
      va_start(ap, format);
      trio_vprintf(format, ap);
      va_end(ap);
   }
}
#else
static unsigned const psx_dbg_level = 0;
#endif

/* Based off(but not the same as) public-domain "JKISS" PRNG. */
struct MDFN_PseudoRNG
{
   uint32_t x,y,z,c;
   uint64_t lcgo;
};

static MDFN_PseudoRNG PSX_PRNG;

uint32_t PSX_GetRandU32(uint32_t mina, uint32_t maxa)
{
   uint32_t tmp;
   const uint32_t range_m1 = maxa - mina;
   uint32_t range_mask = range_m1;

   range_mask |= range_mask >> 1;
   range_mask |= range_mask >> 2;
   range_mask |= range_mask >> 4;
   range_mask |= range_mask >> 8;
   range_mask |= range_mask >> 16;

   do
   {
      uint64_t t = 4294584393ULL * PSX_PRNG.z + PSX_PRNG.c;

      PSX_PRNG.x = 314527869 * PSX_PRNG.x + 1234567;
      PSX_PRNG.y ^= PSX_PRNG.y << 5;
      PSX_PRNG.y ^= PSX_PRNG.y >> 7;
      PSX_PRNG.y ^= PSX_PRNG.y << 22;
      PSX_PRNG.c = t >> 32;
      PSX_PRNG.z = t;
      PSX_PRNG.lcgo = (19073486328125ULL * PSX_PRNG.lcgo) + 1;
      tmp = ((PSX_PRNG.x + PSX_PRNG.y + PSX_PRNG.z) ^ (PSX_PRNG.lcgo >> 16)) & range_mask;
   } while(tmp > range_m1);

   return(mina + tmp);
}

static std::vector<CDIF*> *cdifs = NULL;
static std::vector<const char *> cdifs_scex_ids;
static bool CD_TrayOpen;
static int CD_SelectedDisc;     // -1 for no disc

static uint64_t Memcard_PrevDC[8];
static int64_t Memcard_SaveDelay[8];

PS_CPU *CPU = NULL;
PS_SPU *SPU = NULL;
PS_GPU *GPU = NULL;
PS_CDC *CDC = NULL;
FrontIO *FIO = NULL;

static MultiAccessSizeMem<512 * 1024, uint32, false> *BIOSROM = NULL;
static MultiAccessSizeMem<65536, uint32, false> *PIOMem = NULL;

MultiAccessSizeMem<2048 * 1024, uint32, false> MainRAM;

static uint32_t TextMem_Start;
static std::vector<uint8> TextMem;

static const uint32_t SysControl_Mask[9] = { 0x00ffffff, 0x00ffffff, 0xffffffff, 0x2f1fffff,
					   0xffffffff, 0x2f1fffff, 0x2f1fffff, 0xffffffff,
					   0x0003ffff };

static const uint32_t SysControl_OR[9] = { 0x1f000000, 0x1f000000, 0x00000000, 0x00000000,
					 0x00000000, 0x00000000, 0x00000000, 0x00000000,
					 0x00000000 };

static struct
{
 union
 {
  struct
  {
   uint32_t PIO_Base;	// 0x1f801000	// BIOS Init: 0x1f000000, Writeable bits: 0x00ffffff(assumed, verify), FixedOR = 0x1f000000
   uint32_t Unknown0;	// 0x1f801004	// BIOS Init: 0x1f802000, Writeable bits: 0x00ffffff, FixedOR = 0x1f000000
   uint32_t Unknown1;	// 0x1f801008	// BIOS Init: 0x0013243f, ????
   uint32_t Unknown2;	// 0x1f80100c	// BIOS Init: 0x00003022, Writeable bits: 0x2f1fffff, FixedOR = 0x00000000
   
   uint32_t BIOS_Mapping;	// 0x1f801010	// BIOS Init: 0x0013243f, ????
   uint32_t SPU_Delay;	// 0x1f801014	// BIOS Init: 0x200931e1, Writeable bits: 0x2f1fffff, FixedOR = 0x00000000 - Affects bus timing on access to SPU
   uint32_t CDC_Delay;	// 0x1f801018	// BIOS Init: 0x00020843, Writeable bits: 0x2f1fffff, FixedOR = 0x00000000
   uint32_t Unknown4;	// 0x1f80101c	// BIOS Init: 0x00070777, ????
   uint32_t Unknown5;	// 0x1f801020	// BIOS Init: 0x00031125(but rewritten with other values often), Writeable bits: 0x0003ffff, FixedOR = 0x00000000 -- Possibly CDC related
  };
  uint32_t Regs[9];
 };
} SysControl;

static unsigned DMACycleSteal = 0;   // Doesn't need to be saved in save states, since it's calculated in the ForceEventUpdates() call chain.

void PSX_SetDMACycleSteal(unsigned stealage)
{
   if (stealage > 200) // Due to 8-bit limitations in the CPU core.
      stealage = 200;

   DMACycleSteal = stealage;
}

//
// Event stuff
//

static int32_t Running;	// Set to -1 when not desiring exit, and 0 when we are.

struct event_list_entry
{
 uint32_t which;
 int32_t event_time;
 event_list_entry *prev;
 event_list_entry *next;
};

static event_list_entry events[PSX_EVENT__COUNT];

static void EventReset(void)
{
   unsigned i;
   for(i = 0; i < PSX_EVENT__COUNT; i++)
   {
      events[i].which = i;

      if(i == PSX_EVENT__SYNFIRST)
         events[i].event_time = 0;
      else if(i == PSX_EVENT__SYNLAST)
         events[i].event_time = 0x7FFFFFFF;
      else
         events[i].event_time = PSX_EVENT_MAXTS;

      events[i].prev = (i > 0) ? &events[i - 1] : NULL;
      events[i].next = (i < (PSX_EVENT__COUNT - 1)) ? &events[i + 1] : NULL;
   }
}

//static void RemoveEvent(event_list_entry *e)
//{
// e->prev->next = e->next;
// e->next->prev = e->prev;
//}

static void RebaseTS(const int32_t timestamp)
{
   unsigned i;
   for(i = 0; i < PSX_EVENT__COUNT; i++)
   {
      if(i == PSX_EVENT__SYNFIRST || i == PSX_EVENT__SYNLAST)
         continue;

      assert(events[i].event_time > timestamp);
      events[i].event_time -= timestamp;
   }

   CPU->SetEventNT(events[PSX_EVENT__SYNFIRST].next->event_time);
}

void PSX_SetEventNT(const int type, const int32_t next_timestamp)
{
   assert(type > PSX_EVENT__SYNFIRST && type < PSX_EVENT__SYNLAST);
   event_list_entry *e = &events[type];

   if(next_timestamp < e->event_time)
   {
      event_list_entry *fe = e;

      do
      {
         fe = fe->prev;
      }while(next_timestamp < fe->event_time);

      // Remove this event from the list, temporarily of course.
      e->prev->next = e->next;
      e->next->prev = e->prev;

      // Insert into the list, just after "fe".
      e->prev = fe;
      e->next = fe->next;
      fe->next->prev = e;
      fe->next = e;

      e->event_time = next_timestamp;
   }
   else if(next_timestamp > e->event_time)
   {
      event_list_entry *fe = e;

      do
      {
         fe = fe->next;
      } while(next_timestamp > fe->event_time);

      // Remove this event from the list, temporarily of course
      e->prev->next = e->next;
      e->next->prev = e->prev;

      // Insert into the list, just BEFORE "fe".
      e->prev = fe->prev;
      e->next = fe;
      fe->prev->next = e;
      fe->prev = e;

      e->event_time = next_timestamp;
   }

   CPU->SetEventNT(events[PSX_EVENT__SYNFIRST].next->event_time & Running);
}

// Called from debug.cpp too.
void ForceEventUpdates(const int32_t timestamp)
{
   PSX_SetEventNT(PSX_EVENT_GPU, GPU->Update(timestamp));
   PSX_SetEventNT(PSX_EVENT_CDC, CDC->Update(timestamp));

   PSX_SetEventNT(PSX_EVENT_TIMER, TIMER_Update(timestamp));

   PSX_SetEventNT(PSX_EVENT_DMA, DMA_Update(timestamp));

   PSX_SetEventNT(PSX_EVENT_FIO, FIO->Update(timestamp));

   CPU->SetEventNT(events[PSX_EVENT__SYNFIRST].next->event_time);
}

bool MDFN_FASTCALL PSX_EventHandler(const int32_t timestamp)
{
   event_list_entry *e = events[PSX_EVENT__SYNFIRST].next;

   while(timestamp >= e->event_time)	// If Running = 0, PSX_EventHandler() may be called even if there isn't an event per-se, so while() instead of do { ... } while
   {
      int32_t nt;
      event_list_entry *prev = e->prev;

      switch(e->which)
      {
         default:
            abort();
         case PSX_EVENT_GPU:
            nt = GPU->Update(e->event_time);
            break;
         case PSX_EVENT_CDC:
            nt = CDC->Update(e->event_time);
            break;
         case PSX_EVENT_TIMER:
            nt = TIMER_Update(e->event_time);
            break;
         case PSX_EVENT_DMA:
            nt = DMA_Update(e->event_time);
            break;
         case PSX_EVENT_FIO:
            nt = FIO->Update(e->event_time);
            break;
      }

      PSX_SetEventNT(e->which, nt);

      // Order of events can change due to calling PSX_SetEventNT(), this prev business ensures we don't miss an event due to reordering.
      e = prev->next;
   }

   return(Running);
}


void PSX_RequestMLExit(void)
{
   Running = 0;
   CPU->SetEventNT(0);
}


//
// End event stuff
//


// Remember to update MemPeek<>() when we change address decoding in MemRW()
template<typename T, bool IsWrite, bool Access24> static INLINE void MemRW(int32_t &timestamp, uint32_t A, uint32_t &V)
{
#if 0
   if(IsWrite)
      printf("Write%d: %08x(orig=%08x), %08x\n", (int)(sizeof(T) * 8), A & mask[A >> 29], A, V);
   else
      printf("Read%d: %08x(orig=%08x)\n", (int)(sizeof(T) * 8), A & mask[A >> 29], A);
#endif

   if(!IsWrite)
      timestamp += DMACycleSteal;

   //if(A == 0xa0 && IsWrite)
   // DBG_Break();

   if(A < 0x00800000)
   {
      if(IsWrite)
      {
         //timestamp++;	// Best-case timing.
      }
      else
      {
         timestamp += 3;
      }

      if(Access24)
      {
         if(IsWrite)
            MainRAM.WriteU24(A & 0x1FFFFF, V);
         else
            V = MainRAM.ReadU24(A & 0x1FFFFF);
      }
      else
      {
         if(IsWrite)
            MainRAM.Write<T>(A & 0x1FFFFF, V);
         else
            V = MainRAM.Read<T>(A & 0x1FFFFF);
      }

      return;
   }

   if(A >= 0x1FC00000 && A <= 0x1FC7FFFF)
   {
      if(!IsWrite)
      {
         if(Access24)
            V = BIOSROM->ReadU24(A & 0x7FFFF);
         else
            V = BIOSROM->Read<T>(A & 0x7FFFF);
      }

      return;
   }

   if(timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
      PSX_EventHandler(timestamp);

   if(A >= 0x1F801000 && A <= 0x1F802FFF)
   {

      //if(IsWrite)
      // printf("HW Write%d: %08x %08x\n", (unsigned int)(sizeof(T)*8), (unsigned int)A, (unsigned int)V);
      //else
      // printf("HW Read%d: %08x\n", (unsigned int)(sizeof(T)*8), (unsigned int)A);

      if(A >= 0x1F801C00 && A <= 0x1F801FFF) // SPU
      {
         if(sizeof(T) == 4 && !Access24)
         {
            if(IsWrite)
            {
               //timestamp += 15;

               //if(timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
               // PSX_EventHandler(timestamp);

               SPU->Write(timestamp, A | 0, V);
               SPU->Write(timestamp, A | 2, V >> 16);
            }
            else
            {
               timestamp += 36;

               if(timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
                  PSX_EventHandler(timestamp);

               V = SPU->Read(timestamp, A) | (SPU->Read(timestamp, A | 2) << 16);
            }
         }
         else
         {
            if(IsWrite)
            {
               //timestamp += 8;

               //if(timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
               // PSX_EventHandler(timestamp);

               SPU->Write(timestamp, A & ~1, V);
            }
            else
            {
               timestamp += 16; // Just a guess, need to test.

               if(timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
                  PSX_EventHandler(timestamp);

               V = SPU->Read(timestamp, A & ~1);
            }
         }
         return;
      }		// End SPU


      // CDC: TODO - 8-bit access.
      if(A >= 0x1f801800 && A <= 0x1f80180F)
      {
         if(!IsWrite) 
         {
            timestamp += 6 * sizeof(T); //24;
         }

         if(IsWrite)
            CDC->Write(timestamp, A & 0x3, V);
         else
            V = CDC->Read(timestamp, A & 0x3);

         return;
      }

      if(A >= 0x1F801810 && A <= 0x1F801817)
      {
         if(!IsWrite)
            timestamp++;

         if(IsWrite)
            GPU->Write(timestamp, A, V);
         else
            V = GPU->Read(timestamp, A);

         return;
      }

      if(A >= 0x1F801820 && A <= 0x1F801827)
      {
         if(!IsWrite)
            timestamp++;

         if(IsWrite)
            MDEC_Write(timestamp, A, V);
         else
            V = MDEC_Read(timestamp, A);

         return;
      }

      if(A >= 0x1F801000 && A <= 0x1F801023)
      {
         unsigned index = (A & 0x1F) >> 2;

         if(!IsWrite)
            timestamp++;

         //if(A == 0x1F801014 && IsWrite)
         // fprintf(stderr, "%08x %08x\n",A,V);

         if(IsWrite)
         {
            V <<= (A & 3) * 8;
            SysControl.Regs[index] = V & SysControl_Mask[index];
         }
         else
         {
            V = SysControl.Regs[index] | SysControl_OR[index];
            V >>= (A & 3) * 8;
         }
         return;
      }

      if(A >= 0x1F801040 && A <= 0x1F80104F)
      {
         if(!IsWrite)
            timestamp++;

         if(IsWrite)
            FIO->Write(timestamp, A, V);
         else
            V = FIO->Read(timestamp, A);
         return;
      }

      if(A >= 0x1F801050 && A <= 0x1F80105F)
      {
         if(!IsWrite)
            timestamp++;

#if 0
         if(IsWrite)
         {
            PSX_WARNING("[SIO] Write: 0x%08x 0x%08x %u", A, V, (unsigned)sizeof(T));
         }
         else
         {
            PSX_WARNING("[SIO] Read: 0x%08x", A);
         }
#endif

         if(IsWrite)
            SIO_Write(timestamp, A, V);
         else
            V = SIO_Read(timestamp, A);
         return;
      }

#if 0
      if(A >= 0x1F801060 && A <= 0x1F801063)
      {
         if(IsWrite)
         {

         }
         else
         {

         }

         return;
      }
#endif

      if(A >= 0x1F801070 && A <= 0x1F801077)	// IRQ
      {
         if(!IsWrite)
            timestamp++;

         if(IsWrite)
            ::IRQ_Write(A, V);
         else
            V = ::IRQ_Read(A);
         return;
      }

      if(A >= 0x1F801080 && A <= 0x1F8010FF) 	// DMA
      {
         if(!IsWrite)
            timestamp++;

         if(IsWrite)
            DMA_Write(timestamp, A, V);
         else
            V = DMA_Read(timestamp, A);

         return;
      }

      if(A >= 0x1F801100 && A <= 0x1F80113F)	// Root counters
      {
         if(!IsWrite)
            timestamp++;

         if(IsWrite)
            TIMER_Write(timestamp, A, V);
         else
            V = TIMER_Read(timestamp, A);

         return;
      }
   }


   if(A >= 0x1F000000 && A <= 0x1F7FFFFF)
   {
      if(!IsWrite)
      {
         //if((A & 0x7FFFFF) <= 0x84)
         //PSX_WARNING("[PIO] Read%d from 0x%08x at time %d", (int)(sizeof(T) * 8), A, timestamp);

         V = ~0U;	// A game this affects:  Tetris with Cardcaptor Sakura

         if(PIOMem)
         {
            if((A & 0x7FFFFF) < 65536)
            {
               if(Access24)
                  V = PIOMem->ReadU24(A & 0x7FFFFF);
               else
                  V = PIOMem->Read<T>(A & 0x7FFFFF);
            }
            else if((A & 0x7FFFFF) < (65536 + TextMem.size()))
            {
               if(Access24)
                  V = MDFN_de24lsb(&TextMem[(A & 0x7FFFFF) - 65536]);
               else switch(sizeof(T))
               {
                  case 1: V = TextMem[(A & 0x7FFFFF) - 65536]; break;
                  case 2: V = MDFN_de16lsb(&TextMem[(A & 0x7FFFFF) - 65536]); break;
                  case 4: V = MDFN_de32lsb(&TextMem[(A & 0x7FFFFF) - 65536]); break;
               }
            }
         }
      }
      return;
   }

   if(A == 0xFFFE0130) // Per tests on PS1, ignores the access(sort of, on reads the value is forced to 0 if not aligned) if not aligned to 4-bytes.
   {
      if(!IsWrite)
         V = CPU->GetBIU();
      else
         CPU->SetBIU(V);

      return;
   }

   if(IsWrite)
   {
      PSX_WARNING("[MEM] Unknown write%d to %08x at time %d, =%08x(%d)", (int)(sizeof(T) * 8), A, timestamp, V, V);
   }
   else
   {
      V = 0;
      PSX_WARNING("[MEM] Unknown read%d from %08x at time %d", (int)(sizeof(T) * 8), A, timestamp);
   }
}

void MDFN_FASTCALL PSX_MemWrite8(int32_t timestamp, uint32_t A, uint32_t V)
{
   MemRW<uint8, true, false>(timestamp, A, V);
}

void MDFN_FASTCALL PSX_MemWrite16(int32_t timestamp, uint32_t A, uint32_t V)
{
   MemRW<uint16, true, false>(timestamp, A, V);
}

void MDFN_FASTCALL PSX_MemWrite24(int32_t timestamp, uint32_t A, uint32_t V)
{
 MemRW<uint32, true, true>(timestamp, A, V);
}

void MDFN_FASTCALL PSX_MemWrite32(int32_t timestamp, uint32_t A, uint32_t V)
{
 MemRW<uint32, true, false>(timestamp, A, V);
}

uint8_t MDFN_FASTCALL PSX_MemRead8(int32_t &timestamp, uint32_t A)
{
 uint32_t V;

 MemRW<uint8, false, false>(timestamp, A, V);

 return(V);
}

uint16_t MDFN_FASTCALL PSX_MemRead16(int32_t &timestamp, uint32_t A)
{
 uint32_t V;

 MemRW<uint16, false, false>(timestamp, A, V);

 return(V);
}

uint32_t MDFN_FASTCALL PSX_MemRead24(int32_t &timestamp, uint32_t A)
{
 uint32_t V;

 MemRW<uint32, false, true>(timestamp, A, V);

 return(V);
}

uint32_t MDFN_FASTCALL PSX_MemRead32(int32_t &timestamp, uint32_t A)
{
 uint32_t V;

 MemRW<uint32, false, false>(timestamp, A, V);

 return(V);
}

template<typename T, bool Access24> static INLINE uint32_t MemPeek(int32_t timestamp, uint32_t A)
{
   if(A < 0x00800000)
   {
      if(Access24)
         return(MainRAM.ReadU24(A & 0x1FFFFF));
      return(MainRAM.Read<T>(A & 0x1FFFFF));
   }

   if(A >= 0x1FC00000 && A <= 0x1FC7FFFF)
   {
      if(Access24)
         return(BIOSROM->ReadU24(A & 0x7FFFF));
      return(BIOSROM->Read<T>(A & 0x7FFFF));
   }

   if(A >= 0x1F801000 && A <= 0x1F802FFF)
   {
      if(A >= 0x1F801C00 && A <= 0x1F801FFF) // SPU
      {
         // TODO

      }		// End SPU


      // CDC: TODO - 8-bit access.
      if(A >= 0x1f801800 && A <= 0x1f80180F)
      {
         // TODO

      }

      if(A >= 0x1F801810 && A <= 0x1F801817)
      {
         // TODO

      }

      if(A >= 0x1F801820 && A <= 0x1F801827)
      {
         // TODO

      }

      if(A >= 0x1F801000 && A <= 0x1F801023)
      {
         unsigned index = (A & 0x1F) >> 2;
         return((SysControl.Regs[index] | SysControl_OR[index]) >> ((A & 3) * 8));
      }

      if(A >= 0x1F801040 && A <= 0x1F80104F)
      {
         // TODO

      }

      if(A >= 0x1F801050 && A <= 0x1F80105F)
      {
         // TODO

      }


      if(A >= 0x1F801070 && A <= 0x1F801077)	// IRQ
      {
         // TODO

      }

      if(A >= 0x1F801080 && A <= 0x1F8010FF) 	// DMA
      {
         // TODO

      }

      if(A >= 0x1F801100 && A <= 0x1F80113F)	// Root counters
      {
         // TODO

      }
   }


   if(A >= 0x1F000000 && A <= 0x1F7FFFFF)
   {
      if(PIOMem)
      {
         if((A & 0x7FFFFF) < 65536)
         {
            if(Access24)
               return(PIOMem->ReadU24(A & 0x7FFFFF));
            return(PIOMem->Read<T>(A & 0x7FFFFF));
         }
         else if((A & 0x7FFFFF) < (65536 + TextMem.size()))
         {
            if(Access24)
               return(MDFN_de24lsb(&TextMem[(A & 0x7FFFFF) - 65536]));
            else switch(sizeof(T))
            {
               case 1:
                  return(TextMem[(A & 0x7FFFFF) - 65536]);
               case 2:
                  return(MDFN_de16lsb(&TextMem[(A & 0x7FFFFF) - 65536]));
               case 4:
                  return(MDFN_de32lsb(&TextMem[(A & 0x7FFFFF) - 65536]));
            }
         }
      }
      return(~0U);
   }

   if(A == 0xFFFE0130)
      return CPU->GetBIU();

   return(0);
}

uint8_t PSX_MemPeek8(uint32_t A)
{
   return MemPeek<uint8, false>(0, A);
}

uint16_t PSX_MemPeek16(uint32_t A)
{
   return MemPeek<uint16, false>(0, A);
}

uint32_t PSX_MemPeek32(uint32_t A)
{
   return MemPeek<uint32, false>(0, A);
}

// FIXME: Add PSX_Reset() and FrontIO::Reset() so that emulated input devices don't get power-reset on reset-button reset.
static void PSX_Power(void)
{
   unsigned i;

   PSX_PRNG.x = 123456789;
   PSX_PRNG.y = 987654321;
   PSX_PRNG.z = 43219876;
   PSX_PRNG.c = 6543217;
   PSX_PRNG.lcgo = 0xDEADBEEFCAFEBABEULL;

   memset(MainRAM.data32, 0, 2048 * 1024);

   for(i = 0; i < 9; i++)
      SysControl.Regs[i] = 0;

   CPU->Power();

   EventReset();

   TIMER_Power();

   DMA_Power();

   FIO->Power();
   SIO_Power();

   MDEC_Power();
   CDC->Power();
   GPU->Power();
   //SPU->Power();	// Called from CDC->Power()
   IRQ_Power();

   ForceEventUpdates(0);
}


void PSX_GPULineHook(const int32_t timestamp, const int32_t line_timestamp, bool vsync, uint32_t *pixels, const MDFN_PixelFormat* const format, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider)
{
   FIO->GPULineHook(timestamp, line_timestamp, vsync, pixels, format, width, pix_clock_offset, pix_clock, pix_clock_divider);
}

static bool TestMagic(const char *name, MDFNFILE *fp)
{
   if(GET_FSIZE_PTR(fp) < 0x800)
      return(false);

   if(memcmp(GET_FDATA_PTR(fp), "PS-X EXE", 8))
      return(false);

   return(true);
}

static bool TestMagicCD(std::vector<CDIF *> *CDInterfaces)
{
   uint8_t buf[2048];
   TOC toc;
   int dt;

   TOC_Clear(&toc);

   (*CDInterfaces)[0]->ReadTOC(&toc);

   dt = TOC_FindTrackByLBA(&toc, 4);

   if(dt > 0 && !(toc.tracks[dt].control & 0x4))
      return(false);

   if((*CDInterfaces)[0]->ReadSector(buf, 4, 1) != 0x2)
      return(false);

   if(strncmp((char *)buf + 10, "Licensed  by", strlen("Licensed  by")))
      return(false);

   //if(strncmp((char *)buf + 32, "Sony", 4))
   // return(false);

   //for(int i = 0; i < 2048; i++)
   // printf("%d, %02x %c\n", i, buf[i], buf[i]);
   //exit(1);

#if 0
   {
      uint8_t buf[2048 * 7];

      if((*cdifs)[0]->ReadSector(buf, 5, 7) == 0x2)
      {
         printf("CRC32: 0x%08x\n", (uint32)crc32(0, &buf[0], 0x3278));
      }
   }
#endif

   return(true);
}

static const char *CalcDiscSCEx_BySYSTEMCNF(CDIF *c, unsigned *rr)
{
   const char *ret = NULL;
   Stream *fp = NULL;

   uint8_t pvd[2048];
   unsigned pvd_search_count = 0;

   fp = c->MakeStream(0, ~0U);
   fp->seek(0x8000, SEEK_SET);

   do
   {
      if((pvd_search_count++) == 32)
         throw MDFN_Error(0, "PVD search count limit met.");

      fp->read(pvd, 2048);

      if(memcmp(&pvd[1], "CD001", 5))
         throw MDFN_Error(0, "Not ISO-9660");

      if(pvd[0] == 0xFF)
         throw MDFN_Error(0, "Missing Primary Volume Descriptor");
   } while(pvd[0] != 0x01);
   //[156 ... 189], 34 bytes
   uint32_t rdel = MDFN_de32lsb(&pvd[0x9E]);
   uint32_t rdel_len = MDFN_de32lsb(&pvd[0xA6]);

   if(rdel_len >= (1024 * 1024 * 10))	// Arbitrary sanity check.
      throw MDFN_Error(0, "Root directory table too large");

   fp->seek((int64)rdel * 2048, SEEK_SET);
   //printf("%08x, %08x\n", rdel * 2048, rdel_len);
   while(fp->tell() < (((int64)rdel * 2048) + rdel_len))
   {
      uint8_t len_dr = fp->get_u8();
      uint8_t dr[256 + 1];

      memset(dr, 0xFF, sizeof(dr));

      if(!len_dr)
         break;

      memset(dr, 0, sizeof(dr));
      dr[0] = len_dr;
      fp->read(dr + 1, len_dr - 1);

      uint8_t len_fi = dr[0x20];

      if(len_fi == 12 && !memcmp(&dr[0x21], "SYSTEM.CNF;1", 12))
      {
         uint32_t file_lba = MDFN_de32lsb(&dr[0x02]);
         //uint32_t file_len = MDFN_de32lsb(&dr[0x0A]);
         uint8_t fb[2048 + 1];
         char *bootpos;

         memset(fb, 0, sizeof(fb));
         fp->seek(file_lba * 2048, SEEK_SET);
         fp->read(fb, 2048);

         bootpos = strstr((char*)fb, "BOOT") + 4;
         while(*bootpos == ' ' || *bootpos == '\t') bootpos++;
         if(*bootpos == '=')
         {
            bootpos++;
            while(*bootpos == ' ' || *bootpos == '\t') bootpos++;
            if(!strncasecmp(bootpos, "cdrom:\\", 7))
            { 
               bootpos += 7;
               char *tmp;

               if((tmp = strchr(bootpos, '_'))) *tmp = 0;
               if((tmp = strchr(bootpos, '.'))) *tmp = 0;
               if((tmp = strchr(bootpos, ';'))) *tmp = 0;
               //puts(bootpos);

               if(strlen(bootpos) == 4 && bootpos[0] == 'S' && (bootpos[1] == 'C' || bootpos[1] == 'L' || bootpos[1] == 'I'))
               {
                  switch(bootpos[2])
                  {
                     case 'E': if(rr)
                                  *rr = REGION_EU;
                               ret = "SCEE";
                               goto Breakout;

                     case 'U': if(rr)
                                  *rr = REGION_NA;
                               ret = "SCEA";
                               goto Breakout;

                     case 'K':	// Korea?
                     case 'B':
                     case 'P': if(rr)
                                  *rr = REGION_JP;
                               ret = "SCEI";
                               goto Breakout;
                  }
               }
            }
         }

         //puts((char*)fb);
         //puts("ASOFKOASDFKO");
      }
   }

Breakout:
   if(fp)
   {
      delete fp;
      fp = NULL;
   }

   return(ret);
}

static unsigned CalcDiscSCEx(void)
{
   const char *prev_valid_id = NULL;
   unsigned ret_region = MDFN_GetSettingI("psx.region_default");

   cdifs_scex_ids.clear();

   if(cdifs)
      for(unsigned i = 0; i < cdifs->size(); i++)
      {
         uint8_t buf[2048];
         uint8_t fbuf[2048 + 1];
         unsigned ipos, opos;
         const char *id = CalcDiscSCEx_BySYSTEMCNF((*cdifs)[i], (i == 0) ? &ret_region : NULL);

         memset(fbuf, 0, sizeof(fbuf));

         if(id == NULL && (*cdifs)[i]->ReadSector(buf, 4, 1) == 0x2)
         {
            for(ipos = 0, opos = 0; ipos < 0x48; ipos++)
            {
               if(buf[ipos] > 0x20 && buf[ipos] < 0x80)
               {
                  fbuf[opos++] = tolower(buf[ipos]);
               }
            }

            fbuf[opos++] = 0;

            PSX_DBG(PSX_DBG_SPARSE, "License string: %s", (char *)fbuf);

            if(strstr((char *)fbuf, "licensedby") != NULL)
            {
               if(strstr((char *)fbuf, "america") != NULL)
               {
                  id = "SCEA";
                  if(!i)
                     ret_region = REGION_NA;
               }
               else if(strstr((char *)fbuf, "europe") != NULL)
               {
                  id = "SCEE";
                  if(!i)
                     ret_region = REGION_EU;
               }
               else if(strstr((char *)fbuf, "japan") != NULL)
               {
                  id = "SCEI";	// ?
                  if(!i)
                     ret_region = REGION_JP;
               }
               else if(strstr((char *)fbuf, "sonycomputerentertainmentinc.") != NULL)
               {
                  id = "SCEI";
                  if(!i)
                     ret_region = REGION_JP;
               }
               else	// Failure case
               {
                  if(prev_valid_id != NULL)
                     id = prev_valid_id;
                  else
                  {
                     switch(ret_region)	// Less than correct, but meh, what can we do.
                     {
                        case REGION_JP:
                           id = "SCEI";
                           break;

                        case REGION_NA:
                           id = "SCEA";
                           break;

                        case REGION_EU:
                           id = "SCEE";
                           break;
                     }
                  }
               }
            }
         }

         if(id != NULL)
            prev_valid_id = id;

         cdifs_scex_ids.push_back(id);
      }

   return ret_region;
}

static void InitCommon(std::vector<CDIF *> *CDInterfaces, const bool EmulateMemcards = true, const bool WantPIOMem = false)
{
   unsigned region, i;
   bool emulate_memcard[8];
   bool emulate_multitap[2];
   int sls, sle;

#if PSX_DBGPRINT_ENABLE
   psx_dbg_level = MDFN_GetSettingUI("psx.dbg_level");
#endif

   for(i = 0; i < 8; i++)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "psx.input.port%u.memcard", i + 1);
      emulate_memcard[i] = EmulateMemcards && MDFN_GetSettingB(buf);
   }

   for(i = 0; i < 2; i++)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "psx.input.pport%u.multitap", i + 1);
      emulate_multitap[i] = MDFN_GetSettingB(buf);
   }


   cdifs = CDInterfaces;
   region = CalcDiscSCEx();

   if(!MDFN_GetSettingB("psx.region_autodetect"))
      region = MDFN_GetSettingI("psx.region_default");

   sls = MDFN_GetSettingI((region == REGION_EU) ? "psx.slstartp" : "psx.slstart");
   sle = MDFN_GetSettingI((region == REGION_EU) ? "psx.slendp" : "psx.slend");

   if(sls > sle)
   {
      int tmp = sls;
      sls = sle;
      sle = tmp;
   }

   CPU = new PS_CPU();
   SPU = new PS_SPU();
   GPU = new PS_GPU(region == REGION_EU, sls, sle);
   CDC = new PS_CDC();
   FIO = new FrontIO(emulate_memcard, emulate_multitap);
   FIO->SetAMCT(MDFN_GetSettingB("psx.input.analog_mode_ct"));
   for(unsigned i = 0; i < 8; i++)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "psx.input.port%u.gun_chairs", i + 1);
      FIO->SetCrosshairsColor(i, MDFN_GetSettingUI(buf));
   }

   DMA_Init();

   GPU->FillVideoParams(&EmulatedPSX);

   CD_TrayOpen        = true;
   CD_SelectedDisc    = -1;

   if(cdifs)
   {
      CD_TrayOpen     = false;
      CD_SelectedDisc = 0;
   }

   CDC->SetDisc(true, NULL, NULL);
   CDC->SetDisc(CD_TrayOpen, (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? (*cdifs)[CD_SelectedDisc] : NULL,
         (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? cdifs_scex_ids[CD_SelectedDisc] : NULL);


   BIOSROM = new MultiAccessSizeMem<512 * 1024, uint32, false>();
   PIOMem  = NULL;

   if(WantPIOMem)
      PIOMem = new MultiAccessSizeMem<65536, uint32, false>();

   for(uint32_t ma = 0x00000000; ma < 0x00800000; ma += 2048 * 1024)
   {
      CPU->SetFastMap(MainRAM.data32, 0x00000000 + ma, 2048 * 1024);
      CPU->SetFastMap(MainRAM.data32, 0x80000000 + ma, 2048 * 1024);
      CPU->SetFastMap(MainRAM.data32, 0xA0000000 + ma, 2048 * 1024);
   }

   CPU->SetFastMap(BIOSROM->data32, 0x1FC00000, 512 * 1024);
   CPU->SetFastMap(BIOSROM->data32, 0x9FC00000, 512 * 1024);
   CPU->SetFastMap(BIOSROM->data32, 0xBFC00000, 512 * 1024);

   if(PIOMem)
   {
      CPU->SetFastMap(PIOMem->data32, 0x1F000000, 65536);
      CPU->SetFastMap(PIOMem->data32, 0x9F000000, 65536);
      CPU->SetFastMap(PIOMem->data32, 0xBF000000, 65536);
   }


   MDFNMP_Init(1024, ((uint64)1 << 29) / 1024);
   MDFNMP_AddRAM(2048 * 1024, 0x00000000, MainRAM.data8);
#if 0
   MDFNMP_AddRAM(1024, 0x1F800000, ScratchRAM.data8);
#endif

   const char *biospath_sname;

   if(region == REGION_JP)
      biospath_sname = "psx.bios_jp";
   else if(region == REGION_EU)
      biospath_sname = "psx.bios_eu";
   else if(region == REGION_NA)
      biospath_sname = "psx.bios_na";
   else
      abort();

   {
      std::string biospath = MDFN_MakeFName(MDFNMKF_FIRMWARE, 0, MDFN_GetSettingS(biospath_sname).c_str());
      FileStream BIOSFile(biospath.c_str(), MODE_READ);

      BIOSFile.read(BIOSROM->data8, 512 * 1024);
   }

   i = 0;

   if (!use_mednafen_memcard0_method)
   {
      FIO->LoadMemcard(0);
      i = 1;
   }

   for(; i < 8; i++)
   {
      char ext[64];
      snprintf(ext, sizeof(ext), "%d.mcr", i);
      FIO->LoadMemcard(i, MDFN_MakeFName(MDFNMKF_SAV, 0, ext).c_str());
   }

   for(i = 0; i < 8; i++)
   {
      Memcard_PrevDC[i] = FIO->GetMemcardDirtyCount(i);
      Memcard_SaveDelay[i] = -1;
   }


#ifdef WANT_DEBUGGER
   DBG_Init();
#endif
   PSX_Power();
}

static void LoadEXE(const uint8_t *data, const uint32_t size, bool ignore_pcsp = false)
{
   uint32 PC        = MDFN_de32lsb(&data[0x10]);
   uint32 SP        = MDFN_de32lsb(&data[0x30]);
   uint32 TextStart = MDFN_de32lsb(&data[0x18]);
   uint32 TextSize  = MDFN_de32lsb(&data[0x1C]);

   if(ignore_pcsp)
      log_cb(RETRO_LOG_INFO, "TextStart=0x%08x\nTextSize=0x%08x\n", TextStart, TextSize);
   else
      log_cb(RETRO_LOG_INFO, "PC=0x%08x\nSP=0x%08x\nTextStart=0x%08x\nTextSize=0x%08x\n", PC, SP, TextStart, TextSize);

   TextStart &= 0x1FFFFF;

   if(TextSize > 2048 * 1024)
      throw(MDFN_Error(0, "Text section too large"));

   if(TextSize > (size - 0x800))
      throw(MDFN_Error(0, "Text section recorded size is larger than data available in file.  Header=0x%08x, Available=0x%08x", TextSize, size - 0x800));

   if(TextSize < (size - 0x800))
      throw(MDFN_Error(0, "Text section recorded size is smaller than data available in file.  Header=0x%08x, Available=0x%08x", TextSize, size - 0x800));

   if(!TextMem.size())
   {
      TextMem_Start = TextStart;
      TextMem.resize(TextSize);
   }

   if(TextStart < TextMem_Start)
   {
      uint32 old_size = TextMem.size();

      //printf("RESIZE: 0x%08x\n", TextMem_Start - TextStart);

      TextMem.resize(old_size + TextMem_Start - TextStart);
      memmove(&TextMem[TextMem_Start - TextStart], &TextMem[0], old_size);

      TextMem_Start = TextStart;
   }

   if(TextMem.size() < (TextStart - TextMem_Start + TextSize))
      TextMem.resize(TextStart - TextMem_Start + TextSize);

   memcpy(&TextMem[TextStart - TextMem_Start], data + 0x800, TextSize);

   // BIOS patch
   BIOSROM->WriteU32(0x6990, (3 << 26) | ((0xBF001000 >> 2) & ((1 << 26) - 1)));
#if 0
   BIOSROM->WriteU32(0x691C, (3 << 26) | ((0xBF001000 >> 2) & ((1 << 26) - 1)));
#endif

   uint8 *po;

   po = &PIOMem->data8[0x0800];

   MDFN_en32lsb(po, (0x0 << 26) | (31 << 21) | (0x8 << 0));	// JR
   po += 4;
   MDFN_en32lsb(po, 0);	// NOP(kinda)
   po += 4;

   po = &PIOMem->data8[0x1000];

   // Load cacheable-region target PC into r2
   MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16) | (0x9F001010 >> 16));      // LUI
   po += 4;
   MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (2 << 16) | (0x9F001010 & 0xFFFF));   // ORI
   po += 4;

   // Jump to r2
   MDFN_en32lsb(po, (0x0 << 26) | (2 << 21) | (0x8 << 0));	// JR
   po += 4;
   MDFN_en32lsb(po, 0);	// NOP(kinda)
   po += 4;

   //
   // 0x9F001010:
   //

   // Load source address into r8
   uint32 sa = 0x9F000000 + 65536;
   MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16) | (sa >> 16));	// LUI
   po += 4;
   MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (8 << 16) | (sa & 0xFFFF)); 	// ORI
   po += 4;

   // Load dest address into r9
   MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (TextMem_Start >> 16));	// LUI
   po += 4;
   MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (9 << 16) | (TextMem_Start & 0xFFFF)); 	// ORI
   po += 4;

   // Load size into r10
   MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (TextMem.size() >> 16));	// LUI
   po += 4;
   MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (10 << 16) | (TextMem.size() & 0xFFFF)); 	// ORI
   po += 4;

   //
   // Loop begin
   //

   MDFN_en32lsb(po, (0x24 << 26) | (8 << 21) | (1 << 16));	// LBU to r1
   po += 4;

   MDFN_en32lsb(po, (0x08 << 26) | (10 << 21) | (10 << 16) | 0xFFFF);	// Decrement size
   po += 4;

   MDFN_en32lsb(po, (0x28 << 26) | (9 << 21) | (1 << 16));	// SB from r1
   po += 4;

   MDFN_en32lsb(po, (0x08 << 26) | (8 << 21) | (8 << 16) | 0x0001);	// Increment source addr
   po += 4;

   MDFN_en32lsb(po, (0x05 << 26) | (0 << 21) | (10 << 16) | (-5 & 0xFFFF));
   po += 4;
   MDFN_en32lsb(po, (0x08 << 26) | (9 << 21) | (9 << 16) | 0x0001);	// Increment dest addr
   po += 4;

   //
   // Loop end
   //

   // Load SP into r29
   if(ignore_pcsp)
   {
      po += 16;
   }
   else
   {
      MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (SP >> 16));	// LUI
      po += 4;
      MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (29 << 16) | (SP & 0xFFFF)); 	// ORI
      po += 4;

      // Load PC into r2
      MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | ((PC >> 16) | 0x8000));      // LUI
      po += 4;
      MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (2 << 16) | (PC & 0xFFFF));   // ORI
      po += 4;
   }

   // Half-assed instruction cache flush. ;)
   for(unsigned i = 0; i < 1024; i++)
   {
      MDFN_en32lsb(po, 0);
      po += 4;
   }



   // Jump to r2
   MDFN_en32lsb(po, (0x0 << 26) | (2 << 21) | (0x8 << 0));	// JR
   po += 4;
   MDFN_en32lsb(po, 0);	// NOP(kinda)
   po += 4;
}

static void Cleanup(void);
static int Load(const char *name, MDFNFILE *fp)
{
   const bool IsPSF = false;

   if(!TestMagic(name, fp))
      throw MDFN_Error(0, _("File format is unknown to module \"%s\"."), MDFNGameInfo->shortname);

   InitCommon(NULL, !IsPSF, true);

   TextMem.resize(0);

   if(GET_FSIZE_PTR(fp) >= 0x800)
      LoadEXE(GET_FDATA_PTR(fp), GET_FSIZE_PTR(fp));

   return(1);
}

static int LoadCD(std::vector<CDIF *> *CDInterfaces)
{
   InitCommon(CDInterfaces);
   MDFNGameInfo->GameType = GMT_CDROM;

   return(1);
}

static void Cleanup(void)
{
   TextMem.resize(0);


   if(CDC)
      delete CDC;
   CDC = NULL;

   if(SPU)
      delete SPU;
   SPU = NULL;

   if(GPU)
      delete GPU;
   GPU = NULL;

   if(CPU)
      delete CPU;
   CPU = NULL;

   if(FIO)
      delete FIO;
   FIO = NULL;

   DMA_Kill();

   if(BIOSROM)
      delete BIOSROM;
   BIOSROM = NULL;

   if(PIOMem)
      delete PIOMem;
   PIOMem = NULL;

   cdifs = NULL;
}

static void CloseGame(void)
{
   int i;
   for(i = 0; i < 8; i++)
   {
      if (i == 0 && !use_mednafen_memcard0_method)
      {
         FIO->SaveMemcard(i);
         continue;
      }

      // If there's an error saving one memcard, don't skip trying to save the other, since it might succeed and
      // we can reduce potential data loss!
      try
      {
         char ext[64];
         snprintf(ext, sizeof(ext), "%d.mcr", i);

         FIO->SaveMemcard(i, MDFN_MakeFName(MDFNMKF_SAV, 0, ext).c_str());
      }
      catch(std::exception &e)
      {
         log_cb(RETRO_LOG_ERROR, "%s\n", e.what());
      }
   }

   Cleanup();
}

static void SetInput(int port, const char *type, void *ptr)
{
   FIO->SetInput(port, type, ptr);
}

static int StateAction(StateMem *sm, int load, int data_only)
{
#if 0
   if(!MDFN_GetSettingB("psx.clobbers_lament"))
   {
      return(0);
   }
#endif

   SFORMAT StateRegs[] =
   {
      SFVAR(CD_TrayOpen),
      SFVAR(CD_SelectedDisc),
      SFARRAY(MainRAM.data8, 1024 * 2048),
      SFARRAY32(SysControl.Regs, 9),
      SFVAR(PSX_PRNG.lcgo),
      SFVAR(PSX_PRNG.x),
      SFVAR(PSX_PRNG.y),
      SFVAR(PSX_PRNG.z),
      SFVAR(PSX_PRNG.c),
      SFEND
   };


   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "MAIN");

   // Call SetDisc() BEFORE we load CDC state, since SetDisc() has emulation side effects.  We might want to clean this up in the future.
   if(load)
   {
      if(!cdifs || CD_SelectedDisc >= (int)cdifs->size())
         CD_SelectedDisc = -1;

      CDC->SetDisc(CD_TrayOpen, (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? (*cdifs)[CD_SelectedDisc] : NULL,
            (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? cdifs_scex_ids[CD_SelectedDisc] : NULL);
   }

   // TODO: Remember to increment dirty count in memory card state loading routine.

   ret &= CPU->StateAction(sm, load, data_only);
   ret &= DMA_StateAction(sm, load, data_only);
   ret &= TIMER_StateAction(sm, load, data_only);
   ret &= SIO_StateAction(sm, load, data_only);

   ret &= CDC->StateAction(sm, load, data_only);
   ret &= MDEC_StateAction(sm, load, data_only);
   ret &= GPU->StateAction(sm, load, data_only);
   ret &= SPU->StateAction(sm, load, data_only);

   ret &= FIO->StateAction(sm, load, data_only);
   
   ret &= IRQ_StateAction(sm, load, data_only);	// Do it last.

   if(load)
   {
      ForceEventUpdates(0); // FIXME to work with debugger step mode.
   }

   return(ret);
}

static void CDInsertEject(void)
{
   CD_TrayOpen = !CD_TrayOpen;

   for(unsigned disc = 0; disc < cdifs->size(); disc++)
   {
      if(!(*cdifs)[disc]->Eject(CD_TrayOpen))
      {
         MDFN_DispMessage(_("Eject error."));
         CD_TrayOpen = !CD_TrayOpen;
      }
   }

   if(CD_TrayOpen)
      MDFN_DispMessage(_("Virtual CD Drive Tray Open"));
   else
      MDFN_DispMessage(_("Virtual CD Drive Tray Closed"));

   CDC->SetDisc(CD_TrayOpen, (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? (*cdifs)[CD_SelectedDisc] : NULL,
         (CD_SelectedDisc >= 0 && !CD_TrayOpen) ? cdifs_scex_ids[CD_SelectedDisc] : NULL);
}

static void CDEject(void)
{
   if(!CD_TrayOpen)
      CDInsertEject();
}

static void CDSelect(void)
{
 if(cdifs && CD_TrayOpen)
 {
  CD_SelectedDisc = (CD_SelectedDisc + 1) % (cdifs->size() + 1);

  if((unsigned)CD_SelectedDisc == cdifs->size())
   CD_SelectedDisc = -1;

  if(CD_SelectedDisc == -1)
   MDFN_DispMessage(_("Disc absence selected."));
  else
   MDFN_DispMessage(_("Disc %d of %d selected."), CD_SelectedDisc + 1, (int)cdifs->size());
 }
}

static void DoSimpleCommand(int cmd)
{
   switch(cmd)
   {
      case MDFN_MSC_RESET:
         PSX_Power();
         break;
      case MDFN_MSC_POWER:
         PSX_Power();
         break;
      case MDFN_MSC_INSERT_DISK:
         CDInsertEject();
         break;
      case MDFN_MSC_SELECT_DISK:
         CDSelect();
         break;
      case MDFN_MSC_EJECT_DISK:
         CDEject();
         break;
   }
}

static void GSCondCode(MemoryPatch* patch, const char* cc, const unsigned len, const uint32 addr, const uint16 val)
{
 char tmp[256];

 if(patch->conditions.size() > 0)
  patch->conditions.append(", ");

 if(len == 2)
  trio_snprintf(tmp, 256, "%u L 0x%08x %s 0x%04x", len, addr, cc, val & 0xFFFFU);
 else
  trio_snprintf(tmp, 256, "%u L 0x%08x %s 0x%02x", len, addr, cc, val & 0xFFU);

 patch->conditions.append(tmp);
}

static bool DecodeGS(const std::string& cheat_string, MemoryPatch* patch)
{
 uint64 code = 0;
 unsigned nybble_count = 0;

 for(unsigned i = 0; i < cheat_string.size(); i++)
 {
  if(cheat_string[i] == ' ' || cheat_string[i] == '-' || cheat_string[i] == ':')
   continue;

  nybble_count++;
  code <<= 4;

  if(cheat_string[i] >= '0' && cheat_string[i] <= '9')
   code |= cheat_string[i] - '0';
  else if(cheat_string[i] >= 'a' && cheat_string[i] <= 'f')
   code |= cheat_string[i] - 'a' + 0xA;
  else if(cheat_string[i] >= 'A' && cheat_string[i] <= 'F')
   code |= cheat_string[i] - 'A' + 0xA;  
  else
  {
   if(cheat_string[i] & 0x80)
      log_cb(RETRO_LOG_ERROR, "[Mednafen]: Invalid character in GameShark code..\n");
   else
      log_cb(RETRO_LOG_ERROR, "[Mednafen]: Invalid character in GameShark code: %c.\n", cheat_string[i]);
   return false;
  }
 }

 if(nybble_count != 12)
 {
    log_cb(RETRO_LOG_ERROR, "GameShark code is of an incorrect length.\n");
    return false;
 }

 const uint8 code_type = code >> 40;
 const uint64 cl = code & 0xFFFFFFFFFFULL;

 patch->bigendian = false;
 patch->compare = 0;

 if(patch->type == 'T')
 {
  if(code_type != 0x80)
     log_cb(RETRO_LOG_ERROR, "Unrecognized GameShark code type for second part to copy bytes code.\n");

  patch->addr = cl >> 16;
  return(false);
 }

 switch(code_type)
 {
  default:
     log_cb(RETRO_LOG_ERROR, "GameShark code type 0x%02X is currently not supported.\n", code_type);

	return(false);

  // TODO:
  case 0x10:	// 16-bit increment
	patch->length = 2;
	patch->type = 'A';
	patch->addr = cl >> 16;
	patch->val = cl & 0xFFFF;
	return(false);

  case 0x11:	// 16-bit decrement
	patch->length = 2;
	patch->type = 'A';
	patch->addr = cl >> 16;
	patch->val = (0 - cl) & 0xFFFF;
	return(false);

  case 0x20:	// 8-bit increment
	patch->length = 1;
	patch->type = 'A';
	patch->addr = cl >> 16;
	patch->val = cl & 0xFF;
	return(false);

  case 0x21:	// 8-bit decrement
	patch->length = 1;
	patch->type = 'A';
	patch->addr = cl >> 16;
	patch->val = (0 - cl) & 0xFF;
	return(false);
  //
  //
  //

  case 0x30:	// 8-bit constant
	patch->length = 1;
	patch->type = 'R';
	patch->addr = cl >> 16;
	patch->val = cl & 0xFF;
	return(false);

  case 0x80:	// 16-bit constant
	patch->length = 2;
	patch->type = 'R';
	patch->addr = cl >> 16;
	patch->val = cl & 0xFFFF;
	return(false);

  case 0x50:	// Repeat thingy
	{
	 const uint8 wcount = (cl >> 24) & 0xFF;
	 const uint8 addr_inc = (cl >> 16) & 0xFF;
	 const uint8 val_inc = (cl >> 0) & 0xFF;

	 patch->mltpl_count = wcount;
	 patch->mltpl_addr_inc = addr_inc;
	 patch->mltpl_val_inc = val_inc;
	}
	return(true);

  case 0xC2:	// Copy
	{
	 const uint16 ccount = cl & 0xFFFF;

	 patch->type = 'T';
	 patch->val = 0;
	 patch->length = 1;

	 patch->copy_src_addr = cl >> 16;
	 patch->copy_src_addr_inc = 1;

	 patch->mltpl_count = ccount;
	 patch->mltpl_addr_inc = 1;
	 patch->mltpl_val_inc = 0;
	}
	return(true);

  case 0xD0:	// 16-bit == condition
	GSCondCode(patch, "==", 2, cl >> 16, cl);
	return(true);

  case 0xD1:	// 16-bit != condition
	GSCondCode(patch, "!=", 2, cl >> 16, cl);
	return(true);

  case 0xD2:	// 16-bit < condition
	GSCondCode(patch, "<", 2, cl >> 16, cl);
	return(true);

  case 0xD3:	// 16-bit > condition
	GSCondCode(patch, ">", 2, cl >> 16, cl);
	return(true);



  case 0xE0:	// 8-bit == condition
	GSCondCode(patch, "==", 1, cl >> 16, cl);
	return(true);

  case 0xE1:	// 8-bit != condition
	GSCondCode(patch, "!=", 1, cl >> 16, cl);
	return(true);

  case 0xE2:	// 8-bit < condition
	GSCondCode(patch, "<", 1, cl >> 16, cl);
	return(true);

  case 0xE3:	// 8-bit > condition
	GSCondCode(patch, ">", 1, cl >> 16, cl);
	return(true);

 }
}

static CheatFormatStruct CheatFormats[] =
{
 { "GameShark", "Sharks with lamprey eels for eyes.", DecodeGS },
};

static CheatFormatInfoStruct CheatFormatInfo =
{
 1,
 CheatFormats
};

static const FileExtensionSpecStruct KnownExtensions[] =
{
 { ".psf", "PSF1 Rip" },
 { ".psx", "PS-X Executable" },
 { ".exe", "PS-X Executable" },
 { NULL, NULL }
};

static const MDFNSetting_EnumList Region_List[] =
{
 { "jp", REGION_JP, "Japan" },
 { "na", REGION_NA, "North America" },
 { "eu", REGION_EU, "Europe" },
 { NULL, 0 },
};

#if 0
static const MDFNSetting_EnumList MultiTap_List[] =
{
 { "0", 0, "Disabled" },
 { "1", 1, "Enabled" },
 { "auto", 0, "Automatically-enable multitap.", "NOT IMPLEMENTED YET(currently equivalent to 0") },
 { NULL, 0 },
};
#endif

static MDFNSetting PSXSettings[] =
{
 { "psx.input.mouse_sensitivity", MDFNSF_NOFLAGS, "Emulated mouse sensitivity.", NULL, MDFNST_FLOAT, "1.00", NULL, NULL },

 { "psx.input.analog_mode_ct", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Enable analog mode combo-button alternate toggle.", "When enabled, instead of the configured Analog mode toggle button for the emulated DualShock, use a combination of buttons to toggle it instead.  When Select, Start, and all four shoulder buttons are held down for about 1 second, the mode will toggle.", MDFNST_BOOL, "0", NULL, NULL },

 { "psx.input.pport1.multitap", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Enable multitap on PSX port 1.", "Makes 3 more virtual ports available.\n\nNOTE: Enabling multitap in games that don't fully support it may cause deleterious effects.", MDFNST_BOOL, "0", NULL, NULL }, //MDFNST_ENUM, "auto", NULL, NULL, NULL, NULL, MultiTap_List },
 { "psx.input.pport2.multitap", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Enable multitap on PSX port 2.", "Makes 3 more virtual ports available.\n\nNOTE: Enabling multitap in games that don't fully support it may cause deleterious effects.", MDFNST_BOOL, "0", NULL, NULL },

 { "psx.input.port1.memcard", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Emulate memcard on virtual port 1.", NULL, MDFNST_BOOL, "1", NULL, NULL, },
 { "psx.input.port2.memcard", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Emulate memcard on virtual port 2.", NULL, MDFNST_BOOL, "1", NULL, NULL, },
 { "psx.input.port3.memcard", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Emulate memcard on virtual port 3.", NULL, MDFNST_BOOL, "1", NULL, NULL, },
 { "psx.input.port4.memcard", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Emulate memcard on virtual port 4.", NULL, MDFNST_BOOL, "1", NULL, NULL, },
 { "psx.input.port5.memcard", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Emulate memcard on virtual port 5.", NULL, MDFNST_BOOL, "1", NULL, NULL, },
 { "psx.input.port6.memcard", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Emulate memcard on virtual port 6.", NULL, MDFNST_BOOL, "1", NULL, NULL, },
 { "psx.input.port7.memcard", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Emulate memcard on virtual port 7.", NULL, MDFNST_BOOL, "1", NULL, NULL, },
 { "psx.input.port8.memcard", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Emulate memcard on virtual port 8.", NULL, MDFNST_BOOL, "1", NULL, NULL, },


 { "psx.input.port1.gun_chairs", MDFNSF_NOFLAGS, "Crosshairs color for lightgun on virtual port 1.", "A value of 0x1000000 disables crosshair drawing.", MDFNST_UINT, "0xFF0000", "0x000000", "0x1000000" },
 { "psx.input.port2.gun_chairs", MDFNSF_NOFLAGS, "Crosshairs color for lightgun on virtual port 2.", "A value of 0x1000000 disables crosshair drawing.", MDFNST_UINT, "0x00FF00", "0x000000", "0x1000000" },
 { "psx.input.port3.gun_chairs", MDFNSF_NOFLAGS, "Crosshairs color for lightgun on virtual port 3.", "A value of 0x1000000 disables crosshair drawing.", MDFNST_UINT, "0xFF00FF", "0x000000", "0x1000000" },
 { "psx.input.port4.gun_chairs", MDFNSF_NOFLAGS, "Crosshairs color for lightgun on virtual port 4.", "A value of 0x1000000 disables crosshair drawing.", MDFNST_UINT, "0xFF8000", "0x000000", "0x1000000" },
 { "psx.input.port5.gun_chairs", MDFNSF_NOFLAGS, "Crosshairs color for lightgun on virtual port 5.", "A value of 0x1000000 disables crosshair drawing.", MDFNST_UINT, "0xFFFF00", "0x000000", "0x1000000" },
 { "psx.input.port6.gun_chairs", MDFNSF_NOFLAGS, "Crosshairs color for lightgun on virtual port 6.", "A value of 0x1000000 disables crosshair drawing.", MDFNST_UINT, "0x00FFFF", "0x000000", "0x1000000" },
 { "psx.input.port7.gun_chairs", MDFNSF_NOFLAGS, "Crosshairs color for lightgun on virtual port 7.", "A value of 0x1000000 disables crosshair drawing.", MDFNST_UINT, "0x0080FF", "0x000000", "0x1000000" },
 { "psx.input.port8.gun_chairs", MDFNSF_NOFLAGS, "Crosshairs color for lightgun on virtual port 8.", "A value of 0x1000000 disables crosshair drawing.", MDFNST_UINT, "0x8000FF", "0x000000", "0x1000000" },

 { "psx.region_autodetect", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Attempt to auto-detect region of game.", NULL, MDFNST_BOOL, "1" },
 { "psx.region_default", MDFNSF_EMU_STATE | MDFNSF_UNTRUSTED_SAFE, "Default region to use.", "Used if region autodetection fails or is disabled.", MDFNST_ENUM, "jp", NULL, NULL, NULL, NULL, Region_List },

 { "psx.bios_jp", MDFNSF_EMU_STATE, "Path to the Japan SCPH-5500 ROM BIOS", NULL, MDFNST_STRING, "scph5500.bin" },
 { "psx.bios_na", MDFNSF_EMU_STATE, "Path to the North America SCPH-5501 ROM BIOS", "SHA1 0555c6fae8906f3f09baf5988f00e55f88e9f30b", MDFNST_STRING, "scph5501.bin" },
 { "psx.bios_eu", MDFNSF_EMU_STATE, "Path to the Europe SCPH-5502 ROM BIOS", NULL, MDFNST_STRING, "scph5502.bin" },

 { "psx.spu.resamp_quality", MDFNSF_NOFLAGS, "SPU output resampler quality.",
	"0 is lowest quality and CPU usage, 10 is highest quality and CPU usage.  The resampler that this setting refers to is used for converting from 44.1KHz to the sampling rate of the host audio device Mednafen is using.  Changing Mednafen's output rate, via the \"sound.rate\" setting, to \"44100\" will bypass the resampler, which will decrease CPU usage by Mednafen, and can increase or decrease audio quality, depending on various operating system and hardware factors.", MDFNST_UINT, "5", "0", "10" },


 { "psx.slstart", MDFNSF_NOFLAGS, "First displayed scanline in NTSC mode.", NULL, MDFNST_INT, "0", "0", "239" },
 { "psx.slend", MDFNSF_NOFLAGS, "Last displayed scanline in NTSC mode.", NULL, MDFNST_INT, "239", "0", "239" },

 { "psx.slstartp", MDFNSF_NOFLAGS, "First displayed scanline in PAL mode.", NULL, MDFNST_INT, "0", "0", "287" },
 { "psx.slendp", MDFNSF_NOFLAGS, "Last displayed scanline in PAL mode.", NULL, MDFNST_INT, "287", "0", "287" },

#if PSX_DBGPRINT_ENABLE
 { "psx.dbg_level", MDFNSF_NOFLAGS, "Debug printf verbosity level.", NULL, MDFNST_UINT, "0", "0", "4" },
#endif

 { "psx.clobbers_lament", MDFNSF_NOFLAGS, "Enable experimental save state functionality.", "Save states will destroy your saved game/memory card data if you're careless, and that will make clobber sad.  Poor clobber.", MDFNST_BOOL, "0" },

 { NULL },
};

// Note for the future: If we ever support PSX emulation with non-8-bit RGB color components, or add a new linear RGB colorspace to MDFN_PixelFormat, we'll need
// to buffer the intermediate 24-bit non-linear RGB calculation into an array and pass that into the GPULineHook stuff, otherwise netplay could break when
// an emulated GunCon is used.  This IS assuming, of course, that we ever implement save state support so that netplay actually works at all...
MDFNGI EmulatedPSX =
{
 "psx",
 "Sony PlayStation",
 KnownExtensions,
 MODPRIO_INTERNAL_HIGH,
 #ifdef WANT_DEBUGGER
 &PSX_DBGInfo,
 #else
 NULL,
 #endif
 &FIO_InputInfo,
 Load,
 TestMagic,
 LoadCD,
 TestMagicCD,
 CloseGame,
 NULL,	//ToggleLayer,
 "GPU\0",	//"Background Scroll\0Foreground Scroll\0Sprites\0",
 NULL,
 NULL,
 NULL,
 NULL,
 NULL,
 NULL,
 false,
 StateAction,
 NULL,
 SetInput,
 DoSimpleCommand,
 PSXSettings,
 MDFN_MASTERCLOCK_FIXED(33868800),
 0,

 true, // Multires possible?

 //
 // Note: Following video settings will be overwritten during game load.
 //
 0,	// lcm_width
 0,	// lcm_height
 NULL,  // Dummy

 320,   // Nominal width
 240,   // Nominal height

 0,   // Framebuffer width
 0,   // Framebuffer height
 //
 //
 //

 2,     // Number of output sound channels

};

/* end of Mednafen psx.cpp */

//forward decls
extern void Emulate(EmulateSpecStruct *espec);
extern void SetInput(int port, const char *type, void *ptr);


static bool overscan;
static double last_sound_rate;

static MDFN_Surface *surf;

static bool failed_init;

char *psx_analog_type;

#define RETRO_DEVICE_PS1PAD       RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_DUALANALOG   RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)
#define RETRO_DEVICE_DUALSHOCK    RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETRO_DEVICE_FLIGHTSTICK  RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)

std::string retro_base_directory;
std::string retro_base_name;
std::string retro_save_directory;

static void set_basename(const char *path)
{
   const char *base = strrchr(path, '/');
   if (!base)
      base = strrchr(path, '\\');

   if (base)
      retro_base_name = base + 1;
   else
      retro_base_name = path;

   retro_base_name = retro_base_name.substr(0, retro_base_name.find_last_of('.'));
}

#ifdef NEED_DEINTERLACER
static bool PrevInterlaced;
static Deinterlacer deint;
#endif

#define MEDNAFEN_CORE_NAME_MODULE "psx"
#define MEDNAFEN_CORE_NAME "Mednafen PSX"
#define MEDNAFEN_CORE_VERSION "v0.9.38.6"
#define MEDNAFEN_CORE_EXTENSIONS "cue|toc|ccd|m3u"
#define MEDNAFEN_CORE_GEOMETRY_BASE_W 320
#define MEDNAFEN_CORE_GEOMETRY_BASE_H 240
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 700
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 576
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)

static void check_system_specs(void)
{
   // Hints that we need a fairly powerful system to run this.
   unsigned level = 15;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

static unsigned disk_get_num_images(void)
{
   return cdifs ? cdifs->size() : 0;
}

static bool eject_state;
static bool disk_set_eject_state(bool ejected)
{
   log_cb(RETRO_LOG_INFO, "[Mednafen]: Ejected: %u.\n", ejected);
   if (ejected == eject_state)
      return false;

   DoSimpleCommand(ejected ? MDFN_MSC_EJECT_DISK : MDFN_MSC_INSERT_DISK);
   eject_state = ejected;
   return true;
}

static bool disk_get_eject_state(void)
{
   return eject_state;
}

static unsigned disk_get_image_index(void)
{
   // PSX global. Hacky.
   return CD_SelectedDisc;
}

static bool disk_set_image_index(unsigned index)
{
   CD_SelectedDisc = index;
   if (CD_SelectedDisc > disk_get_num_images())
      CD_SelectedDisc = disk_get_num_images();

   // Very hacky. CDSelect command will increment first.
   CD_SelectedDisc--;

   DoSimpleCommand(MDFN_MSC_SELECT_DISK);
   return true;
}

// Mednafen PSX really doesn't support adding disk images on the fly ...
// Hack around this.
static void update_md5_checksum(CDIF *iface)
{
   uint8 LayoutMD5[16];
   md5_context layout_md5;
   CD_TOC toc;

   md5_starts(&layout_md5);

   TOC_Clear(&toc);

   iface->ReadTOC(&toc);

   md5_update_u32_as_lsb(&layout_md5, toc.first_track);
   md5_update_u32_as_lsb(&layout_md5, toc.last_track);
   md5_update_u32_as_lsb(&layout_md5, toc.tracks[100].lba);

   for (uint32 track = toc.first_track; track <= toc.last_track; track++)
   {
      md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].lba);
      md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].control & 0x4);
   }

   md5_finish(&layout_md5, LayoutMD5);
   memcpy(MDFNGameInfo->MD5, LayoutMD5, 16);
   
   char *md5 = md5_asciistr(MDFNGameInfo->MD5);
   log_cb(RETRO_LOG_INFO, "[Mednafen]: Updated md5 checksum: %s.\n", md5);
}

// Untested ...
static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
   if (index >= disk_get_num_images())
      return false;

   if (!eject_state)
      return false;

   if (!info)
   {
      delete cdifs->at(index);
      cdifs->erase(cdifs->begin() + index);
      if (index < CD_SelectedDisc)
         CD_SelectedDisc--;
      
      // Poke into psx.cpp
      CalcDiscSCEx();
      return true;
   }

   try
   {
      CDIF *iface = CDIF_Open(info->path, false, false);
      delete cdifs->at(index);
      cdifs->at(index) = iface;
      CalcDiscSCEx();
      set_basename(info->path); // If we replace, we want the "swap disk manually effect".
      update_md5_checksum(iface); // Ugly, but needed to get proper disk swapping effect.
      return true;
   }
   catch (const std::exception &e)
   {
      return false;
   }
}

static bool disk_add_image_index(void)
{
   cdifs->push_back(NULL);
   return true;
}

static struct retro_disk_control_callback disk_interface = {
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
};

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
}

void retro_init(void)
{
   struct retro_log_callback log;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else 
      log_cb = fallback_log;

#ifdef NEED_CD
   CDUtility_Init();
#endif

   eject_state = false;

   const char *dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
   {
      retro_base_directory = dir;
      // Make sure that we don't have any lingering slashes, etc, as they break Windows.
      size_t last = retro_base_directory.find_last_not_of("/\\");
      if (last != std::string::npos)
         last++;

      retro_base_directory = retro_base_directory.substr(0, last);
   }
   else
   {
      /* TODO: Add proper fallback */
      log_cb(RETRO_LOG_WARN, "System directory is not defined. Fallback on using same dir as ROM for system directory later ...\n");
      failed_init = true;
   }

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
   {
      // If save directory is defined use it, otherwise use system directory
      retro_save_directory = *dir ? dir : retro_base_directory;
      // Make sure that we don't have any lingering slashes, etc, as they break Windows.
      size_t last = retro_save_directory.find_last_not_of("/\\");
      if (last != std::string::npos)
         last++;

      retro_save_directory = retro_save_directory.substr(0, last);      
   }
   else
   {
      /* TODO: Add proper fallback */
      log_cb(RETRO_LOG_WARN, "Save directory is not defined. Fallback on using SYSTEM directory ...\n");
      retro_save_directory = retro_base_directory;
   }      

   environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface);

   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      perf_get_cpu_features_cb = perf_cb.get_cpu_features;
   else
      perf_get_cpu_features_cb = NULL;

   setting_initial_scanline = 0;
   setting_last_scanline = 239;
   setting_initial_scanline_pal = 0;
   setting_last_scanline_pal = 287;

   check_system_specs();
}

void retro_reset(void)
{
   DoSimpleCommand(MDFN_MSC_RESET);
}

bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t)
{
   return false;
}

static bool old_cdimagecache = false;

// experimental save state support
static bool boot = true;

static bool experimental_savestates = false;
static bool experimental_savestates_toggle = false;

// shared memory cards support
static bool shared_memorycards = false;
static bool shared_memorycards_toggle = false;


const char *msgs[] = {"SHARED MEMCARDS: restart required",
                          "SHARED MEMCARDS DISABLED: memory card method mednafen required",
                          "SAVESTATES: restart required",
                          "SAVESTATES DISABLED: non-shared memcards required",
                          "SAVE STATES ENABLED: experimental, might result in loss of memory card data"};

static bool print_messages[] = {false,false,false,false,false};
static bool pending_messages = false;

static void check_variables(void)
{
   struct retro_variable var = {0};

   extern void PSXDitherApply(bool);

   var.key = "beetle_psx_cdimagecache";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      bool cdimage_cache = true;
      if (strcmp(var.value, "enabled") == 0)
         cdimage_cache = true;
      else if (strcmp(var.value, "disabled") == 0)
         cdimage_cache = false;
      if (cdimage_cache != old_cdimagecache)
      {
         old_cdimagecache = cdimage_cache;
      }
   }

   var.key = "beetle_psx_widescreen_hack";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         widescreen_hack = true;
      else if (strcmp(var.value, "disabled") == 0)
         widescreen_hack = false;
   }
   else
      widescreen_hack = false;

   var.key = "beetle_psx_widescreen_auto_ar";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0 && widescreen_hack)
         widescreen_auto_ar = true;
      else if (strcmp(var.value, "disabled") == 0)
         widescreen_auto_ar = false;
   }
   else
      widescreen_auto_ar = false;

   var.key = "beetle_psx_analog_toggle";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if ((strcmp(var.value, "enabled") == 0)
            && setting_psx_analog_toggle != 1)
      {
         setting_psx_analog_toggle = 1;
         setting_apply_analog_toggle = true;
      }
      else if ((strcmp(var.value, "disabled") == 0)
            && setting_psx_analog_toggle != 0)
      {
         setting_psx_analog_toggle = 0;
         setting_apply_analog_toggle = true;
      }
   }  
   
   var.key = "beetle_psx_enable_multitap_port1";
   
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         setting_psx_multitap_port_1 = true;
      else if (strcmp(var.value, "disabled") == 0)
         setting_psx_multitap_port_1 = false;
   }   

   var.key = "beetle_psx_enable_multitap_port2";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         setting_psx_multitap_port_2 = true;
      else if (strcmp(var.value, "disabled") == 0)
         setting_psx_multitap_port_2 = false;
   }

   var.key = "beetle_psx_initial_scanline";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      setting_initial_scanline = atoi(var.value);
   }

   var.key = "beetle_psx_last_scanline";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      setting_last_scanline = atoi(var.value);
   }

   var.key = "beetle_psx_initial_scanline_pal";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      setting_initial_scanline_pal = atoi(var.value);
   }

   var.key = "beetle_psx_last_scanline_pal";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      setting_last_scanline_pal = atoi(var.value);
   }
   
   if(setting_psx_multitap_port_1)
   {
      if(setting_psx_multitap_port_2)
         players = 8;
      else
         players = 4;
   }
   else
   {
      if(setting_psx_multitap_port_2)
         players = 4;
      else
         players = 2;      
   }
   
   var.key = "beetle_psx_use_mednafen_memcard0_method";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "libretro") == 0)
         use_mednafen_memcard0_method = false;
      else if (strcmp(var.value, "mednafen") == 0)
         use_mednafen_memcard0_method = true;
   }
   
   //this option depends on  beetle_psx_use_mednafen_memcard0_method being disabled so it should be evaluated that
   var.key = "beetle_psx_shared_memory_cards";   
   
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      
     if (strcmp(var.value, "enabled") == 0)
     {
         if(boot)
         {
            if(use_mednafen_memcard0_method)
               shared_memorycards_toggle = true;
            else
               print_messages[1] = true;
         }
         else
         {
            if(!shared_memorycards)
               print_messages[0] = true;
            if(use_mednafen_memcard0_method)
               shared_memorycards_toggle = true;
            else
               print_messages[1] = true;
         }
     }
     else if (strcmp(var.value, "disabled") == 0)
     {
         if(boot)
            shared_memorycards_toggle = false;
         else
         {
            if(shared_memorycards)
               print_messages[0] = true;
            shared_memorycards = false;
         }
     }
   }
   
   //this option depends on  beetle_psx_shared_memory_cards being disabled so it should be evaluated that
   var.key = "beetle_psx_experimental_save_states";
   
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
     if (strcmp(var.value, "enabled") == 0)
     {   
        if(boot)
        {
          if(!shared_memorycards_toggle)
          {
            print_messages[4] = true;
            experimental_savestates_toggle = true;
          }
          else
             print_messages[3] = true;
        }
        else
        {
          if(!shared_memorycards_toggle)
          {
            if(!experimental_savestates)
               print_messages[2] = true;
            else
               print_messages[4] = true;

            experimental_savestates_toggle = true;
          }
          else
          {
            if(!experimental_savestates)
              print_messages[2] = true;
            print_messages[3] = true;
          }
        }
      }
      else if (strcmp(var.value, "disabled") == 0)
      {
        if(experimental_savestates)
          print_messages[2] = true;
        experimental_savestates_toggle = false;
      }
   }
   
   if(print_messages[0] || print_messages[1] || print_messages[2] || print_messages[3] || print_messages[4])
      pending_messages = true;
   else
      pending_messages = false;
}

#ifdef NEED_CD
static void ReadM3U(std::vector<std::string> &file_list, std::string path, unsigned depth = 0)
{
   std::vector<std::string> ret;
   FileWrapper m3u_file(path.c_str(), MODE_READ, _("M3U CD Set"));
   std::string dir_path;
   char linebuf[2048];

   MDFN_GetFilePathComponents(path, &dir_path);

   while(m3u_file.get_line(linebuf, sizeof(linebuf)))
   {
      std::string efp;

      if(linebuf[0] == '#') continue;
      MDFN_rtrim(linebuf);
      if(linebuf[0] == 0) continue;

      efp = MDFN_EvalFIP(dir_path, std::string(linebuf));

      if(efp.size() >= 4 && efp.substr(efp.size() - 4) == ".m3u")
      {
         if(efp == path)
         {
            log_cb(RETRO_LOG_ERROR, "M3U at \"%s\" references self.\n", efp.c_str());
            return;
         }

         if(depth == 99)
         {
            log_cb(RETRO_LOG_ERROR, "M3U load recursion too deep!\n");
            return;
         }

         ReadM3U(file_list, efp, depth++);
      }
      else
         file_list.push_back(efp);
   }
}

#ifdef NEED_CD
static std::vector<CDIF *> CDInterfaces;	// FIXME: Cleanup on error out.
#endif
// TODO: LoadCommon()

MDFNGI *MDFNI_LoadCD(const char *force_module, const char *devicename)
{
 uint8 LayoutMD5[16];

 log_cb(RETRO_LOG_INFO, "Loading %s...\n", devicename ? devicename : "PHYSICAL CD");

 try
 {
  if(devicename && strlen(devicename) > 4 && !strcasecmp(devicename + strlen(devicename) - 4, ".m3u"))
  {
   std::vector<std::string> file_list;

   ReadM3U(file_list, devicename);

   for(unsigned i = 0; i < file_list.size(); i++)
   {
    CDInterfaces.push_back(CDIF_Open(file_list[i].c_str(), false, old_cdimagecache));
   }
  }
  else
  {
   CDInterfaces.push_back(CDIF_Open(devicename, false, old_cdimagecache));
  }
 }
 catch(std::exception &e)
 {
    log_cb(RETRO_LOG_ERROR, "Error opening CD.\n");
    return(0);
 }

 //
 // Print out a track list for all discs.  //
 for(unsigned i = 0; i < CDInterfaces.size(); i++)
 {
    TOC toc;
    TOC_Clear(&toc);

    CDInterfaces[i]->ReadTOC(&toc);

    log_cb(RETRO_LOG_DEBUG, "CD %d Layout:\n", i + 1);

    for(int32 track = toc.first_track; track <= toc.last_track; track++)
    {
       log_cb(RETRO_LOG_DEBUG, "Track %2d, LBA: %6d  %s\n", track, toc.tracks[track].lba, (toc.tracks[track].control & 0x4) ? "DATA" : "AUDIO");
    }

    log_cb(RETRO_LOG_DEBUG, "Leadout: %6d\n", toc.tracks[100].lba);
 }

 // Calculate layout MD5.  The system emulation LoadCD() code is free to ignore this value and calculate
 // its own, or to use it to look up a game in its database.
 {
  md5_context layout_md5;

  md5_starts(&layout_md5);

  for(unsigned i = 0; i < CDInterfaces.size(); i++)
  {
   CD_TOC toc;

   TOC_Clear(&toc);
   CDInterfaces[i]->ReadTOC(&toc);

   md5_update_u32_as_lsb(&layout_md5, toc.first_track);
   md5_update_u32_as_lsb(&layout_md5, toc.last_track);
   md5_update_u32_as_lsb(&layout_md5, toc.tracks[100].lba);

   for(uint32 track = toc.first_track; track <= toc.last_track; track++)
   {
    md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].lba);
    md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].control & 0x4);
   }
  }

  md5_finish(&layout_md5, LayoutMD5);
 }

 // This if statement will be true if force_module references a system without CDROM support.
 if(!MDFNGameInfo->LoadCD)
 {
    log_cb(RETRO_LOG_ERROR, "Specified system \"%s\" doesn't support CDs!", force_module);
    return 0;
 }

 // TODO: include module name in hash
 memcpy(MDFNGameInfo->MD5, LayoutMD5, 16);

 if(!(MDFNGameInfo->LoadCD(&CDInterfaces)))
 {
  for(unsigned i = 0; i < CDInterfaces.size(); i++)
   delete CDInterfaces[i];
  CDInterfaces.clear();

  MDFNGameInfo = NULL;
  return(0);
 }

 //MDFNI_SetLayerEnableMask(~0ULL);

 MDFN_LoadGameCheats(NULL);
 MDFNMP_InstallReadPatches();

 return(MDFNGameInfo);
}
#endif

static MDFNGI *MDFNI_LoadGame(const char *force_module, const char *name)
{
   MDFNFILE *GameFile = file_open(name);

   if(!GameFile)
      goto error;

#ifdef NEED_CD
	if(strlen(name) > 4 && (!strcasecmp(name + strlen(name) - 4, ".cue") || !strcasecmp(name + strlen(name) - 4, ".ccd") || !strcasecmp(name + strlen(name) - 4, ".toc") || !strcasecmp(name + strlen(name) - 4, ".m3u")))
	 return(MDFNI_LoadCD(force_module, name));
#endif

   if(MDFNGameInfo->Load(name, GameFile) <= 0)
      goto error;

   file_close(GameFile);
   GameFile   = NULL;

	if(!MDFNGameInfo->name)
   {
      unsigned int x;
      char *tmp;

      MDFNGameInfo->name = (UTF8 *)strdup(GetFNComponent(name));

      for(x=0;x<strlen((char *)MDFNGameInfo->name);x++)
      {
         if(MDFNGameInfo->name[x] == '_')
            MDFNGameInfo->name[x] = ' ';
      }
      if((tmp = strrchr((char *)MDFNGameInfo->name, '.')))
         *tmp = 0;
   }

   return(MDFNGameInfo);

error:
   if (GameFile)
      file_close(GameFile);
   GameFile     = NULL;
   MDFNGameInfo = NULL;
   return NULL;
}

#define MAX_PLAYERS 8
#define MAX_BUTTONS 16

union
{
   uint32_t u32[MAX_PLAYERS][1 + 8 + 1]; // Buttons + Axes + Rumble
   uint8_t u8[MAX_PLAYERS][(1 + 8 + 1) * sizeof(uint32_t)];
} static buf;

static uint16_t input_buf[MAX_PLAYERS] = {0};

bool retro_load_game(const struct retro_game_info *info)
{
   if (failed_init)
      return false;

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },


      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
      { 1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
      { 2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 2, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
      { 3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 3, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
      { 4, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 4, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
      { 5, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" }, 
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 5, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
      { 6, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" }, 
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 6, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Cross" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Circle" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Triangle" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Square" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,     "L1" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,    "L2" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,    "L3" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,     "R1" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,    "R2" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,    "R3" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT,    "Select" },
      { 7, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,    "Start" }, 
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X, "Left Analog X" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y, "Left Analog Y" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X, "Right Analog X" },
      { 7, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y, "Right Analog Y" },

      { 0 },
   };

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
      return false;

   overscan = false;
   environ_cb(RETRO_ENVIRONMENT_GET_OVERSCAN, &overscan);

   set_basename(info->path);

   check_variables();
   //make sure shared memory cards and save states are enabled only at startup
   shared_memorycards = shared_memorycards_toggle;
   experimental_savestates = experimental_savestates_toggle;

   
   if (environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble) && log_cb)
      log_cb(RETRO_LOG_INFO, "Rumble interface supported!\n");

   if (!MDFNI_LoadGame(MEDNAFEN_CORE_NAME_MODULE, info->path))
      return false;

	MDFN_LoadGameCheats(NULL);
	MDFNMP_InstallReadPatches();

   MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 16, 8, 0, 24);
   
   is_pal = (CalcDiscSCEx() == REGION_EU);
   surf = new MDFN_Surface(NULL, MEDNAFEN_CORE_GEOMETRY_MAX_W, is_pal ? MEDNAFEN_CORE_GEOMETRY_MAX_H  : 480, MEDNAFEN_CORE_GEOMETRY_MAX_W, pix_fmt);

#ifdef NEED_DEINTERLACER
	PrevInterlaced = false;
	deint.ClearState();
#endif

   //SetInput(0, "gamepad", &input_buf[0]);
   //SetInput(1, "gamepad", &input_buf[1]);
   
   for (unsigned i = 0; i < players; i++)
   {
       SetInput(i, "gamepad", &input_buf[i]);
   }
   boot = false;
   return true;
}

void retro_unload_game(void)
{
   if(!MDFNGameInfo)
      return;

   MDFN_FlushGameCheats(0);

   MDFNGameInfo->CloseGame();

   if(MDFNGameInfo->name)
      free(MDFNGameInfo->name);
   MDFNGameInfo->name = NULL;

   MDFNMP_Kill();

   MDFNGameInfo = NULL;

#ifdef NEED_CD
   for(unsigned i = 0; i < CDInterfaces.size(); i++)
      delete CDInterfaces[i];
   CDInterfaces.clear();
#endif
}


// Hardcoded for PSX. No reason to parse lots of structures ...
// See mednafen/psx/input/gamepad.cpp
static void update_input(void)
{
   //input_buf[0] = 0;
   //input_buf[1] = 0;
   
   for (unsigned j = 0; j < players; j++)
   {
       input_buf[j] = 0;
   }
   	
   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_L3,
      RETRO_DEVICE_ID_JOYPAD_R3,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_L2,
      RETRO_DEVICE_ID_JOYPAD_R2,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_X,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_Y,
   };

   for (unsigned j = 0; j < players; j++)
   {
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_buf[j] |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;
   }

   // Buttons.
   //buf.u8[0][0] = (input_buf[0] >> 0) & 0xff;
   //buf.u8[0][1] = (input_buf[0] >> 8) & 0xff;
   //buf.u8[1][0] = (input_buf[1] >> 0) & 0xff;
   //buf.u8[1][1] = (input_buf[1] >> 8) & 0xff;
   
   for (unsigned j = 0; j < players; j++)
   {
        buf.u8[j][0] = (input_buf[j] >> 0) & 0xff;
        buf.u8[j][1] = (input_buf[j] >> 8) & 0xff;
   }

   // Analogs
   for (unsigned j = 0; j < players; j++)
   {
      int analog_left_x = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
            RETRO_DEVICE_ID_ANALOG_X);

      int analog_left_y = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
            RETRO_DEVICE_ID_ANALOG_Y);

      int analog_right_x = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
            RETRO_DEVICE_ID_ANALOG_X);

      int analog_right_y = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
            RETRO_DEVICE_ID_ANALOG_Y);

      uint32_t r_right = analog_right_x > 0 ?  analog_right_x : 0;
      uint32_t r_left  = analog_right_x < 0 ? -analog_right_x : 0;
      uint32_t r_down  = analog_right_y > 0 ?  analog_right_y : 0;
      uint32_t r_up    = analog_right_y < 0 ? -analog_right_y : 0;

      uint32_t l_right = analog_left_x > 0 ?  analog_left_x : 0;
      uint32_t l_left  = analog_left_x < 0 ? -analog_left_x : 0;
      uint32_t l_down  = analog_left_y > 0 ?  analog_left_y : 0;
      uint32_t l_up    = analog_left_y < 0 ? -analog_left_y : 0;

      buf.u32[j][1] = r_right;
      buf.u32[j][2] = r_left;
      buf.u32[j][3] = r_down;
      buf.u32[j][4] = r_up;

      buf.u32[j][5] = l_right;
      buf.u32[j][6] = l_left;
      buf.u32[j][7] = l_down;
      buf.u32[j][8] = l_up;
   }

   //fprintf(stderr, "Rumble strong: %u, weak: %u.\n", buf.u8[0][9 * 4 + 1], buf.u8[0][9 * 4]);
   if (rumble.set_rumble_state)
   {
      // Appears to be correct.
      //rumble.set_rumble_state(0, RETRO_RUMBLE_WEAK, buf.u8[0][9 * 4] * 0x101);
      //rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, buf.u8[0][9 * 4 + 1] * 0x101);
      //rumble.set_rumble_state(1, RETRO_RUMBLE_WEAK, buf.u8[1][9 * 4] * 0x101);
      //rumble.set_rumble_state(1, RETRO_RUMBLE_STRONG, buf.u8[1][9 * 4 + 1] * 0x101);
      
      for (unsigned j = 0; j < players; j++)
      {
          rumble.set_rumble_state(j, RETRO_RUMBLE_WEAK, buf.u8[j][9 * 4] * 0x101);
          rumble.set_rumble_state(j, RETRO_RUMBLE_STRONG, buf.u8[j][9 * 4 + 1] * 0x101);
      }
   }
}

static uint64_t video_frames, audio_frames;
#define SOUND_CHANNELS 2

void retro_run(void)
{
   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
      widescreen_auto_ar_old = widescreen_auto_ar;
      check_variables();

      if (widescreen_auto_ar != widescreen_auto_ar_old)
      {
         struct retro_system_av_info new_av_info;
         retro_get_system_av_info(&new_av_info);
         environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &new_av_info);
      }
   }

   if(pending_messages)
   {
      if(video_frames%120 == 0 && video_frames > 0)
      {
         for(int i=0;i<5;i++)
         {
            if(print_messages[i])
            {
               MDFN_DispMessage(msgs[i]);
               print_messages[i] = false;
               break;
            }
            if(!print_messages[0] && !print_messages[1] && !print_messages[2] && !print_messages[3] && !print_messages[4])
               pending_messages = false;
         }
      }  
   }   
   
  
   if (setting_apply_analog_toggle)
   {
      FIO->SetAMCT(setting_psx_analog_toggle);
      setting_apply_analog_toggle = false;
   }

   input_poll_cb();

   update_input();

   static int32 rects[MEDNAFEN_CORE_GEOMETRY_MAX_H];
   rects[0] = ~0;

   EmulateSpecStruct spec = {0};
   spec.surface = surf;
   spec.SoundRate = 44100;
   spec.SoundBuf = NULL;
   spec.LineWidths = rects;
   spec.SoundBufMaxSize = 0;
   spec.SoundVolume = 1.0;
   spec.soundmultiplier = 1.0;
   spec.SoundBufSize = 0;
   spec.VideoFormatChanged = false;
   spec.SoundFormatChanged = false;

   EmulateSpecStruct *espec = (EmulateSpecStruct*)&spec;
   /* start of Emulate */
   int32_t timestamp = 0;

   espec->skip = false;	
   MDFNGameInfo->mouse_sensitivity = MDFN_GetSettingF("psx.input.mouse_sensitivity");

   MDFNMP_ApplyPeriodicCheats();


   espec->MasterCycles = 0;
   espec->SoundBufSize = 0;

   FIO->UpdateInput();
   GPU->StartFrame(espec);

   Running = -1;
   timestamp = CPU->Run(timestamp);

   assert(timestamp);

   ForceEventUpdates(timestamp);
   if(GPU->GetScanlineNum() < 100)
      PSX_DBG(PSX_DBG_ERROR, "[BUUUUUUUG] Frame timing end glitch; scanline=%u, st=%u\n", GPU->GetScanlineNum(), timestamp);

   //printf("scanline=%u, st=%u\n", GPU->GetScanlineNum(), timestamp);

   espec->SoundBufSize = IntermediateBufferPos;
   IntermediateBufferPos = 0;

   CDC->ResetTS();
   TIMER_ResetTS();
   DMA_ResetTS();
   GPU->ResetTS();
   FIO->ResetTS();

   RebaseTS(timestamp);

   espec->MasterCycles = timestamp;

   // Save memcards if dirty.
   for(int i = 0; i < players; i++)
   {
      uint64_t new_dc = FIO->GetMemcardDirtyCount(i);

      if(new_dc > Memcard_PrevDC[i])
      {
         Memcard_PrevDC[i] = new_dc;
         Memcard_SaveDelay[i] = 0;
      }

      if(Memcard_SaveDelay[i] >= 0)
      {
         Memcard_SaveDelay[i] += timestamp;
         if(Memcard_SaveDelay[i] >= (33868800 * 2))	// Wait until about 2 seconds of no new writes.
         {
            log_cb(RETRO_LOG_INFO, "Saving memcard %d...\n", i);

            if (i == 0 && !use_mednafen_memcard0_method)
            {
               FIO->SaveMemcard(i);
               Memcard_SaveDelay[i] = -1;
               Memcard_PrevDC[i] = 0;
               continue;
            }

            char ext[64];
            snprintf(ext, sizeof(ext), "%d.mcr", i);
            FIO->SaveMemcard(i, MDFN_MakeFName(MDFNMKF_SAV, 0, ext).c_str());
            Memcard_SaveDelay[i] = -1;
            Memcard_PrevDC[i] = 0;
         }
      }
   }

   /* end of Emulate */

#ifdef NEED_DEINTERLACER
   if (spec.InterlaceOn)
   {
      if (!PrevInterlaced)
         deint.ClearState();

      deint.Process(spec.surface, spec.DisplayRect, spec.LineWidths, spec.InterlaceField);

      PrevInterlaced = true;

      spec.InterlaceOn = false;
      spec.InterlaceField = 0;
   }
   else
      PrevInterlaced = false;
#endif

   int16_t *interbuf = (int16_t*)&IntermediateBuffer;

   // PSX is rather special, and needs specific handling ...
   
   unsigned width = rects[0]; // spec.DisplayRect.w is 0. Only rects[0].w seems to return something sane.
   unsigned height = spec.DisplayRect.h;
   //fprintf(stderr, "(%u x %u)\n", width, height);
   // PSX core inserts padding on left and right (overscan). Optionally crop this.

   const uint32_t *pix = surf->pixels;
   if (!overscan)
   {
      // 320 width -> 350 width.
      // 364 width -> 400 width.
      // 256 width -> 280 width.
      // 560 width -> 512 width.
      // 640 width -> 700 width.
      // Rectify this.
      switch (width)
      {
         // The shifts are not simply (padded_width - real_width) / 2.
         case 280:
            pix += 10;
            width = 256;
            break;

         case 350:
            pix += 14;
            width = 320;
            break;

         case 400:
            pix += 15;
            width = 364;
            break;


         case 560:
            pix += 26;
            width = 512;
            break;

         case 700:
            pix += 33;
            width = 640;
            break;

         default:
            // This shouldn't happen.
            break;
      }

      if (is_pal)
      {
         // Attempt to remove black bars.
         // These numbers are arbitrary since the bars differ some by game.
         // Changes aspect ratio in the process.
         height -= 36;
         pix += 5 * (MEDNAFEN_CORE_GEOMETRY_MAX_W << 2);
      }
   }
   video_cb(pix, width, height, MEDNAFEN_CORE_GEOMETRY_MAX_W << 2);

   video_frames++;
   audio_frames += spec.SoundBufSize;

   audio_batch_cb(interbuf, spec.SoundBufSize);
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = MEDNAFEN_CORE_NAME;
   info->library_version  = MEDNAFEN_CORE_VERSION;
   info->need_fullpath    = true;
   info->valid_extensions = MEDNAFEN_CORE_EXTENSIONS;
   info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = is_pal ? 49.842 : 59.941;
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W;
   info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H;
   info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W;
   info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H;
   info->geometry.aspect_ratio = !widescreen_auto_ar ? MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO : (float)16/9;
}

void retro_deinit(void)
{
   delete surf;
   surf = NULL;

   log_cb(RETRO_LOG_INFO, "[%s]: Samples / Frame: %.5f\n",
         MEDNAFEN_CORE_NAME, (double)audio_frames / video_frames);
   log_cb(RETRO_LOG_INFO, "[%s]: Estimated FPS: %.5f\n",
         MEDNAFEN_CORE_NAME, (double)video_frames * 44100 / audio_frames);
}

unsigned retro_get_region(void)
{
   if (is_pal)
      return RETRO_REGION_PAL;
   return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned in_port, unsigned device)
{
   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
      case RETRO_DEVICE_PS1PAD:
         log_cb(RETRO_LOG_INFO, "[%s]: Selected controller type standard gamepad.\n", MEDNAFEN_CORE_NAME);
         SetInput(in_port, "gamepad", &buf.u8[in_port]);    
         break;
      case RETRO_DEVICE_DUALANALOG:
         log_cb(RETRO_LOG_INFO, "[%s]: Selected controller type Dual Analog.\n", MEDNAFEN_CORE_NAME);
         SetInput(in_port, "dualanalog", &buf.u8[in_port]);    
         break;
      case RETRO_DEVICE_DUALSHOCK:
         log_cb(RETRO_LOG_INFO, "[%s]: Selected controller type DualShock.\n", MEDNAFEN_CORE_NAME);
         SetInput(in_port, "dualshock", &buf.u8[in_port]);    
         break;
      case RETRO_DEVICE_FLIGHTSTICK:
         log_cb(RETRO_LOG_INFO, "[%s]: Selected controller type FlightStick.\n", MEDNAFEN_CORE_NAME);
         SetInput(in_port, "analogjoy", &buf.u8[in_port]);
         break;
      default:
         log_cb(RETRO_LOG_WARN, "[%s]: Unsupported controller device %u, falling back to gamepad.\n", MEDNAFEN_CORE_NAME,device);
   }

   if (rumble.set_rumble_state)
   {
      rumble.set_rumble_state(in_port, RETRO_RUMBLE_STRONG, 0);
      rumble.set_rumble_state(in_port, RETRO_RUMBLE_WEAK, 0);
      buf.u32[in_port][9] = 0;
   }
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   static const struct retro_variable vars[] = {
      { "beetle_psx_cdimagecache", "CD Image Cache (restart); disabled|enabled" },
      { "beetle_psx_widescreen_hack", "Widescreen mode hack; disabled|enabled" },
      { "beetle_psx_widescreen_auto_ar", "Widescreen hack auto aspect ratio; disabled|enabled" },
      { "beetle_psx_use_mednafen_memcard0_method", "Memcard 0 method; libretro|mednafen" },
      { "beetle_psx_shared_memory_cards", "Shared memcards (restart); disabled|enabled" },
      { "beetle_psx_experimental_save_states", "Savestates (restart); disabled|enabled" },
      { "beetle_psx_initial_scanline", "Initial scanline; 0|1|2|3|4|5|6|7|8|9|10|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|33|34|35|36|37|38|39|40" },
      { "beetle_psx_initial_scanline_pal", "Initial scanline PAL; 0|1|2|3|4|5|6|7|8|9|10|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|33|34|35|36|37|38|39|40" },
      { "beetle_psx_last_scanline", "Last scanline; 239|238|237|236|235|234|232|231|230|229|228|227|226|225|224|223|222|221|220|219|218|217|216|215|214|213|212|211|210" },
      { "beetle_psx_last_scanline_pal", "Last scanline PAL; 287|286|285|284|283|283|282|281|280|279|278|277|276|275|274|273|272|271|270|269|268|267|266|265|264|263|262|261|260" },
      { "beetle_psx_analog_toggle", "Dualshock analog toggle; disabled|enabled" },
      { "beetle_psx_enable_multitap_port1", "Port 1: Multitap enable; disabled|enabled" },
      { "beetle_psx_enable_multitap_port2", "Port 2: Multitap enable; disabled|enabled" },
      { NULL, NULL },
   };
   static const struct retro_controller_description pads[] = {
      { "PS1 Joypad", RETRO_DEVICE_JOYPAD },
      { "DualAnalog", RETRO_DEVICE_DUALANALOG },
      { "DualShock", RETRO_DEVICE_DUALSHOCK },
      { "FlightStick", RETRO_DEVICE_FLIGHTSTICK },
   };

   static const struct retro_controller_info ports[] = {
      { pads, 4 },
      { pads, 4 },
      { pads, 4 },
      { pads, 4 },
      { pads, 4 },
      { pads, 4 },
      { pads, 4 },
      { pads, 4 },
      { 0 },
   };

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
   environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

static size_t serialize_size;

size_t retro_serialize_size(void)
{
   StateMem st;
   memset(&st, 0, sizeof(st));

   if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL) || !experimental_savestates)
   {
      return 0;
   }

   free(st.data);
   return serialize_size = st.len;
}

bool retro_serialize(void *data, size_t size)
{
   StateMem st;
   memset(&st, 0, sizeof(st));
   st.data     = (uint8_t*)data;
   st.malloced = size;

   return MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);
}

bool retro_unserialize(const void *data, size_t size)
{
   StateMem st;
   memset(&st, 0, sizeof(st));
   st.data = (uint8_t*)data;
   st.len  = size;
   
   return MDFNSS_LoadSM(&st, 0, 0);
}

void *retro_get_memory_data(unsigned type)
{
   uint8_t *data;

   switch (type)
   {
      case RETRO_MEMORY_SAVE_RAM:
         if (use_mednafen_memcard0_method)
            return NULL;
         return FIO->GetMemcardDevice(0)->GetNVData();
      default:
         break;
   }
   return NULL; 
}

size_t retro_get_memory_size(unsigned type)
{
   switch (type)
   {
      case RETRO_MEMORY_SAVE_RAM:
         if (use_mednafen_memcard0_method)
            return 0;
         return (1 << 17);
      default:
         break;
   }

   return 0;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned, bool, const char *)
{}

#ifdef _WIN32
static void sanitize_path(std::string &path)
{
   size_t size = path.size();
   for (size_t i = 0; i < size; i++)
      if (path[i] == '/')
         path[i] = '\\';
}
#endif

// Use a simpler approach to make sure that things go right for libretro.
std::string MDFN_MakeFName(MakeFName_Type type, int id1, const char *cd1)
{
   char slash;
#ifdef _WIN32
   slash = '\\';
#else
   slash = '/';
#endif
   std::string ret;
   switch (type)
   {
      case MDFNMKF_SAV:
         ret = retro_save_directory + slash + (!shared_memorycards ? retro_base_name : "mednafen_psx_libretro_shared") +
         std::string(".") +
#ifndef _XBOX
          (!shared_memorycards ? md5_asciistr(MDFNGameInfo->MD5) : "") + (!shared_memorycards ? std::string(".") : "") +
#endif
          std::string(cd1);         
         break;
      case MDFNMKF_FIRMWARE:
         ret = retro_base_directory + slash + std::string(cd1);
#ifdef _WIN32
   sanitize_path(ret); // Because Windows path handling is mongoloid.
#endif
         break;
      default:	  
         break;
   }

   return ret;
}

void MDFND_DispMessage(unsigned char *str)
{
   const char *strc = (const char*)str;
   struct retro_message msg =
   {
      strc,
      180
   };

   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

void MDFN_DispMessage(const char *format, ...)
{
   struct retro_message msg;
   va_list ap;
   va_start(ap,format);
   char *str = NULL;
   const char *strc = NULL;

   trio_vasprintf(&str, format,ap);
   va_end(ap);
   strc = str;

   msg.frames = 180;
   msg.msg = strc;

   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}
