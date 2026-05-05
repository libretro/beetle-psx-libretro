#ifndef __MDFN_PSX_CDC_C_H
#define __MDFN_PSX_CDC_C_H

#include <stdint.h>

/*
 * C-linkage shim for the PS_CDC member function the DMA controller
 * needs to drive CD-DA / data-track DMA reads. Same pattern as
 * cpu_c.h: a free-function wrapper with `extern "C"` linkage that
 * forwards through the global PSX_CDC instance pointer, defined in
 * cdc.cpp where the class is in scope. C consumers (dma.c) include
 * this header instead of cdc.h, which can't be parsed by a C
 * compiler because it declares `class PS_CDC`.
 *
 * Unlike the cpu_c.h shims, this one does touch instance state
 * (PS_CDC::DMABuffer) - the forwarder isn't elided to a no-op the
 * way CPU_AssertIRQ et al. are. It still inlines to a single
 * indirect call under -O2 / LTO; the instance pointer dereference
 * was already there in the original PSX_CDC->DMARead() form.
 */

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CDC_DMARead(void);

#ifdef __cplusplus
}
#endif

#endif
