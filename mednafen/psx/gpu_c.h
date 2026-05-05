#ifndef __MDFN_PSX_GPU_C_H
#define __MDFN_PSX_GPU_C_H

#include <stdint.h>
#include <boolean.h>

/*
 * C-linkage forward declarations for the subset of the GPU public
 * API that dma.c needs. The full gpu.h pulls in git.h and
 * video/surface.h which declare C++ classes (MDFN_PixelFormat,
 * MDFN_Surface) that a C compiler can't parse - and dma doesn't
 * need any of that, only these five free functions which were
 * already C-callable but trapped behind the unparseable headers.
 *
 * Same pattern as cpu_c.h / cdc_c.h / spu_c.h: a thin C-friendly
 * surface that exposes just what cross-module C consumers need,
 * so they don't have to include the full module header.
 */

#ifdef __cplusplus
extern "C" {
#endif

void     GPU_WriteDMA(uint32_t V, uint32_t addr);
uint32_t GPU_ReadDMA(void);
bool     GPU_DMACanWrite(void);
int32_t  GPU_Update(const int32_t sys_timestamp);
int32_t  GPU_GetScanlineNum(void);

#ifdef __cplusplus
}
#endif

#endif
