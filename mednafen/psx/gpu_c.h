#ifndef __MDFN_PSX_GPU_C_H
#define __MDFN_PSX_GPU_C_H

#include <stdint.h>
#include <boolean.h>

/*
 * C-linkage forward declarations for the subset of the GPU public
 * API that dma.c and other C consumers need. The full gpu.h pulls
 * in math.h, git.h, and (transitively) video/surface.h - all now
 * plain C - plus a heavy PS_GPU struct definition that dma doesn't
 * need any of.
 *
 * Same pattern as cpu_c.h / cdc_c.h / spu_c.h: a thin C-friendly
 * surface that exposes just what cross-module C consumers need,
 * so they don't have to drag in the whole module header (and the
 * compile-time cost of parsing a 268-line struct full of
 * rasteriser state) just to call GPU_WriteDMA.
 */

#ifdef __cplusplus
extern "C" {
#endif

void     GPU_WriteDMA(uint32_t V, uint32_t addr);
uint32_t GPU_ReadDMA(void);
bool     GPU_DMACanWrite(void);
int32_t  GPU_Update(const int32_t sys_timestamp);
int32_t  GPU_GetScanlineNum(void);

/* Used by rsx_lib_gl.c to access the VRAM contents and
 * to drive state restore points. */
uint16_t *GPU_get_vram(void);
void     GPU_RestoreStateP1(bool load);
void     GPU_RestoreStateP2(bool load);
void     GPU_RestoreStateP3(void);

#ifdef __cplusplus
}
#endif

#endif
