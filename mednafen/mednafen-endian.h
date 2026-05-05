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
 * Endian helpers for reading/writing little-endian and big-endian
 * integer values to/from byte buffers.
 *
 * Host endianness is decided at preprocess time via MSB_FIRST (set
 * in mednafen-types.h based on the compiler's __BYTE_ORDER__ macro).
 * On a little-endian host, MDFN_de32lsb is a plain memcpy; on a
 * big-endian host it does memcpy+bswap. The choice is statically
 * dispatched at the source level via #ifdef MSB_FIRST so there is
 * no runtime branch and no reliance on the optimizer to fold the
 * dead path away.
 *
 * This header is C89-compatible: no templates, no decltype, no
 * static_assert, no std::*. All helpers are static inline functions
 * with explicit unsigned int sizes.
 *
 * Aligned-load variant: MDFN_de32lsb_aligned uses
 * __builtin_assume_aligned to give the optimizer an alignment guarantee
 * on the cpu.cpp instruction-fetch hot path. On targets where this
 * builtin is unavailable (MSVC, ancient compilers) it falls back to
 * the unaligned form.
 *
 * Note that all calls take void* / const void* by design - the
 * pointer arithmetic in callers is byte-wise. MSB_FIRST = big-endian
 * host. LSB_FIRST = little-endian host. Exactly one of these is
 * defined; mednafen-types.h #errors otherwise.
 */

#ifndef __MDFN_ENDIAN_H
#define __MDFN_ENDIAN_H

#include <stddef.h>
#include <string.h>

/* Bulk swap helpers (operate on byte buffers, consumed by SPU/SCSP
 * resampler paths in code that links against this module). Only the
 * raw three-_Swap variants have callers; the *_NE_LE and *_NE_BE
 * orientation wrappers were unused and have been removed. */
void Endian_A16_Swap(void *src, uint32 nelements);
void Endian_A32_Swap(void *src, uint32 nelements);
void Endian_A64_Swap(void *src, uint32 nelements);

/* Byte-swap primitives. Static inline; visible to all callers. */

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

static INLINE uint64 MDFN_bswap64(uint64 v)
{
#if defined(_MSC_VER)
   return _byteswap_uint64(v);
#else
   return (v << 56)
        | (v >> 56)
        | ((v & ((uint64)0xFF00U)) << 40)
        | ((v >> 40) & ((uint64)0xFF00U))
        | ((uint64)MDFN_bswap32((uint32)(v >> 16)) << 16);
#endif
}

/*
 * Little-endian readers. On LSB_FIRST hosts these are pure memcpy;
 * on MSB_FIRST hosts they memcpy + bswap.
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

static INLINE uint64 MDFN_de64lsb(const void *ptr)
{
   uint64 tmp;
   memcpy(&tmp, ptr, sizeof(tmp));
#ifdef MSB_FIRST
   return MDFN_bswap64(tmp);
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
 * Big-endian readers.
 */

static INLINE uint16 MDFN_de16msb(const void *ptr)
{
   uint16 tmp;
   memcpy(&tmp, ptr, sizeof(tmp));
#ifdef MSB_FIRST
   return tmp;
#else
   return MDFN_bswap16(tmp);
#endif
}

static INLINE uint32 MDFN_de32msb(const void *ptr)
{
   uint32 tmp;
   memcpy(&tmp, ptr, sizeof(tmp));
#ifdef MSB_FIRST
   return tmp;
#else
   return MDFN_bswap32(tmp);
#endif
}

static INLINE uint64 MDFN_de64msb(const void *ptr)
{
   uint64 tmp;
   memcpy(&tmp, ptr, sizeof(tmp));
#ifdef MSB_FIRST
   return tmp;
#else
   return MDFN_bswap64(tmp);
#endif
}

static INLINE uint32 MDFN_de24msb(const void *ptr)
{
   const uint8 *p = (const uint8 *)ptr;
   return ((uint32)p[0] << 16) | ((uint32)p[1] << 8) | ((uint32)p[2] << 0);
}

/*
 * Native-endian readers. Read raw memory without any swap regardless
 * of host endianness. Used for type-pun reads from memory-mapped
 * regions whose contents are already in host order.
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

static INLINE void MDFN_en64lsb(void *ptr, uint64 value)
{
#ifdef MSB_FIRST
   uint64 tmp = MDFN_bswap64(value);
#else
   uint64 tmp = value;
#endif
   memcpy(ptr, &tmp, sizeof(tmp));
}

/*
 * Big-endian writers.
 */

static INLINE void MDFN_en16msb(void *ptr, uint16 value)
{
#ifdef MSB_FIRST
   uint16 tmp = value;
#else
   uint16 tmp = MDFN_bswap16(value);
#endif
   memcpy(ptr, &tmp, sizeof(tmp));
}

static INLINE void MDFN_en32msb(void *ptr, uint32 value)
{
#ifdef MSB_FIRST
   uint32 tmp = value;
#else
   uint32 tmp = MDFN_bswap32(value);
#endif
   memcpy(ptr, &tmp, sizeof(tmp));
}

static INLINE void MDFN_en64msb(void *ptr, uint64 value)
{
#ifdef MSB_FIRST
   uint64 tmp = value;
#else
   uint64 tmp = MDFN_bswap64(value);
#endif
   memcpy(ptr, &tmp, sizeof(tmp));
}

#endif
