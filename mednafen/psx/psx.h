#ifndef __MDFN_PSX_PSX_C_H
#define __MDFN_PSX_PSX_C_H

#include <stdint.h>
#include <boolean.h>

#include "psx_events.h"

#include "cpu.h"
#include "irq.h"
#include "gpu.h"
#include "dma.h"
#include "cdc.h"

#include "../masmem.h"
#include "../mednafen-types.h"
#include "../video/surface.h"

bool MDFN_FASTCALL PSX_EventHandler(const int32_t timestamp);

void MDFN_FASTCALL PSX_MemWrite8 (int32_t timestamp, uint32_t A, uint32_t V);
void MDFN_FASTCALL PSX_MemWrite16(int32_t timestamp, uint32_t A, uint32_t V);
void MDFN_FASTCALL PSX_MemWrite24(int32_t timestamp, uint32_t A, uint32_t V);
void MDFN_FASTCALL PSX_MemWrite32(int32_t timestamp, uint32_t A, uint32_t V);

uint8_t  PSX_MemPeek8 (uint32_t A);
uint16_t PSX_MemPeek16(uint32_t A);
uint32_t PSX_MemPeek32(uint32_t A);

/* Should write to WO-locations if possible */
void PSX_MemPoke8(uint32_t A, uint8_t V);

void PSX_RequestMLExit(void);

void ForceEventUpdates(const int32_t timestamp);

uint32_t PSX_GetRandU32(uint32_t mina, uint32_t maxa);

uint8_t  MDFN_FASTCALL PSX_MemRead8 (int32_t *timestamp, uint32_t A);
uint16_t MDFN_FASTCALL PSX_MemRead16(int32_t *timestamp, uint32_t A);
uint32_t MDFN_FASTCALL PSX_MemRead24(int32_t *timestamp, uint32_t A);
uint32_t MDFN_FASTCALL PSX_MemRead32(int32_t *timestamp, uint32_t A);


extern PS_CPU *PSX_CPU;

extern MultiAccessSizeMem *BIOSROM;
extern MultiAccessSizeMem *MainRAM;
extern MultiAccessSizeMem *ScratchRAM;

extern unsigned psx_gpu_overclock_shift;

extern uint8_t analog_combo[2];
extern uint8_t analog_combo_hold;

/*
 * PSX_SetEventNT and PSX_EVENT_TIMER come in via psx_events.h
 * (already C-clean and #included above); the rest are
 * one-offs needed by gpu.c. */

void PSX_RequestMLExit(void);

extern unsigned psx_gpu_overclock_shift;

/* gpu.c emits a per-scanline callback into the FrontIO subsystem
 * (which dispatches to lightgun / Justifier / Guncon hooks).  That
 * used to go through a one-line PSX_GPULineHook trampoline in
 * libretro.cpp; gpu.c now calls FrontIO_GPULineHook directly to
 * cut out the indirection.
 *
 * PSX_FIO is owned by libretro.c;
 * the GPU update loop only runs with a game loaded, during which
 * PSX_FIO is guaranteed non-NULL. */
struct FrontIO;
extern struct FrontIO *PSX_FIO;

void FrontIO_GPULineHook(struct FrontIO *fio,
                         const int32_t  timestamp,
                         const int32_t  line_timestamp,
                         bool           vsync,
                         uint32_t      *pixels,
                         const unsigned width,
                         const unsigned pix_clock_offset,
                         const unsigned pix_clock,
                         const unsigned pix_clock_divider,
                         const unsigned surf_pitchinpix,
                         const unsigned upscale_factor);

uint8_t PSX_MemPeek8(uint32_t A);
void    PSX_MemPoke8(uint32_t A, uint8_t V);

#endif
