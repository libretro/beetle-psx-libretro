/******************************************************************************/
/* Mednafen Sony PS1 Emulation Module                                         */
/******************************************************************************/
/* cpu.c:
**  Copyright (C) 2011-2016 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("unroll-loops")
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "psx.h"
#include "cpu.h"
#include "psx_mem.h"

#include "../state_helpers.h"
#include "../math_ops.h"
#include "../mednafen.h"
#include "../../osd_message.h"
#include "../mednafen-endian.h"

/* PGXP */
#include "../pgxp/pgxp_cpu.h"
#include "../pgxp/pgxp_gte.h"
#include "../pgxp/pgxp_main.h"
int pgxpMode;

#ifdef HAVE_LIGHTREC
#include <lightrec.h>
#include <unistd.h>
#include <signal.h>

enum DYNAREC prev_dynarec;
bool         prev_invalidate;
extern bool  psx_dynarec_invalidate;
extern uint8 psx_mmap;
static struct lightrec_state *lightrec_state;
uint8 next_interpreter;
#endif

extern bool psx_gte_overclock;

/* CP0 named-register indices.  Used inside the per-instruction switch
 * in lightrec's MTC0/CTC0 path; kept TU-local since nothing outside
 * cpu.c speaks of them by name. */
enum
{
   CP0REG_BPC   = 3,        /* PC breakpoint address. */
   CP0REG_BDA   = 5,        /* Data load/store breakpoint address. */
   CP0REG_TAR   = 6,        /* Target address(???) */
   CP0REG_DCIC  = 7,        /* Cache control */
   CP0REG_BADA  = 8,
   CP0REG_BDAM  = 9,        /* Data load/store address mask. */
   CP0REG_BPCM  = 11,       /* PC breakpoint address mask. */
   CP0REG_SR    = 12,
   CP0REG_CAUSE = 13,
   CP0REG_EPC   = 14,
   CP0REG_PRID  = 15        /* Product ID */
};

enum
{
   EXCEPTION_INT     = 0,
   EXCEPTION_MOD     = 1,
   EXCEPTION_TLBL    = 2,
   EXCEPTION_TLBS    = 3,
   EXCEPTION_ADEL    = 4,   /* Address error on load */
   EXCEPTION_ADES    = 5,   /* Address error on store */
   EXCEPTION_IBE     = 6,   /* Instruction bus error */
   EXCEPTION_DBE     = 7,   /* Data bus error */
   EXCEPTION_SYSCALL = 8,   /* System call */
   EXCEPTION_BP      = 9,   /* Breakpoint */
   EXCEPTION_RI      = 10,  /* Reserved instruction */
   EXCEPTION_COPU    = 11,  /* Coprocessor unusable */
   EXCEPTION_OV      = 12   /* Arithmetic overflow */
};

/* The class-static members of PS_CPU live at file scope here; the
 * per-instance members live in s_cpu (BSS).  libretro.cpp still
 * expresses ownership through PSX_CPU, which always points at
 * &s_cpu.  Heap allocation is gone - the storage is BSS, init runs
 * in CPU_New. */
static PS_CPU            s_cpu;
PS_CPU                  *PSX_CPU = &s_cpu;
pscpu_timestamp_t        cpu_next_event_ts;
static uint32_t          cpu_IPCache;
static uint32_t          cpu_BIU;
static bool              cpu_Halted;
static CPU_CP0           cpu_CP0;
#ifdef HAVE_LIGHTREC
/* cache_buf shadows the first 64KB of MainRAM while the cache-isolated
 * (CP0 SR bit 16) bit is set, so reads return from the cache snapshot
 * and writes are confined.  Only the lightrec dispatcher honours this
 * MIPS-quirk - the interpreter doesn't model cache isolation - so the
 * shadow only exists when HAVE_LIGHTREC is on. */
static char              cpu_cache_buf[64 * 1024];
#endif

/* Aliases for every PS_CPU instance field used inside this file.
 * Lets the body of methods reference fields by their original bare
 * names (GPR, BACKED_PC, ICache, FastMap, ...) without sprinkling
 * self-> through 3000+ lines of dispatch.  s_cpu is the singleton
 * PSX_CPU points at, so resolution is identical to the original
 * implicit `this` access. */
#define GPR             s_cpu.GPR_full
#define LO              s_cpu.GPR_full[32]
#define HI              s_cpu.GPR_full[33]
#define BACKED_PC       s_cpu.BACKED_PC
#define BACKED_new_PC   s_cpu.BACKED_new_PC
#define BDBT            s_cpu.BDBT
#define ReadAbsorb      s_cpu.ReadAbsorb
#define ReadAbsorbWhich s_cpu.ReadAbsorbWhich
#define ReadFudge       s_cpu.ReadFudge
#define BACKED_LDWhich  s_cpu.BACKED_LDWhich
#define BACKED_LDValue  s_cpu.BACKED_LDValue
#define LDAbsorb        s_cpu.LDAbsorb
#define gte_ts_done     s_cpu.gte_ts_done
#define muldiv_ts_done  s_cpu.muldiv_ts_done
#define addr_mask       s_cpu.addr_mask
#define ICache          s_cpu.ic.ICache
#define ICache_Bulk     s_cpu.ic.ICache_Bulk
#define FastMap         s_cpu.FastMap
#define DummyPage       s_cpu.DummyPage

/* Aliases for the file-scope statics (former class-static members)
 * so the body's bare-name references continue to work. */
#define Halted          cpu_Halted
#define CP0             cpu_CP0.n
#define BIU             cpu_BIU
#define next_event_ts   cpu_next_event_ts
#define IPCache         cpu_IPCache
#define cache_buf       cpu_cache_buf
#define MULT_Tab24      cpu_MULT_Tab24

/* FAST_MAP_SHIFT / FAST_MAP_PSIZE survived from the old class enum;
 * they're now plain macros in cpu.h.  Local aliases for the
 * unprefixed names used in this TU. */
#define FAST_MAP_SHIFT  CPU_FAST_MAP_SHIFT
#define FAST_MAP_PSIZE  CPU_FAST_MAP_PSIZE

/* MULT_Tab24: per-instance precomputed table in the original
 * constructor; the loop computed `v = 7 + (i<12 ? 4 : 0) + (i<21 ? 3 : 0)`,
 * which is a function of the index alone and constant across
 * runs.  Baked at compile time, dropped from the struct. */
static const uint8_t cpu_MULT_Tab24[24] =
{
   /* i = 0..11: v = 7 + 4 + 3 = 14 */
   14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
   /* i = 12..20: v = 7 + 0 + 3 = 10 */
   10, 10, 10, 10, 10, 10, 10, 10, 10,
   /* i = 21..23: v = 7 + 0 + 0 = 7 */
   7,  7,  7
};

#define BIU_ENABLE_ICACHE_S1   0x00000800  /* Enable I-cache, set 1 */
#define BIU_ICACHE_FSIZE_MASK  0x00000300  /* I-cache fill size mask */
#define BIU_ENABLE_DCACHE      0x00000080  /* Enable D-cache */
#define BIU_DCACHE_SCRATCHPAD  0x00000008  /* Enable scratchpad RAM mode of D-cache */
#define BIU_TAG_TEST_MODE      0x00000004  /* Enable TAG test mode */
#define BIU_INVALIDATE_MODE    0x00000002  /* Enable Invalidate mode */
#define BIU_LOCK_MODE          0x00000001  /* Enable Lock mode */

/* Forward decls for methods called before they're defined. */
static uint32_t CPU_Exception(uint32_t code, uint32_t PC, const uint32_t NP, const uint32_t instr);
static pscpu_timestamp_t CPU_RunReal(PS_CPU *self, pscpu_timestamp_t timestamp_in);
#ifdef HAVE_LIGHTREC
static int     lightrec_plugin_init(PS_CPU *self);
static int32_t lightrec_plugin_execute(PS_CPU *self, int32_t timestamp);
static void    lightrec_plugin_shutdown(void);
#endif

/* Local typedef so the body matches the pre-conversion code. */
typedef CPU_ICache __ICache;

PS_CPU *CPU_New(void)
{
   PS_CPU  *self = &s_cpu;
   uint64_t a;

   memset(self, 0, sizeof(*self));

   addr_mask[0] = 0xFFFFFFFF;
   addr_mask[1] = 0xFFFFFFFF;
   addr_mask[2] = 0xFFFFFFFF;
   addr_mask[3] = 0xFFFFFFFF;
   addr_mask[4] = 0x7FFFFFFF;
   addr_mask[5] = 0x1FFFFFFF;
   addr_mask[6] = 0xFFFFFFFF;
   addr_mask[7] = 0xFFFFFFFF;

   Halted = false;

   /* FastMap zeroed by the memset above. */
   memset(DummyPage, 0xFF, sizeof(DummyPage));

   for (a = 0x00000000; a < (1ULL << 32); a += CPU_FAST_MAP_PSIZE)
      CPU_SetFastMap(self, DummyPage, (uint32_t)a, CPU_FAST_MAP_PSIZE);

   GTE_Init();

   pgxpMode = PGXP_GetModes();

   return self;
}

void CPU_Destroy(PS_CPU *self)
{
   (void)self;
#ifdef HAVE_LIGHTREC
   if (lightrec_state)
      lightrec_plugin_shutdown();
#endif
}

void CPU_SetFastMap(PS_CPU *self, void *region_mem, uint32_t region_address, uint32_t region_size)
{
   uint64_t A;
   (void)self;
   for (A = region_address; A < (uint64_t)region_address + region_size; A += CPU_FAST_MAP_PSIZE)
      FastMap[A >> CPU_FAST_MAP_SHIFT] = ((uintptr_t)region_mem - region_address);
}

static INLINE void CPU_RecalcIPCache(void)
{
   IPCache = 0;

   if ((CP0.SR & CP0.CAUSE & 0xFF00) && (CP0.SR & 1))
      IPCache = 0x80;

   if (Halted)
      IPCache = 0x80;
}

void CPU_SetHalt_method(PS_CPU *self, bool status)
{
   (void)self;
   Halted = status;
   CPU_RecalcIPCache();
}

void CPU_AssertIRQ_method(PS_CPU *self, unsigned which, bool asserted)
{
   (void)self;
   assert(which <= 5);

   CP0.CAUSE &= ~(1 << (10 + which));

   if (asserted)
      CP0.CAUSE |= 1 << (10 + which);

   CPU_RecalcIPCache();
}

/* C-linkage shims declared in cpu_c.h.  These exist so non-cpu.c
 * TUs (irq.c, gpu.c, mdec.c, ...) can drive the CPU without
 * depending on the full struct layout in cpu.h. */
void CPU_AssertIRQ(unsigned which, bool asserted)
{
   CPU_AssertIRQ_method(PSX_CPU, which, asserted);
}

void CPU_SetHalt(bool status)
{
   CPU_SetHalt_method(PSX_CPU, status);
}

#ifdef HAVE_LIGHTREC
void CPU_LightrecClear(uint32_t addr, uint32_t size)
{
   CPU_LightrecClear_method(PSX_CPU, addr, size);
}
#endif

const uint8_t *PSX_LoadExpansion1(void);

void CPU_Power(PS_CPU *self)
{
   unsigned i;

   (void)self;

   /* Compile-time check that the ICache union's two arms cover the
    * same byte range. This used to be a runtime assert spelled
    *
    *     assert(sizeof(s_cpu.ic.ICache) == sizeof(s_cpu.ic.ICache_Bulk));
    *
    * but `ICache` and `ICache_Bulk` are file-static macros that expand
    * to `s_cpu.ic.ICache` / `s_cpu.ic.ICache_Bulk` (see the #define
    * block above), so under DEBUG=1 (where assert() is live and the
    * macros aren't gated out) the expression became
    *
    *     sizeof(s_cpu.ic.s_cpu.ic.ICache) ...
    *
    * which fails to compile with `'union <anonymous>' has no member
    * named 's_cpu'`. The release build masked it because -DNDEBUG
    * stripped the assert before macro expansion.
    *
    * Using the underlying types here sidesteps the macros entirely
    * and gets us a stronger check (compile-time, runs in every
    * build) for free. */
   _Static_assert(sizeof(CPU_ICache[1024]) == sizeof(uint32_t[2048]),
                  "ICache union arms must cover the same bytes");

   memset(GPR, 0, 32 * sizeof(uint32_t));
   memset(&cpu_CP0, 0, sizeof(cpu_CP0));
   LO = 0;
   HI = 0;

   gte_ts_done    = 0;
   muldiv_ts_done = 0;

   BACKED_PC      = 0xBFC00000;
   BACKED_new_PC  = BACKED_PC + 4;
   BDBT           = 0;

   BACKED_LDWhich = 0x22;
   BACKED_LDValue = 0;
   LDAbsorb       = 0;
   memset(ReadAbsorb, 0, sizeof(ReadAbsorb));
   ReadAbsorbWhich = 0;
   ReadFudge       = 0;

   CP0.SR |= (1 << 22); /* BEV */
   CP0.SR |= (1 << 21); /* TS  */

   CP0.PRID = 0x2;

   CPU_RecalcIPCache();

   BIU = 0;

   memset(ScratchRAM->data8, 0, 1024);

   PGXP_Init();

#ifdef HAVE_LIGHTREC
   next_interpreter = 0;
   prev_dynarec     = psx_dynarec;
   prev_invalidate  = psx_dynarec_invalidate;
   pgxpMode         = PGXP_GetModes();
   if (psx_dynarec != DYNAREC_DISABLED)
      lightrec_plugin_init(self);
#endif

   /* Not quite sure about these poweron/reset values: */
   for (i = 0; i < 1024; i++)
   {
      ICache[i].TV   = 0x2 | ((BIU & 0x800) ? 0x0 : 0x1);
      ICache[i].Data = 0;
   }

   GTE_Power();
}

int CPU_StateAction(PS_CPU *self, StateMem *sm, const unsigned load, const bool data_only)
{
   SFORMAT StateRegs[] =
   {
      SFARRAY32(GPR, 32),
      SFVAR(LO),
      SFVAR(HI),
      SFVAR(BACKED_PC),
      SFVAR(BACKED_new_PC),
      SFVAR(BDBT),

      SFVAR(IPCache),
      SFVAR(Halted),

      SFVAR(BACKED_LDWhich),
      SFVAR(BACKED_LDValue),
      SFVAR(LDAbsorb),

      SFVAR(next_event_ts),
      SFVAR(gte_ts_done),
      SFVAR(muldiv_ts_done),

      SFVAR(BIU),
      SFVAR(ICache_Bulk),

      SFVAR(cpu_CP0.Regs),

      SFARRAY(ReadAbsorb, 0x20),
      SFVARN(ReadAbsorb[0x20], "ReadAbsorbDummy"),
      SFVAR(ReadAbsorbWhich),
      SFVAR(ReadFudge),

      SFARRAYN(ScratchRAM->data8, 1024, "ScratchRAM.data8"),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "CPU");

   ret &= GTE_StateAction(sm, load, data_only);

   if (load)
   {
#ifdef HAVE_LIGHTREC
      if (psx_dynarec != DYNAREC_DISABLED)
      {
         if (lightrec_state)
         {
            /* Hack to prevent Dynarec + Runahead from causing a crash by
             * switching to lightrec interpreter after load state in bios */
            if (psx_dynarec != DYNAREC_RUN_INTERPRETER &&
                BACKED_PC >= 0xBFC00000 && BACKED_PC <= 0xBFC80000)
            {
               if (next_interpreter == 0)
               {
                  log_cb(RETRO_LOG_INFO, "PC 0x%08x Dynarec using interpreter for a few "
                                         "frames, avoid crash due to Runahead\n", BACKED_PC);
                  lightrec_plugin_init(self);
               }
               /* run lightrec's interpreter for a few frames
                * 76 for NTSC, 93 for PAL seems to prevent crash at max runahead */
               next_interpreter = 93;
            }
            else
               lightrec_invalidate_all(lightrec_state);
         }
         else
            lightrec_plugin_init(self);
      }
#endif
      ReadAbsorbWhich &= 0x1F;
      /* BACKED_LDWhich must be a valid GPR index (0..31) or the dummy
       * slot (0x22). Any other value (including 0x20, the dummy slot
       * from previous save-state versions) is sanitized to the new
       * dummy index. */
      if (BACKED_LDWhich > 31)
         BACKED_LDWhich = 0x22;
   }
   return ret;
}

void CPU_SetBIU(PS_CPU *self, uint32_t val)
{
   const uint32_t old_BIU = BIU;
   (void)self;

   BIU = val & ~(0x440);

   if ((BIU ^ old_BIU) & 0x800)
   {
      unsigned i;
      if (BIU & 0x800) /* ICache enabled */
      {
         for (i = 0; i < 1024; i++)
            ICache[i].TV &= ~0x1;
      }
      else             /* ICache disabled */
      {
         for (i = 0; i < 1024; i++)
            ICache[i].TV |= 0x1;
      }
   }
}

uint32_t CPU_GetBIU(PS_CPU *self)
{
   (void)self;
   return BIU;
}

/* ReadMemory_u{8,16,32} - the templated PS_CPU::ReadMemory<T> body
 * collapsed into three explicit functions.  The original branched
 * on sizeof(T); now the size is implicit in the function name and
 * the dead branches are removed.  DS24 and LWC_timing only ever
 * apply to the 32-bit path - the 8 and 16 variants drop those
 * arguments. */

static INLINE uint8_t ReadMemory_u8(pscpu_timestamp_t *timestamp, uint32_t address)
{
   uint8_t           ret;
   pscpu_timestamp_t lts;

   ReadAbsorb[ReadAbsorbWhich] = 0;
   ReadAbsorbWhich             = 0;

   address &= addr_mask[address >> 29];

   if (address >= 0x1F800000 && address <= 0x1F8003FF)
   {
      LDAbsorb = 0;
      return MASMEM_ReadU8(ScratchRAM, address & 0x3FF);
   }

   *timestamp += (ReadFudge >> 4) & 2;

   lts        = *timestamp;
   ret        = PSX_MemRead8(&lts, address);
   lts       += 2;

   LDAbsorb   = (lts - *timestamp);
   *timestamp = lts;

   return ret;
}

static INLINE uint16_t ReadMemory_u16(pscpu_timestamp_t *timestamp, uint32_t address)
{
   uint16_t          ret;
   pscpu_timestamp_t lts;

   ReadAbsorb[ReadAbsorbWhich] = 0;
   ReadAbsorbWhich             = 0;

   address &= addr_mask[address >> 29];

   if (address >= 0x1F800000 && address <= 0x1F8003FF)
   {
      LDAbsorb = 0;
      return MASMEM_ReadU16(ScratchRAM, address & 0x3FF);
   }

   *timestamp += (ReadFudge >> 4) & 2;

   lts        = *timestamp;
   ret        = PSX_MemRead16(&lts, address);
   lts       += 2;

   LDAbsorb   = (lts - *timestamp);
   *timestamp = lts;

   return ret;
}

static INLINE uint32_t ReadMemory_u32(pscpu_timestamp_t *timestamp, uint32_t address, bool DS24, bool LWC_timing)
{
   uint32_t          ret;
   pscpu_timestamp_t lts;

   ReadAbsorb[ReadAbsorbWhich] = 0;
   ReadAbsorbWhich             = 0;

   address &= addr_mask[address >> 29];

   if (address >= 0x1F800000 && address <= 0x1F8003FF)
   {
      LDAbsorb = 0;
      if (DS24)
         return MASMEM_ReadU24(ScratchRAM, address & 0x3FF);
      return MASMEM_ReadU32(ScratchRAM, address & 0x3FF);
   }

   *timestamp += (ReadFudge >> 4) & 2;

   lts = *timestamp;
   if (DS24)
      ret = PSX_MemRead24(&lts, address) & 0xFFFFFF;
   else
      ret = PSX_MemRead32(&lts, address);

   if (LWC_timing)
      lts += 1;
   else
      lts += 2;

   LDAbsorb   = (lts - *timestamp);
   *timestamp = lts;

   return ret;
}

/* WriteMemory_u{8,16,32} - mirror of ReadMemory_u{8,16,32}.  The
 * IsC (CP0.SR & 0x10000) "isolate cache" path is shared since it's
 * width-agnostic except for the scratchpad write at the end.
 *
 * Performance note: the original function was a single template
 * with size-branching scattered through both the normal and IsC
 * paths.  Splitting by width strips dead branches at compile time
 * - the u8 and u16 paths drop the DS24 flag entirely, since
 * 24-bit writes only happen on the 32-bit path. */

static INLINE void WriteMemory_IsC_misc(uint32_t address, uint32_t value)
{
   if (BIU & BIU_ENABLE_ICACHE_S1)
   {
      if (BIU & (BIU_TAG_TEST_MODE | BIU_INVALIDATE_MODE | BIU_LOCK_MODE))
      {
         const uint8_t valid_bits = (BIU & BIU_TAG_TEST_MODE)
            ? ((value << ((address & 0x3) * 8)) & 0x0F) : 0x00;
         __ICache *ICI = &ICache[((address & 0xFF0) >> 2)];
         unsigned i;

         for (i = 0; i < 4; i++)
            ICI[i].TV = ((valid_bits & (1U << i)) ? 0x00 : 0x02)
                      | (address & 0xFFFFFFF0) | (i << 2);
      }
      else
         ICache[(address & 0xFFC) >> 2].Data = value << ((address & 0x3) * 8);
   }
}

static INLINE void WriteMemory_u8(pscpu_timestamp_t *timestamp, uint32_t address, uint32_t value)
{
   if (MDFN_LIKELY(!(CP0.SR & 0x10000)))
   {
      address &= addr_mask[address >> 29];

      if (address >= 0x1F800000 && address <= 0x1F8003FF)
      {
         MASMEM_WriteU8(ScratchRAM, address & 0x3FF, value);
         return;
      }
      PSX_MemWrite8(*timestamp, address, value);
      return;
   }

   WriteMemory_IsC_misc(address, value);

   if ((BIU & 0x081) == 0x080)
      MASMEM_WriteU8(ScratchRAM, address & 0x3FF, value);
}

static INLINE void WriteMemory_u16(pscpu_timestamp_t *timestamp, uint32_t address, uint32_t value)
{
   if (MDFN_LIKELY(!(CP0.SR & 0x10000)))
   {
      address &= addr_mask[address >> 29];

      if (address >= 0x1F800000 && address <= 0x1F8003FF)
      {
         MASMEM_WriteU16(ScratchRAM, address & 0x3FF, value);
         return;
      }
      PSX_MemWrite16(*timestamp, address, value);
      return;
   }

   WriteMemory_IsC_misc(address, value);

   if ((BIU & 0x081) == 0x080)
      MASMEM_WriteU16(ScratchRAM, address & 0x3FF, value);
}

static INLINE void WriteMemory_u32(pscpu_timestamp_t *timestamp, uint32_t address, uint32_t value, bool DS24)
{
   if (MDFN_LIKELY(!(CP0.SR & 0x10000)))
   {
      address &= addr_mask[address >> 29];

      if (address >= 0x1F800000 && address <= 0x1F8003FF)
      {
         if (DS24)
            MASMEM_WriteU24(ScratchRAM, address & 0x3FF, value);
         else
            MASMEM_WriteU32(ScratchRAM, address & 0x3FF, value);
         return;
      }

      if (DS24)
         PSX_MemWrite24(*timestamp, address, value);
      else
         PSX_MemWrite32(*timestamp, address, value);
      return;
   }

   WriteMemory_IsC_misc(address, value);

   if ((BIU & 0x081) == 0x080)
   {
      if (DS24)
         MASMEM_WriteU24(ScratchRAM, address & 0x3FF, value);
      else
         MASMEM_WriteU32(ScratchRAM, address & 0x3FF, value);
   }
}

//
// ICache emulation here is not very accurate.  More accurate emulation had about a 6% performance penalty for simple
// code that just looped infinitely, with no tangible known benefit for commercially-released games.
//
// We do emulate the tag test mode functionality in regards to loading custom tag, valid bits, and instruction word data, as it could
// hypothetically be useful for homebrew.  However, due to inaccuracies, it's possible to load a tag for an address in the non-cached
// 0xA0000000-0xBFFFFFFF range, jump to the address, and have it execute out of instruction cache, which is wrong and not possible on a PS1.
//
// The other major inaccuracy here is how the region 0x80000000-0x9FFFFFFF is handled.  On a PS1, when fetching instruction word data
// from this region, the upper bit is forced to 0 before the tag compare(though the tag compare IS a full 20 bit compare),
// and this address with the upper bit set to 0 is also written to the tag on cache miss.  We don't do the address masking here,
// so in addition to the tag test mode implications, a program that jumps from somewhere in 0x00000000-0x1FFFFFFF to the corresponding
// location in 0x80000000-0x9FFFFFFF will always cause a cache miss in Mednafen.
//
// On a PS1, icache miss fill size can be programmed to 2, 4, 8, or 16 words(though 4 words is the nominally-correct setting).  We always emulate the cache
// miss fill size as 4-words.  Fill size of 8-words and 16-words are buggy on a PS1, and will write the wrong instruction data values(or read from the wrong
// addresses?) to cache when a cache miss occurs on an address that isn't aligned to a 4-word boundary.
// Fill size of 2-words seems to work on a PS1, and even behaves as if the line size is 2 words in regards to clearing
// the valid bits(when the tag matches, of course), but is obviously not very efficient unless running code that's just endless branching.
//
static INLINE uint32_t ReadInstruction(pscpu_timestamp_t *timestamp, uint32_t address)
{
   uint32_t instr;

   instr = ICache[(address & 0xFFC) >> 2].Data;

   if (ICache[(address & 0xFFC) >> 2].TV != address)
   {
      ReadAbsorb[ReadAbsorbWhich] = 0;
      ReadAbsorbWhich             = 0;

      if (address >= 0xA0000000 || !(BIU & 0x800))
      {
         instr = MDFN_de32lsb_aligned((uint8 *)(FastMap[address >> FAST_MAP_SHIFT] + address));

         if (!psx_gte_overclock)
            *timestamp += 4; /* Approximate best-case cache-disabled time. */
      }
      else
      {
         __ICache    *ICI = &ICache[((address & 0xFF0) >> 2)];
         const uint8 *FMP = (uint8 *)(FastMap[(address & 0xFFFFFFF0) >> FAST_MAP_SHIFT] + (address & 0xFFFFFFF0));

         /* | 0x2 to simulate (in)validity bits. */
         ICI[0x00].TV = (address & 0xFFFFFFF0) | 0x0 | 0x2;
         ICI[0x01].TV = (address & 0xFFFFFFF0) | 0x4 | 0x2;
         ICI[0x02].TV = (address & 0xFFFFFFF0) | 0x8 | 0x2;
         ICI[0x03].TV = (address & 0xFFFFFFF0) | 0xC | 0x2;

         if (!psx_gte_overclock)
            *timestamp += 3;

         switch (address & 0xC)
         {
            case 0x0:
               if (!psx_gte_overclock)
                  (*timestamp)++;
               ICI[0x00].TV  &= ~0x2;
               ICI[0x00].Data = MDFN_de32lsb_aligned(&FMP[0x0]);
            case 0x4:
               if (!psx_gte_overclock)
                  (*timestamp)++;
               ICI[0x01].TV  &= ~0x2;
               ICI[0x01].Data = MDFN_de32lsb_aligned(&FMP[0x4]);
            case 0x8:
               if (!psx_gte_overclock)
                  (*timestamp)++;
               ICI[0x02].TV  &= ~0x2;
               ICI[0x02].Data = MDFN_de32lsb_aligned(&FMP[0x8]);
            case 0xC:
               if (!psx_gte_overclock)
                  (*timestamp)++;
               ICI[0x03].TV  &= ~0x2;
               ICI[0x03].Data = MDFN_de32lsb_aligned(&FMP[0xC]);
               break;
         }
         instr = ICache[(address & 0xFFC) >> 2].Data;
      }
   }

   return instr;
}

static uint32_t NO_INLINE CPU_Exception(uint32_t code, uint32_t PC, const uint32_t NP, const uint32_t instr)
{
   uint32_t handler = 0x80000080;

   assert(code < 16);

   if (CP0.SR & (1 << 22)) /* BEV */
      handler = 0xBFC00180;

   CP0.EPC = PC;
   if (BDBT & 2)
   {
      CP0.EPC -= 4;
      CP0.TAR  = NP;
   }

   /* "Push" IEc and KUc (so that the new IEc and KUc are 0) */
   CP0.SR = (CP0.SR & ~0x3F) | ((CP0.SR << 2) & 0x3F);

   /* Setup cause register */
   CP0.CAUSE &= 0x0000FF00;
   CP0.CAUSE |= code << 2;

   CP0.CAUSE |= BDBT << 30;
   CP0.CAUSE |= (instr << 2) & (0x3 << 28); /* CE */

   CPU_RecalcIPCache();

   BDBT = 0;

   return handler;
}

#define BACKING_TO_ACTIVE			\
	PC = BACKED_PC;				\
	new_PC = BACKED_new_PC;			\
	LDWhich = BACKED_LDWhich;		\
	LDValue = BACKED_LDValue;

#define ACTIVE_TO_BACKING			\
	BACKED_PC = PC;				\
	BACKED_new_PC = new_PC;			\
	BACKED_LDWhich = LDWhich;		\
	BACKED_LDValue = LDValue;

//
//
#define GPR_DEPRES_BEGIN { uint8 back = ReadAbsorb[0];
#define GPR_DEP(n) { unsigned tn = (n); ReadAbsorb[tn] = 0; }
#define GPR_RES(n) { unsigned tn = (n); ReadAbsorb[tn] = 0; }
#define GPR_DEPRES_END ReadAbsorb[0] = back; }

static pscpu_timestamp_t CPU_RunReal(PS_CPU *self, pscpu_timestamp_t timestamp_in)
{
   pscpu_timestamp_t timestamp = timestamp_in;

   uint32_t PC;
   uint32_t new_PC;
   uint32_t LDWhich;
   uint32_t LDValue;

   (void)self;
 //printf("%d %d\n", gte_ts_done, muldiv_ts_done);

 gte_ts_done += timestamp;
 muldiv_ts_done += timestamp;

 BACKING_TO_ACTIVE;

#if defined(HAVE_LIGHTREC) && defined(LIGHTREC_DEBUG)
 u32 oldpc = PC;
#endif

 do
 {
  //printf("Running: %d %d\n", timestamp, next_event_ts);
  while(MDFN_LIKELY(timestamp < next_event_ts))
  {
   uint32 instr;
   uint32 opf;

   // Zero must be zero...until the Master Plan is enacted.
   GPR[0] = 0;


   //
   // Instruction fetch
   //
   if(MDFN_UNLIKELY(PC & 0x3))
   {
    // This will block interrupt processing, but since we're going more for keeping broken homebrew/hacks from working
    // than super-duper-accurate pipeline emulation, it shouldn't be a problem.
    CP0.BADA = PC;
    new_PC = CPU_Exception(EXCEPTION_ADEL, PC, new_PC, 0);
    goto OpDone;
   }

   instr = ReadInstruction(&timestamp, PC);


   // 
   // Instruction decode
   //
   opf = instr & 0x3F;

   if(instr & (0x3F << 26))
    opf = 0x40 | (instr >> 26);

   opf |= IPCache;

   if(ReadAbsorb[ReadAbsorbWhich])
    ReadAbsorb[ReadAbsorbWhich]--;
   else
    timestamp++;

   #define DO_LDS() { s_cpu.GPR_full[LDWhich] = LDValue; ReadAbsorb[LDWhich] = LDAbsorb; ReadFudge = LDWhich; ReadAbsorbWhich |= LDWhich & 0x1F; LDWhich = 0x22; }
   #define BEGIN_OPF(name) { op_##name:
   #define END_OPF goto OpDone; }

   #define DO_BRANCH(arg_cond, arg_offset, arg_mask, arg_dolink, arg_linkreg)\
	{							\
	 const bool cond = (arg_cond);				\
	 const uint32 offset = (arg_offset);			\
	 const uint32 mask = (arg_mask);			\
	 PC = new_PC;						\
	 new_PC += 4;						\
	 BDBT = 2;						\
								\
	 if(arg_dolink)						\
	  GPR[(arg_linkreg)] = new_PC;				\
								\
	 if(cond)						\
	 {							\
	  new_PC = ((new_PC - 4) & mask) + offset;		\
	  BDBT = 3;						\
	 }							\
								\
	 goto SkipNPCStuff;					\
	}

   #define ITYPE uint32 rs MDFN_NOWARN_UNUSED = (instr >> 21) & 0x1F; uint32 rt MDFN_NOWARN_UNUSED = (instr >> 16) & 0x1F; uint32 immediate = (int32)(int16)(instr & 0xFFFF); /*printf(" rs=%02x(%08x), rt=%02x(%08x), immediate=(%08x) ", rs, GPR[rs], rt, GPR[rt], immediate);*/
   #define ITYPE_ZE uint32 rs MDFN_NOWARN_UNUSED = (instr >> 21) & 0x1F; uint32 rt MDFN_NOWARN_UNUSED = (instr >> 16) & 0x1F; uint32 immediate = instr & 0xFFFF; /*printf(" rs=%02x(%08x), rt=%02x(%08x), immediate=(%08x) ", rs, GPR[rs], rt, GPR[rt], immediate);*/
   #define JTYPE uint32 target = instr & ((1 << 26) - 1); /*printf(" target=(%08x) ", target);*/
   #define RTYPE uint32 rs MDFN_NOWARN_UNUSED = (instr >> 21) & 0x1F; uint32 rt MDFN_NOWARN_UNUSED = (instr >> 16) & 0x1F; uint32 rd MDFN_NOWARN_UNUSED = (instr >> 11) & 0x1F; uint32 shamt MDFN_NOWARN_UNUSED = (instr >> 6) & 0x1F; /*printf(" rs=%02x(%08x), rt=%02x(%08x), rd=%02x(%08x) ", rs, GPR[rs], rt, GPR[rt], rd, GPR[rd]);*/

#if HAVE_COMPUTED_GOTO
   #if 0
	//
	// These truncated 32-bit table values apparently can't be calculated at compile/link time by gcc on x86_64, so gcc inserts initialization code, but
	// the compare for the initialization code path is placed sub-optimally(near where the table value is used instead of at the beginning of the function).
	//
	#define CGBEGIN static const uint32 const op_goto_table[256] = {
	#define CGE(l) (uint32)(uintptr_t)&&l,
	#define CGEND }; goto *(void*)(uintptr_t)op_goto_table[opf];
   #else
	#define CGBEGIN static const void *const op_goto_table[256] = {
	#define CGE(l) &&l,
	#define CGEND }; goto *op_goto_table[opf];
   #endif
#else
   /* (uint8) cast for cheaper alternative to generated branch+compare bounds check instructions, but still more
      expensive than computed goto which needs no masking nor bounds checking.
   */
   #define CGBEGIN { enum { CGESB = 1 + __COUNTER__ }; switch((uint8)opf) {
   #define CGE(l) case __COUNTER__ - CGESB: goto l;
   #define CGEND } }
#endif

   CGBEGIN
    CGE(op_SLL)  CGE(op_ILL)   CGE(op_SRL)  CGE(op_SRA)   CGE(op_SLLV)    CGE(op_ILL)   CGE(op_SRLV) CGE(op_SRAV)
    CGE(op_JR)   CGE(op_JALR)  CGE(op_ILL)  CGE(op_ILL)   CGE(op_SYSCALL) CGE(op_BREAK) CGE(op_ILL)  CGE(op_ILL)
    CGE(op_MFHI) CGE(op_MTHI)  CGE(op_MFLO) CGE(op_MTLO)  CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)
    CGE(op_MULT) CGE(op_MULTU) CGE(op_DIV)  CGE(op_DIVU)  CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)
    CGE(op_ADD)  CGE(op_ADDU)  CGE(op_SUB)  CGE(op_SUBU)  CGE(op_AND)     CGE(op_OR)    CGE(op_XOR)  CGE(op_NOR)
    CGE(op_ILL)  CGE(op_ILL)   CGE(op_SLT)  CGE(op_SLTU)  CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)
    CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)
    CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)

    CGE(op_ILL)  CGE(op_BCOND) CGE(op_J)    CGE(op_JAL)   CGE(op_BEQ)  CGE(op_BNE) CGE(op_BLEZ) CGE(op_BGTZ)
    CGE(op_ADDI) CGE(op_ADDIU) CGE(op_SLTI) CGE(op_SLTIU) CGE(op_ANDI) CGE(op_ORI) CGE(op_XORI) CGE(op_LUI)
    CGE(op_COP0) CGE(op_COP13) CGE(op_COP2) CGE(op_COP13) CGE(op_ILL)  CGE(op_ILL) CGE(op_ILL)  CGE(op_ILL)
    CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL) CGE(op_ILL)  CGE(op_ILL)
    CGE(op_LB)   CGE(op_LH)    CGE(op_LWL)  CGE(op_LW)    CGE(op_LBU)  CGE(op_LHU) CGE(op_LWR)  CGE(op_ILL)
    CGE(op_SB)   CGE(op_SH)    CGE(op_SWL)  CGE(op_SW)    CGE(op_ILL)  CGE(op_ILL) CGE(op_SWR)  CGE(op_ILL)
    CGE(op_LWC013) CGE(op_LWC013)  CGE(op_LWC2) CGE(op_LWC013)  CGE(op_ILL)  CGE(op_ILL) CGE(op_ILL)  CGE(op_ILL)
    CGE(op_SWC013) CGE(op_SWC013)  CGE(op_SWC2) CGE(op_SWC013)  CGE(op_ILL)  CGE(op_ILL) CGE(op_ILL)  CGE(op_ILL)

    // Interrupt portion of this table is constructed so that an interrupt won't be taken when the PC is pointing to a GTE instruction,
    // to avoid problems caused by pipeline vs coprocessor nuances that aren't emulated.
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)

    CGE(op_ILL)       CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_COP2)      CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
   CGEND

   {
    BEGIN_OPF(ILL);
	     DO_LDS();
	     new_PC = CPU_Exception(EXCEPTION_RI, PC, new_PC, instr);
    END_OPF;

    //
    // ADD - Add Word
    //
    BEGIN_OPF(ADD);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] + GPR[rt];
	bool ep = ((~(GPR[rs] ^ GPR[rt])) & (GPR[rs] ^ result)) & 0x80000000;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ADD(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	if(MDFN_UNLIKELY(ep))
	{
	 new_PC = CPU_Exception(EXCEPTION_OV, PC, new_PC, instr);
	}
	else
	 GPR[rd] = result;

    END_OPF;

    //
    // ADDI - Add Immediate Word
    //
    BEGIN_OPF(ADDI);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

        uint32 result = GPR[rs] + immediate;
	bool ep = ((~(GPR[rs] ^ immediate)) & (GPR[rs] ^ result)) & 0x80000000;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ADDI(instr, result, GPR[rs]);

	DO_LDS();

        if(MDFN_UNLIKELY(ep))
	{
	 new_PC = CPU_Exception(EXCEPTION_OV, PC, new_PC, instr);
	}
        else
         GPR[rt] = result;

    END_OPF;

    //
    // ADDIU - Add Immediate Unsigned Word
    //
    BEGIN_OPF(ADDIU);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = GPR[rs] + immediate;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ADDIU(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;

    //
    // ADDU - Add Unsigned Word
    //
    BEGIN_OPF(ADDU);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] + GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ADDU(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // AND - And
    //
    BEGIN_OPF(AND);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] & GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_AND(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // ANDI - And Immediate
    //
    BEGIN_OPF(ANDI);
	ITYPE_ZE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = GPR[rs] & immediate;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ANDI(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;

    //
    // BEQ - Branch on Equal
    //
    BEGIN_OPF(BEQ);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	const bool result = (GPR[rs] == GPR[rt]);

	DO_LDS();

	DO_BRANCH(result, (immediate << 2), ~0U, false, 0);
    END_OPF;

    // Bah, why does MIPS encoding have to be funky like this. :(
    // Handles BGEZ, BGEZAL, BLTZ, BLTZAL
    BEGIN_OPF(BCOND);
	const uint32 tv = GPR[(instr >> 21) & 0x1F];
	const uint32 riv = (instr >> 16) & 0x1F;
	const uint32 immediate = (int32)(int16)(instr & 0xFFFF);
	const bool result = (int32)(tv ^ (riv << 31)) < 0;
	const uint32 link = ((riv & 0x1E) == 0x10) ? 31 : 0;

	GPR_DEPRES_BEGIN
	GPR_DEP((instr >> 21) & 0x1F);
	GPR_RES(link);
	GPR_DEPRES_END

	DO_LDS();

	DO_BRANCH(result, (immediate << 2), ~0U, true, link);
    END_OPF;


    //
    // BGTZ - Branch on Greater than Zero
    //
    BEGIN_OPF(BGTZ);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	const bool result = (int32)GPR[rs] > 0;

	DO_LDS();

	DO_BRANCH(result, (immediate << 2), ~0U, false, 0);
    END_OPF;

    //
    // BLEZ - Branch on Less Than or Equal to Zero
    //
    BEGIN_OPF(BLEZ);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	const bool result = (int32)GPR[rs] <= 0;

	DO_LDS();

	DO_BRANCH(result, (immediate << 2), ~0U, false, 0);
    END_OPF;

    //
    // BNE - Branch on Not Equal
    //
    BEGIN_OPF(BNE);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	const bool result = GPR[rs] != GPR[rt];

	DO_LDS();

	DO_BRANCH(result, (immediate << 2), ~0U, false, 0);
    END_OPF;

    //
    // BREAK - Breakpoint
    //
    BEGIN_OPF(BREAK);
	DO_LDS();
	new_PC = CPU_Exception(EXCEPTION_BP, PC, new_PC, instr);
    END_OPF;

    // Cop "instructions":	CFCz(no CP0), COPz, CTCz(no CP0), LWCz(no CP0), MFCz, MTCz, SWCz(no CP0)
    //
    // COP0 instructions
    //
    BEGIN_OPF(COP0);
	const uint32 sub_op = (instr >> 21) & 0x1F;
	const uint32 rt = (instr >> 16) & 0x1F;
	const uint32 rd = (instr >> 11) & 0x1F;
	const uint32 val = GPR[rt];

	switch(sub_op)
	{
	 default:
		DO_LDS();
		break;

	 case 0x02:
	 case 0x06:
		DO_LDS();
		new_PC = CPU_Exception(EXCEPTION_RI, PC, new_PC, instr);
		break;

	 case 0x00:		// MFC0	- Move from Coprocessor
		switch(rd)
		{
		 case 0x00:
		 case 0x01:
		 case 0x02:
		 case 0x04:
		 case 0x0A:
			DO_LDS();
		  	new_PC = CPU_Exception(EXCEPTION_RI, PC, new_PC, instr);
			break;

		 case 0x03:
		 case 0x05:
		 case 0x06:
		 case 0x07:
		 case 0x08:
		 case 0x09:
		 case 0x0B:
		 case 0x0C:
		 case 0x0D:
		 case 0x0E:
		 case 0x0F:
			if(MDFN_UNLIKELY(LDWhich == rt))
		 	 LDWhich = 0;

			DO_LDS();

			LDAbsorb = 0;
			LDWhich = rt;
			LDValue = cpu_CP0.Regs[rd];
			break;

		 default:
			// Tested to be rather NOPish
			DO_LDS();
			break;
		}
		break;

	 case 0x04:		// MTC0	- Move to Coprocessor
		DO_LDS();
		switch(rd)
		{
		 case 0x00:
		 case 0x01:
		 case 0x02:
		 case 0x04:
		 case 0x0A:
			new_PC = CPU_Exception(EXCEPTION_RI, PC, new_PC, instr);
			break;

		 case CP0REG_BPC:
			CP0.BPC = val;
			break;

		 case CP0REG_BDA:
			CP0.BDA = val;
			break;

		 case CP0REG_DCIC:
			CP0.DCIC = val & 0xFF80003F;
			break;

  		 case CP0REG_BDAM:
			CP0.BDAM = val;
			break;

  		 case CP0REG_BPCM:
			CP0.BPCM = val;
			break;

		 case CP0REG_CAUSE:
			CP0.CAUSE &= ~(0x3 << 8);
			CP0.CAUSE |= val & (0x3 << 8);
			CPU_RecalcIPCache();
			break;

		 case CP0REG_SR:
			CP0.SR = val & ~( (0x3 << 26) | (0x3 << 23) | (0x3 << 6));
			CPU_RecalcIPCache();
			break;
		}
		break;

	 case 0x08:	// BC
	 case 0x0C:
		DO_LDS();
		{
		 const uint32 immediate = (int32)(int16)(instr & 0xFFFF);
		 const bool result = (false == (bool)(instr & (1U << 16)));
		 DO_BRANCH(result, (immediate << 2), ~0U, false, 0);
		}
		break;

	 case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
	 case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
		DO_LDS();
		{
		 const uint32 cp0_op = instr & 0x1F;	// Not 0x3F

		 if(MDFN_LIKELY(cp0_op == 0x10))	// RFE
		 {
		  // "Pop"
		  CP0.SR = (CP0.SR & ~0x0F) | ((CP0.SR >> 2) & 0x0F);
		  CPU_RecalcIPCache();
		 }
		 else if(cp0_op == 0x01 || cp0_op == 0x02 || cp0_op == 0x06 || cp0_op == 0x08)	// TLBR, TLBWI, TLBWR, TLBP
		 {
		  new_PC = CPU_Exception(EXCEPTION_RI, PC, new_PC, instr);
		 }
		}
		break;
	}
    END_OPF;

    //
    // COP2
    //
    BEGIN_OPF(COP2);
	const uint32 sub_op = (instr >> 21) & 0x1F;
	const uint32 rt = (instr >> 16) & 0x1F;
	const uint32 rd = (instr >> 11) & 0x1F;
	const uint32 val = GPR[rt];

	if(MDFN_UNLIKELY(!(CP0.SR & (1U << (28 + 2)))))
	{
	 DO_LDS();
         new_PC = CPU_Exception(EXCEPTION_COPU, PC, new_PC, instr);
	}
	else switch(sub_op)
	{
	 default:
		DO_LDS();
		break;

	 case 0x00:		// MFC2	- Move from Coprocessor
		if(MDFN_UNLIKELY(LDWhich == rt))
		 LDWhich = 0;

		DO_LDS();

	        if(timestamp < gte_ts_done)
		{
		 LDAbsorb = gte_ts_done - timestamp;
	         timestamp = gte_ts_done;
		}
		else
		 LDAbsorb = 0;

		LDWhich = rt;
		LDValue = GTE_ReadDR(rd);

                if (PGXP_GetModes() & PGXP_MODE_GTE)
                   PGXP_GTE_MFC2(instr, LDValue, LDValue);

		break;

	 case 0x04:		// MTC2	- Move to Coprocessor
		DO_LDS();

	        if(timestamp < gte_ts_done)
	         timestamp = gte_ts_done;

		GTE_WriteDR(rd, val);

                if (PGXP_GetModes() & PGXP_MODE_GTE)
                   PGXP_GTE_MTC2(instr, val, val);

		break;

	 case 0x02:		// CFC2
		if(MDFN_UNLIKELY(LDWhich == rt))
		 LDWhich = 0;

		DO_LDS();

	        if(timestamp < gte_ts_done)
		{
		 LDAbsorb = gte_ts_done - timestamp;
	         timestamp = gte_ts_done;
		}
		else
		 LDAbsorb = 0;

		LDWhich = rt;
		LDValue = GTE_ReadCR(rd);

                if (PGXP_GetModes() & PGXP_MODE_GTE)
                   PGXP_GTE_CFC2(instr, LDValue, LDValue);

		break;

	 case 0x06:		// CTC2
		DO_LDS();

 	        if(timestamp < gte_ts_done)
	         timestamp = gte_ts_done;

		GTE_WriteCR(rd, val);

                if (PGXP_GetModes() & PGXP_MODE_GTE)
                   PGXP_GTE_CTC2(instr, val, val);

		break;

	 case 0x08:
	 case 0x0C:
		DO_LDS();
		{
		 const uint32 immediate = (int32)(int16)(instr & 0xFFFF);
		 const bool result = (false == (bool)(instr & (1U << 16)));
		 DO_BRANCH(result, (immediate << 2), ~0U, false, 0);
		}
		break;

	 case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
	 case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
		DO_LDS();

	        if(timestamp < gte_ts_done)
	         timestamp = gte_ts_done;
		gte_ts_done = timestamp + GTE_Instruction(instr);
		break;
	}
    END_OPF;

    //
    // COP1, COP3
    //
    BEGIN_OPF(COP13);
	DO_LDS();

	if(!(CP0.SR & (1U << (28 + ((instr >> 26) & 0x3)))))
	{
         new_PC = CPU_Exception(EXCEPTION_COPU, PC, new_PC, instr);
	}
	else
	{
	 const uint32 sub_op = (instr >> 21) & 0x1F;
	 if(sub_op == 0x08 || sub_op == 0x0C)
	 {
	  const uint32 immediate = (int32)(int16)(instr & 0xFFFF);
	  const bool result = (false == (bool)(instr & (1U << 16)));

	  DO_BRANCH(result, (immediate << 2), ~0U, false, 0);
	 }
	}
    END_OPF;

    //
    // LWC0, LWC1, LWC3
    //
    BEGIN_OPF(LWC013);
        ITYPE;
	const uint32 address = GPR[rs] + immediate;

	DO_LDS();

	if(!(CP0.SR & (1U << (28 + ((instr >> 26) & 0x3)))))
	{
         new_PC = CPU_Exception(EXCEPTION_COPU, PC, new_PC, instr);
	}
	else
	{
	 if(MDFN_UNLIKELY(address & 3))
	 {
	  CP0.BADA = address;
          new_PC = CPU_Exception(EXCEPTION_ADEL, PC, new_PC, instr);
	 }
         else
          ReadMemory_u32(&timestamp, address, false, true);
	}
    END_OPF;

    //
    // LWC2
    //
    BEGIN_OPF(LWC2);
        ITYPE;
	const uint32 address = GPR[rs] + immediate;

	DO_LDS();

        if(MDFN_UNLIKELY(address & 3))
	{
	 CP0.BADA = address;
         new_PC = CPU_Exception(EXCEPTION_ADEL, PC, new_PC, instr);
	}
        else
	{
         if(timestamp < gte_ts_done)
          timestamp = gte_ts_done;

         uint32_t value = ReadMemory_u32(&timestamp, address, false, true);
         GTE_WriteDR(rt, value);

         if (PGXP_GetModes() & PGXP_MODE_GTE)
            PGXP_GTE_LWC2(instr, value, address);

	}
	// GTE stuff here
    END_OPF;

    //
    // SWC0, SWC1, SCW3
    //
    BEGIN_OPF(SWC013);
        ITYPE;
	const uint32 address = GPR[rs] + immediate;

	DO_LDS();

	if(!(CP0.SR & (1U << (28 + ((instr >> 26) & 0x3)))))
	{
         new_PC = CPU_Exception(EXCEPTION_COPU, PC, new_PC, instr);
	}
	else
	{
	 if(MDFN_UNLIKELY(address & 0x3))
	 {
	  CP0.BADA = address;
	  new_PC = CPU_Exception(EXCEPTION_ADES, PC, new_PC, instr);
	 }
	}
    END_OPF;

    //
    // SWC2
    //
    BEGIN_OPF(SWC2);
        ITYPE;
	const uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(address & 0x3))
	{
	 CP0.BADA = address;
	 new_PC = CPU_Exception(EXCEPTION_ADES, PC, new_PC, instr);
	}
	else
	{
         if(timestamp < gte_ts_done)
          timestamp = gte_ts_done;

	 WriteMemory_u32(&timestamp, address, GTE_ReadDR(rt), false);

	 if (PGXP_GetModes() & PGXP_MODE_GTE)
            PGXP_GTE_SWC2(instr, GTE_ReadDR(rt), address);
	}
	DO_LDS();
    END_OPF;

    //
    // DIV - Divide Word
    //
    BEGIN_OPF(DIV);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        if(!GPR[rt])
        {
	 if(GPR[rs] & 0x80000000)
	  LO = 1;
	 else
	  LO = 0xFFFFFFFF;

	 HI = GPR[rs];
        }
	else if(GPR[rs] == 0x80000000 && GPR[rt] == 0xFFFFFFFF)
	{
	 LO = 0x80000000;
	 HI = 0;
	}
        else
        {
         LO = (int32)GPR[rs] / (int32)GPR[rt];
         HI = (int32)GPR[rs] % (int32)GPR[rt];
        }
	muldiv_ts_done = timestamp + 37;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_DIV(instr, HI, LO, GPR[rs], GPR[rt]);
	DO_LDS();

    END_OPF;


    //
    // DIVU - Divide Unsigned Word
    //
    BEGIN_OPF(DIVU);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	if(!GPR[rt])
	{
	 LO = 0xFFFFFFFF;
	 HI = GPR[rs];
	}
	else
	{
	 LO = GPR[rs] / GPR[rt];
	 HI = GPR[rs] % GPR[rt];
	}
 	muldiv_ts_done = timestamp + 37;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_DIVU(instr, HI, LO, GPR[rs], GPR[rt]);

	DO_LDS();
    END_OPF;

    //
    // J - Jump
    //
    BEGIN_OPF(J);
	JTYPE;

	DO_LDS();

	DO_BRANCH(true, target << 2, 0xF0000000, false, 0);
    END_OPF;

    //
    // JAL - Jump and Link
    //
    BEGIN_OPF(JAL);
	JTYPE;

	//GPR_DEPRES_BEGIN
	GPR_RES(31);
	//GPR_DEPRES_END

	DO_LDS();

	DO_BRANCH(true, target << 2, 0xF0000000, true, 31);
    END_OPF;

    //
    // JALR - Jump and Link Register
    //
    BEGIN_OPF(JALR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 tmp = GPR[rs];

	DO_LDS();

	DO_BRANCH(true, tmp, 0, true, rd);
    END_OPF;

    //
    // JR - Jump Register
    //
    BEGIN_OPF(JR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 bt = GPR[rs];

	DO_LDS();

	DO_BRANCH(true, bt, 0, false, 0);
    END_OPF;

    //
    // LUI - Load Upper Immediate
    //
    BEGIN_OPF(LUI);
	ITYPE_ZE;		// Actually, probably would be sign-extending...if we were emulating a 64-bit MIPS chip :b

	GPR_DEPRES_BEGIN
	GPR_RES(rt);
	GPR_DEPRES_END

	DO_LDS();

	GPR[rt] = immediate << 16;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_LUI(instr, GPR[rt]);

    END_OPF;

    //
    // MFHI - Move from HI
    //
    BEGIN_OPF(MFHI);
	RTYPE;

	GPR_DEPRES_BEGIN
 	GPR_RES(rd);
	GPR_DEPRES_END

	DO_LDS();

	if(timestamp < muldiv_ts_done)
	{
	 if(timestamp == muldiv_ts_done - 1)
	  muldiv_ts_done--;
	 else
	 {
	  do
	  {
	   if(ReadAbsorb[ReadAbsorbWhich])
	    ReadAbsorb[ReadAbsorbWhich]--;
	   timestamp++;
	  } while(timestamp < muldiv_ts_done);
	 }
	}

	GPR[rd] = HI;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MFHI(instr, GPR[rd], HI);

    END_OPF;


    //
    // MFLO - Move from LO
    //
    BEGIN_OPF(MFLO);
	RTYPE;

	GPR_DEPRES_BEGIN
 	GPR_RES(rd);
	GPR_DEPRES_END

	DO_LDS();

	if(timestamp < muldiv_ts_done)
	{
	 if(timestamp == muldiv_ts_done - 1)
	  muldiv_ts_done--;
	 else
	 {
	  do
	  {
	   if(ReadAbsorb[ReadAbsorbWhich])
	    ReadAbsorb[ReadAbsorbWhich]--;
	   timestamp++;
	  } while(timestamp < muldiv_ts_done);
	 }
	}

	GPR[rd] = LO;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MFLO(instr, GPR[rd], LO);

    END_OPF;


    //
    // MTHI - Move to HI
    //
    BEGIN_OPF(MTHI);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	HI = GPR[rs];

	DO_LDS();

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MTHI(instr, HI, GPR[rs]);

    END_OPF;

    //
    // MTLO - Move to LO
    //
    BEGIN_OPF(MTLO);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	LO = GPR[rs];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MTLO(instr, LO, GPR[rs]);

	DO_LDS();

    END_OPF;


    //
    // MULT - Multiply Word
    //
    BEGIN_OPF(MULT);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	uint64 result;

	result = (int64)(int32)GPR[rs] * (int32)GPR[rt];
	muldiv_ts_done = timestamp + MULT_Tab24[MDFN_lzcount32((GPR[rs] ^ ((int32)GPR[rs] >> 31)) | 0x400)];
	DO_LDS();

	LO = result;
	HI = result >> 32;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MULT(instr, HI, LO, GPR[rs], GPR[rt]);

    END_OPF;

    //
    // MULTU - Multiply Unsigned Word
    //
    BEGIN_OPF(MULTU);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	uint64 result;

	result = (uint64)GPR[rs] * GPR[rt];
	muldiv_ts_done = timestamp + MULT_Tab24[MDFN_lzcount32(GPR[rs] | 0x400)];
	DO_LDS();

	LO = result;
	HI = result >> 32;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MULTU(instr, HI, LO, GPR[rs], GPR[rt]);

    END_OPF;


    //
    // NOR - NOR
    //
    BEGIN_OPF(NOR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = ~(GPR[rs] | GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_NOR(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // OR - OR
    //
    BEGIN_OPF(OR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] | GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_OR(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // ORI - OR Immediate
    //
    BEGIN_OPF(ORI);
	ITYPE_ZE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = GPR[rs] | immediate;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ORI(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;


    //
    // SLL - Shift Word Left Logical
    //
    BEGIN_OPF(SLL);	// SLL
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rt] << shamt;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLL(instr, result, GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SLLV - Shift Word Left Logical Variable
    //
    BEGIN_OPF(SLLV);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rt] << (GPR[rs] & 0x1F);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLLV(instr, result, GPR[rt], GPR[rs]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // SLT - Set on Less Than
    //
    BEGIN_OPF(SLT);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = (bool)((int32)GPR[rs] < (int32)GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLT(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SLTI - Set on Less Than Immediate
    //
    BEGIN_OPF(SLTI);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = (bool)((int32)GPR[rs] < (int32)immediate);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLTI(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;


    //
    // SLTIU - Set on Less Than Immediate, Unsigned
    //
    BEGIN_OPF(SLTIU);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = (bool)(GPR[rs] < (uint32)immediate);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLTIU(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;


    //
    // SLTU - Set on Less Than, Unsigned
    //
    BEGIN_OPF(SLTU);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = (bool)(GPR[rs] < GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLTU(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SRA - Shift Word Right Arithmetic
    //
    BEGIN_OPF(SRA);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = ((int32)GPR[rt]) >> shamt;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SRA(instr, result, GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SRAV - Shift Word Right Arithmetic Variable
    //
    BEGIN_OPF(SRAV);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = ((int32)GPR[rt]) >> (GPR[rs] & 0x1F);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SRAV(instr, result, GPR[rt], GPR[rs]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SRL - Shift Word Right Logical
    //
    BEGIN_OPF(SRL);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rt] >> shamt;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SRL(instr, result, GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // SRLV - Shift Word Right Logical Variable
    //
    BEGIN_OPF(SRLV);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rt] >> (GPR[rs] & 0x1F);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SRLV(instr, result, GPR[rt], GPR[rs]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SUB - Subtract Word
    //
    BEGIN_OPF(SUB);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] - GPR[rt];
	bool ep = (((GPR[rs] ^ GPR[rt])) & (GPR[rs] ^ result)) & 0x80000000;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SUB(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	if(MDFN_UNLIKELY(ep))
	{
	 new_PC = CPU_Exception(EXCEPTION_OV, PC, new_PC, instr);
	}
	else
	 GPR[rd] = result;

    END_OPF;


    //
    // SUBU - Subtract Unsigned Word
    //
    BEGIN_OPF(SUBU);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] - GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SUBU(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SYSCALL
    //
    BEGIN_OPF(SYSCALL);
	DO_LDS();

	new_PC = CPU_Exception(EXCEPTION_SYSCALL, PC, new_PC, instr);
    END_OPF;


    //
    // XOR
    //
    BEGIN_OPF(XOR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] ^ GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_XOR(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // XORI - Exclusive OR Immediate
    //
    BEGIN_OPF(XORI);
	ITYPE_ZE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = GPR[rs] ^ immediate;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_XORI(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;
    END_OPF;

    //
    // Memory access instructions(besides the coprocessor ones) follow:
    //

    //
    // LB - Load Byte
    //
    BEGIN_OPF(LB);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(LDWhich == rt))
	 LDWhich = 0;

	DO_LDS();

	LDWhich = rt;
	LDValue = (int32)(int8)ReadMemory_u8(&timestamp, address);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LB(instr, LDValue, address);
    END_OPF;

    //
    // LBU - Load Byte Unsigned
    //
    BEGIN_OPF(LBU);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(LDWhich == rt))
	 LDWhich = 0;

	DO_LDS();

        LDWhich = rt;
	LDValue = ReadMemory_u8(&timestamp, address);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LBU(instr, LDValue, address);
    END_OPF;

    //
    // LH - Load Halfword
    //
    BEGIN_OPF(LH);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(address & 1))
	{
	 DO_LDS();

	 CP0.BADA = address;
	 new_PC = CPU_Exception(EXCEPTION_ADEL, PC, new_PC, instr);
	}
	else
	{
	 if(MDFN_UNLIKELY(LDWhich == rt))
	  LDWhich = 0;

	 DO_LDS();

	 LDWhich = rt;
         LDValue = (int32)(int16)ReadMemory_u16(&timestamp, address);
	}
	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LH(instr, LDValue, address);
    END_OPF;

    //
    // LHU - Load Halfword Unsigned
    //
    BEGIN_OPF(LHU);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

        if(MDFN_UNLIKELY(address & 1))
	{
	 DO_LDS();

	 CP0.BADA = address;
         new_PC = CPU_Exception(EXCEPTION_ADEL, PC, new_PC, instr);
	}
	else
	{
	 if(MDFN_UNLIKELY(LDWhich == rt))
	  LDWhich = 0;

	 DO_LDS();

	 LDWhich = rt;
         LDValue = ReadMemory_u16(&timestamp, address);
	}

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LHU(instr, LDValue, address);
    END_OPF;


    //
    // LW - Load Word
    //
    BEGIN_OPF(LW);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

        if(MDFN_UNLIKELY(address & 3))
	{
	 DO_LDS();

	 CP0.BADA = address;
         new_PC = CPU_Exception(EXCEPTION_ADEL, PC, new_PC, instr);
	}
        else
	{
	 if(MDFN_UNLIKELY(LDWhich == rt))
	  LDWhich = 0;

	 DO_LDS();

	 LDWhich = rt;
         LDValue = ReadMemory_u32(&timestamp, address, false, false);
	}

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LW(instr, LDValue, address);
    END_OPF;

    //
    // SB - Store Byte
    //
    BEGIN_OPF(SB);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	uint32 address = GPR[rs] + immediate;

	WriteMemory_u8(&timestamp, address, GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SB(instr, GPR[rt], address);

	DO_LDS();
    END_OPF;

    // 
    // SH - Store Halfword
    //
    BEGIN_OPF(SH);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(address & 0x1))
	{
	 CP0.BADA = address;
	 new_PC = CPU_Exception(EXCEPTION_ADES, PC, new_PC, instr);
	}
	else
	 WriteMemory_u16(&timestamp, address, GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SH(instr, GPR[rt], address);

	DO_LDS();
    END_OPF;

    // 
    // SW - Store Word
    //
    BEGIN_OPF(SW);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(address & 0x3))
	{
	 CP0.BADA = address;
	 new_PC = CPU_Exception(EXCEPTION_ADES, PC, new_PC, instr);
	}
	else
	 WriteMemory_u32(&timestamp, address, GPR[rt], false);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SW(instr, GPR[rt], address);

	DO_LDS();
    END_OPF;

    // LWL and LWR load delay slot tomfoolery appears to apply even to MFC0! (and probably MFCn and CFCn as well, though they weren't explicitly tested)

    //
    // LWL - Load Word Left
    //
    BEGIN_OPF(LWL);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	//GPR_DEP(rt);
	GPR_DEPRES_END

	uint32 address = GPR[rs] + immediate;
	uint32 v = GPR[rt];

	if(LDWhich == rt)
	{
	 v = LDValue;
	 ReadFudge = 0;
	}
	else
	{
	 DO_LDS();
	}

	LDWhich = rt;
	switch(address & 0x3)
	{
	 case 0: LDValue = (v & ~(0xFF << 24)) | (ReadMemory_u8(&timestamp, address & ~3) << 24);
		 break;

	 case 1: LDValue = (v & ~(0xFFFF << 16)) | (ReadMemory_u16(&timestamp, address & ~3) << 16);
	         break;

	 case 2: LDValue = (v & ~(0xFFFFFF << 8)) | (ReadMemory_u32(&timestamp, address & ~3, true, false) << 8);
		 break;

	 case 3: LDValue = (v & ~(0xFFFFFFFF << 0)) | (ReadMemory_u32(&timestamp, address & ~3, false, false) << 0);
		 break;
	}

        if (PGXP_GetModes() & PGXP_MODE_MEMORY)
	   PGXP_CPU_LWL(instr, LDValue, address);

    END_OPF;

    //
    // SWL - Store Word Left
    //
    BEGIN_OPF(SWL);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	switch(address & 0x3)
	{
	 case 0: WriteMemory_u8(&timestamp, address & ~3, GPR[rt] >> 24);
		 break;

	 case 1: WriteMemory_u16(&timestamp, address & ~3, GPR[rt] >> 16);
	         break;

	 case 2: WriteMemory_u32(&timestamp, address & ~3, GPR[rt] >> 8, true);
		 break;

	 case 3: WriteMemory_u32(&timestamp, address & ~3, GPR[rt] >> 0, false);
		 break;
	}
        if (PGXP_GetModes() & PGXP_MODE_MEMORY)
	   PGXP_CPU_SWL(instr, GPR[rt], address);

	DO_LDS();

    END_OPF;

    //
    // LWR - Load Word Right
    //
    BEGIN_OPF(LWR);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	//GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;
	uint32 v = GPR[rt];

	if(LDWhich == rt)
	{
	 v = LDValue;
	 ReadFudge = 0;
	}
	else
	{
	 DO_LDS();
	}

	LDWhich = rt;
	switch(address & 0x3)
	{
	 case 0: LDValue = (v & ~(0xFFFFFFFF)) | ReadMemory_u32(&timestamp, address, false, false);
		 break;

	 case 1: LDValue = (v & ~(0xFFFFFF)) | ReadMemory_u32(&timestamp, address, true, false);
		 break;

	 case 2: LDValue = (v & ~(0xFFFF)) | ReadMemory_u16(&timestamp, address);
	         break;

	 case 3: LDValue = (v & ~(0xFF)) | ReadMemory_u8(&timestamp, address);
		 break;
	}

        if (PGXP_GetModes() & PGXP_MODE_MEMORY)
	   PGXP_CPU_LWR(instr, LDValue, address);

    END_OPF;

    //
    // SWR - Store Word Right
    //
    BEGIN_OPF(SWR);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	switch(address & 0x3)
	{
	 case 0: WriteMemory_u32(&timestamp, address, GPR[rt], false);
		 break;

	 case 1: WriteMemory_u32(&timestamp, address, GPR[rt], true);
		 break;

	 case 2: WriteMemory_u16(&timestamp, address, GPR[rt]);
	         break;

	 case 3: WriteMemory_u8(&timestamp, address, GPR[rt]);
		 break;
	}

        if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SWR(instr, GPR[rt], address);


	DO_LDS();

    END_OPF;

    //
    // Mednafen special instruction
    //
    BEGIN_OPF(INTERRUPT);
	if(Halted)
	{
	 goto SkipNPCStuff;
	}
	else
	{
 	 DO_LDS();

	 new_PC = CPU_Exception(EXCEPTION_INT, PC, new_PC, instr);
	}
    END_OPF;
   }

   OpDone: ;
   PC = new_PC;
   new_PC = new_PC + 4;
   BDBT = 0;

   SkipNPCStuff:	;

   //printf("\n");
  }
#if defined(HAVE_LIGHTREC) && defined(LIGHTREC_DEBUG)
  if (timestamp >= 0 && PC != oldpc)
     print_for_big_ass_debugger(timestamp, PC);
#endif
 } while(MDFN_LIKELY(PSX_EventHandler(timestamp)));

 if(gte_ts_done > 0)
  gte_ts_done -= timestamp;

 if(muldiv_ts_done > 0)
  muldiv_ts_done -= timestamp;

 ACTIVE_TO_BACKING;

 return(timestamp);
}

pscpu_timestamp_t CPU_Run(PS_CPU *self, pscpu_timestamp_t timestamp_in)
{
#ifdef HAVE_LIGHTREC
   /* Track options changing. */
   if (MDFN_UNLIKELY(psx_dynarec != prev_dynarec || pgxpMode != PGXP_GetModes()) ||
       prev_invalidate != psx_dynarec_invalidate)
   {
      /* Init lightrec when changing dynarec, invalidate, or PGXP option;
       * cleans entire state if already running. */
      if (psx_dynarec != DYNAREC_DISABLED)
         lightrec_plugin_init(self);
      prev_dynarec    = psx_dynarec;
      pgxpMode        = PGXP_GetModes();
      prev_invalidate = psx_dynarec_invalidate;
   }

   if (next_interpreter > 0)
      next_interpreter--;

   if (psx_dynarec != DYNAREC_DISABLED)
      return lightrec_plugin_execute(self, timestamp_in);
#endif
   return CPU_RunReal(self, timestamp_in);
}

#undef BEGIN_OPF
#undef END_OPF
#undef MK_OPF

#define MK_OPF(op, funct)	((op) ? (0x40 | (op)) : (funct))
#define BEGIN_OPF(op, funct) case MK_OPF(op, funct): {
#define END_OPF } break;

#ifdef HAVE_LIGHTREC
#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#	define LE32TOH(x)	__builtin_bswap32(x)
#	define HTOLE32(x)	__builtin_bswap32(x)
#	define LE16TOH(x)	__builtin_bswap16(x)
#	define HTOLE16(x)	__builtin_bswap16(x)
#else
#	define LE32TOH(x)	(x)
#	define HTOLE32(x)	(x)
#	define LE16TOH(x)	(x)
#	define HTOLE16(x)	(x)
#endif

static inline u32 kunseg(u32 addr)
{
	if (MDFN_UNLIKELY(addr >= 0xa0000000))
		return addr - 0xa0000000;
	else
		return addr &~ 0x80000000;
}

enum opcodes {
        OP_SPECIAL              = 0x00,
        OP_REGIMM               = 0x01,
        OP_J                    = 0x02,
        OP_JAL                  = 0x03,
        OP_BEQ                  = 0x04,
        OP_BNE                  = 0x05,
        OP_BLEZ                 = 0x06,
        OP_BGTZ                 = 0x07,
        OP_ADDI                 = 0x08,
        OP_ADDIU                = 0x09,
        OP_SLTI                 = 0x0a,
        OP_SLTIU                = 0x0b,
        OP_ANDI                 = 0x0c,
        OP_ORI                  = 0x0d,
        OP_XORI                 = 0x0e,
        OP_LUI                  = 0x0f,
        OP_CP0                  = 0x10,
        OP_CP2                  = 0x12,
        OP_LB                   = 0x20,
        OP_LH                   = 0x21,
        OP_LWL                  = 0x22,
        OP_LW                   = 0x23,
        OP_LBU                  = 0x24,
        OP_LHU                  = 0x25,
        OP_LWR                  = 0x26,
        OP_SB                   = 0x28,
        OP_SH                   = 0x29,
        OP_SWL                  = 0x2a,
        OP_SW                   = 0x2b,
        OP_SWR                  = 0x2e,
        OP_LWC2                 = 0x32,
        OP_SWC2                 = 0x3a,
};

/* R3: was `static char *name = (char*) "beetle_psx_libretro"`,
 * a const-cast lie (the literal lives in .rodata).  Switching to a
 * writable char array mirrors what lightrec_init's `char *` arg
 * expects without the const cast. */
static char name[] = "beetle_psx_libretro";

#ifdef LIGHTREC_DEBUG
u32 lightrec_begin_cycles = 0;

u32 hash_calculate(const void *buffer, u32 count)
{
	unsigned int i;
	u32 *data = (u32 *) buffer;
	u32 hash = 0xffffffff;

	count /= 4;
	for(i = 0; i < count; ++i) {
		hash += data[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}

	hash += (hash << 3);
	hash ^= (hash >> 11);
	hash += (hash << 15);
	return hash;
}

static void print_for_big_ass_debugger(int32_t timestamp, uint32_t PC)
{
	uint8_t *psxM = (uint8_t *) MainRAM->data8;
	uint8_t *psxR = (uint8_t *) BIOSROM->data8;
	uint8_t *psxH = (uint8_t *) ScratchRAM->data8;

	unsigned int i;

	printf("CYCLE 0x%08x PC 0x%08x", timestamp, PC);

#ifdef LIGHTREC_VERY_DEBUG
	printf(" RAM 0x%08x SCRATCH 0x%08x",
		hash_calculate(psxM, 0x200000),
		hash_calculate(psxH, 0x400));
#endif

	printf(" CP0 0x%08x",
		hash_calculate(&cpu_CP0.Regs,
			sizeof(cpu_CP0.Regs)));

#ifdef LIGHTREC_VERY_DEBUG
	for (i = 0; i < 32; i++)
		printf(" GPR[%i] 0x%08x", i, GPR[i]);
	printf(" LO 0x%08x", LO);
	printf(" HI 0x%08x", HI);
#else
	printf(" GPR 0x%08x", hash_calculate(&GPR, 32*sizeof(uint32_t)));
#endif
	printf("\n");
}
#endif /* LIGHTREC_DEBUG */

static u32 cop_mfc(struct lightrec_state *state, u32 op, u8 reg)
{
	return cpu_CP0.Regs[reg];
}

static u32 cop_cfc(struct lightrec_state *state, u32 op, u8 reg)
{
	return cpu_CP0.Regs[reg];
}

static u32 cop2_mfc(struct lightrec_state *state, u32 op, u8 reg)
{
	return GTE_ReadDR(reg);
}

static u32 pgxp_cop2_mfc(struct lightrec_state *state, u32 op, u8 reg)
{
	u32 r = GTE_ReadDR(reg);

	if((op >> 26) == OP_CP2)
		PGXP_GTE_MFC2(op, r, r);

	return r;
}

static u32 cop2_cfc(struct lightrec_state *state, u32 op, u8 reg)
{
	return GTE_ReadCR(reg);
}

static u32 pgxp_cop2_cfc(struct lightrec_state *state, u32 op, u8 reg)
{
	u32 r = GTE_ReadCR(reg);

	PGXP_GTE_CFC2(op, r, r);

	return r;
}

static void cop_mtc_ctc(struct lightrec_state *state,
		u8 reg, u32 value)
{
	switch (reg) {
		case 1:
		case 4:
		case 8:
		case 14:
		case 15:
			/* Those registers are read-only */
			break;
		case 12: /* Status */
			if ((CP0.SR & ~value) & (1 << 16)) {
				memcpy(MainRAM->data8, cache_buf, sizeof(cache_buf));
				lightrec_invalidate_all(state);
			} else if ((~CP0.SR & value) & (1 << 16)) {
				memcpy(cache_buf, MainRAM->data8, sizeof(cache_buf));
			}

			CP0.SR = value & ~( (0x3 << 26) | (0x3 << 23) | (0x3 << 6));
			CPU_RecalcIPCache();
			lightrec_set_exit_flags(state,
					LIGHTREC_EXIT_CHECK_INTERRUPT);
			break;
		case 13: /* Cause */
			CP0.CAUSE &= ~0x0300;
			CP0.CAUSE |= value & 0x0300;
			CPU_RecalcIPCache();
			lightrec_set_exit_flags(state,
					LIGHTREC_EXIT_CHECK_INTERRUPT);
			break;
		default:
			cpu_CP0.Regs[reg] = value;
			break;
	}
}

static void cop_mtc(struct lightrec_state *state, u32 op, u8 reg, u32 value)
{
	cop_mtc_ctc(state, reg, value);
}

static void cop_ctc(struct lightrec_state *state, u32 op, u8 reg, u32 value)
{
	cop_mtc_ctc(state, reg, value);
}

static void cop2_mtc(struct lightrec_state *state, u32 op, u8 reg, u32 value)
{
	GTE_WriteDR(reg, value);
}

static void pgxp_cop2_mtc(struct lightrec_state *state, u32 op, u8 reg, u32 value)
{
	GTE_WriteDR(reg, value);
	if((op >> 26) == OP_CP2)
		PGXP_GTE_MTC2(op, value, value);
}

static void cop2_ctc(struct lightrec_state *state, u32 op, u8 reg, u32 value)
{
	GTE_WriteCR(reg, value);
}

static void pgxp_cop2_ctc(struct lightrec_state *state, u32 op, u8 reg, u32 value)
{
	GTE_WriteCR(reg, value);
	PGXP_GTE_CTC2(op, value, value);
}

static bool cp2_ops[0x40] = {0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,
                             1,1,1,1,1,0,1,0,0,0,0,1,1,0,1,0,
                             1,0,0,0,0,0,0,0,1,1,1,0,0,1,1,0,
                             1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1};

static void cop_op(struct lightrec_state *state, u32 func)
{
   osd_message(3, RETRO_LOG_WARN,
         RETRO_MESSAGE_TARGET_LOG, RETRO_MESSAGE_TYPE_NOTIFICATION_ALT,
         "Access to invalid co-processor 0");
}

static void cop2_op(struct lightrec_state *state, u32 func)
{
   if (MDFN_UNLIKELY(!cp2_ops[func & 0x3f]))
   {
      osd_message(3, RETRO_LOG_WARN,
            RETRO_MESSAGE_TARGET_LOG, RETRO_MESSAGE_TYPE_NOTIFICATION_ALT,
            "Invalid CP2 function %u\n", func);
   }
   else
   {
      /* Honour the GTE op cycle latency that the interpreter records
       * via gte_ts_done.  GTE_Instruction returns (cycles - 1); the
       * interpreter tracks completion as
       *   gte_ts_done = timestamp + GTE_Instruction(func);
       * (cpu.c BEGIN_OPF(COP2)/0x10..0x1F).  Lightrec previously
       * dropped the return value, so any subsequent MFC2/CFC2 issued
       * by recompiled code returned the GTE result without the proper
       * stall, while the interpreter blocked on the latency. */
      pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);
      pscpu_timestamp_t latency   = GTE_Instruction(func);
      if (timestamp < gte_ts_done)
         timestamp = gte_ts_done;
      gte_ts_done = timestamp + latency;
   }
}

static void reset_target_cycle_count(struct lightrec_state *state, pscpu_timestamp_t timestamp){
	if (timestamp >= next_event_ts)
		lightrec_set_exit_flags(state, LIGHTREC_EXIT_CHECK_INTERRUPT);
}

static void hw_write_byte(struct lightrec_state *state,
		u32 opcode, void *host,	u32 mem, u8 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	PSX_MemWrite8(timestamp, mem, val);

	reset_target_cycle_count(state, timestamp);
}

static void pgxp_nonhw_write_byte(struct lightrec_state *state,
		u32 opcode, void *host,	u32 mem, u8 val)
{
	*(u8 *)host = val;
	PGXP_CPU_SB(opcode, val, mem);

	if (!psx_dynarec_invalidate)
		lightrec_invalidate(state, mem, 1);
}

static void pgxp_hw_write_byte(struct lightrec_state *state,
		u32 opcode, void *host,	u32 mem, u8 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	u32 kmem = kunseg(mem);

	PSX_MemWrite8(timestamp, kmem, val);

	PGXP_CPU_SB(opcode, val, mem);

	reset_target_cycle_count(state, timestamp);
}

static void hw_write_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u16 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	PSX_MemWrite16(timestamp, mem, val);

	reset_target_cycle_count(state, timestamp);
}

static void pgxp_nonhw_write_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u16 val)
{
	*(u16 *)host = HTOLE16(val);
	PGXP_CPU_SH(opcode, val, mem);

	if (!psx_dynarec_invalidate)
		lightrec_invalidate(state, mem, 2);
}

static void pgxp_hw_write_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u16 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	u32 kmem = kunseg(mem);

	PSX_MemWrite16(timestamp, kmem, val);

	PGXP_CPU_SH(opcode, val, mem);

	reset_target_cycle_count(state, timestamp);
}

static void hw_write_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	PSX_MemWrite32(timestamp, mem, val);

	reset_target_cycle_count(state, timestamp);
}

static void pgxp_nonhw_write_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	*(u32 *)host = HTOLE32(val);

	switch (opcode >> 26){
		case 0x2A:
			PGXP_CPU_SWL(opcode, val, mem + (opcode & 0x3));
			break;
		case 0x2B:
			PGXP_CPU_SW(opcode, val, mem);
			break;
		case 0x2E:
			PGXP_CPU_SWR(opcode, val, mem + (opcode & 0x3));
			break;
		case 0x3A:
			PGXP_GTE_SWC2(opcode, val, mem);
			break;
		default:
			break;
	}

	if (!psx_dynarec_invalidate)
		lightrec_invalidate(state, mem, 4);
}

static void pgxp_hw_write_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	u32 kmem = kunseg(mem);

	PSX_MemWrite32(timestamp, kmem, val);

	switch (opcode >> 26){
		case OP_SWL:
			PGXP_CPU_SWL(opcode, val, mem + (opcode & 0x3));
			break;
		case OP_SW:
			PGXP_CPU_SW(opcode, val, mem);
			break;
		case OP_SWR:
			PGXP_CPU_SWR(opcode, val, mem + (opcode & 0x3));
			break;
		case OP_SWC2:
			PGXP_GTE_SWC2(opcode, val, mem);
			break;
		default:
			break;
	}

	reset_target_cycle_count(state, timestamp);
}

static u8 hw_read_byte(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u8 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	val = PSX_MemRead8(&timestamp, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

static u8 pgxp_nonhw_read_byte(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u8 val = *(u8 *)host;

	if((opcode >> 26) == OP_LB)
		PGXP_CPU_LB(opcode, val, mem);
	else
		PGXP_CPU_LBU(opcode, val, mem);

	return val;
}

static u8 pgxp_hw_read_byte(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u8 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	u32 kmem = kunseg(mem);

	val = PSX_MemRead8(&timestamp, kmem);

	if((opcode >> 26) == OP_LB)
		PGXP_CPU_LB(opcode, val, mem);
	else
		PGXP_CPU_LBU(opcode, val, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

static u16 hw_read_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u16 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	val = PSX_MemRead16(&timestamp, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

static u16 pgxp_nonhw_read_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u16 val = LE16TOH(*(u16 *)host);

	if((opcode >> 26) == OP_LH)
		PGXP_CPU_LH(opcode, val, mem);
	else
		PGXP_CPU_LHU(opcode, val, mem);

	return val;
}

static u16 pgxp_hw_read_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u16 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	u32 kmem = kunseg(mem);

	val = PSX_MemRead16(&timestamp, kmem);

	if((opcode >> 26) == OP_LH)
		PGXP_CPU_LH(opcode, val, mem);
	else
		PGXP_CPU_LHU(opcode, val, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

static u32 hw_read_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u32 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	val = PSX_MemRead32(&timestamp, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

static u32 pgxp_nonhw_read_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u32 val = LE32TOH(*(u32 *)host);

	switch (opcode >> 26){
		case OP_LWL:
			//TODO: OR with masked register
			PGXP_CPU_LWL(opcode, val << (24-(opcode & 0x3)*8), mem + (opcode & 0x3));
			break;
		case OP_LW:
			PGXP_CPU_LW(opcode, val, mem);
			break;
		case OP_LWR:
			//TODO: OR with masked register
			PGXP_CPU_LWR(opcode, val >> ((opcode & 0x3)*8), mem + (opcode & 0x3));
			break;
		case OP_LWC2:
			PGXP_GTE_LWC2(opcode, val, mem);
			break;
		default:
			break;
	}

	return val;
}

static u32 pgxp_hw_read_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u32 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state);

	u32 kmem = kunseg(mem);

	val = PSX_MemRead32(&timestamp, kmem);

	switch (opcode >> 26){
		case OP_LWL:
			//TODO: OR with masked register
			PGXP_CPU_LWL(opcode, val << (24-(opcode & 0x3)*8), mem + (opcode & 0x3));
			break;
		case OP_LW:
			PGXP_CPU_LW(opcode, val, mem);
			break;
		case OP_LWR:
			//TODO: OR with masked register
			PGXP_CPU_LWR(opcode, val >> ((opcode & 0x3)*8), mem + (opcode & 0x3));
			break;
		case OP_LWC2:
			PGXP_GTE_LWC2(opcode, val, mem);
			break;
		default:
			break;
	}

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

static struct lightrec_mem_map_ops pgxp_nonhw_regs_ops = {
	.sb = pgxp_nonhw_write_byte,
	.sh = pgxp_nonhw_write_half,
	.sw = pgxp_nonhw_write_word,
	.lb = pgxp_nonhw_read_byte,
	.lh = pgxp_nonhw_read_half,
	.lw = pgxp_nonhw_read_word,
};

static struct lightrec_mem_map_ops pgxp_hw_regs_ops = {
	.sb = pgxp_hw_write_byte,
	.sh = pgxp_hw_write_half,
	.sw = pgxp_hw_write_word,
	.lb = pgxp_hw_read_byte,
	.lh = pgxp_hw_read_half,
	.lw = pgxp_hw_read_word,
};

static struct lightrec_mem_map_ops hw_regs_ops = {
	.sb = hw_write_byte,
	.sh = hw_write_half,
	.sw = hw_write_word,
	.lb = hw_read_byte,
	.lh = hw_read_half,
	.lw = hw_read_word,
};

static u32 cache_ctrl_read_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LW(opcode, BIU, mem);

	return BIU;
}

static void cache_ctrl_write_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	BIU = val;

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SW(opcode, BIU, mem);
}

static struct lightrec_mem_map_ops cache_ctrl_ops = {
	.sb = NULL,
	.sh = NULL,
	.sw = cache_ctrl_write_word,
	.lb = NULL,
	.lh = NULL,
	.lw = cache_ctrl_read_word,
};

static struct lightrec_mem_map lightrec_map[] = {
	[PSX_MAP_KERNEL_USER_RAM] = {
		/* Kernel and user memory */
		.pc = 0x00000000,
		.length = 0x200000,
	},
	[PSX_MAP_BIOS] = {
		/* BIOS */
		.pc = 0x1fc00000,
		.length = 0x80000,
	},
	[PSX_MAP_SCRATCH_PAD] = {
		/* Scratch pad */
		.pc = 0x1f800000,
		.length = 0x400,
	},
	[PSX_MAP_PARALLEL_PORT] = {
		/* Parallel port */
		.pc = 0x1f000000,
		.length = 0x800000,
	},
	[PSX_MAP_HW_REGISTERS] = {
		/* Hardware registers */
		.pc = 0x1f801000,
		.length = 0x2000,
	},
	[PSX_MAP_CACHE_CONTROL] = {
		/* Cache control */
		.pc = 0x5ffe0130,
		.length = 4,
		.address = NULL,
		.ops = &cache_ctrl_ops,
	},

	/* Mirrors of the kernel/user memory */
	[PSX_MAP_MIRROR1] = {
		.pc = 0x00200000,
		.length = 0x200000,
		.address = NULL,
		.ops = NULL,
		.mirror_of = &lightrec_map[PSX_MAP_KERNEL_USER_RAM],
	},
	[PSX_MAP_MIRROR2] = {
		.pc = 0x00400000,
		.length = 0x200000,
		.address = NULL,
		.ops = NULL,
		.mirror_of = &lightrec_map[PSX_MAP_KERNEL_USER_RAM],
	},
	[PSX_MAP_MIRROR3] = {
		.pc = 0x00600000,
		.length = 0x200000,
		.address = NULL,
		.ops = NULL,
		.mirror_of = &lightrec_map[PSX_MAP_KERNEL_USER_RAM],
	},
};

static struct lightrec_ops ops = {
	.cop0_ops = {
		.mfc = cop_mfc,
		.cfc = cop_cfc,
		.mtc = cop_mtc,
		.ctc = cop_ctc,
		.op = cop_op,
	},
	.cop2_ops = {
		.mfc = cop2_mfc,
		.cfc = cop2_cfc,
		.mtc = cop2_mtc,
		.ctc = cop2_ctc,
		.op = cop2_op,
	},
};

static struct lightrec_ops pgxp_ops = {
	.cop0_ops = {
		.mfc = cop_mfc,
		.cfc = cop_cfc,
		.mtc = cop_mtc,
		.ctc = cop_ctc,
		.op = cop_op,
	},
	.cop2_ops = {
		.mfc = pgxp_cop2_mfc,
		.cfc = pgxp_cop2_cfc,
		.mtc = pgxp_cop2_mtc,
		.ctc = pgxp_cop2_ctc,
		.op = cop2_op,
	},
};

static int lightrec_plugin_init(PS_CPU *self)
{
   struct lightrec_ops *cop_ops;
   uint8_t *psxM = (uint8_t *) MainRAM->data8;
   uint8_t *psxR = (uint8_t *) BIOSROM->data8;
   uint8_t *psxH = (uint8_t *) ScratchRAM->data8;
   uint8_t *psxP = (uint8_t *) PSX_LoadExpansion1();

   (void)self;

   if (lightrec_state)
      lightrec_destroy(lightrec_state);
   else
   {
      log_cb(RETRO_LOG_INFO, "Lightrec map addresses: M=0x%lx, P=0x%lx, R=0x%lx, H=0x%lx\n",
            (uintptr_t) psxM,
            (uintptr_t) psxP,
            (uintptr_t) psxR,
            (uintptr_t) psxH);
   }

   lightrec_map[PSX_MAP_KERNEL_USER_RAM].address = psxM;

   if (psx_mmap == 4)
   {
      lightrec_map[PSX_MAP_MIRROR1].address = psxM + 0x200000;
      lightrec_map[PSX_MAP_MIRROR2].address = psxM + 0x400000;
      lightrec_map[PSX_MAP_MIRROR3].address = psxM + 0x600000;
   }

   lightrec_map[PSX_MAP_BIOS].address          = psxR;
   lightrec_map[PSX_MAP_SCRATCH_PAD].address   = psxH;
   lightrec_map[PSX_MAP_PARALLEL_PORT].address = psxP;

   if (PGXP_GetModes() & (PGXP_MODE_MEMORY | PGXP_MODE_GTE))
   {
      lightrec_map[PSX_MAP_HW_REGISTERS].ops    = &pgxp_hw_regs_ops;
      lightrec_map[PSX_MAP_KERNEL_USER_RAM].ops = &pgxp_nonhw_regs_ops;
      lightrec_map[PSX_MAP_BIOS].ops            = &pgxp_nonhw_regs_ops;
      lightrec_map[PSX_MAP_SCRATCH_PAD].ops     = &pgxp_nonhw_regs_ops;

      cop_ops = &pgxp_ops;
   }
   else
   {
      lightrec_map[PSX_MAP_HW_REGISTERS].ops    = &hw_regs_ops;
      lightrec_map[PSX_MAP_KERNEL_USER_RAM].ops = NULL;
      lightrec_map[PSX_MAP_BIOS].ops            = NULL;
      lightrec_map[PSX_MAP_SCRATCH_PAD].ops     = NULL;

      cop_ops = &ops;
   }

   lightrec_state = lightrec_init(name,
         lightrec_map, ARRAY_SIZE(lightrec_map), cop_ops);

   lightrec_set_invalidate_mode(lightrec_state, psx_dynarec_invalidate);

   return 0;
}

static int32_t lightrec_plugin_execute(PS_CPU *self, int32_t timestamp)
{
   uint32_t PC;
   uint32_t new_PC;
   uint32_t new_PC_mask;
   uint32_t LDWhich;
   uint32_t LDValue;
   u32      flags;

   (void)self;
   (void)new_PC_mask;

   BACKING_TO_ACTIVE;

   do
   {
#ifdef LIGHTREC_DEBUG
      u32 oldpc = PC;
#endif
      /* GPR_full is laid out as [GPR[0..31], LO, HI, LD_Dummy].
       * lightrec's u32 regs[34] expects [r0..r31, LO, HI] in that
       * exact order, so the first 34 entries of GPR_full are a
       * direct match - no scratch buffer or memcpy required. The
       * LD_Dummy slot at [34] is past lightrec's view and stays
       * intact across the call. Saves 256 bytes of memcpy traffic
       * per iteration of this hot loop. */
      lightrec_restore_registers(lightrec_state, s_cpu.GPR_full);
      lightrec_reset_cycle_count(lightrec_state, timestamp);

      if (next_interpreter > 0 || psx_dynarec == DYNAREC_RUN_INTERPRETER)
         PC = lightrec_run_interpreter(lightrec_state, PC);
      else if (psx_dynarec == DYNAREC_EXECUTE)
         PC = lightrec_execute(lightrec_state, PC, next_event_ts);
      else if (psx_dynarec == DYNAREC_EXECUTE_ONE)
         PC = lightrec_execute_one(lightrec_state, PC);

      timestamp = lightrec_current_cycle_count(lightrec_state);

      lightrec_dump_registers(lightrec_state, s_cpu.GPR_full);

      flags = lightrec_exit_flags(lightrec_state);

      if (flags & LIGHTREC_EXIT_SEGFAULT)
      {
         log_cb(RETRO_LOG_ERROR, "Exiting at cycle 0x%08x\n", timestamp);
         exit(1);
      }

      if (flags & LIGHTREC_EXIT_SYSCALL)
         PC = CPU_Exception(EXCEPTION_SYSCALL, PC, PC, 0);

#ifdef LIGHTREC_DEBUG
      if (timestamp >= lightrec_begin_cycles && PC != oldpc)
         print_for_big_ass_debugger(timestamp, PC);
#endif
      if ((CP0.SR & CP0.CAUSE & 0xFF00) && (CP0.SR & 1))
      {
         /* Handle software interrupts */
         PC = CPU_Exception(EXCEPTION_INT, PC, PC, 0);
      }
   } while (MDFN_LIKELY(PSX_EventHandler(timestamp)));

   ACTIVE_TO_BACKING;

   return timestamp;
}

void CPU_LightrecClear_method(PS_CPU *self, uint32_t addr, uint32_t size)
{
   (void)self;
   if (lightrec_state)  /* size * 4: uses DMA units */
      lightrec_invalidate(lightrec_state, addr, size * 4);
}

static void lightrec_plugin_shutdown(void)
{
   lightrec_destroy(lightrec_state);
}

#endif
