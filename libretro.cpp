#include "mednafen/mednafen.h"
#include "mednafen/mempatcher.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include "mednafen/md5.h"
#include <compat/msvc.h>
#include "mednafen/psx/gpu.h"
#ifdef NEED_DEINTERLACER
#include "mednafen/video/Deinterlacer.h"
#endif
#include <libretro.h>
#include <rthreads/rthreads.h>
#include <streams/file_stream.h>
#include <string/stdstring.h>
#include <rhash.h>
#include "ugui_tools.h"
#include "rsx/rsx_intf.h"
#include "libretro_cbs.h"
#include "beetle_psx_globals.h"
#include "libretro_options.h"
#include "input.h"

#include "mednafen/mednafen-endian.h"
#include "mednafen/mednafen-types.h"
#include "mednafen/psx/psx.h"
#include "mednafen/error.h"

#include "pgxp/pgxp_main.h"

#include <vector>
#define ISHEXDEC ((codeLine[cursor]>='0') && (codeLine[cursor]<='9')) || ((codeLine[cursor]>='a') && (codeLine[cursor]<='f')) || ((codeLine[cursor]>='A') && (codeLine[cursor]<='F'))

#ifdef HAVE_LIGHTREC
#include <sys/mman.h>

#ifdef HAVE_ASHMEM
#include <sys/ioctl.h>
#include <linux/ashmem.h>
#endif

#if defined(HAVE_SHM) || defined(HAVE_ASHMEM)
#include <fcntl.h>
#endif

#ifdef HAVE_WIN_SHM
#include <windows.h>
#endif
#endif /* HAVE_LIGHTREC */

//Fast Save States exclude string labels from variables in the savestate, and are at least 20% faster.
extern bool FastSaveStates;
const int DEFAULT_STATE_SIZE = 16 * 1024 * 1024;

static bool libretro_supports_bitmasks = false;

struct retro_perf_callback perf_cb;
retro_get_cpu_features_t perf_get_cpu_features_cb = NULL;
retro_log_printf_t log_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static unsigned frame_count = 0;
static unsigned internal_frame_count = 0;
static bool display_internal_framerate = false;
static bool allow_frame_duping = false;
static bool failed_init = false;
static unsigned image_offset = 0;
static unsigned image_crop = 0;
static bool enable_memcard1 = false;
static bool enable_variable_serialization_size = false;
static int frame_width = 0;
static int frame_height = 0;
static bool gui_inited = false;
static bool gui_show = false;

unsigned cd_2x_speedup = 1;
bool cd_async = false;
bool cd_warned_slow = false;
int64 cd_slow_timeout = 8000; // microseconds

// If true, PAL games will run at 60fps
bool fast_pal = false;

#ifdef HAVE_LIGHTREC
enum DYNAREC psx_dynarec;
bool psx_dynarec_invalidate;
uint8 psx_mmap = 0;
uint8 *psx_mem = NULL;
uint8 *psx_bios = NULL;
uint8 *psx_scratch = NULL;
#if defined(HAVE_ASHMEM)
int memfd;
#endif
#endif

uint32 EventCycles = 128;

// CPU overclock factor (or 0 if disabled)
int32_t psx_overclock_factor = 0;
// GPU rasterizer overclock shift
unsigned psx_gpu_overclock_shift = 0;

// Sets how often (in number of output frames/retro_run invocations)
// the internal framerace counter should be updated if
// display_internal_framerate is true.
#define INTERNAL_FPS_SAMPLE_PERIOD 64

static int psx_skipbios;

bool psx_gte_overclock;
enum dither_mode psx_gpu_dither_mode;

//iCB: PGXP options
unsigned int psx_pgxp_mode;
unsigned int psx_pgxp_vertex_caching;
unsigned int psx_pgxp_texture_correction;
// \iCB

#define NEGCON_RANGE 0x7FFF

char retro_save_directory[4096];
char retro_base_directory[4096];
static char retro_cd_base_directory[4096];
static char retro_cd_path[4096];
char retro_cd_base_name[4096];
#ifdef _WIN32
   static char retro_slash = '\\';
#else
   static char retro_slash = '/';
#endif

enum
{
   REGION_JP = 0,
   REGION_NA = 1,
   REGION_EU = 2,
};

static bool firmware_is_present(unsigned region)
{
   char bios_path[4096];
   static const size_t list_size = 10;
   const char *bios_name_list[list_size];
   const char *bios_sha1 = NULL;

   log_cb(RETRO_LOG_INFO, "Checking if required firmware is present.\n");

   /* SHA1 and alternate BIOS names sourced from
   https://github.com/mamedev/mame/blob/master/src/mame/drivers/psx.cpp */
   if (region == REGION_JP)
   {
      bios_name_list[0] = "scph5500.bin";
      bios_name_list[1] = "SCPH5500.bin";
      bios_name_list[2] = "SCPH-5500.bin";
      bios_name_list[3] = NULL;
      bios_name_list[4] = NULL;
      bios_name_list[5] = NULL;
      bios_name_list[6] = NULL;
      bios_name_list[7] = NULL;
      bios_name_list[8] = NULL;
      bios_name_list[9] = NULL;
      bios_sha1 = "B05DEF971D8EC59F346F2D9AC21FB742E3EB6917";
   }
   else if (region == REGION_NA)
   {
      bios_name_list[0] = "scph5501.bin";
      bios_name_list[1] = "SCPH5501.bin";
      bios_name_list[2] = "SCPH-5501.bin";
      bios_name_list[3] = "scph5503.bin";
      bios_name_list[4] = "SCPH5503.bin";
      bios_name_list[5] = "SCPH-5503.bin";
      bios_name_list[6] = "scph7003.bin";
      bios_name_list[7] = "SCPH7003.bin";
      bios_name_list[8] = "SCPH-7003.bin";
      bios_name_list[9] = NULL;
      bios_sha1 = "0555C6FAE8906F3F09BAF5988F00E55F88E9F30B";
   }
   else if (region == REGION_EU)
   {
      bios_name_list[0] = "scph5502.bin";
      bios_name_list[1] = "SCPH5502.bin";
      bios_name_list[2] = "SCPH-5502.bin";
      bios_name_list[3] = "scph5552.bin";
      bios_name_list[4] = "SCPH5552.bin";
      bios_name_list[5] = "SCPH-5552.bin";
      bios_name_list[6] = NULL;
      bios_name_list[7] = NULL;
      bios_name_list[8] = NULL;
      bios_name_list[9] = NULL;
      bios_sha1 = "F6BC2D1F5EB6593DE7D089C425AC681D6FFFD3F0";
   }

   bool found = false;
   size_t i;
   for (i = 0; i < list_size; ++i)
   {
      if (!bios_name_list[i])
         break;

      int r = snprintf(bios_path, sizeof(bios_path), "%s%c%s", retro_base_directory, retro_slash, bios_name_list[i]);
      if (r >= 4096)
      {
         log_cb(RETRO_LOG_ERROR, "Firmware path longer than 4095: %s\n", bios_path);
         break;
      }

      if (filestream_exists(bios_path))
      {
         found = true;
         break;
      }
   }

   if (!found)
   {
      char s[4096];

      log_cb(RETRO_LOG_ERROR, "Firmware is missing: %s\n", bios_name_list[0]);
      s[4095] = '\0';

      snprintf(s, sizeof(s), "Firmware is missing:\n\n%s", bios_name_list[0]);

      gui_set_message(s);
      gui_show = true;

      return false;
   }

   char obtained_sha1[41];
   sha1_calculate(bios_path, obtained_sha1);
   if (strcmp(obtained_sha1, bios_sha1))
   {
      log_cb(RETRO_LOG_WARN, "Firmware found but has invalid SHA1: %s\n", bios_path);
      log_cb(RETRO_LOG_WARN, "Expected SHA1: %s\n", bios_sha1);
      log_cb(RETRO_LOG_WARN, "Obtained SHA1: %s\n", obtained_sha1);
      log_cb(RETRO_LOG_WARN, "Unsupported firmware may cause emulation glitches.\n");
      return true;
   }

   log_cb(RETRO_LOG_INFO, "Firmware found: %s\n", bios_path);
   log_cb(RETRO_LOG_INFO, "Firmware SHA1: %s\n", obtained_sha1);

   return true;
}

static void extract_basename(char *buf, const char *path, size_t size)
{
   const char *base = strrchr(path, '/');
   if (!base)
      base = strrchr(path, '\\');
   if (!base)
      base = path;

   if (*base == '\\' || *base == '/')
      base++;

   strncpy(buf, base, size - strlen(buf) - 1);
   buf[size - 1] = '\0';

   char *ext = strrchr(buf, '.');
   if (ext)
      *ext = '\0';
}

static void extract_directory(char *buf, const char *path, size_t size)
{
   strncpy(buf, path, size - 1);
   buf[size - 1] = '\0';

   char *base = strrchr(buf, '/');
   if (!base)
      base = strrchr(buf, '\\');

   if (base)
      *base = '\0';
   else
      buf[0] = '\0';
}

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
MDFNGI *MDFNGameInfo = NULL;

#if PSX_DBGPRINT_ENABLE
static unsigned psx_dbg_level = 0;

void PSX_DBG(unsigned level, const char *format, ...)
{
   if(psx_dbg_level >= level)
   {
      va_list ap;
      va_start(ap, format);
      vprintf(format, ap);
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

static std::vector<CDIF *> CDInterfaces;  // FIXME: Cleanup on error out.
static std::vector<CDIF*> *cdifs = NULL;
static std::vector<const char *> cdifs_scex_ids;

static bool eject_state;

static bool CD_TrayOpen;
int CD_SelectedDisc;     // -1 for no disc

static bool CD_IsPBP = false;
extern int PBP_DiscCount;
/* The global value PBP_DiscCount is set to
 * zero when loading single-disk PBP files.
 * We therefore have to maintain a separate
 * 'physical' disk count, otherwise the
 * frontend disk control interface will fail */
static int PBP_PhysicalDiscCount;

typedef struct
{
   unsigned initial_index;
   std::string initial_path;
   std::vector<std::string> image_paths;
   std::vector<std::string> image_labels;
} disk_control_ext_info_t;

static disk_control_ext_info_t disk_control_ext_info;

static uint64_t Memcard_PrevDC[8];
static int64_t Memcard_SaveDelay[8];

PS_CPU *PSX_CPU = NULL;
PS_SPU *PSX_SPU = NULL;
PS_CDC *PSX_CDC = NULL;
FrontIO *PSX_FIO = NULL;

MultiAccessSizeMem<512 * 1024, uint32, false> *BIOSROM = NULL;
MultiAccessSizeMem<65536, uint32, false> *PIOMem = NULL;
MultiAccessSizeMem<2048 * 1024, uint32, false> *MainRAM = NULL;
MultiAccessSizeMem<1024, uint32, false> *ScratchRAM = NULL;

#ifdef HAVE_LIGHTREC
/* Size of Expansion 1 (8MB) */
#define PSX_EXPANSION1_SIZE        0x800000U
/* Base address of Expansion 1 */
#define PSX_EXPANSION1_BASE        0x1F000000U

/* Mednafen splits the expansion in two buffers (PIOMem and TextMem). That's not
 * super convenient for us so I'm going to copy both of them in one contiguous
 * buffer */
const uint8_t *PSX_LoadExpansion1(void) {
   static uint8_t *expansion1 = NULL;

   if (expansion1 == NULL) {
      expansion1 = new uint8_t[PSX_EXPANSION1_SIZE];
   }

   /* Let's read 32bits at a time to speed things up a bit */
   uint32_t *p = reinterpret_cast<uint32_t *>(expansion1);

   for (unsigned i = 0; i < PSX_EXPANSION1_SIZE / 4; i++) {
      p[i] = PSX_MemPeek32(PSX_EXPANSION1_BASE + i * 4);
   }

   return expansion1;
}
#endif

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
         uint32_t PIO_Base;   // 0x1f801000  // BIOS Init: 0x1f000000, Writeable bits: 0x00ffffff(assumed, verify), FixedOR = 0x1f000000
         uint32_t Unknown0;   // 0x1f801004  // BIOS Init: 0x1f802000, Writeable bits: 0x00ffffff, FixedOR = 0x1f000000
         uint32_t Unknown1;   // 0x1f801008  // BIOS Init: 0x0013243f, ????
         uint32_t Unknown2;   // 0x1f80100c  // BIOS Init: 0x00003022, Writeable bits: 0x2f1fffff, FixedOR = 0x00000000

         uint32_t BIOS_Mapping;  // 0x1f801010  // BIOS Init: 0x0013243f, ????
         uint32_t SPU_Delay;  // 0x1f801014  // BIOS Init: 0x200931e1, Writeable bits: 0x2f1fffff, FixedOR = 0x00000000 - Affects bus timing on access to SPU
         uint32_t CDC_Delay;  // 0x1f801018  // BIOS Init: 0x00020843, Writeable bits: 0x2f1fffff, FixedOR = 0x00000000
         uint32_t Unknown4;   // 0x1f80101c  // BIOS Init: 0x00070777, ????
         uint32_t Unknown5;   // 0x1f801020  // BIOS Init: 0x00031125(but rewritten with other values often), Writeable bits: 0x0003ffff, FixedOR = 0x00000000 -- Possibly CDC related
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

static int32_t Running; // Set to -1 when not desiring exit, and 0 when we are.

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
        events[i].event_time = (int32_t)0x80000000;
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

   PSX_CPU->SetEventNT(events[PSX_EVENT__SYNFIRST].next->event_time);
}

void PSX_SetEventNT(const int type, const int32_t next_timestamp)
{
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

   PSX_CPU->SetEventNT(events[PSX_EVENT__SYNFIRST].next->event_time & Running);
}

// Called from debug.cpp too.
void ForceEventUpdates(const int32_t timestamp)
{
   PSX_SetEventNT(PSX_EVENT_GPU, GPU_Update(timestamp));
   PSX_SetEventNT(PSX_EVENT_CDC, PSX_CDC->Update(timestamp));

   PSX_SetEventNT(PSX_EVENT_TIMER, TIMER_Update(timestamp));

   PSX_SetEventNT(PSX_EVENT_DMA, DMA_Update(timestamp));

   PSX_SetEventNT(PSX_EVENT_FIO, PSX_FIO->Update(timestamp));

   PSX_CPU->SetEventNT(events[PSX_EVENT__SYNFIRST].next->event_time);
}

bool MDFN_FASTCALL PSX_EventHandler(const int32_t timestamp)
{
   event_list_entry *e = events[PSX_EVENT__SYNFIRST].next;

   while(timestamp >= e->event_time)   // If Running = 0, PSX_EventHandler() may be called even if there isn't an event per-se, so while() instead of do { ... } while
   {
      int32_t nt;
      event_list_entry *prev = e->prev;

      switch(e->which)
      {
         default:
            abort();
         case PSX_EVENT_GPU:
            nt = GPU_Update(e->event_time);
            break;
         case PSX_EVENT_CDC:
            nt = PSX_CDC->Update(e->event_time);
            break;
         case PSX_EVENT_TIMER:
            nt = TIMER_Update(e->event_time);
            break;
         case PSX_EVENT_DMA:
            nt = DMA_Update(e->event_time);
            break;
         case PSX_EVENT_FIO:
            nt = PSX_FIO->Update(e->event_time);
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
   PSX_CPU->SetEventNT(0);
}


//
// End event stuff
//


/* Remember to update MemPeek<>() and MemPoke<>() when we change address decoding in MemRW() */
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
         //timestamp++; // Best-case timing.
      }
      else
      {
         // Overclock: get rid of memory access latency
         if (!psx_gte_overclock)
            timestamp += 3;
      }

      if(Access24)
      {
         if(IsWrite)
            MainRAM->WriteU24(A & 0x1FFFFF, V);
         else
            V = MainRAM->ReadU24(A & 0x1FFFFF);
      }
      else
      {
         if(IsWrite)
            MainRAM->Write<T>(A & 0x1FFFFF, V);
         else
            V = MainRAM->Read<T>(A & 0x1FFFFF);
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

               PSX_SPU->Write(timestamp, A | 0, V);
               PSX_SPU->Write(timestamp, A | 2, V >> 16);
            }
            else
            {
               timestamp += 36;

               if(timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
                  PSX_EventHandler(timestamp);

               V = PSX_SPU->Read(timestamp, A) | (PSX_SPU->Read(timestamp, A | 2) << 16);
            }
         }
         else
         {
            if(IsWrite)
            {
               //timestamp += 8;

               //if(timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
               // PSX_EventHandler(timestamp);

               PSX_SPU->Write(timestamp, A & ~1, V);
            }
            else
            {
               timestamp += 16; // Just a guess, need to test.

               if(timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
                  PSX_EventHandler(timestamp);

               V = PSX_SPU->Read(timestamp, A & ~1);
            }
         }
         return;
      }     // End SPU


      // CDC: TODO - 8-bit access.
      if(A >= 0x1f801800 && A <= 0x1f80180F)
      {
         if(!IsWrite)
         {
            timestamp += 6 * sizeof(T); //24;
         }

         if(IsWrite)
            PSX_CDC->Write(timestamp, A & 0x3, V);
         else
            V = PSX_CDC->Read(timestamp, A & 0x3);

         return;
      }

      if(A >= 0x1F801810 && A <= 0x1F801817)
      {
         if(!IsWrite)
            timestamp++;

         if(IsWrite)
            GPU_Write(timestamp, A, V);
         else
            V = GPU_Read(timestamp, A);

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
            PSX_FIO->Write(timestamp, A, V);
         else
            V = PSX_FIO->Read(timestamp, A);
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

      if(A >= 0x1F801070 && A <= 0x1F801077) // IRQ
      {
         if(!IsWrite)
            timestamp++;

         if(IsWrite)
            ::IRQ_Write(A, V);
         else
            V = ::IRQ_Read(A);
         return;
      }

      if(A >= 0x1F801080 && A <= 0x1F8010FF)    // DMA
      {
         if(!IsWrite)
            timestamp++;

         if(IsWrite)
            DMA_Write(timestamp, A, V);
         else
            V = DMA_Read(timestamp, A);

         return;
      }

      if(A >= 0x1F801100 && A <= 0x1F80113F) // Root counters
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

         V = ~0U; // A game this affects:  Tetris with Cardcaptor Sakura

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
                  case 2: V = MDFN_de16lsb<false>(&TextMem[(A & 0x7FFFFF) - 65536]); break;
                  case 4: V = MDFN_de32lsb<false>(&TextMem[(A & 0x7FFFFF) - 65536]); break;
               }
            }
         }
      }
      return;
   }

   if(A == 0xFFFE0130) // Per tests on PS1, ignores the access(sort of, on reads the value is forced to 0 if not aligned) if not aligned to 4-bytes.
   {
      if(!IsWrite)
         V = PSX_CPU->GetBIU();
      else
         PSX_CPU->SetBIU(V);

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
         return(MainRAM->ReadU24(A & 0x1FFFFF));
      return(MainRAM->Read<T>(A & 0x1FFFFF));
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

      }     // End SPU


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


      if(A >= 0x1F801070 && A <= 0x1F801077) // IRQ
      {
         // TODO

      }

      if(A >= 0x1F801080 && A <= 0x1F8010FF)    // DMA
      {
         // TODO

      }

      if(A >= 0x1F801100 && A <= 0x1F80113F) // Root counters
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
                  return(MDFN_de16lsb<false>(&TextMem[(A & 0x7FFFFF) - 65536]));
               case 4:
                  return(MDFN_de32lsb<false>(&TextMem[(A & 0x7FFFFF) - 65536]));
            }
         }
      }
      return(~0U);
   }

   if(A == 0xFFFE0130)
      return PSX_CPU->GetBIU();

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

   cd_warned_slow = false;

   memset(MainRAM->data32, 0, 2048 * 1024);

   for(i = 0; i < 9; i++)
      SysControl.Regs[i] = 0;

   PSX_CPU->Power();

   EventReset();

   TIMER_Power();

   DMA_Power();

   PSX_FIO->Power();
   SIO_Power();

   MDEC_Power();
   PSX_CDC->Power();
   GPU_Power();
   //SPU->Power();   // Called from CDC->Power()
   IRQ_Power();

   ForceEventUpdates(0);
}

template<typename T, bool Access24> static INLINE void MemPoke(pscpu_timestamp_t timestamp, uint32 A, T V)
{
   if(A < 0x00800000)
   {
      if(Access24)
         MainRAM->WriteU24(A & 0x1FFFFF, V);
      else
         MainRAM->Write<T>(A & 0x1FFFFF, V);

      return;
   }

   if(A >= 0x1FC00000 && A <= 0x1FC7FFFF)
   {
      if(Access24)
         BIOSROM->WriteU24(A & 0x7FFFF, V);
      else
         BIOSROM->Write<T>(A & 0x7FFFF, V);

      return;
   }

   if(A >= 0x1F801000 && A <= 0x1F802FFF)
   {
      if(A >= 0x1F801000 && A <= 0x1F801023)
      {
         unsigned index = (A & 0x1F) >> 2;
         SysControl.Regs[index] = (V << ((A & 3) * 8)) & SysControl_Mask[index];
         return;
      }
   }

   if(A == 0xFFFE0130)
   {
      PSX_CPU->SetBIU(V);
      return;
   }
}

void PSX_MemPoke8(uint32 A, uint8 V)
{
   MemPoke<uint8, false>(0, A, V);
}

void PSX_MemPoke16(uint32 A, uint16 V)
{
   MemPoke<uint16, false>(0, A, V);
}

void PSX_MemPoke32(uint32 A, uint32 V)
{
   MemPoke<uint32, false>(0, A, V);
}

void PSX_GPULineHook(const int32_t timestamp, const int32_t line_timestamp, bool vsync, uint32_t *pixels, const MDFN_PixelFormat* const format, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor)
{
   PSX_FIO->GPULineHook(timestamp, line_timestamp, vsync, pixels, format, width, pix_clock_offset, pix_clock, pix_clock_divider, surf_pitchinpix, upscale_factor);
}

static bool TestMagic(const char *name, RFILE *fp, int64_t size)
{
   uint8_t header[8];

   if (size < 0x800)
      return(false);

   filestream_read(fp, header, 8);

   if (
         (header[0] == 'P') &&
         (header[1] == 'S') &&
         (header[2] == '-') &&
         (header[3] == 'X') &&
         (header[4] == ' ') &&
         (header[5] == 'E') &&
         (header[6] == 'X') &&
         (header[7] == 'E')
      )
      return(true);

   return(true);
}

static bool TestMagicCD(std::vector<CDIF *> *_CDInterfaces)
{
   uint8_t buf[2048];
   TOC toc;
   int dt;

#ifndef HAVE_CDROM_NEW
   TOC_Clear(&toc);
#endif

   (*_CDInterfaces)[0]->ReadTOC(&toc);

#ifdef HAVE_CDROM_NEW
   dt = toc.FindTrackByLBA(4);
#else
   dt = TOC_FindTrackByLBA(&toc, 4);
#endif

   if(dt > 0 && !(toc.tracks[dt].control & 0x4))
      return(false);

   if((*_CDInterfaces)[0]->ReadSector(buf, 4, 1) != 0x2)
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
   uint8_t pvd[2048];
   uint32_t rdel, rdel_len;
   const char *ret = NULL;
   Stream *fp      = NULL;
   unsigned pvd_search_count = 0;

   fp = c->MakeStream(0, ~0U);
   fp->seek(0x8000, SEEK_SET);

   try // Added this back to fix audio-cd loading issue
   {
   do
   {
      if((pvd_search_count++) == 32)
      {
         log_cb(RETRO_LOG_ERROR, "PVD search count limit met.\n");
         ret = NULL;
         goto Breakout;
      }

      fp->read(pvd, 2048);

      if(memcmp(&pvd[1], "CD001", 5))
      {
         log_cb(RETRO_LOG_ERROR, "Not ISO-9660\n");
         ret = NULL;
         goto Breakout;
      }

      if(pvd[0] == 0xFF)
      {
         log_cb(RETRO_LOG_ERROR, "Missing Primary Volume Descriptor\n");
         ret = NULL;
         goto Breakout;
      }
   } while(pvd[0] != 0x01);

   /*[156 ... 189], 34 bytes */
   rdel = MDFN_de32lsb<false>(&pvd[0x9E]);
   rdel_len = MDFN_de32lsb<false>(&pvd[0xA6]);

   if(rdel_len >= (1024 * 1024 * 10))  /* Arbitrary sanity check. */
   {
      log_cb(RETRO_LOG_ERROR, "Root directory table too large\n");
      ret = NULL;
      goto Breakout;
   }

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
         uint32_t file_lba = MDFN_de32lsb<false>(&dr[0x02]);
         //uint32_t file_len = MDFN_de32lsb<false>(&dr[0x0A]);
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

                     case 'K':   // Korea?
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
   } // try
   catch(std::exception &e)
   {
      //
   }
      catch(...)
   {
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
         const char *id = CalcDiscSCEx_BySYSTEMCNF((*cdifs)[i], (i == 0) ? &ret_region : NULL);

         memset(fbuf, 0, sizeof(fbuf));

         if(id == NULL && (*cdifs)[i]->ReadSector(buf, 4, 1) == 0x2)
         {
            unsigned ipos, opos;
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
                  id = "SCEI";   // ?
                  if(!i)
                     ret_region = REGION_JP;
               }
               else if(strstr((char *)fbuf, "sonycomputerentertainmentinc.") != NULL)
               {
                  id = "SCEI";
                  if(!i)
                     ret_region = REGION_JP;
               }
               else  // Failure case
               {
                  if(prev_valid_id != NULL)
                     id = prev_valid_id;
                  else
                  {
                     switch(ret_region)   // Less than correct, but meh, what can we do.
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

static void SetDiscWrapper(const bool CD_TrayOpen) {
    CDIF *cdif = NULL;
    const char *disc_id = NULL;
    if (CD_SelectedDisc >= 0 && !CD_TrayOpen) {
        // only allow one pbp file to be loaded (at index 0)
        if (CD_IsPBP) {
            cdif = (*cdifs)[0];
            disc_id = cdifs_scex_ids[0];
        } else {
            cdif = (*cdifs)[CD_SelectedDisc];
            disc_id = cdifs_scex_ids[CD_SelectedDisc];
        }
    }

    PSX_CDC->SetDisc(CD_TrayOpen, cdif, disc_id);
}

#ifdef HAVE_LIGHTREC
/* MAP_FIXED_NOREPLACE allows base 0 to work if "sysctl vm.mmap_min_addr = 0"
 was used. Base 0 will perform better by directly mapping emulated addresses
 to host addresses. If MAP_FIXED_NOREPLACE is not available we should not use
 MAP_FIXED, since it can cause strange crashes by unmapping memory mappings. */
#ifndef MAP_FIXED_NOREPLACE
#ifdef USE_FIXED
#define MAP_FIXED_NOREPLACE MAP_FIXED
#else
#define MAP_FIXED_NOREPLACE 0
#endif
#endif

static const uintptr_t supported_io_bases[] = {
	static_cast<uintptr_t>(0x00000000),
	static_cast<uintptr_t>(0x10000000),
	static_cast<uintptr_t>(0x20000000),
	static_cast<uintptr_t>(0x30000000),
	static_cast<uintptr_t>(0x40000000),
	static_cast<uintptr_t>(0x50000000),
	static_cast<uintptr_t>(0x60000000),
	static_cast<uintptr_t>(0x70000000),
	static_cast<uintptr_t>(0x80000000),
	static_cast<uintptr_t>(0x90000000),
   /* Some platforms need higher address base for mmap to work */
#if UINTPTR_MAX == UINT64_MAX
	static_cast<uintptr_t>(0x100000000),
	static_cast<uintptr_t>(0x200000000),
	static_cast<uintptr_t>(0x300000000),
	static_cast<uintptr_t>(0x400000000),
	static_cast<uintptr_t>(0x500000000),
	static_cast<uintptr_t>(0x600000000),
	static_cast<uintptr_t>(0x700000000),
	static_cast<uintptr_t>(0x800000000),
	static_cast<uintptr_t>(0x900000000),
#endif
};

#define RAM_SIZE 0x200000
#define BIOS_SIZE 0x80000
#define SCRATCH_SIZE 0x400

#ifdef HAVE_WIN_SHM
#define MAP(addr, size, fd, offset) \
	MapViewOfFileEx(fd, FILE_MAP_ALL_ACCESS, 0, offset, size, addr)
#define UNMAP(addr, size) UnmapViewOfFile(addr)
#define MFAILED NULL
#define NUM_MEM 4
#elif defined(HAVE_SHM) || defined(HAVE_ASHMEM)
#define MAP(addr, size, fd, offset) \
	mmap(addr,size, PROT_READ | PROT_WRITE, \
		MAP_SHARED | MAP_FIXED_NOREPLACE, fd, offset)
#define UNMAP(addr, size) munmap(addr, size)
#define MFAILED MAP_FAILED
#define NUM_MEM 4
#else
#define MAP(addr, size, fd, offset) \
	mmap(addr,size, PROT_READ | PROT_WRITE, \
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)
#define UNMAP(addr, size) munmap(addr, size)
#define MFAILED MAP_FAILED
#define NUM_MEM 1
#endif

int lightrec_init_mmap()
{
	int r = 0, i, j;
	uintptr_t base;
	void *bios, *scratch, *map;

/* open memfd and set size */
#ifdef HAVE_ASHMEM
	memfd = open("/dev/ashmem", O_RDWR);

	if (memfd < 0) {
		log_cb(RETRO_LOG_ERROR, "Failed to create ASHMEM: %s\n", strerror(errno));
		return 0;
	}

	ioctl(memfd, ASHMEM_SET_NAME, "lightrec_memfd");
	ioctl(memfd, ASHMEM_SET_SIZE, RAM_SIZE+BIOS_SIZE+SCRATCH_SIZE);
#endif
#ifdef HAVE_SHM
	int memfd;
	const char *shm_name = "/lightrec_memfd";

	memfd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL,
			S_IRUSR | S_IWUSR);

	if (memfd < 0) {
		log_cb(RETRO_LOG_ERROR, "Failed to create SHM: %s\n", strerror(errno));
		return 0;
	}

	/* unlink ASAP to prevent leaving a file in shared memory if we crash */
	shm_unlink(shm_name);

	if (ftruncate(memfd, RAM_SIZE+BIOS_SIZE+SCRATCH_SIZE) < 0) {
		log_cb(RETRO_LOG_ERROR, "Could not truncate SHM size: %s\n", strerror(errno));
		goto close_return;
	}
#endif
#ifdef HAVE_WIN_SHM
	HANDLE memfd;

	memfd = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
			RAM_SIZE+BIOS_SIZE+SCRATCH_SIZE, NULL);

	if (memfd == NULL) {
		log_cb(RETRO_LOG_ERROR, "Failed to create WIN_SHM: %s (%d)\n", strerror(errno), GetLastError());
		return 0;
	}
#endif

	/* Try to map at various base addresses*/
	for (i = 0; i < ARRAY_SIZE(supported_io_bases); i++) {
		base = supported_io_bases[i];
		bios = (void *)(base + 0x1fc00000);
		scratch = (void *)(base + 0x1f800000);

		for (j = 0; j < NUM_MEM; j++) {
			map = MAP((void *)(base + j * RAM_SIZE), RAM_SIZE, memfd, 0);
			if (map == MFAILED)
				break;
			else if (map != (void *)(base + j * RAM_SIZE))
			{
				//not at expected address, reject it
				UNMAP(map, RAM_SIZE);
				break;
			}
		}

		/* Impossible to map using this base */
		if (j == 0)
			continue;

		/* All mirrors mapped - we got a match! */
		if (j == NUM_MEM)
		{
			psx_mem = (uint8 *)base;

			map = MAP(bios, BIOS_SIZE, memfd, RAM_SIZE);
			if (map == MFAILED)
				goto err_unmap;

			psx_bios = (uint8 *)map;

			if (map != bios)
				goto err_unmap_bios;

			map = MAP(scratch, SCRATCH_SIZE, memfd, RAM_SIZE+BIOS_SIZE);
			if (map == MFAILED)
				goto err_unmap_bios;

			psx_scratch = (uint8 *)map;

			if (map != scratch)
				goto err_unmap_scratch;

			r = NUM_MEM;

			goto close_return;
		}

err_unmap_scratch:
		if(psx_scratch){
			UNMAP(psx_scratch, SCRATCH_SIZE);
			psx_scratch = NULL;
		}
err_unmap_bios:
		if(psx_bios){
			UNMAP(psx_bios, BIOS_SIZE);
			psx_bios = NULL;
		}
err_unmap:
		/* Clean up any mapped ram or mirrors and try again */
		for (; j > 0; j--)
			UNMAP((void *)(base + (j - 1) * RAM_SIZE), RAM_SIZE);

		psx_mem = NULL;
	}

	if (i == ARRAY_SIZE(supported_io_bases)) {
		log_cb(RETRO_LOG_WARN, "Unable to mmap on any base address, dynarec will be slower\n");
	}

close_return:
#ifdef HAVE_SHM
	close(memfd);
#endif
#ifdef HAVE_WIN_SHM
	CloseHandle(memfd);
#endif
	return r;
}

void lightrec_free_mmap()
{
	for (int i = 0; i < NUM_MEM; i++)
		UNMAP((void *)((uintptr_t)psx_mem + i * RAM_SIZE), RAM_SIZE);

	UNMAP(psx_bios, BIOS_SIZE);
	UNMAP(psx_scratch, SCRATCH_SIZE);

#ifdef HAVE_ASHMEM
	/* ashmem shared memory is not pinned by mmap, it dies on close */
	close(memfd);
#endif
}
#endif /* HAVE_LIGHTREC */

/* Forward declarations, required for disk control
 * 'set initial disk' functionality */
static unsigned disk_get_num_images(void);
static void CDInsertEject(void);
static void CDEject(void);

static void InitCommon(std::vector<CDIF *> *_CDInterfaces, const bool EmulateMemcards = true, const bool WantPIOMem = false)
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

   if (!enable_memcard1) {
     emulate_memcard[1] = false;
   }

   emulate_multitap[0] = setting_psx_multitap_port_1;
   emulate_multitap[1] = setting_psx_multitap_port_2;

   cdifs  = _CDInterfaces;
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

   PSX_CPU = new PS_CPU();
   PSX_SPU = new PS_SPU();

   GPU_Init(region == REGION_EU, sls, sle, psx_gpu_upscale_shift);

   PSX_CDC = new PS_CDC();
   PSX_FIO = new FrontIO(emulate_memcard, emulate_multitap);
   PSX_FIO->SetAMCT(MDFN_GetSettingB("psx.input.analog_mode_ct"));
   for(unsigned i = 0; i < 8; i++)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "psx.input.port%u.gun_chairs", i + 1);
      PSX_FIO->SetCrosshairsColor(i, MDFN_GetSettingUI(buf));
   }

	input_set_fio( PSX_FIO );

   DMA_Init();

   GPU_FillVideoParams(&EmulatedPSX);

   switch (psx_gpu_dither_mode)
   {
      case DITHER_NATIVE:
         GPU_set_dither_upscale_shift(psx_gpu_upscale_shift);
         break;
      case DITHER_UPSCALED:
         GPU_set_dither_upscale_shift(0);
         break;
      case DITHER_OFF:
         break;
   }

   PGXP_SetModes(psx_pgxp_mode | psx_pgxp_vertex_caching | psx_pgxp_texture_correction);

   CD_TrayOpen        = true;
   CD_SelectedDisc    = -1;

   if(cdifs)
   {
      CD_TrayOpen     = false;
      CD_SelectedDisc = 0;

      /* Attempt to set initial disk index */
      if ((disk_control_ext_info.initial_index > 0) &&
          (disk_control_ext_info.initial_index < disk_get_num_images()))
         if (disk_control_ext_info.initial_index <
               disk_control_ext_info.image_paths.size())
            if (string_is_equal(
                  disk_control_ext_info.image_paths[disk_control_ext_info.initial_index].c_str(),
                  disk_control_ext_info.initial_path.c_str()))
               CD_SelectedDisc = (int)disk_control_ext_info.initial_index;
   }

   PSX_CDC->SetDisc(true, NULL, NULL);

   /* Multi-disk PBP files cause additional complication
    * here, since the first disk is always loaded by default */
   if(CD_IsPBP && (CD_SelectedDisc > 0))
   {
      CDEject();
      CDInsertEject();
   }
   else
      SetDiscWrapper(CD_TrayOpen);

#ifdef HAVE_LIGHTREC
   psx_mmap = lightrec_init_mmap();

   if(psx_mmap > 0)
   {
      MainRAM = new(psx_mem) MultiAccessSizeMem<RAM_SIZE, uint32, false>();
      ScratchRAM = new(psx_scratch) MultiAccessSizeMem<SCRATCH_SIZE, uint32, false>();
      BIOSROM = new(psx_bios) MultiAccessSizeMem<BIOS_SIZE, uint32, false>();
   }
   else
#endif
   {
      MainRAM = new MultiAccessSizeMem<2048 * 1024, uint32, false>();
      ScratchRAM = new MultiAccessSizeMem<1024, uint32, false>();
      BIOSROM = new MultiAccessSizeMem<512 * 1024, uint32, false>();
   }

   PIOMem  = NULL;

   if(WantPIOMem)
      PIOMem = new MultiAccessSizeMem<65536, uint32, false>();

   for(uint32_t ma = 0x00000000; ma < 0x00800000; ma += 2048 * 1024)
   {
      PSX_CPU->SetFastMap(MainRAM->data32, 0x00000000 + ma, 2048 * 1024);
      PSX_CPU->SetFastMap(MainRAM->data32, 0x80000000 + ma, 2048 * 1024);
      PSX_CPU->SetFastMap(MainRAM->data32, 0xA0000000 + ma, 2048 * 1024);
   }

   PSX_CPU->SetFastMap(BIOSROM->data32, 0x1FC00000, 512 * 1024);
   PSX_CPU->SetFastMap(BIOSROM->data32, 0x9FC00000, 512 * 1024);
   PSX_CPU->SetFastMap(BIOSROM->data32, 0xBFC00000, 512 * 1024);

   if(PIOMem)
   {
      PSX_CPU->SetFastMap(PIOMem->data32, 0x1F000000, 65536);
      PSX_CPU->SetFastMap(PIOMem->data32, 0x9F000000, 65536);
      PSX_CPU->SetFastMap(PIOMem->data32, 0xBF000000, 65536);
   }


   MDFNMP_Init(1024, ((uint64)1 << 29) / 1024);
   MDFNMP_AddRAM(2048 * 1024, 0x00000000, MainRAM->data8);
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
      const char *biospath = MDFN_MakeFName(MDFNMKF_FIRMWARE,
            0, MDFN_GetSettingS(biospath_sname).c_str());
      RFILE *BIOSFile      = filestream_open(biospath,
            RETRO_VFS_FILE_ACCESS_READ,
            RETRO_VFS_FILE_ACCESS_HINT_NONE);

      if (BIOSFile)
      {
         filestream_read(BIOSFile, BIOSROM->data8, 512 * 1024);
         filestream_close(BIOSFile);
      }
   }

   i = 0;

   if (!use_mednafen_memcard0_method)
   {
      PSX_FIO->LoadMemcard(0);
      i = 1;
   }

   for(; i < 8; i++)
   {
      char ext[64];
      const char *memcard = NULL;
      snprintf(ext, sizeof(ext), "%d.mcr", i);
      memcard = MDFN_MakeFName(MDFNMKF_SAV, 0, ext);
      PSX_FIO->LoadMemcard(i, memcard);
   }

   for(i = 0; i < 8; i++)
   {
      Memcard_PrevDC[i] = PSX_FIO->GetMemcardDirtyCount(i);
      Memcard_SaveDelay[i] = -1;
   }

	input_init_calibration();

#ifdef WANT_DEBUGGER
   DBG_Init();
#endif
   PSX_Power();
}

static bool LoadEXE(const uint8_t *data, const uint32_t size, bool ignore_pcsp = false)
{
   uint32 PC        = MDFN_de32lsb<false>(&data[0x10]);
   uint32 SP        = MDFN_de32lsb<false>(&data[0x30]);
   uint32 TextStart = MDFN_de32lsb<false>(&data[0x18]);
   uint32 TextSize  = MDFN_de32lsb<false>(&data[0x1C]);

   if(ignore_pcsp)
      log_cb(RETRO_LOG_INFO, "TextStart=0x%08x\nTextSize=0x%08x\n", TextStart, TextSize);
   else
      log_cb(RETRO_LOG_INFO, "PC=0x%08x\nSP=0x%08x\nTextStart=0x%08x\nTextSize=0x%08x\n", PC, SP, TextStart, TextSize);

   TextStart &= 0x1FFFFF;

   if(TextSize > 2048 * 1024)
   {
      MDFN_Error(0, "Text section too large");
      return false;
   }

   if(TextSize > (size - 0x800))
   {
      MDFN_Error(0, "Text section recorded size is larger than data available in file.  Header=0x%08x, Available=0x%08x", TextSize, size - 0x800);
      return false;
   }

   if(TextSize < (size - 0x800))
   {
      MDFN_Error(0, "Text section recorded size is smaller than data available in file.  Header=0x%08x, Available=0x%08x", TextSize, size - 0x800);
      return false;
   }

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

   MDFN_en32lsb<false>(po, (0x0 << 26) | (31 << 21) | (0x8 << 0)); // JR
   po += 4;
   MDFN_en32lsb<false>(po, 0); // NOP(kinda)
   po += 4;

   po = &PIOMem->data8[0x1000];

   // Load cacheable-region target PC into r2
   MDFN_en32lsb<false>(po, (0xF << 26) | (0 << 21) | (1 << 16) | (0x9F001010 >> 16));      // LUI
   po += 4;
   MDFN_en32lsb<false>(po, (0xD << 26) | (1 << 21) | (2 << 16) | (0x9F001010 & 0xFFFF));   // ORI
   po += 4;

   // Jump to r2
   MDFN_en32lsb<false>(po, (0x0 << 26) | (2 << 21) | (0x8 << 0));  // JR
   po += 4;
   MDFN_en32lsb<false>(po, 0); // NOP(kinda)
   po += 4;

   //
   // 0x9F001010:
   //

   // Load source address into r8
   uint32 sa = 0x9F000000 + 65536;
   MDFN_en32lsb<false>(po, (0xF << 26) | (0 << 21) | (1 << 16) | (sa >> 16));  // LUI
   po += 4;
   MDFN_en32lsb<false>(po, (0xD << 26) | (1 << 21) | (8 << 16) | (sa & 0xFFFF));  // ORI
   po += 4;

   // Load dest address into r9
   MDFN_en32lsb<false>(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (TextMem_Start >> 16));  // LUI
   po += 4;
   MDFN_en32lsb<false>(po, (0xD << 26) | (1 << 21) | (9 << 16) | (TextMem_Start & 0xFFFF));   // ORI
   po += 4;

   // Load size into r10
   MDFN_en32lsb<false>(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (TextMem.size() >> 16)); // LUI
   po += 4;
   MDFN_en32lsb<false>(po, (0xD << 26) | (1 << 21) | (10 << 16) | (TextMem.size() & 0xFFFF));    // ORI
   po += 4;

   //
   // Loop begin
   //

   MDFN_en32lsb<false>(po, (0x24 << 26) | (8 << 21) | (1 << 16));  // LBU to r1
   po += 4;

   MDFN_en32lsb<false>(po, (0x08 << 26) | (10 << 21) | (10 << 16) | 0xFFFF);   // Decrement size
   po += 4;

   MDFN_en32lsb<false>(po, (0x28 << 26) | (9 << 21) | (1 << 16));  // SB from r1
   po += 4;

   MDFN_en32lsb<false>(po, (0x08 << 26) | (8 << 21) | (8 << 16) | 0x0001);  // Increment source addr
   po += 4;

   MDFN_en32lsb<false>(po, (0x05 << 26) | (0 << 21) | (10 << 16) | (-5 & 0xFFFF));
   po += 4;
   MDFN_en32lsb<false>(po, (0x08 << 26) | (9 << 21) | (9 << 16) | 0x0001);  // Increment dest addr
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
      MDFN_en32lsb<false>(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (SP >> 16)); // LUI
      po += 4;
      MDFN_en32lsb<false>(po, (0xD << 26) | (1 << 21) | (29 << 16) | (SP & 0xFFFF));    // ORI
      po += 4;

      // Load PC into r2
      MDFN_en32lsb<false>(po, (0xF << 26) | (0 << 21) | (1 << 16)  | ((PC >> 16) | 0x8000));      // LUI
      po += 4;
      MDFN_en32lsb<false>(po, (0xD << 26) | (1 << 21) | (2 << 16) | (PC & 0xFFFF));   // ORI
      po += 4;
   }

   // Half-assed instruction cache flush. ;)
   for(unsigned i = 0; i < 1024; i++)
   {
      MDFN_en32lsb<false>(po, 0);
      po += 4;
   }



   // Jump to r2
   MDFN_en32lsb<false>(po, (0x0 << 26) | (2 << 21) | (0x8 << 0));  // JR
   po += 4;
   MDFN_en32lsb<false>(po, 0); // NOP(kinda)
   po += 4;

#ifdef HAVE_LIGHTREC
   /* Reload Expansion1 copy */
   PSX_LoadExpansion1();
#endif

   return true;
}

static int Load(const char *name, RFILE *fp)
{
   int64_t size     = filestream_get_size(fp);
   const bool IsPSF = false;
   char image_label[4096];

   image_label[0] = '\0';

   if(!TestMagic(name, fp, size))
   {
      MDFN_Error(0, "File format is unknown to module psx..");
      return -1;
   }

   InitCommon(NULL, !IsPSF, true);

   TextMem.resize(0);

   if(size >= 0x800)
   {
      int64_t len     = size;
      uint8_t *header = (uint8_t*)malloc(len * sizeof(uint8_t));

      filestream_read_file(name, (void**)&header, &len);

      if (!LoadEXE(header, len))
         return -1;

      free(header);
   }

   MDFNGameInfo = &EmulatedPSX;

   disk_control_ext_info.image_paths.push_back(name);
   extract_basename(image_label, name, sizeof(image_label));
   disk_control_ext_info.image_labels.push_back(image_label);

   return(1);
}

static int LoadCD(std::vector<CDIF *> *_CDInterfaces)
{
   InitCommon(_CDInterfaces);

   if (psx_skipbios == 1)
   BIOSROM->WriteU32(0x6990, 0);

   MDFNGameInfo->GameType = GMT_CDROM;

   return(1);
}

static void Cleanup(void)
{
   TextMem.resize(0);

   if(PSX_CDC)
      delete PSX_CDC;
   PSX_CDC = NULL;

   if(PSX_SPU)
      delete PSX_SPU;
   PSX_SPU = NULL;

   GPU_Destroy();

   if(PSX_CPU)
      delete PSX_CPU;
   PSX_CPU = NULL;

   if(PSX_FIO)
      delete PSX_FIO;
   PSX_FIO = NULL;
   input_set_fio(NULL);

   DMA_Kill();

#ifdef HAVE_LIGHTREC
   MainRAM = NULL;
   ScratchRAM = NULL;
   BIOSROM = NULL;
   if(psx_mmap > 0)
      lightrec_free_mmap();
#else
   if(MainRAM)
      delete MainRAM;
   MainRAM = NULL;

   if(ScratchRAM)
      delete ScratchRAM;
   ScratchRAM = NULL;

   if(BIOSROM)
      delete BIOSROM;
   BIOSROM = NULL;
#endif

   if(PIOMem)
      delete PIOMem;
   PIOMem = NULL;

   cdifs = NULL;
}

static void CloseGame(void)
{
   int i;

   if (!failed_init)
   {
      for(i = 0; i < 8; i++)
      {
         if (i == 0 && !use_mednafen_memcard0_method)
         {
            PSX_FIO->SaveMemcard(i);
            continue;
         }

         // If there's an error saving one memcard, don't skip trying to save the other, since it might succeed and
         // we can reduce potential data loss!
         try
         {
            char ext[64];
            const char *memcard = NULL;
            snprintf(ext, sizeof(ext), "%d.mcr", i);
            memcard = MDFN_MakeFName(MDFNMKF_SAV, 0, ext);
            PSX_FIO->SaveMemcard(i, memcard);
         }
         catch(std::exception &e)
         {
            log_cb(RETRO_LOG_ERROR, "%s\n", e.what());
         }
      }
   }

   Cleanup();
}

static void CDInsertEject(void)
{
   CD_TrayOpen = !CD_TrayOpen;

   for(unsigned disc = 0; disc < cdifs->size(); disc++)
   {
#ifndef HAVE_CDROM_NEW
      if(!(*cdifs)[disc]->Eject(CD_TrayOpen))
      {
         MDFN_DispMessage(_("Eject error."));
         CD_TrayOpen = !CD_TrayOpen;
      }
#endif
   }

   if(CD_TrayOpen)
      MDFN_DispMessage(_("Virtual CD Drive Tray Open"));
   else
      MDFN_DispMessage(_("Virtual CD Drive Tray Closed"));

   SetDiscWrapper(CD_TrayOpen);
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
  int disc_count = (CD_IsPBP ? PBP_PhysicalDiscCount : (int)cdifs->size());

  CD_SelectedDisc = (CD_SelectedDisc + 1) % (disc_count + 1);

  if(CD_SelectedDisc == disc_count)
   CD_SelectedDisc = -1;

  if(CD_SelectedDisc == -1)
   MDFN_DispMessage(_("Disc absence selected."));
  else
   MDFN_DispMessage(_("Disc %d of %d selected."), CD_SelectedDisc + 1, disc_count);
 }
}

int StateAction(StateMem *sm, int load, int data_only)
{
   SFORMAT StateRegs[] =
   {
      SFVAR(CD_TrayOpen),
      SFVAR(CD_SelectedDisc),
      SFARRAYN(MainRAM->data8, 1024 * 2048, "MainRAM.data8"),
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
      if(CD_IsPBP)
      {
         if(!cdifs || CD_SelectedDisc >= PBP_PhysicalDiscCount)
            CD_SelectedDisc = -1;

         CDEject();
         CDInsertEject();
      } else {
         if(!cdifs || CD_SelectedDisc >= (int)cdifs->size())
            CD_SelectedDisc = -1;

         SetDiscWrapper(CD_TrayOpen);
      }
   }

   // TODO: Remember to increment dirty count in memory card state loading routine.

   ret &= PSX_CPU->StateAction(sm, load, data_only);
   ret &= DMA_StateAction(sm, load, data_only);
   ret &= TIMER_StateAction(sm, load, data_only);
   ret &= SIO_StateAction(sm, load, data_only);

   ret &= PSX_CDC->StateAction(sm, load, data_only);
   ret &= MDEC_StateAction(sm, load, data_only);
   ret &= GPU_StateAction(sm, load, data_only);
   ret &= PSX_SPU->StateAction(sm, load, data_only);

   ret &= PSX_FIO->StateAction(sm, load, data_only);

   ret &= IRQ_StateAction(sm, load, data_only); // Do it last.

   if(load)
   {
      ForceEventUpdates(0); // FIXME to work with debugger step mode.
   }

   return(ret);
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
      snprintf(tmp, 256, "%u L 0x%08x %s 0x%04x", len, addr, cc, val & 0xFFFFU);
   else
      snprintf(tmp, 256, "%u L 0x%08x %s 0x%02x", len, addr, cc, val & 0xFFU);

   patch->conditions.append(tmp);
}

static bool DecodeGS(const std::string& cheat_string, MemoryPatch* patch)
{
   uint64 code = 0;
   unsigned nybble_count = 0;

   for(unsigned i = 0; i < cheat_string.size(); i++)
   {
      if(cheat_string[i] == ' ' || cheat_string[i] == '-' || cheat_string[i] == ':' || cheat_string[i] == '+')
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
   case 0x10:   // 16-bit increment
      patch->length = 2;
      patch->type = 'A';
      patch->addr = cl >> 16;
      patch->val = cl & 0xFFFF;
      return(false);

   case 0x11:   // 16-bit decrement
      patch->length = 2;
      patch->type = 'A';
      patch->addr = cl >> 16;
      patch->val = (0 - cl) & 0xFFFF;
      return(false);

   case 0x20:   // 8-bit increment
      patch->length = 1;
      patch->type = 'A';
      patch->addr = cl >> 16;
      patch->val = cl & 0xFF;
      return(false);

   case 0x21:   // 8-bit decrement
      patch->length = 1;
      patch->type = 'A';
      patch->addr = cl >> 16;
      patch->val = (0 - cl) & 0xFF;
      return(false);
   //
   //
   //

   case 0x30:   // 8-bit constant
      patch->length = 1;
      patch->type = 'R';
      patch->addr = cl >> 16;
      patch->val = cl & 0xFF;
      return(false);

   case 0x80:   // 16-bit constant
      patch->length = 2;
      patch->type = 'R';
      patch->addr = cl >> 16;
      patch->val = cl & 0xFFFF;
      return(false);

   case 0x50:   // Repeat thingy
   {
      const uint8 wcount = (cl >> 24) & 0xFF;
      const uint8 addr_inc = (cl >> 16) & 0xFF;
      const uint8 val_inc = (cl >> 0) & 0xFF;

      patch->mltpl_count = wcount;
      patch->mltpl_addr_inc = addr_inc;
      patch->mltpl_val_inc = val_inc;
   }
   return(true);

   case 0xC2:   // Copy
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

  case 0xD0:   // 16-bit == condition
   GSCondCode(patch, "==", 2, cl >> 16, cl);
   return(true);

  case 0xD1:   // 16-bit != condition
   GSCondCode(patch, "!=", 2, cl >> 16, cl);
   return(true);

  case 0xD2:   // 16-bit < condition
   GSCondCode(patch, "<", 2, cl >> 16, cl);
   return(true);

  case 0xD3:   // 16-bit > condition
   GSCondCode(patch, ">", 2, cl >> 16, cl);
   return(true);



  case 0xE0:   // 8-bit == condition
   GSCondCode(patch, "==", 1, cl >> 16, cl);
   return(true);

  case 0xE1:   // 8-bit != condition
   GSCondCode(patch, "!=", 1, cl >> 16, cl);
   return(true);

  case 0xE2:   // 8-bit < condition
   GSCondCode(patch, "<", 1, cl >> 16, cl);
   return(true);

  case 0xE3:   // 8-bit > condition
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

   { NULL },
};

// Note for the future: If we ever support PSX emulation with non-8-bit RGB color components, or add a new linear RGB colorspace to MDFN_PixelFormat, we'll need
// to buffer the intermediate 24-bit non-linear RGB calculation into an array and pass that into the GPULineHook stuff, otherwise netplay could break when
// an emulated GunCon is used.  This IS assuming, of course, that we ever implement save state support so that netplay actually works at all...
MDFNGI EmulatedPSX =
{
   PSXSettings,
   MDFN_MASTERCLOCK_FIXED(33868800),
   0,

   true, // Multires possible?

   //
   // Note: Following video settings will be overwritten during game load.
   //
   0,   // lcm_width
   0,   // lcm_height
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

static bool overscan;
static double last_sound_rate;

#ifdef NEED_DEINTERLACER
static bool PrevInterlaced;
static Deinterlacer deint;
#endif

static MDFN_Surface *surf = NULL;

static void alloc_surface(void)
{
   MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 16, 8, 0, 24);
   uint32_t width  = MEDNAFEN_CORE_GEOMETRY_MAX_W;
   uint32_t height = content_is_pal ? MEDNAFEN_CORE_GEOMETRY_MAX_H  : 480;

   width  <<= GPU_get_upscale_shift();
   height <<= GPU_get_upscale_shift();

   if (surf != NULL)
      delete surf;

   surf = new MDFN_Surface(NULL, width, height, width, pix_fmt);
}

static void check_system_specs(void)
{
   // Hints that we need a fairly powerful system to run this.
   unsigned level = 15;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

static unsigned disk_get_num_images(void)
{
   if(cdifs)
      return CD_IsPBP ? PBP_PhysicalDiscCount : cdifs->size();
   return 0;
}

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
static void mednafen_update_md5_checksum(CDIF *iface)
{
   uint8 LayoutMD5[16];
   md5_context layout_md5;
   TOC toc;

   mednafen_md5_starts(&layout_md5);

#ifndef HAVE_CDROM_NEW
   TOC_Clear(&toc);
#endif

   iface->ReadTOC(&toc);

   mednafen_md5_update_u32_as_lsb(&layout_md5, toc.first_track);
   mednafen_md5_update_u32_as_lsb(&layout_md5, toc.last_track);
   mednafen_md5_update_u32_as_lsb(&layout_md5, toc.tracks[100].lba);

   for (uint32 track = toc.first_track; track <= toc.last_track; track++)
   {
      mednafen_md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].lba);
      mednafen_md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].control & 0x4);
   }

   mednafen_md5_finish(&layout_md5, LayoutMD5);
   memcpy(MDFNGameInfo->MD5, LayoutMD5, 16);

   char *md5 = mednafen_md5_asciistr(MDFNGameInfo->MD5);
   log_cb(RETRO_LOG_INFO, "[Mednafen]: Updated md5 checksum: %s.\n", md5);
}

// Untested ...
static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
   if (index >= disk_get_num_images() || !eject_state || CD_IsPBP)
      return false;

   if (!info)
   {
      delete cdifs->at(index);
      cdifs->erase(cdifs->begin() + index);
      if (index < CD_SelectedDisc)
         CD_SelectedDisc--;

      disk_control_ext_info.image_paths.erase(
            disk_control_ext_info.image_paths.begin() + index);
      disk_control_ext_info.image_labels.erase(
            disk_control_ext_info.image_labels.begin() + index);

      // Poke into psx.cpp
      CalcDiscSCEx();
      return true;
   }

   bool success = true;

#ifdef HAVE_CDROM_NEW
   CDIF *iface = CDIF_Open(info->path, false);
#else
   CDIF *iface = CDIF_Open(&success, info->path, false, false);
#endif

   if (!success)
      return false;

   delete cdifs->at(index);
   cdifs->at(index) = iface;
   CalcDiscSCEx();

   /* If we replace, we want the "swap disk manually effect". */
   extract_basename(retro_cd_base_name, info->path, sizeof(retro_cd_base_name));
   /* Ugly, but needed to get proper disk swapping effect. */
   mednafen_update_md5_checksum(iface);

   /* Update disk path/label vectors */
   disk_control_ext_info.image_paths[index]  = info->path;
   disk_control_ext_info.image_labels[index] = retro_cd_base_name;

   return true;
}

static bool disk_add_image_index(void)
{
   if(CD_IsPBP)
      return false;

   cdifs->push_back(NULL);
   disk_control_ext_info.image_paths.push_back("");
   disk_control_ext_info.image_labels.push_back("");
   return true;
}

static bool disk_set_initial_image(unsigned index, const char *path)
{
	if (string_is_empty(path))
		return false;

	disk_control_ext_info.initial_index = index;
	disk_control_ext_info.initial_path  = path;

	return true;
}

static bool disk_get_image_path(unsigned index, char *path, size_t len)
{
	if (len < 1)
		return false;

	if ((index < disk_get_num_images()) &&
		 (index < disk_control_ext_info.image_paths.size()))
	{
		if (!string_is_empty(disk_control_ext_info.image_paths[index].c_str()))
		{
			strlcpy(path, disk_control_ext_info.image_paths[index].c_str(), len);
			return true;
		}
	}

	return false;
}

static bool disk_get_image_label(unsigned index, char *label, size_t len)
{
	if (len < 1)
		return false;

	if ((index < disk_get_num_images()) &&
		 (index < disk_control_ext_info.image_labels.size()))
	{
		if (!string_is_empty(disk_control_ext_info.image_labels[index].c_str()))
		{
			strlcpy(label, disk_control_ext_info.image_labels[index].c_str(), len);
			return true;
		}
	}

	return false;
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

static struct retro_disk_control_ext_callback disk_interface_ext =
{
	disk_set_eject_state,
	disk_get_eject_state,
	disk_get_image_index,
	disk_set_image_index,
	disk_get_num_images,
	disk_replace_image_index,
	disk_add_image_index,
	disk_set_initial_image,
	disk_get_image_path,
	disk_get_image_label,
};

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
}


void retro_init(void)
{
   struct retro_log_callback log;
   uint64_t serialization_quirks = RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE;
   unsigned dci_version          = 0;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = fallback_log;

   CDUtility_Init();

   eject_state = false;

   const char *dir = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
   {
      snprintf(retro_base_directory, sizeof(retro_base_directory), "%s", dir);
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
      if (dir)
         snprintf(retro_save_directory, sizeof(retro_save_directory), "%s", dir);
      else
         snprintf(retro_save_directory, sizeof(retro_save_directory), "%s", retro_base_directory);
   }
   else
   {
      /* TODO: Add proper fallback */
      log_cb(RETRO_LOG_WARN, "Save directory is not defined. Fallback on using SYSTEM directory ...\n");
      snprintf(retro_save_directory, sizeof(retro_save_directory), "%s", retro_base_directory);
   }

   /* Initialise disk control interface */
   disk_control_ext_info.initial_index = 0;
   disk_control_ext_info.initial_path.clear();
   disk_control_ext_info.image_paths.clear();
   disk_control_ext_info.image_labels.clear();

   if (environ_cb(RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION, &dci_version) && (dci_version >= 1))
      environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &disk_interface_ext);
   else
      environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface);

   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      perf_get_cpu_features_cb = perf_cb.get_cpu_features;
   else
      perf_get_cpu_features_cb = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS, &serialization_quirks) &&
       (serialization_quirks & RETRO_SERIALIZATION_QUIRK_FRONT_VARIABLE_SIZE))
      enable_variable_serialization_size = true;

   setting_initial_scanline = 0;
   setting_last_scanline = 239;
   setting_initial_scanline_pal = 0;
   setting_last_scanline_pal = 287;

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_bitmasks = true;

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

#ifdef EMSCRIPTEN
static bool old_cdimagecache = true;
#else
static bool old_cdimagecache = false;
#endif

static bool boot = true;

// shared memory cards support
static bool shared_memorycards = false;

static bool has_new_geometry = false;
static bool has_new_timing = false;

static void check_variables(bool startup)
{
   struct retro_variable var = {0};

   extern void PSXDitherApply(bool);

#ifndef EMSCRIPTEN
   var.key = BEETLE_OPT(cd_access_method);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "sync") == 0)
      {
         old_cdimagecache = false;
         cd_async = false;
      }
      else if (strcmp(var.value, "async") == 0)
      {
         old_cdimagecache = false;
         cd_async = true;
      }
      else if (strcmp(var.value, "precache") == 0)
      {
         old_cdimagecache = true;
         cd_async = false;
      }
   }
#endif

#ifdef HAVE_LIGHTREC
   var.key = BEETLE_OPT(cpu_dynarec);

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "execute") == 0)
         psx_dynarec = DYNAREC_EXECUTE;
      else if (strcmp(var.value, "execute_one") == 0)
         psx_dynarec = DYNAREC_EXECUTE_ONE;
      else if (strcmp(var.value, "run_interpreter") == 0)
         psx_dynarec = DYNAREC_RUN_INTERPRETER;
      else
         psx_dynarec = DYNAREC_DISABLED;
   }
   else
      psx_dynarec = DYNAREC_DISABLED;

   var.key = BEETLE_OPT(dynarec_invalidate);

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "full") == 0)
         psx_dynarec_invalidate = false;
      else if (strcmp(var.value, "dma") == 0)
         psx_dynarec_invalidate = true;
   }
   else
      psx_dynarec_invalidate = false;

   var.key = BEETLE_OPT(dynarec_eventcycles);

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	EventCycles = atoi(var.value);
   }
   else
      EventCycles = 128;
#endif

   var.key = BEETLE_OPT(cpu_freq_scale);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int scale_percent = atoi(var.value);

      if (scale_percent == 100)
         psx_overclock_factor = 0;
      else
         psx_overclock_factor = ((scale_percent << OVERCLOCK_SHIFT) + 50) / 100;
   }
   else
      psx_overclock_factor = 0;

   // Need to adjust the CPU<->GPU frequency ratio if the overclocking changes
   GPU_RecalcClockRatio();

   var.key = BEETLE_OPT(gte_overclock);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         psx_gte_overclock = true;
      else if (strcmp(var.value, "disabled") == 0)
         psx_gte_overclock = false;
   }
   else
      psx_gte_overclock = false;

   var.key = BEETLE_OPT(gpu_overclock);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         unsigned val = atoi(var.value);

         // Upscale must be a power of two
         assert((val & (val - 1)) == 0);

         // Crappy "ffs" implementation since the standard function is not
         // widely supported by libc in the wild
         uint8_t n;
         for (n = 0; (val & 1) == 0; ++n)
            {
               val >>= 1;
            }
         psx_gpu_overclock_shift = n;
      }
   else
      psx_gpu_overclock_shift = 0;

   var.key = BEETLE_OPT(skip_bios);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         psx_skipbios = 1;
      else
         psx_skipbios = 0;
   }

   var.key = BEETLE_OPT(widescreen_hack);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
      {
         if (widescreen_hack == false)
            has_new_geometry = true;
         widescreen_hack = true;
      }
      else if (strcmp(var.value, "disabled") == 0)
      {
         if (widescreen_hack == true)
            has_new_geometry = true;
         widescreen_hack = false;
      }
   }
   else
   {
      if (widescreen_hack == true)
         has_new_geometry = true;
      widescreen_hack = false;
   }

   var.key = BEETLE_OPT(pal_video_timing_override);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      bool want_fast_pal = (strcmp(var.value, "enabled") == 0);

      if (want_fast_pal != fast_pal) {
         fast_pal = want_fast_pal;
         has_new_timing = true;
      }
   }

   var.key = BEETLE_OPT(analog_calibration);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         input_enable_calibration(true);
      else if (strcmp(var.value, "disabled") == 0)
         input_enable_calibration(false);
   }
   else
      input_enable_calibration(false);

   var.key = BEETLE_OPT(core_timing_fps);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "force_progressive") == 0)
      {
         if (!startup && core_timing_fps_mode != FORCE_PROGRESSIVE_TIMING)
            has_new_timing = true;

         core_timing_fps_mode = FORCE_PROGRESSIVE_TIMING;
      }
      else if (strcmp(var.value, "force_interlaced") == 0)
      {
         if (!startup && core_timing_fps_mode != FORCE_INTERLACED_TIMING)
            has_new_timing = true;

         core_timing_fps_mode = FORCE_INTERLACED_TIMING;
      }
      else // auto toggle setting, timing changes are allowed
      {
         if (!startup && core_timing_fps_mode != AUTO_TOGGLE_TIMING)
            has_new_timing = true;

         core_timing_fps_mode = AUTO_TOGGLE_TIMING;
      }
   }

   var.key = BEETLE_OPT(aspect_ratio);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "corrected"))
      {
         if (!startup && aspect_ratio_setting != 0)
            has_new_geometry = true;
         aspect_ratio_setting = 0;
      }
      else if (!strcmp(var.value, "uncorrected"))
      {
         if (!startup && aspect_ratio_setting != 1)
            has_new_geometry = true;
         aspect_ratio_setting = 1;
      }
      else if (!strcmp(var.value, "4:3"))
      {
         if (!startup && aspect_ratio_setting != 2)
            has_new_geometry = true;
         aspect_ratio_setting = 2;
      }
      else if (!strcmp(var.value, "ntsc"))
      {
         if (!startup && aspect_ratio_setting != 3)
            has_new_geometry = true;
         aspect_ratio_setting = 3;
      }
   }

   if (startup)
   {
      var.key = BEETLE_OPT(renderer);
      bool hw_renderer = false;
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "hardware") || !strcmp(var.value, "hardware_gl") || !strcmp(var.value, "hardware_vk"))
         {
            hw_renderer = true;
         }
      }

      if (hw_renderer)
      {
         psx_gpu_upscale_shift = 0;
      }
      else
      {
         var.key = BEETLE_OPT(internal_resolution);
         if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
         {
            uint8_t new_upscale_shift;
            uint8_t val = atoi(var.value);

            // Upscale must be a power of two
            assert((val & (val - 1)) == 0);

            // Crappy "ffs" implementation since the standard function is not
            // widely supported by libc in the wild
            for (new_upscale_shift = 0; (val & 1) == 0; ++new_upscale_shift)
               val >>= 1;
            psx_gpu_upscale_shift = new_upscale_shift;
         }
         else
            psx_gpu_upscale_shift = 0;
      }
   }
   else
   {
      rsx_intf_refresh_variables();

      switch (rsx_intf_is_type())
      {
         case RSX_SOFTWARE:
            var.key = BEETLE_OPT(internal_resolution);

            if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
            {
               uint8_t new_upscale_shift;
               uint8_t val = atoi(var.value);

               // Upscale must be a power of two
               assert((val & (val - 1)) == 0);

               // Crappy "ffs" implementation since the standard function is not
               // widely supported by libc in the wild
               for (new_upscale_shift = 0; (val & 1) == 0; ++new_upscale_shift)
                  val >>= 1;
               psx_gpu_upscale_shift = new_upscale_shift;
            }
            else
               psx_gpu_upscale_shift = 0;

            break;
         case RSX_OPENGL:
         case RSX_VULKAN:
            psx_gpu_upscale_shift = 0;
            break;
      }
   }

   var.key = BEETLE_OPT(dither_mode);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "1x(native)") == 0)
         psx_gpu_dither_mode = DITHER_NATIVE;
      else if (strcmp(var.value, "internal resolution") == 0)
         psx_gpu_dither_mode = DITHER_UPSCALED;
      else if (strcmp(var.value, "disabled") == 0)
         psx_gpu_dither_mode = DITHER_OFF;
   }
   else
      psx_gpu_dither_mode = DITHER_NATIVE;

   // iCB: PGXP settings
   var.key = BEETLE_OPT(pgxp_mode);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         psx_pgxp_mode = PGXP_MODE_NONE;
      else if (strcmp(var.value, "memory only") == 0)
         psx_pgxp_mode = PGXP_MODE_MEMORY | PGXP_MODE_GTE;
      else if (strcmp(var.value, "memory + CPU") == 0)
         psx_pgxp_mode = PGXP_MODE_MEMORY | PGXP_MODE_GTE | PGXP_MODE_CPU;
   }
   else
      psx_pgxp_mode = PGXP_MODE_NONE;

   var.key = BEETLE_OPT(pgxp_vertex);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         psx_pgxp_vertex_caching = PGXP_MODE_NONE;
      else if (strcmp(var.value, "enabled") == 0)
         psx_pgxp_vertex_caching = PGXP_VERTEX_CACHE;
   }
   else
      psx_pgxp_vertex_caching = PGXP_MODE_NONE;

   var.key = BEETLE_OPT(pgxp_texture);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         psx_pgxp_texture_correction = PGXP_MODE_NONE;
      else if (strcmp(var.value, "enabled") == 0)
         psx_pgxp_texture_correction = PGXP_TEXTURE_CORRECTION;
   }
   else
      psx_pgxp_texture_correction = PGXP_MODE_NONE;
   // \iCB

   var.key = BEETLE_OPT(line_render);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         line_render_mode = 0;
      else if (strcmp(var.value, "default") == 0)
         line_render_mode = 1;
      else if (strcmp(var.value, "aggressive") == 0)
         line_render_mode = 2;
   }

   var.key = BEETLE_OPT(filter);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int old_filter_mode = filter_mode;
      if (strcmp(var.value, "nearest") == 0)
         filter_mode = 0;
      else if (strcmp(var.value, "xBR") == 0)
         filter_mode = 1;
      else if (strcmp(var.value, "SABR") == 0)
         filter_mode = 2;
      else if (strcmp(var.value, "bilinear") == 0)
         filter_mode = 3;
      else if (strcmp(var.value, "3-point") == 0)
         filter_mode = 4;
      else if (strcmp(var.value, "JINC2") == 0)
         filter_mode = 5;

      if(filter_mode != old_filter_mode)
      {
         opaque_check = true;
         semitrans_check = true;
         old_filter_mode = filter_mode;
      }
   }

   var.key = BEETLE_OPT(analog_toggle);
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

   var.key = BEETLE_OPT(enable_multitap_port1);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         setting_psx_multitap_port_1 = true;
      else if (strcmp(var.value, "disabled") == 0)
         setting_psx_multitap_port_1 = false;
   }

   var.key = BEETLE_OPT(enable_multitap_port2);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         setting_psx_multitap_port_2 = true;
      else if (strcmp(var.value, "disabled") == 0)
         setting_psx_multitap_port_2 = false;
   }

   var.key = BEETLE_OPT(mouse_sensitivity);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      input_set_mouse_sensitivity(atoi(var.value));

   var.key = BEETLE_OPT(gun_cursor);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "off") == 0)
         input_set_gun_cursor(FrontIO::SETTING_GUN_CROSSHAIR_OFF);
      else if (strcmp(var.value, "cross") == 0)
         input_set_gun_cursor(FrontIO::SETTING_GUN_CROSSHAIR_CROSS);
      else if (strcmp(var.value, "dot") == 0)
         input_set_gun_cursor(FrontIO::SETTING_GUN_CROSSHAIR_DOT);
   }

   var.key = BEETLE_OPT(gun_input_mode);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "lightgun") == 0)
         gun_input_mode = SETTING_GUN_INPUT_LIGHTGUN;
      else if (strcmp(var.value, "touchscreen") == 0)
         gun_input_mode = SETTING_GUN_INPUT_POINTER;
   }
   else
      gun_input_mode = SETTING_GUN_INPUT_LIGHTGUN;

   var.key = BEETLE_OPT(negcon_deadzone);
   input_set_negcon_deadzone(0);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      input_set_negcon_deadzone((int)(atoi(var.value) * 0.01f * NEGCON_RANGE));
   }

   var.key = BEETLE_OPT(negcon_response);
   input_set_negcon_linearity(1);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "quadratic") == 0)
         input_set_negcon_linearity(2);
      else if (strcmp(var.value, "cubic") == 0)
         input_set_negcon_linearity(3);
   }

   // Initial scanline NTSC
   var.key = BEETLE_OPT(initial_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_scanline_value = atoi(var.value);
      if (setting_initial_scanline != new_scanline_value)
      {
         has_new_geometry = true;
         setting_initial_scanline = new_scanline_value;
      }
   }

   // Last scanline NTSC
   var.key = BEETLE_OPT(last_scanline);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_scanline_value = atoi(var.value);
      if (setting_last_scanline != new_scanline_value)
      {
         has_new_geometry = true;
         setting_last_scanline = new_scanline_value;
      }
   }

   // Initial scanline PAL
   var.key = BEETLE_OPT(initial_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_scanline_value = atoi(var.value);
      if (setting_initial_scanline_pal != new_scanline_value)
      {
         has_new_geometry = true;
         setting_initial_scanline_pal = new_scanline_value;
      }
   }

   // Last scanline PAL
   var.key = BEETLE_OPT(last_scanline_pal);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int new_scanline_value = atoi(var.value);
      if (setting_last_scanline_pal != new_scanline_value)
      {
         has_new_geometry = true;
         setting_last_scanline_pal = new_scanline_value;
      }
   }

   if(setting_psx_multitap_port_1 && setting_psx_multitap_port_2)
      input_set_player_count(8);
   else if (setting_psx_multitap_port_1 || setting_psx_multitap_port_2)
      input_set_player_count(5);
   else
      input_set_player_count(2);

   /* Memcards (startup only) */
   if (startup)
   {
      var.key = BEETLE_OPT(use_mednafen_memcard0_method);
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "libretro"))
            use_mednafen_memcard0_method = false;
         else if (!strcmp(var.value, "mednafen"))
            use_mednafen_memcard0_method = true;
      }

      var.key = BEETLE_OPT(enable_memcard1);
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "enabled"))
            enable_memcard1 = true;
         else if (!strcmp(var.value, "disabled"))
            enable_memcard1 = false;
      }

      var.key = BEETLE_OPT(shared_memory_cards);
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "enabled"))
         {
            if(use_mednafen_memcard0_method)
               shared_memorycards = true;
            else
               MDFN_DispMessage("Memory Card 0 Method not set to Mednafen; shared memory cards could not be enabled.");
         }
         else if (!strcmp(var.value, "disabled"))
         {
            shared_memorycards = false;
         }
      }
   }
   /* End Memcards */

   var.key = BEETLE_OPT(frame_duping);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
      {
         bool can_dupe = false;
         if (environ_cb(RETRO_ENVIRONMENT_GET_CAN_DUPE, &can_dupe))
            allow_frame_duping = can_dupe;
      }
      else if (strcmp(var.value, "disabled") == 0)
         allow_frame_duping = false;
   }
   else
      allow_frame_duping = false;

   var.key = BEETLE_OPT(display_internal_fps);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         display_internal_framerate = true;
      else if (strcmp(var.value, "disabled") == 0)
         display_internal_framerate = false;
   }
   else
      display_internal_framerate = false;

   var.key = BEETLE_OPT(crop_overscan);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
      {
         if (crop_overscan == false)
            has_new_geometry = true;
         crop_overscan = true;
      }
      else if (strcmp(var.value, "disabled") == 0)
      {
         if (crop_overscan == true)
            has_new_geometry = true;
         crop_overscan = false;
      }
   }

   var.key = BEETLE_OPT(image_offset);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         image_offset = 0;
      else
         image_offset = atoi(var.value);
   }

   var.key = BEETLE_OPT(image_crop);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         image_crop = 0;
      else
         image_crop = atoi(var.value);
   }

   var.key = BEETLE_OPT(cd_fastload);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      uint8_t val = var.value[0] - '0';
      if (var.value[1] != 'x')
      {
         val  = (var.value[0] - '0') * 10;
         val += var.value[1] - '0';
      }
      // Value is a multiplier from the native 2x, so we divide by two
      cd_2x_speedup = val / 2;
   }
   else
      cd_2x_speedup = 1;
}

#ifdef NEED_CD
static void ReadM3U(std::vector<std::string> &file_list, std::string path, unsigned depth = 0)
{
   std::string dir_path;
   char linebuf[2048];
   FILE *fp = fopen(path.c_str(), "rb");

   if (fp == NULL)
      return;

   MDFN_GetFilePathComponents(path, &dir_path);

   while(fgets(linebuf, sizeof(linebuf), fp) != NULL)
   {
      std::string efp;

      if(linebuf[0] == '#')
         continue;
      string_trim_whitespace_right(linebuf);
      if(linebuf[0] == 0)
         continue;

      efp = MDFN_EvalFIP(dir_path, std::string(linebuf));

      if(efp.size() >= 4 && efp.substr(efp.size() - 4) == ".m3u")
      {
         if(efp == path)
         {
            log_cb(RETRO_LOG_ERROR, "M3U at \"%s\" references self.\n", efp.c_str());
            goto end;
         }

         if(depth == 99)
         {
            log_cb(RETRO_LOG_ERROR, "M3U load recursion too deep!\n");
            goto end;
         }

         ReadM3U(file_list, efp, depth++);
      }
      else
         file_list.push_back(efp);
   }

end:
   fclose(fp);
}

// TODO: LoadCommon()

static MDFNGI *MDFNI_LoadCD(const char *devicename)
{
   uint8 LayoutMD5[16];

   log_cb(RETRO_LOG_INFO, "Loading %s...\n", devicename);

   try
   {
      if(devicename && strlen(devicename) > 4 && !strcasecmp(devicename + strlen(devicename) - 4, ".m3u"))
      {
         ReadM3U(disk_control_ext_info.image_paths, devicename);

         for(unsigned i = 0; i < disk_control_ext_info.image_paths.size(); i++)
         {
            char image_label[4096];
            bool success = true;

            image_label[0] = '\0';

#ifdef HAVE_CDROM_NEW
            CDIF *image  = CDIF_Open(
                  disk_control_ext_info.image_paths[i].c_str(), old_cdimagecache);
#else
            CDIF *image  = CDIF_Open(
                  &success, disk_control_ext_info.image_paths[i].c_str(), false, old_cdimagecache);
#endif
            CDInterfaces.push_back(image);

            extract_basename(
                  image_label, disk_control_ext_info.image_paths[i].c_str(),
                  sizeof(image_label));
            disk_control_ext_info.image_labels.push_back(image_label);
         }
      }
      else if(devicename && strlen(devicename) > 4 && !strcasecmp(devicename + strlen(devicename) - 4, ".pbp"))
      {
         bool success = true;
#ifdef HAVE_CDROM_NEW
         CDIF *image  = CDIF_Open(devicename, old_cdimagecache);
#else
         CDIF *image  = CDIF_Open(&success, devicename, false, old_cdimagecache);
#endif
         CD_IsPBP     = true;
         CDInterfaces.push_back(image);

         /* CDIF_Open() sets PBP_DiscCount, so we can populate
          * image_paths/image_labels here */
         PBP_PhysicalDiscCount = (PBP_DiscCount == 0) ?
               1 : PBP_DiscCount;

         for(unsigned i = 0; i < PBP_PhysicalDiscCount; i++)
         {
            char image_name[4096];
            char image_label[4096];

            image_name[0]  = '\0';
            image_label[0] = '\0';

            /* All 'disks' have the same path when using
             * multi-disk PBP files */
            disk_control_ext_info.image_paths.push_back(devicename);

            /* Label is name+index */
            extract_basename(image_name, devicename, sizeof(image_name));
            snprintf(image_label, sizeof(image_label), "%s #%u", image_name, i + 1);
            disk_control_ext_info.image_labels.push_back(image_label);
         }
      }
      else
      {
         char image_label[4096];
         bool success = true;

         image_label[0] = '\0';

#ifdef HAVE_CDROM_NEW
         CDIF *image  = CDIF_Open(devicename, old_cdimagecache);
#else
         CDIF *image  = CDIF_Open(&success, devicename, false, old_cdimagecache);
#endif
         if (!success)
            return(0);

         CDInterfaces.push_back(image);

         disk_control_ext_info.image_paths.push_back(devicename);
         extract_basename(image_label, devicename, sizeof(image_label));
         disk_control_ext_info.image_labels.push_back(image_label);
      }
   }
   catch(std::exception &e)
   {
      log_cb(RETRO_LOG_ERROR, "Error opening CD.\n");
      return(0);
   }

   // Print out a track list for all discs.
   for(unsigned i = 0; i < CDInterfaces.size(); i++)
   {
      TOC toc;
#ifndef HAVE_CDROM_NEW
      TOC_Clear(&toc);
#endif

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

      mednafen_md5_starts(&layout_md5);

      for(unsigned i = 0; i < CDInterfaces.size(); i++)
      {
         TOC toc;

#ifndef HAVE_CDROM_NEW
         TOC_Clear(&toc);
#endif
         CDInterfaces[i]->ReadTOC(&toc);

         mednafen_md5_update_u32_as_lsb(&layout_md5, toc.first_track);
         mednafen_md5_update_u32_as_lsb(&layout_md5, toc.last_track);
         mednafen_md5_update_u32_as_lsb(&layout_md5, toc.tracks[100].lba);

         for(uint32 track = toc.first_track; track <= toc.last_track; track++)
         {
            mednafen_md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].lba);
            mednafen_md5_update_u32_as_lsb(&layout_md5, toc.tracks[track].control & 0x4);
         }
      }

      mednafen_md5_finish(&layout_md5, LayoutMD5);
   }

   if (MDFNGameInfo == NULL)
   {
      MDFNGameInfo = &EmulatedPSX;
   }

   // TODO: include module name in hash
   memcpy(MDFNGameInfo->MD5, LayoutMD5, 16);

   if(!(LoadCD(&CDInterfaces)))
   {
      for(unsigned i = 0; i < CDInterfaces.size(); i++)
         delete CDInterfaces[i];
      CDInterfaces.clear();

      disk_control_ext_info.initial_index = 0;
      disk_control_ext_info.initial_path.clear();
      disk_control_ext_info.image_paths.clear();
      disk_control_ext_info.image_labels.clear();

      MDFNGameInfo = NULL;
      return NULL;
   }

   //MDFNI_SetLayerEnableMask(~0ULL);

   MDFN_LoadGameCheats(NULL);
   MDFNMP_InstallReadPatches();

   return(MDFNGameInfo);
}
#endif

static MDFNGI *MDFNI_LoadGame(const char *name)
{
   RFILE *GameFile = NULL;

   if(strlen(name) > 4 && (
      !strcasecmp(name + strlen(name) - 4, ".cue") ||
      !strcasecmp(name + strlen(name) - 4, ".ccd") ||
      !strcasecmp(name + strlen(name) - 4, ".toc") ||
      !strcasecmp(name + strlen(name) - 4, ".m3u") ||
      !strcasecmp(name + strlen(name) - 4, ".chd") ||
      !strcasecmp(name + strlen(name) - 4, ".pbp")
      ))
    return MDFNI_LoadCD(name);

   GameFile = filestream_open(name,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);

   if(!GameFile)
      goto error;

   if(Load(name, GameFile) <= 0)
      goto error;

   filestream_close(GameFile);
   GameFile   = NULL;

   return(MDFNGameInfo);

error:
   if (GameFile)
      filestream_close(GameFile);
   GameFile     = NULL;
   MDFNGameInfo = NULL;
   return NULL;
}

bool retro_load_game(const struct retro_game_info *info)
{
   char tocbasepath[4096];

   if (failed_init)
      return false;

   input_init_env(environ_cb);

   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
      return false;

   extract_basename(retro_cd_base_name,       info->path, sizeof(retro_cd_base_name));
   extract_directory(retro_cd_base_directory, info->path, sizeof(retro_cd_base_directory));

   int r = snprintf(tocbasepath, sizeof(tocbasepath), "%s%c%s.toc", retro_cd_base_directory, retro_slash, retro_cd_base_name);

   if (r >= 0 && r < 4096 && filestream_exists(tocbasepath))
      snprintf(retro_cd_path, sizeof(retro_cd_path), "%s", tocbasepath);
   else
      snprintf(retro_cd_path, sizeof(retro_cd_path), "%s", info->path);

   check_variables(true);

   if (!MDFNI_LoadGame(retro_cd_path))
   {
      failed_init = true;
      return false;
   }

   MDFN_LoadGameCheats(NULL);
   MDFNMP_InstallReadPatches();

   // Determine content_is_pal before calling alloc_surface()
   unsigned disc_region = CalcDiscSCEx();
   content_is_pal = (disc_region == REGION_EU);

   alloc_surface();

#ifdef NEED_DEINTERLACER
   PrevInterlaced = false;
   deint.ClearState();
#endif

   input_init();

   boot = false;

   frame_count = 0;
   internal_frame_count = 0;

   // MDFNI_LoadGame() has been called and surface has been allocated,
   // we can now perform firmware check
   bool force_software_renderer = false;
   if (!firmware_is_present(disc_region))
   {
      log_cb(RETRO_LOG_ERROR, "Content cannot be loaded\n");

      /* TODO - We're forcing the sw renderer to show the ugui error message. Figure out
      how to copy the ugui framebuffer to the hardware renderer side with rsx_intf calls,
      so we don't have to force this anymore. */
      force_software_renderer = true;

#ifdef HAVE_LIGHTREC
      /* Do not run lightrec if firmware is not found, recompiling garbage is bad*/
      psx_dynarec = DYNAREC_DISABLED;
#endif
   }

   bool ret = rsx_intf_open(content_is_pal, force_software_renderer);

   /* Hide irrelevant core options */
   switch (rsx_intf_is_type())
   {
      case RSX_SOFTWARE:
      {
         struct retro_core_option_display option_display;
         option_display.visible = false;

         option_display.key = BEETLE_OPT(renderer_software_fb);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(adaptive_smoothing);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(super_sampling);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(msaa);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(mdec_yuv);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(depth);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(wireframe);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(display_vram);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(filter);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(pgxp_vertex);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(pgxp_texture);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         option_display.key = BEETLE_OPT(image_offset_cycles);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }
      case RSX_OPENGL:
      {
         struct retro_core_option_display option_display;
         option_display.visible = false;

         option_display.key = BEETLE_OPT(adaptive_smoothing);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(super_sampling);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(msaa);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(mdec_yuv);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         option_display.key = BEETLE_OPT(image_offset);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(image_crop);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         option_display.key = BEETLE_OPT(frame_duping);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }
      case RSX_VULKAN:
      {
         struct retro_core_option_display option_display;
         option_display.visible = false;

         option_display.key = BEETLE_OPT(depth);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(wireframe);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         option_display.key = BEETLE_OPT(image_offset);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(image_crop);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }
   }

   /* Hide irrelevant scanline core options for current content */
   struct retro_core_option_display option_display;
   option_display.visible = false;
   if (content_is_pal)
   {
      option_display.key = BEETLE_OPT(initial_scanline);
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
      option_display.key = BEETLE_OPT(last_scanline);
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   }
   else
   {
      option_display.key = BEETLE_OPT(initial_scanline_pal);
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
      option_display.key = BEETLE_OPT(last_scanline_pal);
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
   }

   return ret;
}

void retro_unload_game(void)
{
   if(!MDFNGameInfo)
      return;

   rsx_intf_close();

   MDFN_FlushGameCheats(0);

   CloseGame();

   MDFNMP_Kill();

   MDFNGameInfo = NULL;

   for(unsigned i = 0; i < CDInterfaces.size(); i++)
      delete CDInterfaces[i];
   CDInterfaces.clear();

   disk_control_ext_info.initial_index = 0;
   disk_control_ext_info.initial_path.clear();
   disk_control_ext_info.image_paths.clear();
   disk_control_ext_info.image_labels.clear();

   retro_cd_base_directory[0] = '\0';
   retro_cd_path[0]           = '\0';
   retro_cd_base_name[0]      = '\0';
}

static uint64_t video_frames, audio_frames;
#define SOUND_CHANNELS 2

void retro_run(void)
{
   bool updated = false;
   //code to implement audio and video disable is not yet implemented
   //bool disableVideo = false;
   //bool disableAudio = false;
   //bool hardDisableAudio = false;
   //int flags = 3;
   //if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &flags))
   //{
   //   disableVideo = !(flags & 1);
   //   disableAudio = !(flags & 2);
   //   hardDisableAudio = !!(flags & 8);
   //}

   if (gui_show && gui_inited && frame_width > 0 && frame_height > 0)
   {
      gui_draw();
      video_cb(gui_get_framebuffer(), frame_width, frame_height, frame_width * sizeof(unsigned));
   }

   rsx_intf_prepare_frame();

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
      check_variables(false);
      struct retro_system_av_info new_av_info;

      /* Max width/height changed, need to call SET_SYSTEM_AV_INFO */
      if (GPU_get_upscale_shift() != psx_gpu_upscale_shift)
      {
         retro_get_system_av_info(&new_av_info);
         if (environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &new_av_info))
         {
            // We successfully changed the frontend's resolution, we can
            // apply the change immediately
            GPU_Rescale(psx_gpu_upscale_shift);
            alloc_surface();
            has_new_geometry = false;
         }
         else
         {
            // Failed, we have to postpone the upscaling change
            psx_gpu_upscale_shift = GPU_get_upscale_shift();
         }
      }

      /* Core timing option changed, need to call SET_SYSTEM_AV_INFO
       *
       * Note: May be possible to bundle this dirty flag with the other
       * dirty flags such as the one for widescreen hack and do a full
       * RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO callback; should be acceptable
       * to have video/audio reinits after changing core options
       */
      if (has_new_timing)
      {
         retro_get_system_av_info(&new_av_info);
         if (environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &new_av_info))
            has_new_timing = false;
      }

      /* Widescreen hack, scanlines, overscan cropping, or aspect ratio setting
         changed, need to call SET_GEOMETRY to change aspect ratio */
      if (has_new_geometry)
      {
         retro_get_system_av_info(&new_av_info);
         if (environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &new_av_info))
         {
            has_new_geometry = false;
         }
      }

      switch (psx_gpu_dither_mode)
      {
         case DITHER_NATIVE:
            GPU_set_dither_upscale_shift(psx_gpu_upscale_shift);
            break;
         case DITHER_UPSCALED:
            GPU_set_dither_upscale_shift(0);
            break;
         case DITHER_OFF:
            break;
      }

      GPU_set_visible_scanlines(MDFN_GetSettingI(content_is_pal ? "psx.slstartp" : "psx.slstart"),
                                MDFN_GetSettingI(content_is_pal ? "psx.slendp" : "psx.slend"));

      PGXP_SetModes(psx_pgxp_mode | psx_pgxp_vertex_caching | psx_pgxp_texture_correction);
   }

   /* We only start counting after the first frame we encounter. This
      way the value we display remains consistent if the real
      framerate is not a multiple of INTERNAL_FPS_SAMPLE_PERIOD
   */
   if (display_internal_framerate && internal_frame_count)
   {
      frame_count++;

      if (frame_count % INTERNAL_FPS_SAMPLE_PERIOD == 0)
      {
         char msg_buffer[64];
         // Just report the "real-world" refresh rate here regardless of system av info reported to the frontend
         float fps = (content_is_pal && !fast_pal) ?
                        (currently_interlaced ? FPS_PAL_INTERLACED : FPS_PAL_NONINTERLACED) :
                        (currently_interlaced ? FPS_NTSC_INTERLACED : FPS_NTSC_NONINTERLACED);
         float internal_fps = (internal_frame_count * fps) / INTERNAL_FPS_SAMPLE_PERIOD;

         snprintf(msg_buffer, sizeof(msg_buffer), _("Internal FPS: %.2f"), internal_fps);

         MDFN_DispMessage(msg_buffer);

         internal_frame_count = 0;
      }
   }
   else
   {
      // Keep the counters at 0 so that they don't display a bogus
      // value if this option is enabled later on
      frame_count = 0;
      internal_frame_count = 0;
   }

   if (setting_apply_analog_toggle)
   {
      PSX_FIO->SetAMCT(setting_psx_analog_toggle);
      setting_apply_analog_toggle = false;
   }

   input_poll_cb();

   input_update(libretro_supports_bitmasks, input_state_cb );

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

   //if (disableVideo)
   //{
   //   spec.skip = true;
   //}

   EmulateSpecStruct *espec = (EmulateSpecStruct*)&spec;
   /* start of Emulate */
   int32_t timestamp = 0;

   espec->skip = false;

   MDFNMP_ApplyPeriodicCheats();


   espec->MasterCycles = 0;
   espec->SoundBufSize = 0;

   PSX_FIO->UpdateInput();
   GPU_StartFrame(espec);

   Running = -1;
   timestamp = PSX_CPU->Run(timestamp, false, false);

   assert(timestamp);

   ForceEventUpdates(timestamp);
#if 0
   if(GPU_GetScanlineNum() < 100)
      PSX_DBG(PSX_DBG_ERROR, "[BUUUUUUUG] Frame timing end glitch; scanline=%u, st=%u\n", GPU_GetScanlineNum(), timestamp);
#endif

   //printf("scanline=%u, st=%u\n", GPU_GetScanlineNum(), timestamp);

   espec->SoundBufSize = IntermediateBufferPos;
   IntermediateBufferPos = 0;

   PSX_CDC->ResetTS();
   TIMER_ResetTS();
   DMA_ResetTS();
   GPU_ResetTS();
   PSX_FIO->ResetTS();

   RebaseTS(timestamp);

   espec->MasterCycles = timestamp;

   // Save memcards if dirty.
   unsigned players = input_get_player_count();
   for(int i = 0; i < players; i++)
   {
      uint64_t new_dc = PSX_FIO->GetMemcardDirtyCount(i);

      if(new_dc > Memcard_PrevDC[i])
      {
         Memcard_PrevDC[i] = new_dc;
         Memcard_SaveDelay[i] = 0;
      }

      if(Memcard_SaveDelay[i] >= 0)
      {
         Memcard_SaveDelay[i] += timestamp;
         if(Memcard_SaveDelay[i] >= (33868800 * 2))   // Wait until about 2 seconds of no new writes.
         {
            char ext[64];
            const char *memcard = NULL;

            log_cb(RETRO_LOG_INFO, "Saving memcard %d...\n", i);

            if (i == 0 && !use_mednafen_memcard0_method)
            {
               PSX_FIO->SaveMemcard(i);
               Memcard_SaveDelay[i] = -1;
               Memcard_PrevDC[i] = 0;
               continue;
            }

            snprintf(ext, sizeof(ext), "%d.mcr", i);
            memcard = MDFN_MakeFName(MDFNMKF_SAV, 0, ext);
            PSX_FIO->SaveMemcard(i, memcard);
            Memcard_SaveDelay[i] = -1;
            Memcard_PrevDC[i] = 0;
         }
      }
   }

   /* end of Emulate */

   // Check if aspect ratio needs to be changed due to display mode change on this frame
   if (MDFN_UNLIKELY((aspect_ratio_setting == 1) && aspect_ratio_dirty))
   {
      struct retro_system_av_info new_av_info;
      retro_get_system_av_info(&new_av_info);

      if (environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &new_av_info.geometry))
         aspect_ratio_dirty = false;

      // If unable to change geometry here, defer to next frame and leave aspect_ratio_dirty flagged
   }

   // Check if timing needs to be changed due to interlacing change on this frame
   // May be possible to track interlacing via espec instead of via RSX?
   if (MDFN_UNLIKELY((core_timing_fps_mode == AUTO_TOGGLE_TIMING) && interlace_setting_dirty))
   {
      // This may cause video and audio reinit on the frontend, so it may be preferable to
      // set the core option to force progressive or interlaced timings
      struct retro_system_av_info new_av_info;
      retro_get_system_av_info(&new_av_info);

      if (environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &new_av_info))
         interlace_setting_dirty = false;

      // If unable to change AV info here, defer to next frame and leave interlace_setting_dirty flagged
   }

   const void *fb        = NULL;
   unsigned width        = rects[0];
   unsigned height       = spec.DisplayRect.h;
   uint8_t upscale_shift = GPU_get_upscale_shift();

   if (rsx_intf_is_type() == RSX_SOFTWARE)
   {
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
      // PSX is rather special, and needs specific handling ...

      width = rects[0]; // spec.DisplayRect.w is 0. Only rects[0].w seems to return something sane.
      height = spec.DisplayRect.h;
      //fprintf(stderr, "(%u x %u)\n", width, height);

      // PSX core inserts padding on left and right (overscan). Optionally crop this.
      const uint32_t *pix = surf->pixels;
      unsigned pix_offset = 0;

      if (crop_overscan)
      {
         // Crop total # of pixels output by PSX in active scanline region down to # of pixels in corresponding horizontal display mode
         // 280 width -> 256 width.
         // 350 width -> 320 width.
         // 400 width -> 366 width.
         // 560 width -> 512 width.
         // 700 width -> 640 width.
         switch (width)
         {
            case 280:
               pix_offset += 12 - image_offset + floor(0.5 * image_crop);
               width = 256 - image_crop;
               break;

            case 350:
               pix_offset += 15 - image_offset + floor(0.5 * image_crop);
               width = 320 - image_crop;
               break;

            /* 368px mode. Some games are overcropped at 364 width or undercropped at 368 width, so crop to 366.
               Adjust in future if there are issues. */
            case 400:
               pix_offset += 17 - image_offset + floor(0.5 * image_crop);
               width = 366 - image_crop;
               break;

            case 560:
               pix_offset += 24 - image_offset + floor(0.5 * image_crop);
               width = 512 - image_crop;
               break;

            case 700:
               pix_offset += 30 - image_offset + floor(0.5 * image_crop);
               width = 640 - image_crop;
               break;

            default:
               // This shouldn't happen.
               break;
         }
      }


      width  <<= upscale_shift;
      height <<= upscale_shift;
      pix     += pix_offset << upscale_shift;

      if (GPU_get_display_possibly_dirty() || (GPU_get_display_change_count() != 0) || !allow_frame_duping)
         fb = pix;
   }

   int16_t *interbuf = (int16_t*)&IntermediateBuffer;

   if (gui_show)
   {
      if (!gui_inited)
      {
         frame_width = width;
         frame_height = height;

         gui_init(frame_width, frame_height, sizeof(unsigned));
         gui_set_window_title("Error");
         gui_inited = true;
      }

      if (width != frame_width || height != frame_height)
      {
        frame_width = width;
        frame_height = height;
        gui_window_resize(0, 0, frame_width, frame_height);
      }
   }
   else
   {
      rsx_intf_finalize_frame(fb, width, height,
            MEDNAFEN_CORE_GEOMETRY_MAX_W << (2 + upscale_shift));
   }

   video_frames++;
   audio_frames += spec.SoundBufSize;

   audio_batch_cb(interbuf, spec.SoundBufSize);

   if (GPU_get_display_possibly_dirty() || (GPU_get_display_change_count() != 0))
   {
      internal_frame_count++;
      GPU_set_display_change_count(0);
      GPU_set_display_possibly_dirty(false);
   }
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = MEDNAFEN_CORE_NAME;
#ifdef GIT_VERSION
   info->library_version  = MEDNAFEN_CORE_VERSION GIT_VERSION;
#else
   info->library_version  = MEDNAFEN_CORE_VERSION;
#endif
   info->need_fullpath    = true;
   info->valid_extensions = MEDNAFEN_CORE_EXTENSIONS;
   info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   rsx_intf_get_system_av_info(info);
}

void retro_deinit(void)
{
   delete surf;
   surf = NULL;

   log_cb(RETRO_LOG_INFO, "[%s]: Samples / Frame: %.5f\n",
         MEDNAFEN_CORE_NAME, (double)audio_frames / video_frames);
   log_cb(RETRO_LOG_INFO, "[%s]: Estimated FPS: %.5f\n",
         MEDNAFEN_CORE_NAME, (double)video_frames * 44100 / audio_frames);

   libretro_supports_bitmasks = false;
}

unsigned retro_get_region(void)
{
   // simias: should I override this when fast_pal is set?
   //
   // I'm not entirely sure what's that used for.
   return content_is_pal ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

#include "libretro_core_options.h"

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;
   environ_cb = cb;

   libretro_set_core_options(environ_cb);

   vfs_iface_info.required_interface_version = 1;
   vfs_iface_info.iface                      = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
	   filestream_vfs_init(&vfs_iface_info);

	input_set_env( cb );

   rsx_intf_set_environment(cb);
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

   rsx_intf_set_video_refresh(cb);
}

static size_t serialize_size;

size_t retro_serialize_size(void)
{
   if (enable_variable_serialization_size)
   {
      StateMem st;

      st.data           = NULL;
      st.loc            = 0;
      st.len            = 0;
      st.malloced       = 0;
      st.initial_malloc = 0;

      if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL))
         return 0;

      free(st.data);
      return serialize_size = st.len;
   }

   return serialize_size = DEFAULT_STATE_SIZE; // 16MB
}

bool UsingFastSavestates()
{
   int flags;
   if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &flags))
   {
      return flags & 4;
   }
   return false;
}

bool retro_serialize(void *data, size_t size)
{
   if (size == DEFAULT_STATE_SIZE)  //16MB buffer reserved
   {
      //actual size is around 3.75MB (3.67MB for fast savestates) rather than 16MB, so 16MB will hold a savestate without worrying about realloc

      //save state in place
      StateMem st;
      st.data = (uint8_t*)data;
      st.len = 0;
      st.loc = 0;
      st.malloced = size;
      st.initial_malloc = 0;

      //fast save states are at least 20% faster
      FastSaveStates = UsingFastSavestates();
      bool ret = MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);
      FastSaveStates = false;
      return ret;
   }
   else
   {
      /* it seems that mednafen can realloc pointers sent to it?
         since we don't know the disposition of void* data (is it safe to realloc?) we have to manage a new buffer here */
      static bool logged;
      StateMem st;
      bool ret = false;
      uint8_t *_dat = (uint8_t*)malloc(size);

      if (!_dat)
         return false;

      st.data = _dat;
      st.loc = 0;
      st.len = 0;
      st.malloced = size;
      st.initial_malloc = 0;

      /* there are still some errors with the save states,
       * the size seems to change on some games for now
       * just log when this happens */
      if (!logged && st.len != size)
      {
         log_cb(RETRO_LOG_WARN, "warning, save state size has changed\n");
         logged = true;
      }

      FastSaveStates = UsingFastSavestates();
      ret = MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);
      FastSaveStates = false;

      memcpy(data, st.data, size);
      free(st.data);
      return ret;
   }
}

bool retro_unserialize(const void *data, size_t size)
{
   StateMem st;

   st.data           = (uint8_t*)data;
   st.loc            = 0;
   st.len            = size;
   st.malloced       = 0;
   st.initial_malloc = 0;

   //fast save states are at least 20% faster
   FastSaveStates = UsingFastSavestates();
   bool okay = MDFNSS_LoadSM(&st, 0, 0);
   FastSaveStates = false;
   return okay;
}

void *retro_get_memory_data(unsigned type)
{
   uint8_t *data;

   switch (type)
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return MainRAM->data8;
      case RETRO_MEMORY_SAVE_RAM:
         if (use_mednafen_memcard0_method)
            return NULL;
         return PSX_FIO->GetMemcardDevice(0)->GetNVData();
      default:
         break;
   }
   return NULL;
}

size_t retro_get_memory_size(unsigned type)
{
   switch (type)
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return 0x200000;
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
{
   MDFN_FlushGameCheats(1);
}

void retro_cheat_set(unsigned index, bool enabled, const char * codeLine)
{
   const CheatFormatStruct* cf = CheatFormats;
   char name[256];
   std::vector<std::string> codeParts;
   int matchLength=0;
   int cursor;
   std::string part;

   if (codeLine==NULL) return;

   //Break the code into Parts
   for (cursor=0;;cursor++)
   {
      if (ISHEXDEC)
      {
         matchLength++;
      } else {
         if (matchLength)
         {
            part=codeLine+cursor-matchLength;
            part.erase(matchLength,std::string::npos);
            codeParts.push_back(part);
            matchLength=0;
         }
      }
      if (!codeLine[cursor])
      {
         break;
      }
   }

   MemoryPatch patch;
   bool trueML=0;
   for (cursor=0;cursor<codeParts.size();cursor++)
   {
      part=codeParts[cursor];
      if (part.length()==8)
      {
         part+=codeParts[++cursor];
      }
      if (part.length()==12)
      {
         //Decode the cheat
         try
         {
            if(!cf->DecodeCheat(std::string(part), &patch))
            {
               //Generate a name
               sprintf(name,"cheat_%i_%i",index,cursor);

               //Set parameters
               patch.name=(std::string)name;
               patch.status=enabled;

               MDFNI_AddCheat(patch);
               patch=MemoryPatch();
            }
         }
         catch(std::exception &e)
         {
             continue;
         }
      }
   }
}

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
const char *MDFN_MakeFName(MakeFName_Type type, int id1, const char *cd1)
{
   static char fullpath[4096];
   int r = 0;

   fullpath[0] = '\0';

   switch (type)
   {
      case MDFNMKF_SAV:
         r = snprintf(fullpath, sizeof(fullpath), "%s%c%s.%s",
               retro_save_directory,
               retro_slash,
               shared_memorycards ? "mednafen_psx_libretro_shared" : retro_cd_base_name,
               cd1);
         break;
      case MDFNMKF_FIRMWARE:
         r = snprintf(fullpath, sizeof(fullpath), "%s%c%s", retro_base_directory, retro_slash, cd1);
         break;
      default:
         break;
   }

   if (r > 4095)
   {
      log_cb(RETRO_LOG_ERROR,"MakeFName path longer than 4095\n");
      fullpath[4095] = '\0';
   }

   return fullpath;
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
   va_list ap;
   struct retro_message msg;
   const char *strc = NULL;
   char *str        = (char*)malloc(4096 * sizeof(char));

   va_start(ap,format);

   vsnprintf(str, 4096, format, ap);
   va_end(ap);
   strc       = str;

   msg.frames = 180;
   msg.msg    = strc;

   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
   free(str);
}
