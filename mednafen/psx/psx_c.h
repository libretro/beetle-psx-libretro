#ifndef __MDFN_PSX_PSX_C_H
#define __MDFN_PSX_PSX_C_H

#include <stdint.h>
#include <boolean.h>

#include "psx_events.h"

/*
 * C-linkage forward declarations for the subset of the PSX public
 * API that converted-C modules (gpu.c, etc.) need. The full psx.h
 * includes cpu.h which declares `class PS_CPU` and uses C++
 * reference parameters in PSX_MemRead* signatures, neither of
 * which a C compiler can parse.
 *
 * Same pattern as cpu_c.h / cdc_c.h / gpu_c.h / spu_c.h: a thin
 * C-friendly surface that exposes just what cross-module C
 * consumers need, so they don't have to drag in the whole
 * module header.
 *
 * PSX_SetEventNT and PSX_EVENT_TIMER come in via psx_events.h
 * (already C-clean and #included above); the rest are
 * one-offs needed by gpu.c.
 *
 * Header is C-only; the C++ build never includes it (it includes
 * psx.h directly), so no extern "C" wrapping is needed.
 */

void PSX_RequestMLExit(void);

extern unsigned psx_gpu_overclock_shift;

/* PSX_GPULineHook: per-scanline callback invoked from inside the
 * GPU update loop into the libretro front-end. The full signature
 * matches psx.h's declaration verbatim - same param order, same
 * types. */
void PSX_GPULineHook(const int32_t timestamp,
                     const int32_t line_timestamp,
                     bool          vsync,
                     uint32_t     *pixels,
                     const unsigned width,
                     const unsigned pix_clock_offset,
                     const unsigned pix_clock,
                     const unsigned pix_clock_divider,
                     const unsigned surf_pitchinpix,
                     const unsigned upscale_factor);

#endif
