#ifndef __MDFN_PSX_PSX_H
#define __MDFN_PSX_PSX_H

#include "../mednafen.h"
#include "../masmem.h"

#ifdef HAVE_CDROM_NEW
#include "../cdrom-new/cdromif.h"
#else
#include "../cdrom/cdromif.h"
#endif
#include "../general.h"
#include "../FileStream.h"

// Comment out these 2 defines for extra speeeeed.
#define PSX_DBGPRINT_ENABLE    1
#define PSX_EVENT_SYSTEM_CHECKS 1

// It's highly unlikely the user will want these if they're intentionally compiling without the debugger.
#ifndef WANT_DEBUGGER
#undef PSX_DBGPRINT_ENABLE
#undef PSX_EVENT_SYSTEM_CHECKS
#endif

#define PSX_DBG_ERROR      0  // Emulator-level error.
#define PSX_DBG_WARNING    1  // Warning about game doing questionable things/hitting stuff that might not be emulated correctly.
#define PSX_DBG_BIOS_PRINT 2  // BIOS printf/putchar output.
#define PSX_DBG_SPARSE     3  // Sparse(relatively) information debug messages(CDC commands).
#define PSX_DBG_FLOOD      4  // Heavy informational debug messages(GPU commands; TODO).

#if PSX_DBGPRINT_ENABLE
void PSX_DBG(unsigned level, const char *format, ...);

#define PSX_WARNING(format, ...) { PSX_DBG(PSX_DBG_WARNING, format "\n", ## __VA_ARGS__); }
#define PSX_DBGINFO(format, ...) { }
#else
static void PSX_DBG(unsigned level, const char* format, ...) { }
static void PSX_WARNING(const char* format, ...) { }
static void PSX_DBGINFO(const char* format, ...) { }
#endif

typedef int32_t pscpu_timestamp_t;

bool MDFN_FASTCALL PSX_EventHandler(const int32_t timestamp);

void MDFN_FASTCALL PSX_MemWrite8(int32_t timestamp, uint32_t A, uint32_t V);
void MDFN_FASTCALL PSX_MemWrite16(int32_t timestamp, uint32_t A, uint32_t V);
void MDFN_FASTCALL PSX_MemWrite24(int32_t timestamp, uint32_t A, uint32_t V);
void MDFN_FASTCALL PSX_MemWrite32(int32_t timestamp, uint32_t A, uint32_t V);

uint8_t MDFN_FASTCALL PSX_MemRead8(int32_t &timestamp, uint32_t A);
uint16_t MDFN_FASTCALL PSX_MemRead16(int32_t &timestamp, uint32_t A);
uint32_t MDFN_FASTCALL PSX_MemRead24(int32_t &timestamp, uint32_t A);
uint32_t MDFN_FASTCALL PSX_MemRead32(int32_t &timestamp, uint32_t A);

uint8_t PSX_MemPeek8(uint32_t A);
uint16_t PSX_MemPeek16(uint32_t A);
uint32_t PSX_MemPeek32(uint32_t A);

// Should write to WO-locations if possible
void PSX_MemPoke8(uint32_t A, uint8_t V);
void PSX_MemPoke16(uint32_t A, uint16_t V);
void PSX_MemPoke32(uint32_t A, uint32_t V);

void PSX_RequestMLExit(void);
void ForceEventUpdates(const int32_t timestamp);

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

// PSX_GPULineHook modified to take surface pitch (in pixels) and upscale factor for software renderer internal upscaling
void PSX_GPULineHook(const int32_t timestamp, const int32_t line_timestamp, bool vsync, uint32_t *pixels, const MDFN_PixelFormat* const format, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor);

uint32_t PSX_GetRandU32(uint32_t mina, uint32_t maxa);

#include "dis.h"
#include "cpu.h"
#include "irq.h"
#include "gpu.h"
#include "dma.h"
#include "debug.h"

class PS_CDC;
class PS_SPU;

extern PS_CPU *PSX_CPU;
extern PS_CDC *PSX_CDC;
extern PS_SPU *PSX_SPU;
extern MultiAccessSizeMem<512 * 1024, uint32, false> *BIOSROM;
extern MultiAccessSizeMem<2048 * 1024, uint32_t, false> *MainRAM;
extern MultiAccessSizeMem<1024, uint32_t, false> *ScratchRAM;

#ifdef HAVE_LIGHTREC
enum DYNAREC {DYNAREC_DISABLED, DYNAREC_EXECUTE, DYNAREC_EXECUTE_ONE, DYNAREC_RUN_INTERPRETER};
extern enum DYNAREC psx_dynarec;
#endif

#define OVERCLOCK_SHIFT 8
extern int32_t psx_overclock_factor;

static INLINE void overclock_device_to_cpu(int32_t &clock) {
   if (psx_overclock_factor) {
      int64_t n = clock;

      n = (n * psx_overclock_factor) + (1 << (OVERCLOCK_SHIFT)) - 1;

      n >>= OVERCLOCK_SHIFT;

      clock = n;
   }
}

static INLINE void overclock_cpu_to_device(int32_t &clock) {
   if (psx_overclock_factor) {
      int64_t n = clock;

      n = (n << OVERCLOCK_SHIFT) + (psx_overclock_factor - 1);

      n /= psx_overclock_factor;

      clock = n;
   }
}

extern unsigned psx_gpu_overclock_shift;

#endif
