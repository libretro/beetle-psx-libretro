#ifndef __MDFN_PSX_PSX_MEM_H
#define __MDFN_PSX_PSX_MEM_H

#include <stdint.h>

/*
 * C-linkage accessors for MainRAM. The underlying storage is a
 * MultiAccessSizeMem<2048 * 1024> singleton declared in libretro.cpp,
 * and the C++ Read/Write member functions on MultiAccessSizeMem are
 * the public access path. C TUs (dma.c, ...) can't include masmem.h
 * because the type is a class template, so this header exposes the
 * accesses dma needs as plain C functions.
 *
 * Implementations are in libretro.cpp at file scope where MainRAM is
 * defined, with `extern "C"` linkage; they're one-line forwarders
 * to the existing member functions and inline away under -O2 / LTO.
 *
 * Address parameters are pre-masked by the caller (typically with
 * `& 0x1FFFFC` to enforce 32-bit alignment within the 2 MB MainRAM
 * range) - same convention as the C++ MainRAM->ReadU32 / WriteU32
 * call sites.
 */

#ifdef __cplusplus
extern "C" {
#endif

uint32_t MainRAM_ReadU32(uint32_t address);
void     MainRAM_WriteU32(uint32_t address, uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
