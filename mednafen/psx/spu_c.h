#ifndef __MDFN_PSX_SPU_C_H
#define __MDFN_PSX_SPU_C_H

#include <stdint.h>

/*
 * C-linkage shims for the PS_SPU member functions the DMA controller
 * needs to drive SPU-RAM transfers. Same pattern as cpu_c.h /
 * cdc_c.h: free-function wrappers with `extern "C"` linkage,
 * defined in spu.cpp where the class is in scope, that forward
 * through the global PSX_SPU instance pointer. C consumers (dma.c)
 * include this header instead of spu.h, which can't be parsed by a
 * C compiler because it declares `class PS_SPU`.
 *
 * Both shims touch instance state (RWAddr, SPURAM[]) - the
 * forwarders are a single indirect call each, the same indirection
 * the PSX_SPU->WriteDMA() / PSX_SPU->ReadDMA() callsites already
 * carried.
 */

#ifdef __cplusplus
extern "C" {
#endif

void SPU_WriteDMA(uint32_t V);
uint32_t SPU_ReadDMA(void);

#ifdef __cplusplus
}
#endif

#endif
