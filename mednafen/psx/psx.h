#ifndef __MDFN_PSX_PSX_H
#define __MDFN_PSX_PSX_H

#include <stdint.h>

#include "../masmem.h"
#include "../mednafen-types.h"
#include "../video/surface.h"

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

#include "psx_events.h"

// PSX_GPULineHook modified to take surface pitch (in pixels) and upscale factor for software renderer internal upscaling
void PSX_GPULineHook(const int32_t timestamp, const int32_t line_timestamp, bool vsync, uint32_t *pixels, const unsigned width, const unsigned pix_clock_offset, const unsigned pix_clock, const unsigned pix_clock_divider, const unsigned surf_pitchinpix, const unsigned upscale_factor);

uint32_t PSX_GetRandU32(uint32_t mina, uint32_t maxa);

#include "cpu.h"
#include "irq.h"
#include "gpu.h"
#include "dma.h"
#include "cdc.h"

extern PS_CPU *PSX_CPU;
extern MultiAccessSizeMem<512 * 1024> *BIOSROM;
extern MultiAccessSizeMem<2048 * 1024> *MainRAM;
extern MultiAccessSizeMem<1024> *ScratchRAM;

#ifdef HAVE_LIGHTREC
enum DYNAREC {DYNAREC_DISABLED, DYNAREC_EXECUTE, DYNAREC_EXECUTE_ONE, DYNAREC_RUN_INTERPRETER};
extern enum DYNAREC psx_dynarec;
#endif

extern unsigned psx_gpu_overclock_shift;

extern uint8_t analog_combo[2];
extern uint8_t analog_combo_hold;

#endif
