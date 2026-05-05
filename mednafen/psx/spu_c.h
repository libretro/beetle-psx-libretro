#ifndef __MDFN_PSX_SPU_C_H
#define __MDFN_PSX_SPU_C_H

#include <stdint.h>

#include "../state.h"

/*
 * C-linkage external API for the SPU module. All declarations here
 * have extern "C" linkage so both C consumers (dma.c, eventually
 * spu.c itself) and C++ consumers (libretro.cpp, cdc.cpp) see the
 * same calling convention - which means the shim implementations
 * in spu.cpp must be `extern "C"` to match.
 *
 * Same pattern as cpu_c.h / cdc_c.h / gpu_c.h: a thin C-friendly
 * surface that exposes just what cross-module consumers need, so
 * they don't have to include spu.h (which declares `class PS_SPU`
 * and is therefore C++-only).
 *
 * SPU_Init / SPU_Kill replace the historical
 *   PSX_SPU = new PS_SPU()
 *   delete PSX_SPU; PSX_SPU = NULL
 * idiom from libretro.cpp; the PSX_SPU singleton becomes private
 * to spu.cpp (will be a file-scope static once the module is
 * converted to C in a follow-up commit).
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Audio output buffer. SPU samples are mixed into this buffer at
 * the SPU's internal 44.1 kHz rate; the libretro frontend drains
 * it once per video frame via audio_batch_cb. Sized for two
 * worst-case frames (2*882 samples in PAL) plus headroom for
 * resampler leftovers and jitter; 4096 because powers of two are
 * convenient.
 */
extern uint32_t IntermediateBufferPos;
extern int16_t  IntermediateBuffer[4096][2];

void     SPU_Init(void);
void     SPU_Kill(void);

void     SPU_Power(void);
void     SPU_Write(int32_t timestamp, uint32_t A, uint16_t V);
uint16_t SPU_Read(int32_t timestamp, uint32_t A);

void     SPU_WriteDMA(uint32_t V);
uint32_t SPU_ReadDMA(void);

int32_t  SPU_UpdateFromCDC(int32_t clocks);

int      SPU_StateAction(StateMem *sm, int load, int data_only);

#ifdef __cplusplus
}
#endif

#endif
