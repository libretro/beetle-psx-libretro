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

/* Flush any pending subdivision-buffered polygons to the SW
 * rasteriser.  Must be called before any operation that depends on
 * VRAM state being up-to-date (FBRead, display flip, end of frame)
 * or that changes drawing state in a way that would make the
 * buffered polygons inconsistent (texpage, CLUT, drawing-area,
 * drawing-offset).  Zero cost when subdivision is disabled. */
struct PS_GPU;
void gpu_polygon_subdiv_flush(struct PS_GPU *gpu);

/* Variant that uses the singleton GPU instance.  For callers that
 * don't have a PS_GPU pointer handy (rsx_intf, libretro front-end);
 * internally just calls gpu_polygon_subdiv_flush(&GPU). */
void gpu_polygon_subdiv_flush_global(void);

/* Clear the subdivision system's per-vertex normal cache.  Must be
 * called once per frame after the end-of-frame flush.  See
 * gpu_subdiv.h for full rationale.  Forwarded here so the rsx_intf
 * end-of-frame hook can call it without dragging in gpu_subdiv.h. */
void tt_subdiv_frame_end(void);

#ifdef __cplusplus
}
#endif

#endif
