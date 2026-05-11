#ifndef __MDFN_PSX_PSX_EVENTS_H
#define __MDFN_PSX_PSX_EVENTS_H

#include <stdint.h>

#include <retro_inline.h>

/*
 * PSX event-scheduler API. Extracted from psx.h so that converted-C
 * modules (timer.c and similar) can use the event API without
 * pulling in psx.h, which transitively includes cpu.h's
 * `class PS_CPU` declaration that a C compiler can't parse.
 *
 * psx.h #includes this header so existing C++ callers see exactly
 * the same declarations - there's no duplication, just a smaller
 * unit that's safe for both languages.
 */

enum
{
   PSX_EVENT__SYNFIRST = 0,
   PSX_EVENT_GPU,
   PSX_EVENT_CDC,
   //PSX_EVENT_SPU,
   PSX_EVENT_TIMER,
   PSX_EVENT_DMA,
   PSX_EVENT_FIO,
   PSX_EVENT__SYNLAST,
   PSX_EVENT__COUNT
};

#define PSX_EVENT_MAXTS             0x20000000

void PSX_SetEventNT(const int type, const int32_t next_timestamp);
void PSX_SetDMACycleSteal(unsigned stealage);

uint32_t PSX_GetRandU32(uint32_t mina, uint32_t maxa);

#define OVERCLOCK_SHIFT 8
extern int32_t psx_overclock_factor;

static INLINE void overclock_device_to_cpu(int32_t *clock) {
   if (psx_overclock_factor) {
      int64_t n = *clock;

      n = (n * psx_overclock_factor) + (1 << (OVERCLOCK_SHIFT)) - 1;

      n >>= OVERCLOCK_SHIFT;

      *clock = n;
   }
}

static INLINE void overclock_cpu_to_device(int32_t *clock) {
   if (psx_overclock_factor) {
      int64_t n = *clock;

      n = (n << OVERCLOCK_SHIFT) + (psx_overclock_factor - 1);

      n /= psx_overclock_factor;

      *clock = n;
   }
}

#endif
