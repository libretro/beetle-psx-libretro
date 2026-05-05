#ifndef __MDFN_PSX_CPU_C_H
#define __MDFN_PSX_CPU_C_H

#include <boolean.h>

/*
 * C-linkage shims for PS_CPU member functions that don't actually
 * touch instance state - they only modify static class members
 * (CP0.CAUSE, RecalcIPCache) and so calling them through the global
 * PSX_CPU pointer is purely a syntax mechanism. Exposing them as
 * plain C functions lets irq.cpp -> irq.c (and similar) call into
 * the CPU module without including cpu.h, which a C compiler can't
 * parse because it declares `class PS_CPU`.
 *
 * Each shim is a one-liner forwarder defined in cpu.cpp; the
 * optimizer inlines them away under -O2.
 *
 * This header currently exposes only what irq.c needs. Add further
 * shims (CPU_SetHalt, CPU_LightrecClear, etc.) when the
 * corresponding C consumers land - dma.cpp -> dma.c is a likely
 * future user but blocked on additional psx.h dependencies
 * (PSX_CDC, PSX_SPU, MainRAM) that need their own C-linkage paths
 * before that rename is feasible.
 */

#ifdef __cplusplus
extern "C" {
#endif

void CPU_AssertIRQ(unsigned which, bool asserted);

#ifdef __cplusplus
}
#endif

#endif
