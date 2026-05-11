#include "mednafen/mednafen.h"
#include "mednafen/mempatcher.h"
#include "mednafen/git.h"
#include "mednafen/general_c.h"
#include "mednafen/settings.h"
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
#include "rsx/rsx_intf.h"
#include "libretro_cbs.h"
#include "beetle_psx_globals.h"
#include "libretro_options.h"
#include "input.h"
#include "osd_message.h"

#include "parallel-psx/custom-textures/dbg_input_callback.h"
retro_input_state_t dbg_input_state_cb = 0;

#include "mednafen/mednafen-endian.h"
#include "mednafen/mednafen-types.h"
#include "mednafen/psx/psx.h"
#include "mednafen/error.h"

#include "pgxp/pgxp_main.h"

#include "deps/openbios/openbios.bin.h"

#if defined(HAVE_ASHMEM) || defined(HAVE_SHM)
#include <errno.h>
#endif
#include <stdlib.h>
#include <string.h>
#define ISHEXDEC ((codeLine[cursor]>='0') && (codeLine[cursor]<='9')) || ((codeLine[cursor]>='a') && (codeLine[cursor]<='f')) || ((codeLine[cursor]>='A') && (codeLine[cursor]<='F'))

#ifdef HAVE_LIGHTREC
#include <sys/mman.h>

#ifdef HAVE_ASHMEM
#include <sys/ioctl.h>
#include <linux/ashmem.h>
#include <dlfcn.h>
#endif

#if defined(HAVE_SHM) || defined(HAVE_ASHMEM)
#include <sys/stat.h>
#include <fcntl.h>
#endif

#ifdef HAVE_WIN_SHM
#include <windows.h>
#endif
#endif /* HAVE_LIGHTREC */

#if __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <mach/shared_region.h>
#include <sys/attr.h>
#define __MACOS__ 1
#define MACOS_VM_BASE (SHARED_REGION_BASE+SHARED_REGION_SIZE+ATTR_VOL_RESERVED_SIZE)
#endif
#endif

//Fast Save States exclude string labels from variables in the savestate, and are at least 20% faster.
extern bool FastSaveStates;

const int DEFAULT_STATE_SIZE = 16 * 1024 * 1024;

static bool libretro_supports_option_categories = false;
static bool libretro_supports_bitmasks = false;
static unsigned libretro_msg_interface_version = 0;

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
static bool display_notifications = true;
static bool allow_frame_duping = false;
static unsigned image_offset = 0;
static unsigned image_crop = 0;
static bool enable_memcard1 = false;
static bool enable_variable_serialization_size = false;
static int frame_width = 0;
static int frame_height = 0;
static char bios_path[4096];
static bool firmware_found = false;

// Switchable memory cards
static int memcard_left_index = 0;
static int memcard_left_index_old;
static int memcard_right_index = 1;
static int memcard_right_index_old;

unsigned cd_2x_speedup = 1;
/* Compatibility cap on cd_2x_speedup for known-fragile titles.  Set
 * once during disc identification (see the cd_speedup_compat_table
 * in CalcDiscSCEx_BySYSTEMCNF), 0 means "no cap" (i.e. honour the
 * full user-selected CD Loading Speed setting).  Applied in
 * check_variables after the user-facing option is parsed. */
static unsigned cd_speedup_compat_max = 0;
bool cd_async = false;
bool cd_warned_slow = false;
int64 cd_slow_timeout = 8000; // microseconds

// If true, PAL games will run at 60fps
bool fast_pal = false;
unsigned image_height = 0;

#ifdef HAVE_LIGHTREC
enum DYNAREC psx_dynarec;
bool         psx_dynarec_invalidate;
uint8        psx_mmap = 0;
uint8 *psx_mem = NULL;
uint8 *psx_bios = NULL;
uint8 *psx_scratch = NULL;
#if defined(HAVE_ASHMEM)
int memfd;
#endif
#endif

int32 EventCycles = 128;
uint8_t spu_samples = 1;

// CPU overclock factor (or 0 if disabled)
int32_t psx_overclock_factor = 0;
// GPU rasterizer overclock shift
unsigned psx_gpu_overclock_shift = 0;

// Sets how often (in number of output frames/retro_run invocations)
// the internal framerace counter should be updated if
// display_internal_framerate is true.
#define INTERNAL_FPS_SAMPLE_PERIOD 64

static int psx_skipbios;
static int override_bios;

bool psx_gte_overclock;
enum dither_mode psx_gpu_dither_mode;

//iCB: PGXP options
unsigned int psx_pgxp_mode;
int psx_pgxp_2d_tol;
unsigned int psx_pgxp_vertex_caching;
unsigned int psx_pgxp_texture_correction;
unsigned int psx_pgxp_nclip;
// \iCB

#define NEGCON_RANGE 0x7FFF

char retro_save_directory[4096];
char retro_base_directory[4096];
char retro_cd_base_directory[4096];
static char retro_cd_path[4096];
char retro_cd_base_name[4096];
#ifdef _WIN32
   static const char retro_slash = '\\';
#else
   static const char retro_slash = '/';
#endif

enum
{
   REGION_JP = 0,
   REGION_NA = 1,
   REGION_EU = 2,
};

static bool firmware_is_present(unsigned region)
{
   /* C90 requires array sizes to be integer constant expressions; a
    * `static const size_t list_size = 16` is not one, so spell it
    * with an enum. */
   enum { list_size = 16 };
   const char *bios_name_list[list_size] = {0};
   const char *bios_sha1 = NULL;
   char        obtained_sha1[41];
   size_t      i;
   int         r;

   log_cb(RETRO_LOG_INFO, "Checking if required firmware is present...\n");

   /* SHA1 and alternate BIOS names sourced from
   https://github.com/mamedev/mame/blob/master/src/mame/drivers/psx.cpp */


   if (override_bios)
   {
      if (override_bios == 1)
      {
        bios_name_list[0] = "psxonpsp660.bin";
        bios_name_list[1] = "PSXONPSP660.bin";
        bios_name_list[2] = "PSXONPSP660.BIN";
        bios_name_list[3] = NULL;
        bios_sha1 = "96880D1CA92A016FF054BE5159BB06FE03CB4E14";
      }
      else if (override_bios == 2)
      {
        bios_name_list[0] = "ps1_rom.bin";
        bios_name_list[1] = "PS1_ROM.bin";
        bios_name_list[2] = "PS1_ROM.BIN";
        bios_name_list[3] = NULL;
        bios_sha1 = "C40146361EB8CF670B19FDC9759190257803CAB7";
      }
      else if (override_bios == 3)
      {
        bios_name_list[0] = "openbios.bin";
        bios_name_list[1] = "OPENBIOS.bin";
        bios_name_list[2] = "OPENBIOS.BIN";
        bios_name_list[3] = NULL;
        bios_sha1 = NULL;
      }

      for (i = 0; i < list_size; ++i)
      {
         if (!bios_name_list[i])
            break;

         r = snprintf(bios_path, sizeof(bios_path), "%s%c%s", retro_base_directory, retro_slash, bios_name_list[i]);
         if (r >= 4096)
         {
            bios_path[4095] = '\0';
            log_cb(RETRO_LOG_ERROR, "Firmware path longer than 4095: %s\n", bios_path);
            break;
         }

         if (filestream_exists(bios_path))
         {
            firmware_found = true;
            break;
         }
      }

      if (firmware_found)
      {
         sha1_calculate(bios_path, obtained_sha1);
         if (bios_sha1 && strcmp(obtained_sha1, bios_sha1))
         {
            log_cb(RETRO_LOG_WARN, "Override firmware found but has invalid SHA1: %s\n", bios_path);
            log_cb(RETRO_LOG_WARN, "Expected SHA1: %s\n", bios_sha1);
            log_cb(RETRO_LOG_WARN, "Obtained SHA1: %s\n", obtained_sha1);
            log_cb(RETRO_LOG_WARN, "Unsupported firmware may cause emulation glitches.\n");
            return true;
         }

         log_cb(RETRO_LOG_INFO, "Override firmware found: %s\n", bios_path);
         log_cb(RETRO_LOG_INFO, "Override firmware SHA1: %s\n", obtained_sha1);

         return true;
      }
      log_cb(RETRO_LOG_WARN, "Override firmware is missing: %s\n", bios_name_list[0]);
      log_cb(RETRO_LOG_WARN, "Fallback to region specific firmware.\n");
   }


   if (region == REGION_JP)
   {
      bios_name_list[0] = "scph5500.bin";
      bios_name_list[1] = "SCPH5500.bin";
      bios_name_list[2] = "SCPH5500.BIN";
      bios_name_list[3] = "SCPH-5500.bin";
      bios_name_list[4] = "SCPH-5500.BIN";
      bios_name_list[5] = NULL;
      bios_sha1 = "B05DEF971D8EC59F346F2D9AC21FB742E3EB6917";
   }
   else if (region == REGION_NA)
   {
      bios_name_list[ 0] = "scph5501.bin";
      bios_name_list[ 1] = "SCPH5501.bin";
      bios_name_list[ 2] = "SCPH5501.BIN";
      bios_name_list[ 3] = "SCPH-5501.bin";
      bios_name_list[ 4] = "SCPH-5501.BIN";
      bios_name_list[ 5] = "scph5503.bin";
      bios_name_list[ 6] = "SCPH5503.bin";
      bios_name_list[ 7] = "SCPH5503.BIN";
      bios_name_list[ 8] = "SCPH-5503.bin";
      bios_name_list[ 9] = "SCPH-5503.BIN";
      bios_name_list[10] = "scph7003.bin";
      bios_name_list[11] = "SCPH7003.bin";
      bios_name_list[12] = "SCPH7003.BIN";
      bios_name_list[13] = "SCPH-7003.bin";
      bios_name_list[14] = "SCPH-7003.BIN";
      bios_name_list[15] = NULL;
      bios_sha1 = "0555C6FAE8906F3F09BAF5988F00E55F88E9F30B";
   }
   else if (region == REGION_EU)
   {
      bios_name_list[ 0] = "scph5502.bin";
      bios_name_list[ 1] = "SCPH5502.bin";
      bios_name_list[ 2] = "SCPH5502.BIN";
      bios_name_list[ 3] = "SCPH-5502.bin";
      bios_name_list[ 4] = "SCPH-5502.BIN";
      bios_name_list[ 5] = "scph5552.bin";
      bios_name_list[ 6] = "SCPH5552.bin";
      bios_name_list[ 7] = "SCPH5552.BIN";
      bios_name_list[ 8] = "SCPH-5552.bin";
      bios_name_list[ 9] = "SCPH-5552.BIN";
      bios_name_list[10] = NULL;
      bios_sha1 = "F6BC2D1F5EB6593DE7D089C425AC681D6FFFD3F0";
   }

   for (i = 0; i < list_size; ++i)
   {
      if (!bios_name_list[i])
         break;

      r = snprintf(bios_path, sizeof(bios_path), "%s%c%s", retro_base_directory, retro_slash, bios_name_list[i]);
      if (r >= 4096)
      {
         bios_path[4095] = '\0';
         log_cb(RETRO_LOG_ERROR, "Firmware path longer than 4095: %s\n", bios_path);
         break;
      }

      if (filestream_exists(bios_path))
      {
         firmware_found = true;
         break;
      }
   }

   if (!firmware_found)
   {
      log_cb(RETRO_LOG_INFO, "Firmware is missing: %s\n", bios_name_list[0]);
      return false;
   }

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
   char       *ext;
   if (!base)
      base = strrchr(path, '\\');
   if (!base)
      base = path;

   if (*base == '\\' || *base == '/')
      base++;

   strncpy(buf, base, size - strlen(buf) - 1);
   buf[size - 1] = '\0';

   ext = strrchr(buf, '.');
   if (ext)
      *ext = '\0';
}

static void extract_directory(char *buf, const char *path, size_t size)
{
   char *base;
   strncpy(buf, path, size - 1);
   buf[size - 1] = '\0';

   base = strrchr(buf, '/');
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
#include "mednafen/psx/spu_c.h"
#include "mednafen/mempatcher.h"

#include <stdarg.h>
#include <ctype.h>

bool setting_apply_analog_toggle  = false;
bool setting_apply_analog_default = false;
bool use_mednafen_memcard0_method = false;

/* Based off(but not the same as) public-domain "JKISS" PRNG. */
struct MDFN_PseudoRNG
{
   uint32_t x,y,z,c;
   uint64_t lcgo;
};

static struct MDFN_PseudoRNG PSX_PRNG;

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

/* CDIF storage.
 *
 * Used to be three globals:
 *   static std::vector<CDIF *> CDInterfaces;     // owner
 *   static std::vector<CDIF *> *cdifs = NULL;    // alias set by InitCommon
 *   static std::vector<const char *> cdifs_scex_ids; // parallel SCEx ids
 *
 * They're folded into one struct that owns the CDIF and SCEx id
 * pairs and a flag to indicate whether the emulator side has the
 * array "live" (post-InitCommon, pre-CloseGame).  The old
 * "if (cdifs)" test for liveness becomes "if (cdifs_loaded)".
 *
 * The FIXME on the original CDInterfaces ("Cleanup on error out") is
 * addressed by routing every error path through cdif_array_clear,
 * which calls CDIF_Close on each entry rather than the previous
 * "delete" (which silently skipped CDIF's thread/mutex teardown,
 * leaking the read-thread on every failed multi-disc M3U load).
 */
struct cdif_array
{
   CDIF        **items;
   const char  **scex_ids;
   size_t        count;
   size_t        cap;
};

static struct cdif_array cdifs;
static int               cdifs_loaded;

static int cdif_array_reserve(struct cdif_array *a, size_t need)
{
   size_t       new_cap;
   CDIF        **new_items;
   const char  **new_ids;

   if (a->cap >= need)
      return 0;

   new_cap = a->cap ? a->cap * 2 : 8;
   while (new_cap < need)
      new_cap *= 2;

   new_items = (CDIF **)      realloc(a->items,    new_cap * sizeof(*new_items));
   if (!new_items)
      return -1;
   a->items = new_items;

   new_ids   = (const char **)realloc(a->scex_ids, new_cap * sizeof(*new_ids));
   if (!new_ids)
      return -1;
   a->scex_ids = new_ids;

   a->cap = new_cap;
   return 0;
}

static int cdif_array_push(struct cdif_array *a, CDIF *cdif)
{
   if (cdif_array_reserve(a, a->count + 1) < 0)
      return -1;
   a->items   [a->count] = cdif;
   a->scex_ids[a->count] = NULL;
   a->count++;
   return 0;
}

static void cdif_array_remove_at(struct cdif_array *a, size_t idx)
{
   size_t i;
   if (idx >= a->count)
      return;
   for (i = idx + 1; i < a->count; i++)
   {
      a->items   [i - 1] = a->items   [i];
      a->scex_ids[i - 1] = a->scex_ids[i];
   }
   a->count--;
}

/* Close every CDIF, free the backing arrays, reset to empty.
 *
 * Uses CDIF_Close - which does thread/mutex/cond teardown - rather
 * than `delete`, which on the post-cdromif-C struct CDIF would just
 * free the bytes and leak the read thread + sync primitives. */
static void cdif_array_clear(struct cdif_array *a)
{
   size_t i;
   for (i = 0; i < a->count; i++)
   {
      if (a->items[i])
         CDIF_Close(a->items[i]);
   }
   free(a->items);
   free(a->scex_ids);
   a->items    = NULL;
   a->scex_ids = NULL;
   a->count    = 0;
   a->cap      = 0;
}

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

/* Dynamic array of C strings.  Used for the disk-control image
 * paths/labels lists and the M3U file list - all places where the
 * C++ code carried std::vector<std::string>.  count is the number
 * of strings, cap is the allocated slot count; strings are
 * strdup-ed and freed individually by sv_clear. */
typedef struct
{
   char  **items;
   size_t  count;
   size_t  cap;
} string_vec_t;

static void sv_clear(string_vec_t *v)
{
   size_t i;
   for (i = 0; i < v->count; i++)
      free(v->items[i]);
   free(v->items);
   v->items = NULL;
   v->count = 0;
   v->cap   = 0;
}

static void sv_push(string_vec_t *v, const char *s)
{
   if (v->count == v->cap)
   {
      size_t new_cap = v->cap ? v->cap * 2 : 8;
      char **new_items = (char **)realloc(v->items, new_cap * sizeof(char *));
      if (!new_items) return;
      v->items = new_items;
      v->cap   = new_cap;
   }
   v->items[v->count++] = strdup(s ? s : "");
}

static void sv_set(string_vec_t *v, size_t idx, const char *s)
{
   if (idx >= v->count) return;
   free(v->items[idx]);
   v->items[idx] = strdup(s ? s : "");
}

static void sv_erase(string_vec_t *v, size_t idx)
{
   if (idx >= v->count) return;
   free(v->items[idx]);
   if (idx < v->count - 1)
      memmove(&v->items[idx], &v->items[idx + 1],
              (v->count - idx - 1) * sizeof(char *));
   v->count--;
}

typedef struct
{
   unsigned     initial_index;
   char        *initial_path;
   string_vec_t image_paths;
   string_vec_t image_labels;
} disk_control_ext_info_t;

static disk_control_ext_info_t disk_control_ext_info;

static uint64_t Memcard_PrevDC[8];
static int64_t Memcard_SaveDelay[8];

/* PSX_CPU lives at file scope in cpu.c (always points at &s_cpu). */
PS_CDC *PSX_CDC = NULL;
FrontIO *PSX_FIO = NULL;

MultiAccessSizeMem *BIOSROM = NULL;
MultiAccessSizeMem *PIOMem = NULL;
MultiAccessSizeMem *MainRAM = NULL;
MultiAccessSizeMem *ScratchRAM = NULL;

/* Helper inline funcs that dispatch reads/writes to a
 * MultiAccessSizeMem instance by size at runtime.  Replaces the
 * Read<T> / Write<T> templated member functions; size is always
 * a literal constant at the call site so the optimizer folds the
 * switch away. */
static INLINE uint32_t MASMEM_Read_size(MultiAccessSizeMem *mem, uint32_t address, unsigned size)
{
   if (size == 4)
      return MASMEM_ReadU32(mem, address);
   if (size == 2)
      return MASMEM_ReadU16(mem, address);
   return MASMEM_ReadU8(mem, address);
}

static INLINE void MASMEM_Write_size(MultiAccessSizeMem *mem, uint32_t address, uint32_t value, unsigned size)
{
   if (size == 4)
      MASMEM_WriteU32(mem, address, value);
   else if (size == 2)
      MASMEM_WriteU16(mem, address, value);
   else
      MASMEM_WriteU8(mem, address, value);
}

/*
 * C-linkage accessors for MainRAM declared in mednafen/psx/psx_mem.h.
 * MainRAM itself is a MultiAccessSizeMem<> template instance and so
 * isn't reachable from a C TU; these forwarders let C consumers
 * (dma.c, ...) access the same byte-level read/write helpers
 * without going through psx.h's class declarations.
 *
 * Inline away to a single load/store under -O2 / LTO.
 */
uint32_t MainRAM_ReadU32(uint32_t address)
{
   return MASMEM_ReadU32(MainRAM, address);
}

void MainRAM_WriteU32(uint32_t address, uint32_t value)
{
   MASMEM_WriteU32(MainRAM, address, value);
}

uint8_t  ScratchRAM_ReadU8 (uint32_t address) { return MASMEM_ReadU8(ScratchRAM, address); }
uint16_t ScratchRAM_ReadU16(uint32_t address) { return MASMEM_ReadU16(ScratchRAM, address); }
uint32_t ScratchRAM_ReadU24(uint32_t address) { return MASMEM_ReadU24(ScratchRAM, address); }
uint32_t ScratchRAM_ReadU32(uint32_t address) { return MASMEM_ReadU32(ScratchRAM, address); }
void     ScratchRAM_WriteU8 (uint32_t address, uint8_t  value) { MASMEM_WriteU8(ScratchRAM, address, value); }
void     ScratchRAM_WriteU16(uint32_t address, uint16_t value) { MASMEM_WriteU16(ScratchRAM, address, value); }
void     ScratchRAM_WriteU24(uint32_t address, uint32_t value) { MASMEM_WriteU24(ScratchRAM, address, value); }
void     ScratchRAM_WriteU32(uint32_t address, uint32_t value) { MASMEM_WriteU32(ScratchRAM, address, value); }
uint8_t *ScratchRAM_data8(void) { return ScratchRAM->data8; }
uint8_t *MainRAM_data8   (void) { return MainRAM->data8;    }
uint8_t *BIOSROM_data8   (void) { return BIOSROM->data8;    }

#ifdef HAVE_LIGHTREC
/* Size of Expansion 1 (8MB) */
#define PSX_EXPANSION1_SIZE        0x800000U
/* Base address of Expansion 1 */
#define PSX_EXPANSION1_BASE        0x1F000000U

/* Mednafen splits the expansion in two buffers (PIOMem and TextMem). That's not
 * super convenient for us so we copy both of them into one contiguous buffer.
 *
 * Hoisted out of PSX_LoadExpansion1's function scope so Cleanup() can
 * actually free it - the previous function-static was allocated once
 * and leaked on every core unload. */
static uint8_t *psx_expansion1 = NULL;

const uint8_t *PSX_LoadExpansion1(void)
{
   uint32_t *p;
   unsigned i;

   if (psx_expansion1 == NULL)
   {
      psx_expansion1 = (uint8_t *)malloc(PSX_EXPANSION1_SIZE);
      if (!psx_expansion1)
         return NULL;
   }

   /* Read 32 bits at a time to speed things up. */
   p = (uint32_t *)psx_expansion1;
   for (i = 0; i < PSX_EXPANSION1_SIZE / 4; i++)
      p[i] = PSX_MemPeek32(PSX_EXPANSION1_BASE + i * 4);

   return psx_expansion1;
}

static void PSX_FreeExpansion1(void)
{
   if (psx_expansion1)
   {
      free(psx_expansion1);
      psx_expansion1 = NULL;
   }
}
#endif

static uint32_t TextMem_Start;
/* TextMem is a dynamic byte buffer that grows as PSX EXEs are
 * loaded; keeps PIOMem text segments contiguous from a base
 * address.  Realloc'd in place, freed on Cleanup. */
static uint8_t *TextMem      = NULL;
static size_t   TextMem_size = 0;

static void TextMem_resize(size_t new_size)
{
   uint8_t *new_buf;
   if (new_size == TextMem_size)
      return;
   new_buf = (uint8_t *)realloc(TextMem, new_size);
   if (!new_buf && new_size)
      return;
   if (new_size > TextMem_size)
      memset(new_buf + TextMem_size, 0, new_size - TextMem_size);
   TextMem      = new_buf;
   TextMem_size = new_size;
}

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
   struct event_list_entry *prev;
   struct event_list_entry *next;
};

static struct event_list_entry events[PSX_EVENT__COUNT];

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

   CPU_SetEventNT(events[PSX_EVENT__SYNFIRST].next->event_time);
}

void PSX_SetEventNT(const int type, const int32_t next_timestamp)
{
   struct event_list_entry *e = &events[type];

   if(next_timestamp < e->event_time)
   {
      struct event_list_entry *fe = e;

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
      struct event_list_entry *fe = e;

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

   CPU_SetEventNT(events[PSX_EVENT__SYNFIRST].next->event_time & Running);
}

// Called from debug.cpp too.
void ForceEventUpdates(const int32_t timestamp)
{
   PSX_SetEventNT(PSX_EVENT_GPU, GPU_Update(timestamp));
   PSX_SetEventNT(PSX_EVENT_CDC, PS_CDC_Update(PSX_CDC, timestamp));

   PSX_SetEventNT(PSX_EVENT_TIMER, TIMER_Update(timestamp));

   PSX_SetEventNT(PSX_EVENT_DMA, DMA_Update(timestamp));

   PSX_SetEventNT(PSX_EVENT_FIO, FrontIO_Update(PSX_FIO, timestamp));

   CPU_SetEventNT(events[PSX_EVENT__SYNFIRST].next->event_time);
}

bool MDFN_FASTCALL PSX_EventHandler(const int32_t timestamp)
{
   struct event_list_entry *e = events[PSX_EVENT__SYNFIRST].next;

   while(timestamp >= e->event_time)   // If Running = 0, PSX_EventHandler() may be called even if there isn't an event per-se, so while() instead of do { ... } while
   {
      int32_t nt;
      struct event_list_entry *prev = e->prev;

      switch(e->which)
      {
         default:
            abort();
         case PSX_EVENT_GPU:
            nt = GPU_Update(e->event_time);
            break;
         case PSX_EVENT_CDC:
            nt = PS_CDC_Update(PSX_CDC, e->event_time);
            break;
         case PSX_EVENT_TIMER:
            nt = TIMER_Update(e->event_time);
            break;
         case PSX_EVENT_DMA:
            nt = DMA_Update(e->event_time);
            break;
         case PSX_EVENT_FIO:
            nt = FrontIO_Update(PSX_FIO, e->event_time);
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
   CPU_SetEventNT(0);
}


//
// End event stuff
//


/* Remember to update MemPeek<>() and MemPoke<>() when we change address decoding in MemRW() */
static INLINE void MemRW(int32_t *timestamp, uint32_t A, uint32_t *V_p, unsigned size, bool is_write, bool access24)
{
#if 0
   if(is_write)
      printf("Write%d: %08x(orig=%08x), %08x\n", (int)(size * 8), A & mask[A >> 29], A, (*V_p));
   else
      printf("Read%d: %08x(orig=%08x)\n", (int)(size * 8), A & mask[A >> 29], A);
#endif

   if(!is_write)
      *timestamp += DMACycleSteal;

   //if(A == 0xa0 && is_write)
   // DBG_Break();

   if(A < 0x00800000)
   {
      if(is_write)
      {
         //(*timestamp)++; // Best-case timing.
      }
      else
      {
         // Overclock: get rid of memory access latency
         if (!psx_gte_overclock)
            *timestamp += 3;
      }

      if(access24)
      {
         if(is_write)
            MASMEM_WriteU24(MainRAM, A & 0x1FFFFF, (*V_p));
         else
            (*V_p) = MASMEM_ReadU24(MainRAM, A & 0x1FFFFF);
      }
      else
      {
         if(is_write)
            MASMEM_Write_size(MainRAM, A & 0x1FFFFF, (*V_p), size);
         else
            (*V_p) = MASMEM_Read_size(MainRAM, A & 0x1FFFFF, size);
      }

      return;
   }

   if(A >= 0x1FC00000 && A <= 0x1FC7FFFF)
   {
      if(!is_write)
      {
         if(access24)
            (*V_p) = MASMEM_ReadU24(BIOSROM, A & 0x7FFFF);
         else
            (*V_p) = MASMEM_Read_size(BIOSROM, A & 0x7FFFF, size);
      }

      return;
   }

   if(*timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
      PSX_EventHandler(*timestamp);

   if(A >= 0x1F801000 && A <= 0x1F802FFF)
   {

      //if(is_write)
      // printf("HW Write%d: %08x %08x\n", (unsigned int)(size*8), (unsigned int)A, (unsigned int)V);
      //else
      // printf("HW Read%d: %08x\n", (unsigned int)(size*8), (unsigned int)A);

      if(A >= 0x1F801C00 && A <= 0x1F801FFF) // SPU
      {
         if(size == 4 && !access24)
         {
            if(is_write)
            {
               //*timestamp += 15;

               //if(*timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
               // PSX_EventHandler(*timestamp);

               SPU_Write(*timestamp, A | 0, (*V_p));
               SPU_Write(*timestamp, A | 2, (*V_p) >> 16);
            }
            else
            {
               *timestamp += 36;

               if(*timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
                  PSX_EventHandler(*timestamp);

               (*V_p) = SPU_Read(*timestamp, A) | (SPU_Read(*timestamp, A | 2) << 16);
            }
         }
         else
         {
            if(is_write)
            {
               //*timestamp += 8;

               //if(*timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
               // PSX_EventHandler(*timestamp);

               SPU_Write(*timestamp, A & ~1, (*V_p));
            }
            else
            {
               *timestamp += 16; // Just a guess, need to test.

               if(*timestamp >= events[PSX_EVENT__SYNFIRST].next->event_time)
                  PSX_EventHandler(*timestamp);

               (*V_p) = SPU_Read(*timestamp, A & ~1);
            }
         }
         return;
      }     // End SPU


      // CDC: TODO - 8-bit access.
      if(A >= 0x1f801800 && A <= 0x1f80180F)
      {
         if(!is_write)
         {
            *timestamp += 6 * size; //24;
         }

         if(is_write)
            PS_CDC_Write(PSX_CDC, *timestamp, A & 0x3, (*V_p));
         else
            (*V_p) = PS_CDC_Read(PSX_CDC, *timestamp, A & 0x3);

         return;
      }

      if(A >= 0x1F801810 && A <= 0x1F801817)
      {
         if(!is_write)
            (*timestamp)++;

         if(is_write)
            GPU_Write(*timestamp, A, (*V_p));
         else
            (*V_p) = GPU_Read(*timestamp, A);

         return;
      }

      if(A >= 0x1F801820 && A <= 0x1F801827)
      {
         if(!is_write)
            (*timestamp)++;

         if(is_write)
            MDEC_Write(*timestamp, A, (*V_p));
         else
            (*V_p) = MDEC_Read(*timestamp, A);

         return;
      }

      if(A >= 0x1F801000 && A <= 0x1F801023)
      {
         unsigned index = (A & 0x1F) >> 2;

         if(!is_write)
            (*timestamp)++;

         //if(A == 0x1F801014 && is_write)
         // fprintf(stderr, "%08x %08x\n",A,V);

         if(is_write)
         {
            (*V_p) <<= (A & 3) * 8;
            SysControl.Regs[index] = (*V_p) & SysControl_Mask[index];
         }
         else
         {
            (*V_p) = SysControl.Regs[index] | SysControl_OR[index];
            (*V_p) >>= (A & 3) * 8;
         }
         return;
      }

      if(A >= 0x1F801040 && A <= 0x1F80104F)
      {
         if(!is_write)
            (*timestamp)++;

         if(is_write)
            FrontIO_Write(PSX_FIO, *timestamp, A, (*V_p));
         else
            (*V_p) = FrontIO_Read(PSX_FIO, *timestamp, A);
         return;
      }

      if(A >= 0x1F801050 && A <= 0x1F80105F)
      {
         if(!is_write)
            (*timestamp)++;

         if(is_write)
            SIO_Write(*timestamp, A, (*V_p));
         else
            (*V_p) = SIO_Read(*timestamp, A);
         return;
      }


      if(A >= 0x1F801070 && A <= 0x1F801077) // IRQ
      {
         if(!is_write)
            (*timestamp)++;

         if(is_write)
            IRQ_Write(A, (*V_p));
         else
            (*V_p) = IRQ_Read(A);
         return;
      }

      if(A >= 0x1F801080 && A <= 0x1F8010FF)    // DMA
      {
         if(!is_write)
            (*timestamp)++;

         if(is_write)
            DMA_Write(*timestamp, A, (*V_p));
         else
            (*V_p) = DMA_Read(*timestamp, A);

         return;
      }

      if(A >= 0x1F801100 && A <= 0x1F80113F) // Root counters
      {
         if(!is_write)
            (*timestamp)++;

         if(is_write)
            TIMER_Write(*timestamp, A, (*V_p));
         else
            (*V_p) = TIMER_Read(*timestamp, A);

         return;
      }
   }


   if(A >= 0x1F000000 && A <= 0x1F7FFFFF)
   {
      if(!is_write)
      {
         (*V_p) = ~0U; // A game this affects:  Tetris with Cardcaptor Sakura

         if(PIOMem)
         {
            if((A & 0x7FFFFF) < 65536)
            {
               if(access24)
                  (*V_p) = MASMEM_ReadU24(PIOMem, A & 0x7FFFFF);
               else
                  (*V_p) = MASMEM_Read_size(PIOMem, A & 0x7FFFFF, size);
            }
            else if((A & 0x7FFFFF) < (65536 + TextMem_size))
            {
               if(access24)
                  (*V_p) = MDFN_de24lsb(&TextMem[(A & 0x7FFFFF) - 65536]);
               else switch(size)
               {
                  case 1: (*V_p) = TextMem[(A & 0x7FFFFF) - 65536]; break;
                  case 2: (*V_p) = MDFN_de16lsb(&TextMem[(A & 0x7FFFFF) - 65536]); break;
                  case 4: (*V_p) = MDFN_de32lsb(&TextMem[(A & 0x7FFFFF) - 65536]); break;
               }
            }
         }
      }
      return;
   }

   if(A == 0xFFFE0130) // Per tests on PS1, ignores the access(sort of, on reads the value is forced to 0 if not aligned) if not aligned to 4-bytes.
   {
      if(!is_write)
         (*V_p) = CPU_GetBIU(PSX_CPU);
      else
         CPU_SetBIU(PSX_CPU, (*V_p));

      return;
   }

   if(!is_write)
      (*V_p) = 0;
}

void MDFN_FASTCALL PSX_MemWrite8(int32_t timestamp, uint32_t A, uint32_t V)
{
   MemRW(&timestamp, A, &V, 1, true, false);
}

void MDFN_FASTCALL PSX_MemWrite16(int32_t timestamp, uint32_t A, uint32_t V)
{
   MemRW(&timestamp, A, &V, 2, true, false);
}

void MDFN_FASTCALL PSX_MemWrite24(int32_t timestamp, uint32_t A, uint32_t V)
{
   MemRW(&timestamp, A, &V, 4, true, true);
}

void MDFN_FASTCALL PSX_MemWrite32(int32_t timestamp, uint32_t A, uint32_t V)
{
   MemRW(&timestamp, A, &V, 4, true, false);
}

/* PSX_MemRead{8,16,24,32}: signatures take int32_t *timestamp;
 * cpu.c calls these from its ReadMemory_uN helpers. */
uint8_t MDFN_FASTCALL PSX_MemRead8(int32_t *timestamp, uint32_t A)
{
   uint32_t V;
   MemRW(timestamp, A, &V, 1, false, false);
   return V;
}

uint16_t MDFN_FASTCALL PSX_MemRead16(int32_t *timestamp, uint32_t A)
{
   uint32_t V;
   MemRW(timestamp, A, &V, 2, false, false);
   return V;
}

uint32_t MDFN_FASTCALL PSX_MemRead24(int32_t *timestamp, uint32_t A)
{
   uint32_t V;
   MemRW(timestamp, A, &V, 4, false, true);
   return V;
}

uint32_t MDFN_FASTCALL PSX_MemRead32(int32_t *timestamp, uint32_t A)
{
   uint32_t V;
   MemRW(timestamp, A, &V, 4, false, false);
   return V;
}

static INLINE uint32_t MemPeek(int32_t timestamp, uint32_t A, unsigned size, bool access24)
{
   if(A < 0x00800000)
   {
      if(access24)
         return(MASMEM_ReadU24(MainRAM, A & 0x1FFFFF));
      return(MASMEM_Read_size(MainRAM, A & 0x1FFFFF, size));
   }

   if(A >= 0x1FC00000 && A <= 0x1FC7FFFF)
   {
      if(access24)
         return(MASMEM_ReadU24(BIOSROM, A & 0x7FFFF));
      return(MASMEM_Read_size(BIOSROM, A & 0x7FFFF, size));
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
            if(access24)
               return(MASMEM_ReadU24(PIOMem, A & 0x7FFFFF));
            return(MASMEM_Read_size(PIOMem, A & 0x7FFFFF, size));
         }
         else if((A & 0x7FFFFF) < (65536 + TextMem_size))
         {
            if(access24)
               return(MDFN_de24lsb(&TextMem[(A & 0x7FFFFF) - 65536]));
            else switch(size)
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
      return CPU_GetBIU(PSX_CPU);

   return(0);
}

uint8_t PSX_MemPeek8(uint32_t A)
{
   return MemPeek(0, A, 1, false);
}

uint16_t PSX_MemPeek16(uint32_t A)
{
   return MemPeek(0, A, 2, false);
}

uint32_t PSX_MemPeek32(uint32_t A)
{
   return MemPeek(0, A, 4, false);
}

// FIXME: Add PSX_Reset() and Reset() so that emulated input devices don't get power-reset on reset-button reset.
static void PSX_Power(void)
{
   unsigned i;

   PSX_PRNG.x = 123456789;
   PSX_PRNG.y = 987654321;
   PSX_PRNG.z = 43219876;
   PSX_PRNG.c = 6543217;
   PSX_PRNG.lcgo = 0xDEADBEEFCAFEBABEULL;

   cd_warned_slow = false;

   memset(MultiAccessSizeMem_get_data32(MainRAM), 0, 2048 * 1024);

   for(i = 0; i < 9; i++)
      SysControl.Regs[i] = 0;

   CPU_Power(PSX_CPU);

   EventReset();

   TIMER_Power();

   DMA_Power();

   FrontIO_Power(PSX_FIO);
   SIO_Power();

   MDEC_Power();
   PS_CDC_Power(PSX_CDC);
   GPU_Power();
   //SPU->Power();   // Called from CDC->Power()
   IRQ_Power();

   ForceEventUpdates(0);
   startup_frame_count = 0;
}

static INLINE void MemPoke(pscpu_timestamp_t timestamp, uint32 A, uint32_t V, unsigned size, bool access24)
{
   if(A < 0x00800000)
   {
      if(access24)
         MASMEM_WriteU24(MainRAM, A & 0x1FFFFF, V);
      else
         MASMEM_Write_size(MainRAM, A & 0x1FFFFF, V, size);

      return;
   }

   if(A >= 0x1FC00000 && A <= 0x1FC7FFFF)
   {
      if(access24)
         MASMEM_WriteU24(BIOSROM, A & 0x7FFFF, V);
      else
         MASMEM_Write_size(BIOSROM, A & 0x7FFFF, V, size);

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
      CPU_SetBIU(PSX_CPU, V);
      return;
   }
}

void PSX_MemPoke8(uint32 A, uint8 V)
{
   MemPoke(0, A, V, 1, false);
}

/* Test whether the supplied file looks like a PS-X EXE. The original
 * code returned true unconditionally because both branches returned
 * true - the magic check had no effect. Now we actually require the
 * "PS-X EXE" signature, which is what PSX EXEs have at offset 0. */
static bool TestMagic(const char *name, RFILE *fp, int64_t size)
{
   uint8_t header[8];

   if (size < 0x800)
      return false;

   if (filestream_read(fp, header, 8) != 8)
      return false;

   return (header[0] == 'P') && (header[1] == 'S') && (header[2] == '-') &&
          (header[3] == 'X') && (header[4] == ' ') && (header[5] == 'E') &&
          (header[6] == 'X') && (header[7] == 'E');
}

static const char *CalcDiscSCEx_BySYSTEMCNF(CDIF *c, unsigned *rr)
{
   uint8_t pvd[2048];
   uint32_t rdel, rdel_len;
   const char *ret = NULL;
   struct Stream *fp = NULL;
   unsigned pvd_search_count = 0;

   fp = CDIF_MakeStream(c, 0, ~0U);
   if (!fp)
      return NULL;
   stream_seek(fp, 0x8000, SEEK_SET);

   do
   {
      if((pvd_search_count++) == 32)
      {
         log_cb(RETRO_LOG_ERROR, "PVD search count limit met.\n");
         goto Breakout;
      }

      stream_read(fp, pvd, 2048);

      if(memcmp(&pvd[1], "CD001", 5))
      {
         log_cb(RETRO_LOG_ERROR, "Not ISO-9660\n");
         goto Breakout;
      }

      if(pvd[0] == 0xFF)
      {
         log_cb(RETRO_LOG_ERROR, "Missing Primary Volume Descriptor\n");
         goto Breakout;
      }
   } while(pvd[0] != 0x01);

   /* [156 ... 189], 34 bytes - Root directory record */
   rdel     = MDFN_de32lsb(&pvd[0x9E]);
   rdel_len = MDFN_de32lsb(&pvd[0xA6]);

   if(rdel_len >= (1024 * 1024 * 10))  /* Arbitrary sanity check. */
   {
      log_cb(RETRO_LOG_ERROR, "Root directory table too large\n");
      goto Breakout;
   }

   stream_seek(fp, (int64)rdel * 2048, SEEK_SET);

   while(stream_tell(fp) < (((int64)rdel * 2048) + rdel_len))
   {
      uint8_t dr[256 + 1];
      uint8_t len_dr = stream_get_u8(fp);
      uint8_t len_fi;

      if(!len_dr)
         break;

      /* len_dr counts the directory record header byte itself, so we
       * read len_dr-1 more bytes. Cap at sizeof(dr)-1 (=256) to be safe. */
      if (len_dr - 1 > (int)sizeof(dr) - 1)
      {
         log_cb(RETRO_LOG_ERROR, "Directory record length out of range: %u\n", len_dr);
         goto Breakout;
      }

      memset(dr, 0, sizeof(dr));
      dr[0] = len_dr;
      stream_read(fp, dr + 1, len_dr - 1);

      len_fi = dr[0x20];

      if(len_fi == 12 && !memcmp(&dr[0x21], "SYSTEM.CNF;1", 12))
      {
         uint32_t file_lba = MDFN_de32lsb(&dr[0x02]);
         uint8_t fb[2048 + 1];
         char *bootpos;
         char *tmp;

         memset(fb, 0, sizeof(fb));
         stream_seek(fp, file_lba * 2048, SEEK_SET);
         stream_read(fp, fb, 2048);

         /* Find "BOOT" in the SYSTEM.CNF buffer; bail out if missing
          * rather than dereferencing strstr's NULL return. */
         bootpos = strstr((char*)fb, "BOOT");
         if (!bootpos)
            goto Breakout;
         bootpos += 4;

         while(*bootpos == ' ' || *bootpos == '\t') bootpos++;
         if (*bootpos != '=')
            goto Breakout;

         bootpos++;
         while(*bootpos == ' ' || *bootpos == '\t') bootpos++;
         if (strncasecmp(bootpos, "cdrom:\\", 7) != 0)
            goto Breakout;

         bootpos += 7;

         /* Game-specific framebuffer-write tweak for Monkey Hero. The
          * filename portion of BOOT=cdrom:\SLUS_007.65;1 starts right
          * at bootpos here - the previous "bootpos + 7" bug compared
          * the substring starting 7 chars later, so this never matched. */
         if (!strncmp(bootpos, "SLUS_007.65", 11) ||
             !strncmp(bootpos, "SLES_009.79", 11))
         {
            is_monkey_hero = true;
            log_cb(RETRO_LOG_INFO, "Monkey Hero FBWrite Tweak Activated\n");
         }

         /* Per-game CD-speedup compatibility caps.
          *
          * Some titles' CD-handling code can't keep up when the CDC
          * feeds data sectors at the high end of the cd_2x_speedup
          * range; the user picks "8x" in core options and the game
          * wedges or crashes during a streaming read.  We don't try
          * to fix the underlying mismatch (the timing assumptions
          * are baked into the game binary), we just clamp the
          * speedup to the highest value that's been observed to
          * work for that title.
          *
          * The cap is the max value of cd_2x_speedup itself, not
          * the user-facing "Nx" label - so 3 here means "up to 6x
          * (2 * 3)" is fine.  Add new entries with the BOOT-format
          * serial (AAAA_NNN.NN) as it appears in SYSTEM.CNF, not
          * the redump/SLUS-NNNNN form. */
         {
            static const struct { const char *serial; unsigned cap; }
            cd_speedup_compat_table[] = {
               /* Myst (Cyan / Psygnosis): freezes/crashes at 8x
                * when the post-seek streaming pipeline desyncs.
                * 6x is the highest speed observed to work. */
               { "SCUS_946.02", 3 }, /* NTSC-U */
               { "SLES_002.18", 3 }, /* PAL */
               { "SLPS_000.24", 3 }, /* NTSC-J original */
               { "SLPS_910.23", 3 }, /* NTSC-J [Playstation the Best] */
               { "SLPS_911.23", 3 }, /* NTSC-J [Playstation the Best] [Rerelease] */
               { "SLPS_029.24", 3 }, /* NTSC-J [Value 1500] */
            };
            unsigned k;
            for (k = 0; k < sizeof(cd_speedup_compat_table) / sizeof(cd_speedup_compat_table[0]); k++)
            {
               if (!strncmp(bootpos, cd_speedup_compat_table[k].serial, 11))
               {
                  cd_speedup_compat_max = cd_speedup_compat_table[k].cap;
                  log_cb(RETRO_LOG_INFO,
                        "CD speedup capped to %ux for compatibility (serial %s)\n",
                        cd_speedup_compat_max * 2,
                        cd_speedup_compat_table[k].serial);
                  break;
               }
            }
         }

         if ((tmp = strchr(bootpos, '_'))) *tmp = 0;
         if ((tmp = strchr(bootpos, '.'))) *tmp = 0;
         if ((tmp = strchr(bootpos, ';'))) *tmp = 0;

         if(strlen(bootpos) == 4 && bootpos[0] == 'S' &&
            (bootpos[1] == 'C' || bootpos[1] == 'L' || bootpos[1] == 'I'))
         {
            switch(bootpos[2])
            {
               case 'E':
                  if (rr) *rr = REGION_EU;
                  ret = "SCEE";
                  goto Breakout;

               case 'U':
                  if (rr) *rr = REGION_NA;
                  ret = "SCEA";
                  goto Breakout;

               case 'K':   /* Korea? */
               case 'B':
               case 'P':
                  if (rr) *rr = REGION_JP;
                  ret = "SCEI";
                  goto Breakout;
            }
         }
      }
   }

Breakout:
   if(fp)
   {
      stream_destroy(fp);
      fp = NULL;
   }

   return ret;
}

static unsigned CalcDiscSCEx(void)
{
   const char *prev_valid_id = NULL;
   unsigned ret_region       = MDFN_GetSettingI("psx.region_default");

   if (cdifs_loaded)
   {
      unsigned i;
      for (i = 0; i < cdifs.count; i++)
      {
         uint8_t buf[2048];
         uint8_t fbuf[2048 + 1];
         const char *id = CalcDiscSCEx_BySYSTEMCNF(cdifs.items[i], (i == 0) ? &ret_region : NULL);

         memset(fbuf, 0, sizeof(fbuf));

         if(id == NULL && CDIF_ReadSector(cdifs.items[i], buf, 4, 1) == 0x2)
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

         cdifs.scex_ids[i] = id;
      }
   }

   return ret_region;
}

static void SetDiscWrapper(const bool CD_TrayOpen) {
   CDIF *cdif = NULL;
   const char *disc_id = NULL;

   if (cdifs_loaded && CD_SelectedDisc >= 0 && !CD_TrayOpen) {
      /* Only allow one pbp file to be loaded (at index 0). */
      if (CD_IsPBP) {
         if (cdifs.count > 0)
         {
            cdif    = cdifs.items   [0];
            disc_id = cdifs.scex_ids[0];
         }
      } else {
         size_t idx = (size_t)CD_SelectedDisc;
         if (idx < cdifs.count)
         {
            cdif    = cdifs.items   [idx];
            disc_id = cdifs.scex_ids[idx];
         }
      }
   }

   if (PSX_CDC)
      PS_CDC_SetDisc(PSX_CDC, CD_TrayOpen, cdif, disc_id);
}

/* PSX memory region sizes - these are used unconditionally below (e.g. for
 * MultiAccessSizeMem_New() in the non-lightrec path), so they must NOT be
 * gated on HAVE_LIGHTREC. */
#define RAM_SIZE     0x200000
#define BIOS_SIZE    0x80000
#define SCRATCH_SIZE 0x400
#define SHM_SIZE     (RAM_SIZE + BIOS_SIZE + SCRATCH_SIZE)
#define PIO_SIZE     (65536)

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
#if !__MACOS__
	(uintptr_t)(0x00000000),
	(uintptr_t)(0x10000000),
	(uintptr_t)(0x20000000),
	(uintptr_t)(0x30000000),
#else
   (uintptr_t)(MACOS_VM_BASE),
#endif
	(uintptr_t)(0x40000000),
	(uintptr_t)(0x50000000),
	(uintptr_t)(0x60000000),
	(uintptr_t)(0x70000000),
	(uintptr_t)(0x80000000),
	(uintptr_t)(0x90000000),
   /* Some platforms need higher address base for mmap to work */
#if UINTPTR_MAX == UINT64_MAX
	(uintptr_t)(0x100000000),
	(uintptr_t)(0x200000000),
	(uintptr_t)(0x300000000),
	(uintptr_t)(0x400000000),
	(uintptr_t)(0x500000000),
	(uintptr_t)(0x600000000),
	(uintptr_t)(0x700000000),
	(uintptr_t)(0x800000000),
	(uintptr_t)(0x900000000),
#endif
};

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
		/* Android 10+ / API 29+ gives EACCES (permission denied) opening /dev/ashmem
		 * fallback to ASharedMemory_create available since Android 8 / API 26 */
		if(errno == EACCES) {
			void *lib;
			int (*create)(const char*, size_t);
			int (*setProt)(int, int);
			char *error1, *error2;

			dlerror();      /* Clear any existing error */
			lib = dlopen("libandroid.so", RTLD_NOW);
			if (lib == NULL) {
				log_cb(RETRO_LOG_ERROR, "Failed to dlopen: %s\n", dlerror());
				return 0;
			}

			*(void **)(&create) = dlsym(lib, "ASharedMemory_create");
			error1 = dlerror();
			*(void **)(&setProt) = dlsym(lib, "ASharedMemory_setProt");
			error2 = dlerror();

			if (error1 == NULL)
				memfd = (*create)("lightrec_memfd",SHM_SIZE);

			if (memfd < 0) {
				log_cb(RETRO_LOG_ERROR, "Failed to ASharedMemory_create: %s\n",
							(error1 != NULL) ? error1 : strerror(errno));
				dlclose(lib);
				return 0;
			}

			if (error2 != NULL || (((*setProt)(memfd, PROT_READ|PROT_WRITE)) < 0))
				log_cb(RETRO_LOG_ERROR, "Failed to ASharedMemory_setProt: %s\n",
							(error2 != NULL) ? error2 : strerror(errno));

			dlclose(lib);
		} else {
			log_cb(RETRO_LOG_ERROR, "Failed to create ASHMEM: %s\n", strerror(errno));
			return 0;
		}
	} else {
		ioctl(memfd, ASHMEM_SET_NAME, "lightrec_memfd");
		ioctl(memfd, ASHMEM_SET_SIZE, SHM_SIZE);
	}
#endif
#ifdef HAVE_SHM
	int memfd;
	const char *shm_name = "/lightrec_memfd_beetle";

	memfd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);

	if (memfd < 0 && errno == EEXIST) {
		shm_unlink(shm_name);
		memfd = shm_open(shm_name, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
	}

	if (memfd < 0) {
		log_cb(RETRO_LOG_ERROR, "Failed to create SHM: %s\n", strerror(errno));
		return 0;
	}

	/* unlink ASAP to prevent leaving a file in shared memory if we crash */
	shm_unlink(shm_name);

	if (ftruncate(memfd, SHM_SIZE) < 0) {
		log_cb(RETRO_LOG_ERROR, "Could not truncate SHM size: %s\n", strerror(errno));
		goto close_return;
	}
#endif
#ifdef HAVE_WIN_SHM
	HANDLE memfd;

	memfd = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SHM_SIZE, NULL);

	if (memfd == NULL) {
		log_cb(RETRO_LOG_ERROR, "Failed to create WIN_SHM: %s (%d)\n", strerror(errno), GetLastError());
		return 0;
	}
#endif

	/* Try to map at various base addresses */
	for (i = 0; i < ARRAY_SIZE(supported_io_bases); i++) {
		base = supported_io_bases[i];

		/* Skip base=0: even if mmap honors NULL+MAP_FIXED_NOREPLACE
		 * (some kernels permit it when mmap_min_addr is small or
		 * inside containers), psx_mem=NULL is indistinguishable from
		 * "no mmap" and would then be passed to placement-new for
		 * MainRAM, leaving MainRAM->data8 also NULL and breaking
		 * retro_get_memory_data + every direct RAM access. The first
		 * usable base is 0x10000000. */
		if (base == 0)
			continue;

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
	/* android shared memory is not pinned by mmap, it dies on close */
	close(memfd);
#endif
}
#endif /* HAVE_LIGHTREC */

/* LED interface */
static retro_set_led_state_t led_state_cb = NULL;
static unsigned int retro_led_state[2] = {0};
static void retro_led_interface(void)
{
   /* 0: Power
    * 1: CD */

   unsigned int led_state[2] = {0};
   unsigned int l            = 0;

   led_state[0] = (!Running) ? 1 : 0;
   led_state[1] = (PSX_CDC->DriveStatus > 0) ? 1 : 0;

   for (l = 0; l < sizeof(led_state)/sizeof(led_state[0]); l++)
   {
      if (retro_led_state[l] != led_state[l])
      {
         retro_led_state[l] = led_state[l];
         led_state_cb(l, led_state[l]);
      }
   }
}

/* Forward declarations, required for disk control
 * 'set initial disk' functionality */
static unsigned disk_get_num_images(void);
static void CDInsertEject(void);
static void CDEject(void);
static void Cleanup(void);

static void InitCommon(const bool EmulateMemcards, const bool WantPIOMem)
{
   unsigned region, i;
   bool emulate_memcard[8];
   bool emulate_multitap[2];
   int sls, sle;
   RFILE *BIOSFile;
   char biospath[4096];

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

   cdifs_loaded = 1;
   region       = CalcDiscSCEx();

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

   PSX_CPU = CPU_New();
   SPU_Init();

   /* GPU_Init returns false on VRAM allocation failure. There is no
    * graceful recovery path through InitCommon's existing void
    * signature - the caller (retro_load_game) doesn't propagate
    * failure either. Log to the frontend and bail out hard; users
    * will see the error message and the core will be in an aborted
    * state until reset. Full propagation (InitCommon, MDFNI_LoadGame,
    * retro_load_game all returning the failure) is left to a follow
    * up; that is an architectural change with broader scope than
    * the current GPU audit. */
   if (!GPU_Init(region == REGION_EU, sls, sle, psx_gpu_upscale_shift))
   {
      log_cb(RETRO_LOG_ERROR,
            "GPU_Init: VRAM allocation failed for upscale shift %u\n",
            (unsigned)psx_gpu_upscale_shift);
      MDFN_Error(0, "GPU_Init failed");
      return;
   }

   PSX_CDC = (PS_CDC *)calloc(1, sizeof(PS_CDC));
   PS_CDC_Init(PSX_CDC);
   PSX_FIO = FrontIO_New(emulate_memcard, emulate_multitap);
   FrontIO_SetAMCT(PSX_FIO, setting_psx_analog_toggle);
   for(unsigned i = 0; i < 2; i++)
   {
      char buf[64];
      snprintf(buf, sizeof(buf), "psx.input.port%u.gun_chairs", i + 1);
      FrontIO_SetCrosshairsColor(PSX_FIO, i, MDFN_GetSettingUI(buf));
   }

   input_set_fio(PSX_FIO);

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

   PGXP_SetModes(psx_pgxp_mode | psx_pgxp_vertex_caching | psx_pgxp_texture_correction | psx_pgxp_nclip);

   CD_TrayOpen        = true;
   CD_SelectedDisc    = -1;

   if(cdifs_loaded)
   {
      CD_TrayOpen     = false;
      CD_SelectedDisc = 0;

      /* Attempt to set initial disk index */
      if ((disk_control_ext_info.initial_index > 0) &&
          (disk_control_ext_info.initial_index < disk_get_num_images()))
         if (disk_control_ext_info.initial_index <
               disk_control_ext_info.image_paths.count)
            if (string_is_equal(
                  disk_control_ext_info.image_paths.items[disk_control_ext_info.initial_index],
                  disk_control_ext_info.initial_path))
               CD_SelectedDisc = (int)disk_control_ext_info.initial_index;
   }

   PS_CDC_SetDisc(PSX_CDC, true, NULL, NULL);

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

   if (psx_mmap > 0)
   {
      MainRAM = MultiAccessSizeMem_Attach(psx_mem, RAM_SIZE);
      ScratchRAM = MultiAccessSizeMem_Attach(psx_scratch, SCRATCH_SIZE);
      BIOSROM = MultiAccessSizeMem_Attach(psx_bios, BIOS_SIZE);
   }
   else
#endif
   {
      MainRAM = MultiAccessSizeMem_New(RAM_SIZE);
      ScratchRAM = MultiAccessSizeMem_New(SCRATCH_SIZE);
      BIOSROM = MultiAccessSizeMem_New(BIOS_SIZE);
   }

   PIOMem = NULL;

   if (WantPIOMem)
   {
      PIOMem = MultiAccessSizeMem_New(PIO_SIZE);
   }

   for(uint32_t ma = 0x00000000; ma < 0x00800000; ma += 2048 * 1024)
   {
      CPU_SetFastMap(PSX_CPU, MultiAccessSizeMem_get_data32(MainRAM), 0x00000000 + ma, 2048 * 1024);
      CPU_SetFastMap(PSX_CPU, MultiAccessSizeMem_get_data32(MainRAM), 0x80000000 + ma, 2048 * 1024);
      CPU_SetFastMap(PSX_CPU, MultiAccessSizeMem_get_data32(MainRAM), 0xA0000000 + ma, 2048 * 1024);
   }

   CPU_SetFastMap(PSX_CPU, MultiAccessSizeMem_get_data32(BIOSROM), 0x1FC00000, 512 * 1024);
   CPU_SetFastMap(PSX_CPU, MultiAccessSizeMem_get_data32(BIOSROM), 0x9FC00000, 512 * 1024);
   CPU_SetFastMap(PSX_CPU, MultiAccessSizeMem_get_data32(BIOSROM), 0xBFC00000, 512 * 1024);

   if(PIOMem)
   {
      CPU_SetFastMap(PSX_CPU, MultiAccessSizeMem_get_data32(PIOMem), 0x1F000000, 65536);
      CPU_SetFastMap(PSX_CPU, MultiAccessSizeMem_get_data32(PIOMem), 0x9F000000, 65536);
      CPU_SetFastMap(PSX_CPU, MultiAccessSizeMem_get_data32(PIOMem), 0xBF000000, 65536);
   }


   MDFNMP_Init(1024, ((uint64)1 << 29) / 1024);
   MDFNMP_AddRAM(2048 * 1024, 0x00000000, MainRAM->data8);

   if(firmware_is_present(region))
   {
      BIOSFile      = filestream_open(bios_path,
            RETRO_VFS_FILE_ACCESS_READ,
            RETRO_VFS_FILE_ACCESS_HINT_NONE);
   }
   else
   {
      const char *biospath_sname;

      if (region == REGION_JP)
         biospath_sname = "psx.bios_jp";
      else if (region == REGION_EU)
         biospath_sname = "psx.bios_eu";
      else if (region == REGION_NA)
         biospath_sname = "psx.bios_na";
      else
      {
         /* A libretro core must never abort() - that would tear down
          * the host process. Log the unexpected region value and fall
          * back to NA so we can still attempt to boot rather than
          * killing RetroArch. */
         log_cb(RETRO_LOG_WARN,
               "Unknown region %u, falling back to NA BIOS\n", region);
         biospath_sname = "psx.bios_na";
      }

      MDFN_MakeFName(MDFNMKF_FIRMWARE, 0,
            MDFN_GetSettingS(biospath_sname), biospath, sizeof(biospath));

      BIOSFile      = filestream_open(biospath,
            RETRO_VFS_FILE_ACCESS_READ,
            RETRO_VFS_FILE_ACCESS_HINT_NONE);
   }

   if (BIOSFile)
   {
      const int64_t expected = 512 * 1024;
      int64_t bios_size      = filestream_get_size(BIOSFile);
      int64_t got;

      /* Zero out the BIOS region first so a short read leaves a
       * deterministic state - the previous code left the tail
       * uninitialized whenever the file was smaller than 512 KB. */
      memset(BIOSROM->data8, 0, expected);

      got = filestream_read(BIOSFile, BIOSROM->data8, expected);
      filestream_close(BIOSFile);

      if (bios_size != expected)
         log_cb(RETRO_LOG_WARN,
               "BIOS file size %lld does not match expected %lld; results may differ from real hardware\n",
               (long long)bios_size, (long long)expected);
      else if (got != expected)
         log_cb(RETRO_LOG_WARN,
               "BIOS file short read (%lld of %lld bytes); results may differ from real hardware\n",
               (long long)got, (long long)expected);
   }
   else
   {
      memcpy(BIOSROM->data8, openbios, sizeof(openbios));
   }

   i = 0;

   if (!use_mednafen_memcard0_method)
   {
      FrontIO_LoadMemcard(PSX_FIO, 0);
      i = 1;
   }

   for(; i < 8; i++)
   {
      char ext[64];
      char memcard[4096];
      if (i == 0)
         snprintf(ext, sizeof(ext), "%d.mcr", memcard_left_index);
      else if (i == 1)
         snprintf(ext, sizeof(ext), "%d.mcr", memcard_right_index);
      else
         snprintf(ext, sizeof(ext), "%d.mcr", i);
      MDFN_MakeFName(MDFNMKF_SAV, 0, ext, memcard, sizeof(memcard));
      FrontIO_LoadMemcardFromPath(PSX_FIO, i, memcard, false);
   }

   for(i = 0; i < 8; i++)
   {
      Memcard_PrevDC[i] = FrontIO_GetMemcardDirtyCount(PSX_FIO, i);
      Memcard_SaveDelay[i] = -1;
   }

	input_init_calibration();

   PSX_Power();
}

static bool LoadEXE(const uint8_t *data, const uint32_t size, bool ignore_pcsp)
{
   uint32 PC        = MDFN_de32lsb(&data[0x10]);
   uint32 SP        = MDFN_de32lsb(&data[0x30]);
   uint32 TextStart = MDFN_de32lsb(&data[0x18]);
   uint32 TextSize  = MDFN_de32lsb(&data[0x1C]);
   uint8 *po;
   uint32 sa;

   if(ignore_pcsp)
      log_cb(RETRO_LOG_DEBUG, "TextStart=0x%08x\nTextSize=0x%08x\n", TextStart, TextSize);
   else
      log_cb(RETRO_LOG_DEBUG, "PC=0x%08x\nSP=0x%08x\nTextStart=0x%08x\nTextSize=0x%08x\n", PC, SP, TextStart, TextSize);

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

   if(!TextMem_size)
   {
      TextMem_Start = TextStart;
      TextMem_resize(TextSize);
   }

   if(TextStart < TextMem_Start)
   {
      uint32 old_size = TextMem_size;

      //printf("RESIZE: 0x%08x\n", TextMem_Start - TextStart);

      TextMem_resize(old_size + TextMem_Start - TextStart);
      memmove(&TextMem[TextMem_Start - TextStart], &TextMem[0], old_size);

      TextMem_Start = TextStart;
   }

   if(TextMem_size < (TextStart - TextMem_Start + TextSize))
      TextMem_resize(TextStart - TextMem_Start + TextSize);

   memcpy(&TextMem[TextStart - TextMem_Start], data + 0x800, TextSize);

   // BIOS patch
   MASMEM_WriteU32(BIOSROM, 0x6990, (3 << 26) | ((0xBF001000 >> 2) & ((1 << 26) - 1)));

   po = &PIOMem->data8[0x0800];

   MDFN_en32lsb(po, (0x0 << 26) | (31 << 21) | (0x8 << 0)); // JR
   po += 4;
   MDFN_en32lsb(po, 0); // NOP(kinda)
   po += 4;

   po = &PIOMem->data8[0x1000];

   // Load cacheable-region target PC into r2
   MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16) | (0x9F001010 >> 16));      // LUI
   po += 4;
   MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (2 << 16) | (0x9F001010 & 0xFFFF));   // ORI
   po += 4;

   // Jump to r2
   MDFN_en32lsb(po, (0x0 << 26) | (2 << 21) | (0x8 << 0));  // JR
   po += 4;
   MDFN_en32lsb(po, 0); // NOP(kinda)
   po += 4;

   //
   // 0x9F001010:
   //

   // Load source address into r8
   sa = 0x9F000000 + 65536;
   MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16) | (sa >> 16));  // LUI
   po += 4;
   MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (8 << 16) | (sa & 0xFFFF));  // ORI
   po += 4;

   // Load dest address into r9
   MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (TextMem_Start >> 16));  // LUI
   po += 4;
   MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (9 << 16) | (TextMem_Start & 0xFFFF));   // ORI
   po += 4;

   // Load size into r10
   MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (TextMem_size >> 16)); // LUI
   po += 4;
   MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (10 << 16) | (TextMem_size & 0xFFFF));    // ORI
   po += 4;

   //
   // Loop begin
   //

   MDFN_en32lsb(po, (0x24 << 26) | (8 << 21) | (1 << 16));  // LBU to r1
   po += 4;

   MDFN_en32lsb(po, (0x08 << 26) | (10 << 21) | (10 << 16) | 0xFFFF);   // Decrement size
   po += 4;

   MDFN_en32lsb(po, (0x28 << 26) | (9 << 21) | (1 << 16));  // SB from r1
   po += 4;

   MDFN_en32lsb(po, (0x08 << 26) | (8 << 21) | (8 << 16) | 0x0001);  // Increment source addr
   po += 4;

   MDFN_en32lsb(po, (0x05 << 26) | (0 << 21) | (10 << 16) | (-5 & 0xFFFF));
   po += 4;
   MDFN_en32lsb(po, (0x08 << 26) | (9 << 21) | (9 << 16) | 0x0001);  // Increment dest addr
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
      MDFN_en32lsb(po, (0xF << 26) | (0 << 21) | (1 << 16)  | (SP >> 16)); // LUI
      po += 4;
      MDFN_en32lsb(po, (0xD << 26) | (1 << 21) | (29 << 16) | (SP & 0xFFFF));    // ORI
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
   MDFN_en32lsb(po, (0x0 << 26) | (2 << 21) | (0x8 << 0));  // JR
   po += 4;
   MDFN_en32lsb(po, 0); // NOP(kinda)
   po += 4;

#ifdef HAVE_LIGHTREC
   /* Reload Expansion1 copy */
   PSX_LoadExpansion1();
#endif

   return true;
}

/* Load a raw PS-X EXE. The previous version had two leaks:
 *   - malloc()'d a buffer, then immediately overwrote the pointer with
 *     filestream_read_file(), which allocates its own buffer. The
 *     original malloc was leaked unconditionally.
 *   - On LoadEXE failure, the function returned without freeing the
 *     read buffer or undoing InitCommon's allocations - and the
 *     frontend never calls retro_unload_game when retro_load_game
 *     fails, so those allocations would leak for the rest of the
 *     core's lifetime. */
static int Load(const char *name, RFILE *fp)
{
   int64_t size     = filestream_get_size(fp);
   char image_label[4096];
   void   *header   = NULL;
   int64_t len      = 0;

   image_label[0] = '\0';

   if (!TestMagic(name, fp, size))
   {
      MDFN_Error(0, "File format is unknown to module psx..");
      return -1;
   }

   InitCommon(true, true);

   TextMem_resize(0);

   if (size >= 0x800)
   {
      /* filestream_read_file allocates its own buffer; the previous
       * malloc-then-overwrite pattern leaked the malloc. */
      if (filestream_read_file(name, &header, &len) == 0 || !header)
      {
         MDFN_Error(0, "Failed to read \"%s\"", name);
         Cleanup();
         return -1;
      }

      if (!LoadEXE((const uint8_t *)header, (uint32_t)len, false))
      {
         free(header);
         Cleanup();
         return -1;
      }

      free(header);
   }

   sv_push(&disk_control_ext_info.image_paths, name);
   extract_basename(image_label, name, sizeof(image_label));
   sv_push(&disk_control_ext_info.image_labels, image_label);

   return 1;
}

static int LoadCD(void)
{
   InitCommon(true, false);

   if (psx_skipbios == 1 && BIOSROM)
      MASMEM_WriteU32(BIOSROM, 0x6990, 0);

   return 1;
}

static void Cleanup(void)
{
   TextMem_resize(0);

   if (PSX_CDC)
   {
      PS_CDC_Destroy(PSX_CDC);
      free(PSX_CDC);
   }
   PSX_CDC = NULL;

   SPU_Kill();

   GPU_Destroy();

   if (PSX_CPU)
   {
      CPU_Destroy(PSX_CPU);
      PSX_CPU = NULL;
   }

   if(PSX_FIO)
   {
      FrontIO_Free(PSX_FIO);
      PSX_FIO = NULL;
   }
   input_set_fio(NULL);

#ifdef HAVE_LIGHTREC
   /* InitCommon picks one of two allocation strategies based on whether
    * lightrec_init_mmap() was able to reserve the requested base
    * address: the mmap path stores pointers obtained from MAP() into
    * psx_mem/psx_bios/psx_scratch, while the fallback uses plain
    * operator new. We must free along whichever path was taken;
    * previously the HAVE_LIGHTREC branch always skipped delete and
    * relied solely on lightrec_free_mmap, leaking ~2.5MB per load
    * cycle on the fallback path. */
   if (psx_mmap > 0)
   {
      lightrec_free_mmap();
   }
   else
   {
      if (MainRAM)
         MultiAccessSizeMem_Free(MainRAM);
      if (ScratchRAM)
         MultiAccessSizeMem_Free(ScratchRAM);
      if (BIOSROM)
         MultiAccessSizeMem_Free(BIOSROM);
   }
   MainRAM    = NULL;
   ScratchRAM = NULL;
   BIOSROM    = NULL;
#else
   if (MainRAM)
      MultiAccessSizeMem_Free(MainRAM);
   MainRAM = NULL;

   if (ScratchRAM)
      MultiAccessSizeMem_Free(ScratchRAM);
   ScratchRAM = NULL;

   if (BIOSROM)
      MultiAccessSizeMem_Free(BIOSROM);
   BIOSROM = NULL;
#endif

   if(PIOMem)
      MultiAccessSizeMem_Free(PIOMem);
   PIOMem = NULL;

#ifdef HAVE_LIGHTREC
   PSX_FreeExpansion1();
#endif

   cdifs_loaded = 0;
}

static void CloseGame(void)
{
   int i;

   /* If load failed partway through, PSX_FIO may be NULL while the
    * cdifs array and other state still exist. Skip memcard saves in
    * that case rather than segfaulting. */
   if (PSX_FIO)
   {
      for (i = 0; i < 8; i++)
      {
         char ext[64];
         char memcard[4096];

         if (i == 0 && !use_mednafen_memcard0_method)
         {
            FrontIO_SaveMemcard(PSX_FIO, i);
            continue;
         }

         /* If saving one memcard fails, keep trying the others to
          * minimize potential data loss. SaveMemcard does not throw
          * (it logs and returns on error). */
         if (i == 0)
            snprintf(ext, sizeof(ext), "%d.mcr", memcard_left_index);
         else if (i == 1)
            snprintf(ext, sizeof(ext), "%d.mcr", memcard_right_index);
         else
            snprintf(ext, sizeof(ext), "%d.mcr", i);
         MDFN_MakeFName(MDFNMKF_SAV, 0, ext, memcard, sizeof(memcard));
         FrontIO_SaveMemcardToPath(PSX_FIO, i, memcard, false);
      }
   }

   Cleanup();
}

static void CDInsertEject(void)
{
   CD_TrayOpen = !CD_TrayOpen;

   /* cdifs_loaded is 0 when no game is loaded; individual entries may
    * still be NULL after disk_add_image_index() reserves a slot but
    * before disk_replace_image_index() fills it. */
   if (cdifs_loaded)
   {
      size_t disc;
      for (disc = 0; disc < cdifs.count; disc++)
      {
         CDIF *cdif = cdifs.items[disc];
         if (!cdif)
            continue;
         if (!CDIF_Eject(cdif, CD_TrayOpen))
            CD_TrayOpen = !CD_TrayOpen;
      }
   }

   SetDiscWrapper(CD_TrayOpen);
}

static void CDEject(void)
{
   if(!CD_TrayOpen)
      CDInsertEject();
}

static void CDSelect(void)
{
   if(cdifs_loaded && CD_TrayOpen)
   {
      int disc_count = (CD_IsPBP ? PBP_PhysicalDiscCount : (int)cdifs.count);

      CD_SelectedDisc = (CD_SelectedDisc + 1) % (disc_count + 1);

      if(CD_SelectedDisc == disc_count)
         CD_SelectedDisc = -1;
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
         if(!cdifs_loaded || CD_SelectedDisc >= PBP_PhysicalDiscCount)
            CD_SelectedDisc = -1;

         CDEject();
         CDInsertEject();
      } else {
         if(!cdifs_loaded || CD_SelectedDisc >= (int)cdifs.count)
            CD_SelectedDisc = -1;

         SetDiscWrapper(CD_TrayOpen);
      }
   }

   // TODO: Remember to increment dirty count in memory card state loading routine.

   ret &= CPU_StateAction(PSX_CPU, sm, load, data_only);
   ret &= DMA_StateAction(sm, load, data_only);
   ret &= TIMER_StateAction(sm, load, data_only);
   ret &= SIO_StateAction(sm, load, data_only);

   ret &= PS_CDC_StateAction(PSX_CDC, sm, load, data_only);
   ret &= MDEC_StateAction(sm, load, data_only);
   ret &= GPU_StateAction(sm, load, data_only);
   ret &= SPU_StateAction(sm, load, data_only);

   ret &= FrontIO_StateAction(PSX_FIO, sm, load, data_only);

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

   if (patch->conditions[0] != 0)
      strlcat(patch->conditions, ", ", sizeof(patch->conditions));

   if (len == 2)
      snprintf(tmp, 256, "%u L 0x%08x %s 0x%04x", len, addr, cc, val & 0xFFFFU);
   else
      snprintf(tmp, 256, "%u L 0x%08x %s 0x%02x", len, addr, cc, val & 0xFFU);

   strlcat(patch->conditions, tmp, sizeof(patch->conditions));
}

static bool DecodeGS(const char *cheat_string, MemoryPatch *patch)
{
   uint64 code = 0;
   unsigned nybble_count = 0;
   const size_t len = strlen(cheat_string);
   uint8  code_type;
   uint64 cl;
   unsigned i;

   for(i = 0; i < len; i++)
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
            log_cb(RETRO_LOG_ERROR, "Invalid character in GameShark code..\n");
         else
            log_cb(RETRO_LOG_ERROR, "Invalid character in GameShark code: %c.\n", cheat_string[i]);
         return false;
      }
   }

   if(nybble_count != 12)
   {
      log_cb(RETRO_LOG_ERROR, "GameShark code is of an incorrect length.\n");
      return false;
   }

   code_type = code >> 40;
   cl = code & 0xFFFFFFFFFFULL;

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

/* end of Mednafen psx.cpp */

//forward decls
extern void Emulate(EmulateSpecStruct *espec);

#ifdef NEED_DEINTERLACER
static bool PrevInterlaced;
static Deinterlacer deint;
#endif

static MDFN_Surface *surf = NULL;

static void alloc_surface(void)
{
   uint32_t width  = MEDNAFEN_CORE_GEOMETRY_MAX_W;
   uint32_t height = content_is_pal ? MEDNAFEN_CORE_GEOMETRY_MAX_H : 480;

   width  <<= GPU_get_upscale_shift();
   height <<= GPU_get_upscale_shift();

   if (surf != NULL)
      MDFN_Surface_Delete(surf);

   surf = MDFN_Surface_New(width, height, width);
}

static void check_system_specs(void)
{
   // Hints that we need a fairly powerful system to run this.
   unsigned level = 15;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

static unsigned disk_get_num_images(void)
{
   if(cdifs_loaded)
      return CD_IsPBP ? PBP_PhysicalDiscCount : (unsigned)cdifs.count;
   return 0;
}

static bool disk_set_eject_state(bool ejected)
{
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
   unsigned num_images = disk_get_num_images();

   /* The frontend's contract on this callback is that index is in
    * [0, num_images). Be defensive: refuse impossible values rather
    * than letting them flow through to CD_SelectedDisc--. */
   if (num_images == 0)
      return false;
   if (index >= num_images)
      index = num_images - 1;

   /* CD_SelectedDisc is signed (-1 sentinel for "no disc"); the cast
    * is safe because we just bounded index above. The decrement is
    * because CDSelect's command path increments first. */
   CD_SelectedDisc = (int)index - 1;

   DoSimpleCommand(MDFN_MSC_SELECT_DISK);
   return true;
}

// Mednafen PSX really doesn't support adding disk images on the fly ...
// Hack around this.

// Untested ...
static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
   if (!cdifs_loaded || index >= disk_get_num_images() || !eject_state || CD_IsPBP)
      return false;

   if (!info)
   {
      CDIF_Close(cdifs.items[index]);
      cdif_array_remove_at(&cdifs, index);
      /* CD_SelectedDisc is signed; explicit cast to silence the
       * mixed-sign comparison and to make the intent clear. */
      if ((int)index < CD_SelectedDisc)
         CD_SelectedDisc--;

      sv_erase(&disk_control_ext_info.image_paths, index);
      sv_erase(&disk_control_ext_info.image_labels, index);

      // Poke into psx.cpp
      CalcDiscSCEx();
      return true;
   }

   {
      bool success = true;
      CDIF *iface  = CDIF_Open(&success, info->path, false, false);

      if (!success || !iface)
      {
         /* CDIF_Open's contract is to return NULL when success is false,
          * but be defensive about a partially-constructed CDIF leak. */
         if (iface)
            CDIF_Close(iface);
         return false;
      }

      CDIF_Close(cdifs.items[index]);
      cdifs.items[index] = iface;
      CalcDiscSCEx();

      /* If we replace, we want the "swap disk manually effect". */
      extract_basename(retro_cd_base_name, info->path, sizeof(retro_cd_base_name));

      /* Update disk path/label vectors */
      sv_set(&disk_control_ext_info.image_paths, index, info->path);
      sv_set(&disk_control_ext_info.image_labels, index, retro_cd_base_name);
   }

   return true;
}

static bool disk_add_image_index(void)
{
   if (CD_IsPBP || !cdifs_loaded)
      return false;

   if (cdif_array_push(&cdifs, NULL) < 0)
      return false;
   sv_push(&disk_control_ext_info.image_paths, "");
   sv_push(&disk_control_ext_info.image_labels, "");
   return true;
}

static bool disk_set_initial_image(unsigned index, const char *path)
{
	if (string_is_empty(path))
		return false;

	disk_control_ext_info.initial_index = index;
	(free(disk_control_ext_info.initial_path), disk_control_ext_info.initial_path = strdup(path));

	return true;
}

static bool disk_get_image_path(unsigned index, char *path, size_t len)
{
	if (len < 1)
		return false;

	if ((index < disk_get_num_images()) &&
		 (index < disk_control_ext_info.image_paths.count))
	{
		if (!string_is_empty(disk_control_ext_info.image_paths.items[index]))
		{
			strlcpy(path, disk_control_ext_info.image_paths.items[index], len);
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
		 (index < disk_control_ext_info.image_labels.count))
	{
		if (!string_is_empty(disk_control_ext_info.image_labels.items[index]))
		{
			strlcpy(label, disk_control_ext_info.image_labels.items[index], len);
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
   va_list va;
   (void)level;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

void retro_init(void)
{
   struct retro_log_callback log;
   uint64_t serialization_quirks = RETRO_SERIALIZATION_QUIRK_CORE_VARIABLE_SIZE;
   unsigned dci_version          = 0;
   const char *dir               = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = fallback_log;

#ifdef NEED_DEINTERLACER
   /* The deinterlacer is a static-storage struct, so all fields
    * are zero-initialised at process start; explicit init is
    * still needed for the DeintType default (DEINT_WEAVE), which
    * is a non-zero enum value. */
   Deinterlacer_Init(&deint);
#endif

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
   {
      snprintf(retro_base_directory, sizeof(retro_base_directory), "%s", dir);
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

   CDUtility_Init();
   eject_state = false;

   /* Initialise disk control interface */
   disk_control_ext_info.initial_index = 0;
   (free(disk_control_ext_info.initial_path), disk_control_ext_info.initial_path = NULL);
   sv_clear(&disk_control_ext_info.image_paths);
   sv_clear(&disk_control_ext_info.image_labels);

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

   libretro_msg_interface_version = 0;
   environ_cb(RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION, &libretro_msg_interface_version);

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
   /* PSX_Power() touches MainRAM, PSX_CPU, etc. - all NULL before
    * retro_load_game and after retro_unload_game. */
   if (!MainRAM || !PSX_CPU || !PSX_FIO)
      return;

   DoSimpleCommand(MDFN_MSC_RESET);
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
   return false;
}

#ifdef EMSCRIPTEN
static bool cdimagecache = true;
#else
static bool cdimagecache = false;
#endif

static bool boot = true;

// shared memory cards support
static bool shared_memorycards = false;

static bool has_new_geometry = false;
static bool has_new_timing = false;

uint8_t analog_combo[2] = {0};
uint8_t analog_combo_hold = 0;

extern void PSXDitherApply(bool);

static void check_variables(bool startup)
{
   struct retro_variable var = {0};

   /* Region default fallback (used by CalcDiscSCEx() for non-disc
    * content like raw PS-X EXEs, and for any disc whose region cannot
    * be determined from SYSTEM.CNF / "Licensed by" header).  Disc
    * region detection still wins for normal CD content.  Read on
    * startup only -- changing region implies a different BIOS file and
    * a fresh GPU_Init(), neither of which we can do mid-session. */
   if (startup)
   {
      var.key = BEETLE_OPT(region);
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "ntsc-j"))
            setting_region_default = 0; /* REGION_JP */
         else if (!strcmp(var.value, "ntsc-u"))
            setting_region_default = 1; /* REGION_NA */
         else if (!strcmp(var.value, "pal"))
            setting_region_default = 2; /* REGION_EU */
         else
            setting_region_default = 1; /* "auto" -> NA fallback */
      }
   }

#ifndef EMSCRIPTEN
   var.key = BEETLE_OPT(cd_access_method);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "sync") == 0)
      {
         cdimagecache = false;
         cd_async = false;
      }
      else if (strcmp(var.value, "async") == 0)
      {
         cdimagecache = false;
         cd_async = true;
      }
      else if (strcmp(var.value, "precache") == 0)
      {
         cdimagecache = true;
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

   var.key = BEETLE_OPT(dynarec_spu_samples);

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
	spu_samples = atoi(var.value);
   }
   else
      spu_samples = 1;
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
         uint8_t n;

         // Upscale must be a power of two
         assert((val & (val - 1)) == 0);

         // Crappy "ffs" implementation since the standard function is not
         // widely supported by libc in the wild
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

   var.key = BEETLE_OPT(override_bios);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "disabled"))
      {
         override_bios = 0;
      }
      else if (!strcmp(var.value, "psxonpsp"))
	  {
         override_bios = 1;
      }
      else if (!strcmp(var.value, "ps1_rom"))
      {
         override_bios = 2;
      }
      else if (!strcmp(var.value, "openbios"))
      {
         override_bios = 3;
      }
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

   var.key = BEETLE_OPT(widescreen_hack_aspect_ratio);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "16:10"))
      {
         if (!startup && widescreen_hack_aspect_ratio_setting != 0)
            has_new_geometry = true;
         widescreen_hack_aspect_ratio_setting = 0;
      }
      else if (!strcmp(var.value, "16:9"))
      {
         if (!startup && widescreen_hack_aspect_ratio_setting != 1)
            has_new_geometry = true;
         widescreen_hack_aspect_ratio_setting = 1;
      }
      else if (!strcmp(var.value, "18:9"))
      {
         if (!startup && widescreen_hack_aspect_ratio_setting != 2)
            has_new_geometry = true;
         widescreen_hack_aspect_ratio_setting = 2;
      }
      else if (!strcmp(var.value, "19:9"))
      {
         if (!startup && widescreen_hack_aspect_ratio_setting != 3)
            has_new_geometry = true;
         widescreen_hack_aspect_ratio_setting = 3;
      }
      else if (!strcmp(var.value, "20:9"))
      {
         if (!startup && widescreen_hack_aspect_ratio_setting != 4)
            has_new_geometry = true;
         widescreen_hack_aspect_ratio_setting = 4;
      }
      else if (!strcmp(var.value, "21:9")) // 64:27
      {
         if (!startup && widescreen_hack_aspect_ratio_setting != 5)
            has_new_geometry = true;
         widescreen_hack_aspect_ratio_setting = 5;
      }
      else if (!strcmp(var.value, "32:9"))
      {
         if (!startup && widescreen_hack_aspect_ratio_setting != 6)
            has_new_geometry = true;
         widescreen_hack_aspect_ratio_setting = 6;
      }
   }
   else
   {
      if (!startup && widescreen_hack_aspect_ratio_setting != 1)
         has_new_geometry = true;
      widescreen_hack_aspect_ratio_setting = 1;
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
      bool hw_renderer = false;

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
      var.key = BEETLE_OPT(renderer);
      if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      {
         if (!strcmp(var.value, "hardware") || !strcmp(var.value, "hardware_gl") || !strcmp(var.value, "hardware_vk"))
         {
            hw_renderer = true;
         }
      }
#endif

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
         psx_gpu_upscale_shift_hw = new_upscale_shift;
      }
      else
         psx_gpu_upscale_shift_hw = 0;
      
      if (hw_renderer)
         psx_gpu_upscale_shift = 0;
      else
         psx_gpu_upscale_shift = psx_gpu_upscale_shift_hw;
   }
   else
   {
      rsx_intf_refresh_variables();

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
         psx_gpu_upscale_shift_hw = new_upscale_shift;
      }
      else
         psx_gpu_upscale_shift_hw = 0;

      switch (rsx_intf_is_type())
      {
         case RSX_SOFTWARE:
            psx_gpu_upscale_shift = psx_gpu_upscale_shift_hw;
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

   var.key = BEETLE_OPT(pgxp_2d_tol);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         psx_pgxp_2d_tol = -1;
      else
         psx_pgxp_2d_tol = atoi(var.value);
   }
   else
      psx_pgxp_2d_tol = -1;

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

   var.key = BEETLE_OPT(pgxp_nclip);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "disabled") == 0)
         psx_pgxp_nclip = PGXP_MODE_NONE;
      else if (strcmp(var.value, "enabled") == 0)
         psx_pgxp_nclip = PGXP_NCLIP_IMPL;
   }
   else
      psx_pgxp_nclip = PGXP_MODE_NONE;

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
         /* opaque_check / semitrans_check were set here; both were
          * write-only globals with no remaining readers (the
          * renderer cleanup path that consumed them was removed
          * long ago). Removed. */
         old_filter_mode = filter_mode;
      }
   }

   var.key = BEETLE_OPT(analog_toggle);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if ((strcmp(var.value, "disabled") == 0)
            && setting_psx_analog_toggle)
      {
         setting_psx_analog_toggle = 0;
         setting_apply_analog_toggle = true;
         setting_apply_analog_default = false;
      }
      else if ((strcmp(var.value, "enabled") == 0)
            && (!setting_psx_analog_toggle || setting_apply_analog_default))
      {
         setting_psx_analog_toggle = 1;
         setting_apply_analog_toggle = true;
         setting_apply_analog_default = false;
      }
      else if ((strcmp(var.value, "enabled-analog") == 0)
            && (!setting_psx_analog_toggle || !setting_apply_analog_default))
      {
         setting_psx_analog_toggle = 1;
         setting_apply_analog_toggle = true;
         setting_apply_analog_default = true;
      }

      /* No need to apply if going to do it in InitCommon */
      if (startup)
         setting_apply_analog_toggle = false;
   }

   var.key = BEETLE_OPT(analog_toggle_combo);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "l1+l2+r1+r2+start+select") == 0)
      {
         analog_combo[0] = 0x09;
         analog_combo[1] = 0x0f;
      }
      else if (strcmp(var.value, "l1+r1+select") == 0)
      {
         analog_combo[0] = 0x01;
         analog_combo[1] = 0x0c;
      }
      else if (strcmp(var.value, "l1+r1+start") == 0)
      {
         analog_combo[0] = 0x08;
         analog_combo[1] = 0x0c;
      }
      else if (strcmp(var.value, "l1+r1+l3") == 0)
      {
         analog_combo[0] = 0x02;
         analog_combo[1] = 0x0c;
      }
      else if (strcmp(var.value, "l1+r1+r3") == 0)
      {
         analog_combo[0] = 0x04;
         analog_combo[1] = 0x0c;
      }
      else if (strcmp(var.value, "l2+r2+select") == 0)
      {
         analog_combo[0] = 0x01;
         analog_combo[1] = 0x03;
      }
      else if (strcmp(var.value, "l2+r2+start") == 0)
      {
         analog_combo[0] = 0x08;
         analog_combo[1] = 0x03;
      }
      else if (strcmp(var.value, "l2+r2+l3") == 0)
      {
         analog_combo[0] = 0x02;
         analog_combo[1] = 0x03;
      }
      else if (strcmp(var.value, "l2+r2+r3") == 0)
      {
         analog_combo[0] = 0x04;
         analog_combo[1] = 0x03;
      }
      else if (strcmp(var.value, "l3+r3") == 0)
      {
         analog_combo[0] = 0x06;
         analog_combo[1] = 0x00;
      }
   }

   var.key = BEETLE_OPT(analog_toggle_hold);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      analog_combo_hold = atoi(var.value);
   }

   var.key = BEETLE_OPT(crosshair_color_p1);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "red") == 0)
         setting_crosshair_color_p1 = 0xFF0000;
      else if (strcmp(var.value, "blue") == 0)
         setting_crosshair_color_p1 = 0x0080FF;
      else if (strcmp(var.value, "green") == 0)
         setting_crosshair_color_p1 = 0x00FF00;
      else if (strcmp(var.value, "orange") == 0)
         setting_crosshair_color_p1 = 0xFF8000;
      else if (strcmp(var.value, "yellow") == 0)
         setting_crosshair_color_p1 = 0xFFFF00;
      else if (strcmp(var.value, "cyan") == 0)
         setting_crosshair_color_p1 = 0x00FFFF;
      else if (strcmp(var.value, "pink") == 0)
         setting_crosshair_color_p1 = 0xFF00FF;
      else if (strcmp(var.value, "purple") == 0)
         setting_crosshair_color_p1 = 0x8000FF;
      else if (strcmp(var.value, "black") == 0)
         setting_crosshair_color_p1 = 0x000000;
      else if (strcmp(var.value, "white") == 0)
         setting_crosshair_color_p1 = 0xFFFFFF;
   }

   var.key = BEETLE_OPT(crosshair_color_p2);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "red") == 0)
         setting_crosshair_color_p2 = 0xFF0000;
      else if (strcmp(var.value, "blue") == 0)
         setting_crosshair_color_p2 = 0x0080FF;
      else if (strcmp(var.value, "green") == 0)
         setting_crosshair_color_p2 = 0x00FF00;
      else if (strcmp(var.value, "orange") == 0)
         setting_crosshair_color_p2 = 0xFF8000;
      else if (strcmp(var.value, "yellow") == 0)
         setting_crosshair_color_p2 = 0xFFFF00;
      else if (strcmp(var.value, "cyan") == 0)
         setting_crosshair_color_p2 = 0x00FFFF;
      else if (strcmp(var.value, "pink") == 0)
         setting_crosshair_color_p2 = 0xFF00FF;
      else if (strcmp(var.value, "purple") == 0)
         setting_crosshair_color_p2 = 0x8000FF;
      else if (strcmp(var.value, "black") == 0)
         setting_crosshair_color_p2 = 0x000000;
      else if (strcmp(var.value, "white") == 0)
         setting_crosshair_color_p2 = 0xFFFFFF;
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
         input_set_gun_cursor(SETTING_GUN_CROSSHAIR_OFF);
      else if (strcmp(var.value, "cross") == 0)
         input_set_gun_cursor(SETTING_GUN_CROSSHAIR_CROSS);
      else if (strcmp(var.value, "dot") == 0)
         input_set_gun_cursor(SETTING_GUN_CROSSHAIR_DOT);
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
            // if(use_mednafen_memcard0_method)
               shared_memorycards = true;
            // else
               // osd_message(3, RETRO_LOG_WARN,
                     // RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
                     // "Memory Card 0 Method not set to Mednafen; shared memory cards could not be enabled.");
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

   var.key = BEETLE_OPT(display_osd);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "enabled") == 0)
         display_notifications = true;
      else if (strcmp(var.value, "disabled") == 0)
         display_notifications = false;
   }
   else
      display_notifications = true;

   var.key = BEETLE_OPT(crop_overscan);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      int old_crop_overscan = crop_overscan;
      if (strcmp(var.value, "disabled") == 0)
         crop_overscan = 0;
      else if (strcmp(var.value, "static") == 0)
         crop_overscan = 1;
      else if (strcmp(var.value, "smart") == 0)
         crop_overscan = 2;

      if (crop_overscan != old_crop_overscan)
         has_new_geometry = true;
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

   /* Apply per-game compatibility cap if the loaded disc is on the
    * known-fragile list.  Silent clamp - the one-time log message at
    * detection covers the user notification. */
   if (cd_speedup_compat_max && cd_2x_speedup > cd_speedup_compat_max)
      cd_2x_speedup = cd_speedup_compat_max;

   var.key = BEETLE_OPT(memcard_left_index);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      memcard_left_index_old = memcard_left_index;
      memcard_left_index     = atoi(var.value);
   }

   var.key = BEETLE_OPT(memcard_right_index);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      memcard_right_index_old = memcard_right_index;
      memcard_right_index     = atoi(var.value);
   }

   var.key = BEETLE_OPT(deinterlacer);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (strcmp(var.value, "bob") == 0)
         Deinterlacer_SetType(&deint, DEINT_BOB);
      else if (strcmp(var.value, "off") == 0)
         Deinterlacer_SetType(&deint, DEINT_OFF);
      else
         Deinterlacer_SetType(&deint, DEINT_WEAVE);

      psx_gpu_rasterize_both_fields = (Deinterlacer_GetType(&deint) == DEINT_OFF);
   }
}

#ifdef NEED_CD
/* Read an M3U playlist into file_list, recursively expanding any
 * referenced .m3u entries.
 *
 * Two latent issues fixed here:
 *   1. The depth check used post-increment ("depth++"), so the
 *      recursive call received the original depth value. Mutual
 *      recursion (a.m3u -> b.m3u -> a.m3u) could pile up frames
 *      indefinitely. Each frame holds a 2KB linebuf plus a few
 *      std::strings; on PSP/Vita-class devices with small stacks
 *      that would overflow.
 *   2. The self-reference check (efp == path) only caught direct
 *      cycles, not mutual ones. The depth limit now bounds total
 *      recursion regardless, lowered from 99 to 8 since any
 *      legitimate playlist fits well within that. */
#define M3U_MAX_DEPTH 8

static void ReadM3U(string_vec_t *file_list, const char *path, unsigned depth)
{
   char  dir_path[4096];
   char  linebuf[2048];
   RFILE *fp;

   if (depth >= M3U_MAX_DEPTH)
   {
      log_cb(RETRO_LOG_ERROR, "M3U recursion limit (%u) reached at \"%s\"\n",
            M3U_MAX_DEPTH, path);
      return;
   }

   fp = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);

   if (fp == NULL)
      return;

   MDFN_GetFilePathComponents_c(path,
         dir_path, sizeof(dir_path),
         NULL, 0, NULL, 0);

   while(filestream_gets(fp, linebuf, sizeof(linebuf)) != NULL)
   {
      char efp_buf[4096];
      size_t efp_len;

      if(linebuf[0] == '#')
         continue;
      string_trim_whitespace_right(linebuf);
      if(linebuf[0] == 0)
         continue;

      MDFN_EvalFIP_c(dir_path, linebuf, efp_buf, sizeof(efp_buf));

      efp_len = strlen(efp_buf);

      if(efp_len >= 4 && !strcmp(efp_buf + efp_len - 4, ".m3u"))
      {
         if(!strcmp(efp_buf, path))
         {
            log_cb(RETRO_LOG_ERROR, "M3U at \"%s\" references self.\n", efp_buf);
            goto end;
         }

         /* Pre-increment so the depth limit actually trips. */
         ReadM3U(file_list, efp_buf, depth + 1);
      }
      else
         sv_push(file_list, efp_buf);
   }

end:
   filestream_close(fp);
}

// TODO: LoadCommon()

/* Tear down the per-game disc state (CDIF array + the image
 * path/label vectors + the initial-index hint).  Call this from
 * every error path in MDFNI_LoadCD and from retro_unload_game so
 * three previously-duplicated cleanup blocks - which all also got
 * the broken `delete` wrong - share one correct implementation. */
static void clear_disc_state(void)
{
   cdif_array_clear(&cdifs);

   disk_control_ext_info.initial_index = 0;
   (free(disk_control_ext_info.initial_path), disk_control_ext_info.initial_path = NULL);
   sv_clear(&disk_control_ext_info.image_paths);
   sv_clear(&disk_control_ext_info.image_labels);
}

static bool MDFNI_LoadCD(const char *devicename)
{
   size_t devicename_len;
   bool   load_ok = true;

   log_cb(RETRO_LOG_INFO, "Loading \"%s\"\n", devicename);

   if (!devicename)
      return false;

   devicename_len = strlen(devicename);

   if (devicename_len > 4 && !strcasecmp(devicename + devicename_len - 4, ".m3u"))
   {
      ReadM3U(&disk_control_ext_info.image_paths, devicename, 0);

      for (unsigned i = 0; i < disk_control_ext_info.image_paths.count; i++)
      {
         char image_label[4096];
         bool success = true;
         CDIF *image  = CDIF_Open(&success,
               disk_control_ext_info.image_paths.items[i], false, cdimagecache);

         if (!success || !image)
         {
            log_cb(RETRO_LOG_ERROR, "Error opening CD: %s\n",
                  disk_control_ext_info.image_paths.items[i]);
            if (image)
               CDIF_Close(image);
            load_ok = false;
            break;
         }

         cdif_array_push(&cdifs, image);

         image_label[0] = '\0';
         extract_basename(image_label,
               disk_control_ext_info.image_paths.items[i],
               sizeof(image_label));
         sv_push(&disk_control_ext_info.image_labels, image_label);
      }
   }
   else if (devicename_len > 4 && !strcasecmp(devicename + devicename_len - 4, ".pbp"))
   {
      bool success = true;
      CDIF *image  = CDIF_Open(&success, devicename, false, cdimagecache);

      if (!success || !image)
      {
         log_cb(RETRO_LOG_ERROR, "Error opening PBP: %s\n", devicename);
         if (image)
            CDIF_Close(image);
         load_ok = false;
      }
      else
      {
         CD_IsPBP = true;
         cdif_array_push(&cdifs, image);

         /* CDIF_Open() sets PBP_DiscCount, so we can populate
          * image_paths/image_labels here */
         PBP_PhysicalDiscCount = (PBP_DiscCount == 0) ?
               1 : PBP_DiscCount;

         for (unsigned i = 0; i < PBP_PhysicalDiscCount; i++)
         {
            /* image_name is at most 4096 - 4 (removing ".pbp")
             * gives label room to add index and quiets gcc warnings */
            char image_name[4092];
            char image_label[4096];
            char idx_suffix[16]; /* " #" + uint up to 10 digits + NUL */

            image_name[0]  = '\0';
            image_label[0] = '\0';

            /* All 'disks' have the same path when using
             * multi-disk PBP files */
            sv_push(&disk_control_ext_info.image_paths, devicename);

            /* Label is name+index. Build the suffix in a small fixed
             * buffer first, then concatenate via strlcpy/strlcat so
             * gcc can see no truncation is possible into image_label. */
            extract_basename(image_name, devicename, sizeof(image_name));
            snprintf(idx_suffix, sizeof(idx_suffix), " #%u", i + 1);
            strlcpy(image_label, image_name, sizeof(image_label));
            strlcat(image_label, idx_suffix, sizeof(image_label));
            sv_push(&disk_control_ext_info.image_labels, image_label);
         }
      }
   }
   else
   {
      char image_label[4096];
      bool success = true;
      bool cache = cdimagecache;
      CDIF *image;

      /* Don't precache if physical cdrom, will take way too long and be unresponsive */
      if (cdimagecache && !strncasecmp(devicename, "cdrom:", 6))
      {
         cache = false;
         log_cb(RETRO_LOG_INFO, "Skipping Pre-Cache due to using physical media: %s\n", devicename);
      }

      image = CDIF_Open(&success, devicename, false, cache);
      if (!success || !image)
      {
         log_cb(RETRO_LOG_ERROR, "Error opening CD: %s\n", devicename);
         if (image)
            CDIF_Close(image);
         /* No partial state has been pushed, so a bare return is
          * still safe here.  Routing through clear_disc_state would
          * be a no-op but stays consistent with the other branches. */
         clear_disc_state();
         return false;
      }

      cdif_array_push(&cdifs, image);

      image_label[0] = '\0';
      sv_push(&disk_control_ext_info.image_paths, devicename);
      extract_basename(image_label, devicename, sizeof(image_label));
      sv_push(&disk_control_ext_info.image_labels, image_label);
   }

   if (!load_ok)
   {
      clear_disc_state();
      return false;
   }

#ifdef DEBUG
   // Print out a track list for all discs.
   for(unsigned i = 0; i < cdifs.count; i++)
   {
      TOC toc;
      TOC_Clear(&toc);

      CDIF_ReadTOC(cdifs.items[i], &toc);

      log_cb(RETRO_LOG_DEBUG, "CD %d Layout:\n", i + 1);

      for(int32 track = toc.first_track; track <= toc.last_track; track++)
      {
         log_cb(RETRO_LOG_DEBUG, "Track %2d, LBA: %6d  %s\n", track, toc.tracks[track].lba, (toc.tracks[track].control & 0x4) ? "DATA" : "AUDIO");
      }

      log_cb(RETRO_LOG_DEBUG, "Leadout: %6d\n", toc.tracks[100].lba);
   }
#endif

   if(!LoadCD())
   {
      clear_disc_state();
      return false;
   }

   //MDFNI_SetLayerEnableMask(~0ULL);

   MDFN_LoadGameCheats(NULL);
   MDFNMP_InstallReadPatches();

   return true;
}
#endif

static bool MDFNI_LoadGame(const char *name)
{
   RFILE *GameFile = NULL;
   size_t name_len;

   if (!name)
      return false;
   name_len = strlen(name);

   if(name_len > 3 && (
      !strcasecmp(name + name_len - 3, "cue") ||
      !strcasecmp(name + name_len - 3, "ccd") ||
      !strcasecmp(name + name_len - 3, "toc") ||
      !strcasecmp(name + name_len - 3, "m3u") ||
      !strcasecmp(name + name_len - 3, "chd") ||
      !strcasecmp(name + name_len - 3, "pbp")
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

   return true;

error:
   if (GameFile)
      filestream_close(GameFile);
   GameFile     = NULL;

   return false;
}

bool retro_load_game(const struct retro_game_info *info)
{
   char tocbasepath[4096];
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   int r;
   unsigned disc_region;
   bool force_software_renderer;
   bool ret;
   struct retro_core_option_display option_display;

   if (!info || !info->path)
      return false;

   input_init_env(environ_cb);

   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
      return false;

   extract_basename(retro_cd_base_name,       info->path, sizeof(retro_cd_base_name));
   extract_directory(retro_cd_base_directory, info->path, sizeof(retro_cd_base_directory));

   r = snprintf(tocbasepath, sizeof(tocbasepath), "%s%c%s.toc", retro_cd_base_directory, retro_slash, retro_cd_base_name);

   if (r >= 0 && r < (int)sizeof(tocbasepath) && filestream_exists(tocbasepath))
      snprintf(retro_cd_path, sizeof(retro_cd_path), "%s", tocbasepath);
   else
      snprintf(retro_cd_path, sizeof(retro_cd_path), "%s", info->path);

   check_variables(true);

   if (!MDFNI_LoadGame(retro_cd_path))
      return false;

   MDFN_LoadGameCheats(NULL);
   MDFNMP_InstallReadPatches();

   // Determine content_is_pal before calling alloc_surface()
   cd_speedup_compat_max = 0; /* reset; CalcDiscSCEx may repopulate */
   disc_region = CalcDiscSCEx();
   content_is_pal = (disc_region == REGION_EU);

   /* CalcDiscSCEx may have populated cd_speedup_compat_max from the
    * disc serial.  check_variables(true) above ran before disc
    * identification, so re-apply the cap here to honour it on the
    * very first frame as well. */
   if (cd_speedup_compat_max && cd_2x_speedup > cd_speedup_compat_max)
      cd_2x_speedup = cd_speedup_compat_max;

   alloc_surface();

#ifdef NEED_DEINTERLACER
   PrevInterlaced = false;
   Deinterlacer_ClearState(&deint);
#endif

   input_init();

   boot = false;

   frame_count = 0;
   internal_frame_count = 0;

   // MDFNI_LoadGame() has been called and surface has been allocated,
   // we can now perform firmware check
   force_software_renderer = false;
   ret = rsx_intf_open(content_is_pal, force_software_renderer);

   /* Hide irrelevant core options */
   switch (rsx_intf_is_type())
   {
      case RSX_SOFTWARE:
      {
         struct retro_core_option_display option_display;
         option_display.visible = false;

         option_display.key = BEETLE_OPT(renderer_software_fb);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(scaled_uv_offset);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(filter_exclude_sprite);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(filter_exclude_2d_polygon);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(adaptive_smoothing);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(super_sampling);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(msaa);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(mdec_yuv);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(track_textures);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(dump_textures);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(replace_textures);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(depth);
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

         option_display.key = BEETLE_OPT(scaled_uv_offset);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(filter_exclude_sprite);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(filter_exclude_2d_polygon);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(adaptive_smoothing);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(super_sampling);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(msaa);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(mdec_yuv);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(track_textures);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(dump_textures);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);
         option_display.key = BEETLE_OPT(replace_textures);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         option_display.key = BEETLE_OPT(image_offset);
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

         option_display.key = BEETLE_OPT(image_offset);
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY, &option_display);

         break;
      }
   }

   /* Hide irrelevant scanline core options for current content */
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
   rsx_intf_close();

   MDFN_FlushGameCheats(0);

   CloseGame();

   MDFNMP_Kill();

   clear_disc_state();

   retro_cd_base_directory[0] = '\0';
   retro_cd_path[0]           = '\0';
   retro_cd_base_name[0]      = '\0';
}

static bool retro_set_geometry(void)
{
   struct retro_system_av_info new_av_info;

   retro_get_system_av_info(&new_av_info);
   return environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &new_av_info);
}

static bool retro_set_system_av_info(void)
{
   struct retro_system_av_info new_av_info;

   retro_get_system_av_info(&new_av_info);
   return environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &new_av_info);
}

void retro_run(void)
{
   bool updated = false;
   static int32 rects[MEDNAFEN_CORE_GEOMETRY_MAX_H];
   EmulateSpecStruct spec = {0};
   EmulateSpecStruct *espec;
   int32_t timestamp = 0;
   const void     *fb;
   unsigned        width;
   unsigned        height;
   uint8_t         upscale_shift;
   const uint32_t *pix;
   unsigned        pix_offset;

   /* Defensive: a frontend should not call retro_run before
    * retro_load_game succeeds, but if it does we'd crash on the
    * unconditional PSX_CPU->Run / PSX_FIO->UpdateInput calls below. */
   if (!PSX_CPU || !PSX_FIO || !PSX_CDC)
      return;

   rsx_intf_prepare_frame();

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
   {
      check_variables(false);

      /* Max width/height changed, need to call SET_SYSTEM_AV_INFO */
      if (GPU_get_upscale_shift() != psx_gpu_upscale_shift)
      {
         if (retro_set_system_av_info())
         {
            /* We successfully changed the frontend's resolution.
             * Apply the rescale; on VRAM allocation failure GPU_Rescale
             * leaves GPU.vram intact and returns false, so we just
             * roll back our cached shift to whatever the GPU still
             * has. The user-visible effect is "upscale change ignored
             * due to OOM" rather than a crash or a NULL VRAM pointer. */
            if (GPU_Rescale(psx_gpu_upscale_shift))
            {
               alloc_surface();
               has_new_geometry = false;
            }
            else
            {
               log_cb(RETRO_LOG_WARN,
                     "GPU_Rescale: VRAM allocation failed for upscale "
                     "shift %u; keeping previous resolution\n",
                     (unsigned)psx_gpu_upscale_shift);
               psx_gpu_upscale_shift = GPU_get_upscale_shift();
            }
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
         if (retro_set_system_av_info())
            has_new_timing = false;
      }

      /* Widescreen hack, scanlines, overscan cropping, or aspect ratio setting
         changed, need to call SET_GEOMETRY to change aspect ratio */
      if (has_new_geometry)
      {
         if (retro_set_geometry())
            has_new_geometry = false;
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

      PGXP_SetModes(psx_pgxp_mode | psx_pgxp_vertex_caching | psx_pgxp_texture_correction | psx_pgxp_nclip);

      // Reload memory cards if they were changed
      if (use_mednafen_memcard0_method &&
          memcard_left_index_old != memcard_left_index)
      {
         osd_message(0, RETRO_LOG_INFO,
                          RETRO_MESSAGE_TARGET_OSD, RETRO_MESSAGE_TYPE_NOTIFICATION_ALT,
                          "changing from memory card %d to memory card %d in left slot",
                          memcard_left_index_old, memcard_left_index);

         {
            char ext[64];
            char memcard[4096];

            /* Save contents of left memory card to previously selected index */
            snprintf(ext, sizeof(ext), "%d.mcr", memcard_left_index_old);
            MDFN_MakeFName(MDFNMKF_SAV, 0, ext, memcard, sizeof(memcard));
            FrontIO_SaveMemcardToPath(PSX_FIO, 0, memcard, true);

            /* Load contents of currently selected index to left memory card */
            snprintf(ext, sizeof(ext), "%d.mcr", memcard_left_index);
            MDFN_MakeFName(MDFNMKF_SAV, 0, ext, memcard, sizeof(memcard));
            FrontIO_LoadMemcardFromPath(PSX_FIO, 0, memcard, true);
         }
      }

      if (memcard_right_index_old != memcard_right_index)
      {
         osd_message(0, RETRO_LOG_INFO,
                          RETRO_MESSAGE_TARGET_OSD, RETRO_MESSAGE_TYPE_NOTIFICATION_ALT,
                          "changing from memory card %d to memory card %d in right slot",
                          memcard_right_index_old, memcard_right_index);

         {
            char ext[64];
            char memcard[4096];

            /* Save contents of right memory card to previously selected index */
            snprintf(ext, sizeof(ext), "%d.mcr", memcard_right_index_old);
            MDFN_MakeFName(MDFNMKF_SAV, 0, ext, memcard, sizeof(memcard));
            FrontIO_SaveMemcardToPath(PSX_FIO, 1, memcard, true);

            /* Load contents of currently selected index to right memory card */
            snprintf(ext, sizeof(ext), "%d.mcr", memcard_right_index);
            MDFN_MakeFName(MDFNMKF_SAV, 0, ext, memcard, sizeof(memcard));
            FrontIO_LoadMemcardFromPath(PSX_FIO, 1, memcard, true);
         }
      }

      // Update gun crosshair color
      FrontIO_SetCrosshairsColor(PSX_FIO, 0, setting_crosshair_color_p1);
      FrontIO_SetCrosshairsColor(PSX_FIO, 1, setting_crosshair_color_p2);
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
         float fps;
         float internal_fps;

         msg_buffer[0] = '\0';

         // Just report the "real-world" refresh rate here regardless of system av info reported to the frontend
         fps = (content_is_pal && !fast_pal) ?
                        (currently_interlaced ? FPS_PAL_INTERLACED : FPS_PAL_NONINTERLACED) :
                        (currently_interlaced ? FPS_NTSC_INTERLACED : FPS_NTSC_NONINTERLACED);
         internal_fps = (internal_frame_count * fps) / INTERNAL_FPS_SAMPLE_PERIOD;

         snprintf(msg_buffer, sizeof(msg_buffer),
               "Internal FPS: %.2f", internal_fps);

         osd_message(1, RETRO_LOG_INFO,
               RETRO_MESSAGE_TARGET_OSD, RETRO_MESSAGE_TYPE_STATUS,
               msg_buffer);

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
      FrontIO_SetAMCT(PSX_FIO, setting_psx_analog_toggle);
      setting_apply_analog_toggle = false;
   }

   if (input_poll_cb)
      input_poll_cb();

   input_update(libretro_supports_bitmasks, input_state_cb);

   rects[0] = ~0;

   spec.surface = surf;
   spec.SoundRate = 44100;
   spec.LineWidths = rects;
   spec.SoundBufSize = 0;

   espec = (EmulateSpecStruct*)&spec;
   /* start of Emulate */

   espec->skip = false;

   MDFNMP_ApplyPeriodicCheats();

   espec->SoundBufSize = 0;

   FrontIO_UpdateInput(PSX_FIO);
   GPU_StartFrame(espec);

   Running = -1;
   timestamp = CPU_Run(PSX_CPU, timestamp);

   assert(timestamp);

   ForceEventUpdates(timestamp);

   /* Drain any deferred SW-renderer scanout records collected
    * during this frame's emulation.  See GPU_FlushDeferredScanout
    * and psx_gpu_rasterize_both_fields - this is the safe point
    * to read VRAM since rasterisation for this frame is finished
    * and the frontend hasn't yet read the surface for display. */
   GPU_FlushDeferredScanout();

   espec->SoundBufSize = IntermediateBufferPos;
   IntermediateBufferPos = 0;

   PS_CDC_ResetTS(PSX_CDC);
   TIMER_ResetTS();
   DMA_ResetTS();
   GPU_ResetTS();
   FrontIO_ResetTS(PSX_FIO);

   RebaseTS(timestamp);

   // Save memcards if dirty.
   {
      unsigned players = input_get_player_count();
      int i;
      for(i = 0; i < (int)players; i++)
      {
         uint64_t new_dc = FrontIO_GetMemcardDirtyCount(PSX_FIO, i);

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
            char memcard[4096];
            int  index = i;

#ifndef NDEBUG
            log_cb(RETRO_LOG_INFO, "Saving memcard %d...\n", i);
#endif

            if (i == 0 && !use_mednafen_memcard0_method)
            {
               FrontIO_SaveMemcard(PSX_FIO, i);
               Memcard_SaveDelay[i] = -1;
               Memcard_PrevDC[i] = 0;
               continue;
            }

            if (i == 0) index = memcard_left_index;
            else if (i == 1) index = memcard_right_index;

            snprintf(ext, sizeof(ext), "%d.mcr", index);
            MDFN_MakeFName(MDFNMKF_SAV, 0, ext, memcard, sizeof(memcard));
            FrontIO_SaveMemcardToPath(PSX_FIO, i, memcard, false);
            Memcard_SaveDelay[i] = -1;
            Memcard_PrevDC[i] = 0;
         }
      }
   }
   }

   /* end of Emulate */

   // Check if aspect ratio needs to be changed due to display mode change on this frame
   if (MDFN_UNLIKELY((aspect_ratio_setting == 1) && aspect_ratio_dirty))
   {
      if (retro_set_geometry())
         aspect_ratio_dirty = false;

      // If unable to change geometry here, defer to next frame and leave aspect_ratio_dirty flagged
   }

   // Check if timing needs to be changed due to interlacing change on this frame
   // May be possible to track interlacing via espec instead of via RSX?
   if (MDFN_UNLIKELY((core_timing_fps_mode == AUTO_TOGGLE_TIMING) && interlace_setting_dirty))
   {
      // This may cause video and audio reinit on the frontend, so it may be preferable to
      // set the core option to force progressive or interlaced timings
      if (retro_set_system_av_info())
         interlace_setting_dirty = false;

      // If unable to change AV info here, defer to next frame and leave interlace_setting_dirty flagged
   }

   fb            = NULL;
   width         = rects[0];
   height        = spec.DisplayRect.h;
   upscale_shift = GPU_get_upscale_shift();

   if (rsx_intf_is_type() == RSX_SOFTWARE)
   {
#ifdef NEED_DEINTERLACER
      if (spec.InterlaceOn)
      {
         if (!PrevInterlaced)
            Deinterlacer_ClearState(&deint);

         Deinterlacer_Process(&deint, surf, &spec.DisplayRect, rects, spec.InterlaceField);

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

#ifdef NEED_DEINTERLACER
      if (     (currently_interlaced || PrevInterlaced)
            && Deinterlacer_GetType(&deint) == DEINT_BOB
            && height > MEDNAFEN_CORE_GEOMETRY_MAX_H / 2)
         height /= 2;
#endif

      // PSX core inserts padding on left and right (overscan). Optionally crop this.
      pix = surf->pixels;
      pix_offset = 0;

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
               pix_offset += 12 - image_offset + (image_crop / 2);
               width = 256 - image_crop;
               break;

            case 350:
               pix_offset += 15 - image_offset + (image_crop / 2);
               width = 320 - image_crop;
               break;

            /* 368px mode. Some games are overcropped at 364 width or undercropped at 368 width, so crop to 366.
               Adjust in future if there are issues. */
            case 400:
               pix_offset += 17 - image_offset + (image_crop / 2);
               width = 366 - image_crop;
               break;

            case 560:
               pix_offset += 24 - image_offset + (image_crop / 2);
               width = 512 - image_crop;
               break;

            case 700:
               pix_offset += 30 - image_offset + (image_crop / 2);
               width = 640 - image_crop;
               break;

            default:
               // This shouldn't happen.
               break;
         }

         /* Smart height geometry trigger */
         if (crop_overscan == 2)
         {
            if (image_height != height)
            {
               image_height = height;
               retro_set_geometry();
            }
         }
      }

      width  <<= upscale_shift;
      height <<= upscale_shift;
      pix     += pix_offset << upscale_shift;

      if (     GPU_get_display_possibly_dirty()
            || GPU_get_display_change_count()
            || (currently_interlaced || PrevInterlaced)
            || !allow_frame_duping)
         fb = pix;
   }

   rsx_intf_finalize_frame(fb, width, height,
		   MEDNAFEN_CORE_GEOMETRY_MAX_W << (2 + upscale_shift));

   if (audio_batch_cb)
      audio_batch_cb(&IntermediateBuffer[0][0], spec.SoundBufSize);

   if (GPU_get_display_possibly_dirty() || (GPU_get_display_change_count() != 0))
   {
      internal_frame_count++;
      GPU_set_display_change_count(0);
      GPU_set_display_possibly_dirty(false);
   }

   /* LED interface */
   if (led_state_cb)
      retro_led_interface();
}

void retro_get_system_info(struct retro_system_info *info)
{
   if (!info)
      return;
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
   if (!info)
      return;
   rsx_intf_get_system_av_info(info);
}

/* Reset all process-lifetime state so that a subsequent retro_init()
 * starts from a clean slate. This matters on consoles and other
 * platforms that statically link the core: the same process loads
 * and unloads multiple games, and any state retained between sessions
 * leaks across them.
 *
 * The original implementation only reset two booleans, which meant
 * frame counters, GUI state, disc state, firmware-found flag, and
 * other module-level variables persisted into the next session and
 * caused subtle misbehavior. */
void retro_deinit(void)
{
   if (surf)
   {
      MDFN_Surface_Delete(surf);
      surf = NULL;
   }

#ifdef NEED_DEINTERLACER
   Deinterlacer_Cleanup(&deint);
#endif

   /* Companion to retro_init's CDUtility_Init. Frees the Reed-Solomon
    * and Galois L-EC correction tables (~4 KB) which would otherwise
    * leak per dlopen/dlclose cycle. The Init has an internal Inited
    * guard, so a re-init in a subsequent retro_init call works
    * correctly. */
   CDUtility_Kill();

   /* Free any lazily-allocated PGXP buffers (vertex cache).  Same
    * dlopen/dlclose-cycle leak concern as CDUtility above. */
   PGXP_Shutdown();

   /* Frame/UI state. */
   frame_count           = 0;
   internal_frame_count  = 0;
   frame_width           = 0;
   frame_height          = 0;

   /* Loaded-content state. */
   firmware_found                = false;
   bios_path[0]                  = '\0';
   retro_cd_path[0]              = '\0';

   /* Disc-tray / multi-disc state. */
   eject_state           = false;
   CD_TrayOpen           = false;
   CD_IsPBP              = false;
   PBP_PhysicalDiscCount = 0;
   image_offset          = 0;
   image_crop            = 0;

   /* Memcard housekeeping. */
   memcard_left_index      = 0;
   memcard_left_index_old  = 0;
   memcard_right_index     = 1;
   memcard_right_index_old = 0;
   enable_memcard1         = false;
   memset(Memcard_PrevDC,    0, sizeof(Memcard_PrevDC));
   memset(Memcard_SaveDelay, 0, sizeof(Memcard_SaveDelay));

   /* Display/notification toggles. */
   display_internal_framerate = false;
   display_notifications      = true;
   allow_frame_duping         = false;

   /* Capability flags re-detected by retro_init. */
   libretro_supports_option_categories = false;
   libretro_supports_bitmasks          = false;
   libretro_msg_interface_version      = 0;
   enable_variable_serialization_size  = false;
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
   struct retro_led_interface led_interface;
   environ_cb = cb;

   libretro_supports_option_categories = false;
   libretro_set_core_options(environ_cb, &libretro_supports_option_categories);

   vfs_iface_info.required_interface_version = 2;
   vfs_iface_info.iface                      = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
      filestream_vfs_init(&vfs_iface_info);

   if (environ_cb(RETRO_ENVIRONMENT_GET_LED_INTERFACE, &led_interface))
      if (led_interface.set_led_state && !led_state_cb)
         led_state_cb = led_interface.set_led_state;

   input_set_env(cb);

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
   dbg_input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;

   rsx_intf_set_video_refresh(cb);
}

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
      return st.len;
   }

   return DEFAULT_STATE_SIZE; // 16MB
}

bool UsingFastSavestates(void)
{
   int flags;
   if (environ_cb(RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE, &flags))
      return flags & 4;
   return false;
}

/* Serialize emulator state into the frontend's `data` buffer of `size`
 * bytes.
 *
 * The previous implementation was unsafe in two ways:
 *
 *   1. When size == DEFAULT_STATE_SIZE (the default 16MB hint), it set
 *      st.data = (uint8_t*)data - i.e. handed the frontend's buffer to
 *      the state-saving code. smem_write() in mednafen/state.c calls
 *      realloc() on st.data when the running save exceeds st.malloced.
 *      Calling realloc() on memory the core does not own is undefined
 *      behavior. Today it works because PSX states fit comfortably in
 *      16MB, but a single growth would corrupt the frontend's heap.
 *
 *   2. The non-default branch checked "st.len != size" before saving,
 *      but st.len is always 0 at that point - it's set by smem_write
 *      while saving. The warning was meaningless.
 *
 * The new implementation always saves into a core-owned malloc buffer
 * and memcpys into the frontend's buffer afterward, with explicit
 * truncation handling. */
bool retro_serialize(void *data, size_t size)
{
   StateMem st;
   bool     ret;
   uint8_t *scratch;

   if (!data || size == 0)
      return false;

   /* Same defensive check as retro_unserialize - StateAction touches
    * every subsystem and segfaults if any of them are NULL. */
   if (!MainRAM || !PSX_CDC || !PSX_CPU || !PSX_FIO)
      return false;

   scratch = (uint8_t*)malloc(size);
   if (!scratch)
      return false;

   st.data           = scratch;
   st.loc            = 0;
   st.len            = 0;
   st.malloced       = size;
   st.initial_malloc = 0;

   FastSaveStates = UsingFastSavestates();
   ret            = MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);
   FastSaveStates = false;

   if (ret)
   {
      /* st.data may have been reallocated by smem_write; st.len is the
       * actual amount written. Copy what we have, but warn if it
       * exceeded the frontend's buffer. */
      size_t copy_len = (st.len <= size) ? (size_t)st.len : size;
      if (st.len > size)
      {
         log_cb(RETRO_LOG_ERROR,
               "retro_serialize: state grew to %u bytes, frontend buffer is %u; truncating\n",
               (unsigned)st.len, (unsigned)size);
         ret = false;
      }
      memcpy(data, st.data, copy_len);
   }

   /* st.data may differ from `scratch` if smem_write realloc'd. */
   if (st.data)
      free(st.data);

   return ret;
}

bool retro_unserialize(const void *data, size_t size)
{
   StateMem st;
   bool okay;

   /* Frontends should only call this when a game is loaded, but be
    * defensive: MDFNSS_LoadSM walks every subsystem's StateAction
    * which derefs MainRAM, PSX_CDC, etc. */
   if (!data || size == 0 || !MainRAM || !PSX_CDC || !PSX_CPU || !PSX_FIO)
      return false;

   st.data           = (uint8_t*)data;
   st.loc            = 0;
   st.len            = size;
   st.malloced       = 0;
   st.initial_malloc = 0;

   FastSaveStates = UsingFastSavestates();
   okay           = MDFNSS_LoadSM(&st, 0, 0);
   FastSaveStates = false;
   return okay;
}

void *retro_get_memory_data(unsigned type)
{
   /* This callback may be invoked before retro_load_game or after
    * retro_unload_game. Returning NULL is a valid response per the
    * libretro spec; the frontend must cope with that. */
   switch (type)
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return MainRAM ? MainRAM->data8 : NULL;
      case RETRO_MEMORY_SAVE_RAM:
         if (!use_mednafen_memcard0_method && PSX_FIO)
         {
            InputDevice *mc = FrontIO_GetMemcardDevice(PSX_FIO, 0);
            return mc ? mc->vt->GetNVData(mc) : NULL;
         }
         break;
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
         if (!use_mednafen_memcard0_method)
            return (1 << 17);
         break;
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
   /* PSX cheat codes are tokens of hex digits.  Each token is at
    * most 12 chars (full code) or 8 chars (half-code; two of these
    * concatenate to a full code).  An entire codeLine yields one
    * token roughly per 4 chars, so a 64-token cap covers everything
    * realistic.  Bounded buffer avoids dynamic allocation entirely. */
   char codeParts[64][16];
   int  numParts = 0;
   int  matchLength = 0;
   int  cursor;
   char part[32];
   MemoryPatch patch;

   if (codeLine == NULL)
      return;

   /* Break the code into Parts */
   for (cursor = 0; ; cursor++)
   {
      if (ISHEXDEC)
         matchLength++;
      else
      {
         if (matchLength)
         {
            int copy_len = matchLength;
            if (copy_len >= (int)sizeof(codeParts[0]))
               copy_len = (int)sizeof(codeParts[0]) - 1;
            if (numParts < (int)(sizeof(codeParts) / sizeof(codeParts[0])))
            {
               memcpy(codeParts[numParts], codeLine + cursor - matchLength, copy_len);
               codeParts[numParts][copy_len] = '\0';
               numParts++;
            }
            matchLength = 0;
         }
      }
      if (!codeLine[cursor])
         break;
   }

   MemoryPatch_Init(&patch);
   for (cursor = 0; cursor < numParts; cursor++)
   {
      size_t plen;
      strlcpy(part, codeParts[cursor], sizeof(part));
      plen = strlen(part);
      if (plen == 8 && cursor + 1 < numParts)
      {
         strlcat(part, codeParts[++cursor], sizeof(part));
         plen = strlen(part);
      }
      if (plen == 12)
      {
         /* Decode the cheat. DecodeCheat returns false on bad codes;
          * MDFNI_AddCheat does not throw. */
         if (!cf->DecodeCheat(part, &patch))
         {
            /* Generate a name */
            snprintf(name, sizeof(name), "cheat_%i_%i", index, cursor);

            /* Set parameters */
            strlcpy(patch.name, name, sizeof(patch.name));
            patch.status = enabled;

            MDFNI_AddCheat(&patch);
            MemoryPatch_Init(&patch);
         }
      }
   }
}

void MDFN_MakeFName(MakeFName_Type type, int id1, const char *cd1,
      char *out, size_t outlen)
{
   int r;

   (void)id1;

   if (!out || outlen == 0)
      return;
   out[0] = '\0';

   if (!cd1)
      return;

   switch (type)
   {
      case MDFNMKF_SAV:
         r = snprintf(out, outlen, "%s%c%s.%s",
               retro_save_directory,
               retro_slash,
               shared_memorycards ? "mednafen_psx_libretro_shared" : retro_cd_base_name,
               cd1);
         break;
      case MDFNMKF_FIRMWARE:
         r = snprintf(out, outlen, "%s%c%s",
               retro_base_directory, retro_slash, cd1);
         break;
      default:
         return;
   }

   if (r < 0 || (size_t)r >= outlen)
   {
      out[outlen - 1] = '\0';
      if (log_cb)
         log_cb(RETRO_LOG_ERROR,
               "MDFN_MakeFName: path truncated to %zu bytes: %s\n",
               outlen, out);
   }
}

/*
 * osd_message - the only libretro OSD/log message helper for
 * this core. Formats the printf-style args into a 4 KiB stack
 * buffer (matching the historical MDFN_DispMessage behaviour;
 * 4 KiB is generous - none of the call sites approach that
 * size), then dispatches via RETRO_ENVIRONMENT_SET_MESSAGE_EXT
 * if the frontend advertises RETRO_MESSAGE_INTERFACE_VERSION
 * >= 1, falling back to the legacy SET_MESSAGE otherwise.
 *
 * Replaces the previous MDFN_DispMessage + MDFND_DispMessage
 * pair, which were a holdover from Mednafen's standalone-build
 * driver-abstraction layer (MDFND_* was the abstract driver
 * entry point implemented per frontend, MDFN_* was the printf-
 * style wrapper that built a string then called the driver).
 * In the libretro core there's only one frontend, so the
 * indirection added nothing; folded into one function. See
 * osd_message.h for the API contract.
 */
void osd_message(unsigned priority,
                 enum retro_log_level level,
                 enum retro_message_target target,
                 enum retro_message_type type,
                 const char *format, ...)
{
   char str[4096];
   va_list ap;

   if (!display_notifications)
      return;

   va_start(ap, format);
   vsnprintf(str, sizeof(str), format, ap);
   va_end(ap);

   if (libretro_msg_interface_version >= 1)
   {
      struct retro_message_ext msg = {
         str,
         3000,
         priority,
         level,
         target,
         type,
         -1
      };
      environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE_EXT, &msg);
   }
   else
   {
      struct retro_message msg = { str, 180 };
      environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
   }
}
