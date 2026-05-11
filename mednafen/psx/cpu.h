/******************************************************************************/
/* Mednafen Sony PS1 Emulation Module                                         */
/******************************************************************************/
/* cpu.h:
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

#ifndef __MDFN_PSX_CPU_H
#define __MDFN_PSX_CPU_H

#include <stdint.h>
#include <boolean.h>

#include "../mednafen-types.h"
#include "psx_events.h"
#include "../state.h"

#include "gte.h"
#ifdef HAVE_LIGHTREC
#include <lightrec.h>
#endif

/* FAST_MAP_* enums are in BYTES (8-bit), not in 32-bit units ("words"
 * in MIPS context), but the sizes will always be multiples of 4. */
#define CPU_FAST_MAP_SHIFT 16
#define CPU_FAST_MAP_PSIZE (1U << CPU_FAST_MAP_SHIFT)

/* I-cache line, used by the instruction-cache emulation.
 *
 *   TV  bit 0:    0 = icache enabled ((BIU & 0x800) == 0x800)
 *                 1 = icache disabled (changed in bulk on BIU value
 *                                      changes; preserve elsewhere)
 *   TV  bit 1:    0 = valid, 1 = invalid
 *   TV  bits 4-11: always 0
 *   TV  bits 12-31: tag.
 */
typedef struct
{
   uint32_t TV;
   uint32_t Data;
} CPU_ICache;

/* CP0 register file.  The named-field overlay matches the index
 * order in the Regs[] array, so save-state code can reference
 * either form. */
typedef union
{
   uint32_t Regs[32];
   struct
   {
      uint32_t Unused00;
      uint32_t Unused01;
      uint32_t Unused02;
      uint32_t BPC;        /* RW */
      uint32_t Unused04;
      uint32_t BDA;        /* RW */
      uint32_t TAR;        /* R */
      uint32_t DCIC;       /* RW */
      uint32_t BADA;       /* R */
      uint32_t BDAM;       /* R/W */
      uint32_t Unused0A;
      uint32_t BPCM;       /* R/W */
      uint32_t SR;         /* R/W */
      uint32_t CAUSE;      /* R/W (partial) */
      uint32_t EPC;        /* R */
      uint32_t PRID;       /* R */
   } n;
} CPU_CP0;

/* Per-instance CPU state.  The struct used to be `class PS_CPU`; the
 * conversion to plain C kept the layout but moved former-static class
 * members (next_event_ts, IPCache, BIU, Halted, CP0, cache_buf) to
 * file scope inside cpu.c, and dropped them from the struct.  Only
 * one PS_CPU is ever instantiated, so file-scope statics are
 * semantically identical and fold to BSS. */
typedef struct PS_CPU
{
   /* GPR layout: GPR_full[0..31] = MIPS GPRs, [32]=LO, [33]=HI,
    * [34]=LD_Dummy (the load-delay absorption sink).  Lightrec's
    * u32 regs[34] expects exactly r0..r31, LO, HI - matches the
    * first 34 entries, so &GPR_full[0] can be passed directly to
    * lightrec_restore_registers / _dump_registers with no scratch
    * buffer or memcpy. */
   uint32_t GPR_full[35];

   uint32_t BACKED_PC;
   uint32_t BACKED_new_PC;
   uint8_t  BDBT;

   uint8_t  ReadAbsorb[35]; /* 32 GPRs + LO + HI + LD_Dummy slot. */
   uint8_t  ReadAbsorbWhich;
   uint8_t  ReadFudge;

   uint32_t BACKED_LDWhich;
   uint32_t BACKED_LDValue;
   uint32_t LDAbsorb;

   pscpu_timestamp_t gte_ts_done;
   pscpu_timestamp_t muldiv_ts_done;

   uint32_t addr_mask[8];

   /* I-cache: 1024 lines (8 bytes each = 8 KB) overlaid as a flat
    * uint32 array for bulk savestate. */
   union
   {
      CPU_ICache ICache[1024];
      uint32_t   ICache_Bulk[2048];
   } ic;

   uintptr_t FastMap[1U << (32 - CPU_FAST_MAP_SHIFT)];
   uint8_t   DummyPage[CPU_FAST_MAP_PSIZE];
} PS_CPU;

/* Field shorthands for direct access in cpu.c.  These index the
 * GPR_full array and are equivalent to the named members the C++
 * class had via an anonymous-struct-in-union. */
#define CPU_GPR(self)      ((self)->GPR_full)
#define CPU_LO(self)       ((self)->GPR_full[32])
#define CPU_HI(self)       ((self)->GPR_full[33])
#define CPU_LD_DUMMY(self) ((self)->GPR_full[34])

/* C-callable surface.
 *
 * CPU_New returns the singleton PS_CPU pointer (PSX_CPU itself).
 * CPU_Destroy tears down lightrec state if any.  There is only one
 * PS_CPU instance ever live; the pointer interface is preserved
 * because libretro.cpp still expresses ownership through it.
 *
 * SetEventNT/GetEventNT are inlined; the rest are out-of-line. */
PS_CPU *CPU_New(void);
void    CPU_Destroy(PS_CPU *self);

void              CPU_SetFastMap(PS_CPU *self, void *region_mem, uint32_t region_address, uint32_t region_size);
pscpu_timestamp_t CPU_Run        (PS_CPU *self, pscpu_timestamp_t timestamp_in);
void              CPU_Power      (PS_CPU *self);
void              CPU_AssertIRQ_method(PS_CPU *self, unsigned which, bool asserted);
void              CPU_SetHalt_method  (PS_CPU *self, bool status);
void              CPU_SetBIU     (PS_CPU *self, uint32_t val);
uint32_t          CPU_GetBIU     (PS_CPU *self);
int               CPU_StateAction(PS_CPU *self, StateMem *sm, const unsigned load, const bool data_only);
#ifdef HAVE_LIGHTREC
void              CPU_LightrecClear_method(PS_CPU *self, uint32_t addr, uint32_t size);
#endif

/* SetEventNT / GetEventNT are accessed every memory op in the CPU
 * loop; defined inline so they fold to a load/store on the file-
 * scope cpu_next_event_ts. */
extern pscpu_timestamp_t cpu_next_event_ts;

static INLINE void CPU_SetEventNT(pscpu_timestamp_t next_event_ts_arg)
{
   cpu_next_event_ts = next_event_ts_arg;
}

static INLINE pscpu_timestamp_t CPU_GetEventNT(void)
{
   return cpu_next_event_ts;
}

#endif
