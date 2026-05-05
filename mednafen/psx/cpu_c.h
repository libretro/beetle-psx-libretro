#ifndef __MDFN_PSX_CPU_C_H
#define __MDFN_PSX_CPU_C_H

#include <stdint.h>
#include <boolean.h>

/*
 * C-linkage shims for PS_CPU member functions that don't actually
 * touch instance state - they only modify static class members
 * (CP0.CAUSE, Halted, lightrec_state) and call other static helpers
 * (RecalcIPCache, lightrec_invalidate). Exposing them as plain C
 * functions lets converted .c TUs (irq.c, dma.c, ...) call into the
 * CPU module without including cpu.h, which a C compiler can't parse
 * because it declares `class PS_CPU`.
 *
 * Each shim is a one-liner forwarder defined in cpu.cpp; the
 * optimizer inlines them away under -O2.
 */

#ifdef __cplusplus
extern "C" {
#endif

void CPU_AssertIRQ(unsigned which, bool asserted);
void CPU_SetHalt(bool status);

#ifdef HAVE_LIGHTREC
void CPU_LightrecClear(uint32_t addr, uint32_t size);
#endif

#ifdef __cplusplus
}
#endif

#endif
