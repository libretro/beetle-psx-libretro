#ifndef __MDFN_PSX_SPU_C_H
#define __MDFN_PSX_SPU_C_H

#include <stdint.h>

#include "../state.h"

/*
 * C-linkage external API for the SPU module. All declarations here
 * have extern "C" linkage so both C consumers (dma.c, spu.c
 * itself - this header is its own header too) and C++ consumers
 * (libretro.cpp, cdc.cpp) see the same calling convention - the
 * implementations in spu.c emit unmangled C symbols that match.
 *
 * Same pattern as cpu_c.h / cdc_c.h / gpu_c.h: a thin C-friendly
 * surface that exposes just what cross-module consumers need.
 *
 * SPU_Init zeroes all module state (file-scope statics in spu.c).
 * SPU_Kill is a no-op kept as a stable lifecycle hook so
 * libretro.cpp's teardown sequence doesn't have to special-case
 * the SPU among the other PSX modules. Both replace the
 * historical
 *     PSX_SPU = new PS_SPU();
 *     delete PSX_SPU; PSX_SPU = NULL;
 * idiom that used to live in libretro.cpp - PS_SPU is gone, the
 * singleton pointer is gone, no heap allocation, no leak surface.
 */

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

#endif
