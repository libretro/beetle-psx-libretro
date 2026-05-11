/******************************************************************************/
/* Mednafen - Multi-system Emulator                                           */
/******************************************************************************/
/* mednafen-endian.h:
**  Copyright (C) 2006-2017 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
*/

/*
 * Endian helpers for reading/writing little-endian integer values
 * to/from byte buffers.
 *
 * Only the variants that have callers in this code base are kept.
 * Big-endian readers/writers, MDFN_de64lsb, MDFN_en64lsb,
 * MDFN_de24msb, and MDFN_bswap64 had zero external callers and have
 * been removed; if a future caller needs them they are trivially
 * derivable from the existing pattern.
 *
 * Host endianness is decided at preprocess time via MSB_FIRST (set
 * in mednafen-types.h based on __BYTE_ORDER__). On a little-endian
 * host the readers/writers are plain memcpy; on a big-endian host
 * they memcpy + bswap. The choice is a #ifdef so there is no runtime
 * branch.
 *
 * Header is C89-compatible: no templates, no decltype, no
 * static_assert, no std::*. INLINE falls back through retro_inline.h
 * on pre-C99 compilers.
 *
 * The aligned variant (MDFN_de32lsb_aligned) uses
 * __builtin_assume_aligned to give the optimizer an alignment
 * guarantee on the cpu.cpp instruction-fetch hot path. On targets
 * where the builtin is unavailable (MSVC, ancient compilers) it
 * falls back to the unaligned form.
 */

#ifndef __MDFN_ENDIAN_H
#define __MDFN_ENDIAN_H

#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bulk swap helpers (operate on byte buffers, consumed by SPU/SCSP
 * resampler paths). The *_NE_LE / *_NE_BE / Endian_V_NE_* orientation
 * wrappers were unused and have been removed. */
void Endian_A16_Swap(void *src, uint32 nelements);
void Endian_A32_Swap(void *src, uint32 nelements);
void Endian_A64_Swap(void *src, uint32 nelements);

#ifdef __cplusplus
}
#endif

/* Byte-swap primitives. Only used inside #ifdef MSB_FIRST branches
 * below; on a little-endian host both helpers are dead-stripped. */

static INLINE uint16 MDFN_bswap16(uint16 v)
{
#if defined(_MSC_VER)
   return _byteswap_ushort(v);
#else
   return (uint16)((v << 8) | (v >> 8));
#endif
}

static INLINE uint32 MDFN_bswap32(uint32 v)
{
#if defined(_MSC_VER)
   return _byteswap_ulong(v);
#else
   return (v << 24)
        | ((v & 0xFF00U) << 8)
        | ((v >> 8) & 0xFF00U)
        | (v >> 24);
#endif
}

/*
 * Little-endian readers.
 */

static INLINE uint16 MDFN_de16lsb(const void *ptr)
{
   uint16 tmp;
   memcpy(&tmp, ptr, sizeof(tmp));
#ifdef MSB_FIRST
   return MDFN_bswap16(tmp);
#else
   return tmp;
#endif
}

static INLINE uint32 MDFN_de32lsb(const void *ptr)
{
   uint32 tmp;
   memcpy(&tmp, ptr, sizeof(tmp));
#ifdef MSB_FIRST
   return MDFN_bswap32(tmp);
#else
   return tmp;
#endif
}

/* 24-bit reader: byte-shift, no endian dependency. */
static INLINE uint32 MDFN_de24lsb(const void *ptr)
{
   const uint8 *p = (const uint8 *)ptr;
   return ((uint32)p[0] << 0) | ((uint32)p[1] << 8) | ((uint32)p[2] << 16);
}

/*
 * Aligned little-endian 32-bit reader. Used by the cpu.cpp
 * instruction-fetch hot path where the pointer is guaranteed
 * 4-byte aligned. The __builtin_assume_aligned hint helps the
 * optimizer skip the byte-fallback codegen on targets like ARMv5
 * and old MIPS where unaligned access is slow or trapping.
 */

#if defined(__GNUC__) || defined(__clang__)
#define MDFN_BUILTIN_ASSUME_ALIGNED(p, n) __builtin_assume_aligned((p), (n))
#else
#define MDFN_BUILTIN_ASSUME_ALIGNED(p, n) (p)
#endif

static INLINE uint32 MDFN_de32lsb_aligned(const void *ptr)
{
   uint32 tmp;
   memcpy(&tmp, MDFN_BUILTIN_ASSUME_ALIGNED(ptr, 4), sizeof(tmp));
#ifdef MSB_FIRST
   return MDFN_bswap32(tmp);
#else
   return tmp;
#endif
}

/*
 * Native-endian aligned u32 reader. Reads raw memory without any
 * swap regardless of host endianness; used for type-pun reads from
 * memory-mapped regions whose contents are already in host order.
 */

static INLINE uint32 MDFN_densb_u32_aligned(const void *ptr)
{
   uint32 tmp;
   memcpy(&tmp, MDFN_BUILTIN_ASSUME_ALIGNED(ptr, 4), sizeof(tmp));
   return tmp;
}

/*
 * Little-endian writers.
 */

static INLINE void MDFN_en16lsb(void *ptr, uint16 value)
{
#ifdef MSB_FIRST
   uint16 tmp = MDFN_bswap16(value);
#else
   uint16 tmp = value;
#endif
   memcpy(ptr, &tmp, sizeof(tmp));
}

static INLINE void MDFN_en32lsb(void *ptr, uint32 value)
{
#ifdef MSB_FIRST
   uint32 tmp = MDFN_bswap32(value);
#else
   uint32 tmp = value;
#endif
   memcpy(ptr, &tmp, sizeof(tmp));
}

#endif
