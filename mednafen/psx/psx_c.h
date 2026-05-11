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

/* gpu.c emits a per-scanline callback into the FrontIO subsystem
 * (which dispatches to lightgun / Justifier / Guncon hooks).  That
 * used to go through a one-line PSX_GPULineHook trampoline in
 * libretro.cpp; gpu.c now calls FrontIO_GPULineHook directly to
 * cut out the indirection.
 *
 * FrontIO is opaque from C's perspective - the pointer is forwarded
 * verbatim to the FrontIO C API.  PSX_FIO is owned by libretro.cpp;
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

/* Cheat-table peek/poke surface for mempatcher.c.  The full psx.h
 * decls are C-clean (no `&` references) but they live in a header
 * that drags in cpu.h with `class PS_CPU`, which a C TU can't
 * parse.  Re-declared here with identical signatures. */
uint8_t PSX_MemPeek8(uint32_t A);
void    PSX_MemPoke8(uint32_t A, uint8_t V);

#endif
