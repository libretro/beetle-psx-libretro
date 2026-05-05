#ifndef __MDFN_PSX_CDC_C_H
#define __MDFN_PSX_CDC_C_H

#include <stdint.h>

/*
 * C-linkage shims for PS_CDC member functions consumed by the
 * (now-C) DMA controller and SPU. Same pattern as cpu_c.h: free-
 * function wrappers with `extern "C"` linkage that forward through
 * the global PSX_CDC instance pointer, defined in cdc.cpp where
 * the class is in scope. C consumers (dma.c, spu.c) include this
 * header instead of cdc.h, which can't be parsed by a C compiler
 * because it declares `class PS_CDC`.
 *
 * Unlike the cpu_c.h shims, these touch instance state - the
 * forwarders aren't elided to no-ops the way CPU_AssertIRQ et al.
 * are. They still inline to a single indirect call under -O2 /
 * LTO; the instance pointer dereference was already there in the
 * original PSX_CDC->Member() form.
 *
 * CDC_GetCDAudioSample wraps the historical
 *   const unsigned freq = (PSX_CDC->AudioBuffer.ReadPos <
 *                          PSX_CDC->AudioBuffer.Size)
 *                       ? PSX_CDC->AudioBuffer.Freq : 0;
 *   samples[0] = samples[1] = 0;
 *   if (freq) PSX_CDC->GetCDAudio(samples, freq);
 * idiom that was previously coded out at the SPU's call site, so
 * spu.c doesn't need to know about CD_Audio_Buffer's layout.
 * `samples` is always written; if there's no audio playing or the
 * read pointer is past the end of the decoded buffer, both
 * channels are zeroed.
 */

#ifdef __cplusplus
extern "C" {
#endif

uint32_t CDC_DMARead(void);
void     CDC_GetCDAudioSample(int32_t samples[2]);

#ifdef __cplusplus
}
#endif

#endif
