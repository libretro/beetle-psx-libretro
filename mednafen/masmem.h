#ifndef __MDFN_PSX_MASMEM_H
#define __MDFN_PSX_MASMEM_H

#include <stdlib.h>
#include <string.h>

#include <retro_inline.h>

#include "mednafen-types.h"

/*
 * Endian model: a single MSB_FIRST define determines host endianness
 * for the entire file. Defined => big-endian host (PowerPC consoles -
 * PS3, Xenon/Xbox 360, GameCube, Wii, ppc OSX); undefined => little-
 * endian host (everything else, which is the overwhelming majority of
 * libretro frontends today).
 */

#ifdef MSB_FIRST

static INLINE uint16 LoadU16_LE(const uint16 *a)
{
#ifdef ARCH_POWERPC
   uint16 tmp;
   __asm__ ("lhbrx %0, %y1" : "=r"(tmp) : "Z"(*a));
   return tmp;
#else
   return (*a << 8) | (*a >> 8);
#endif
}

static INLINE uint32 LoadU32_LE(const uint32 *a)
{
#ifdef ARCH_POWERPC
   uint32 tmp;
   __asm__ ("lwbrx %0, %y1" : "=r"(tmp) : "Z"(*a));
   return tmp;
#else
   uint32 tmp = *a;
   return (tmp << 24) | ((tmp & 0xFF00) << 8) | ((tmp >> 8) & 0xFF00) | (tmp >> 24);
#endif
}

static INLINE void StoreU16_LE(uint16 *a, const uint16 v)
{
#ifdef ARCH_POWERPC
   __asm__ ("sthbrx %0, %y1" : : "r"(v), "Z"(*a));
#else
   *a = (v << 8) | (v >> 8);
#endif
}

static INLINE void StoreU32_LE(uint32 *a, const uint32 v)
{
#ifdef ARCH_POWERPC
   __asm__ ("stwbrx %0, %y1" : : "r"(v), "Z"(*a));
#else
   *a = (v << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | (v >> 24);
#endif
}

#else /* !MSB_FIRST */

static INLINE uint16 LoadU16_LE(const uint16 *a)  { return *a; }
static INLINE uint32 LoadU32_LE(const uint32 *a)  { return *a; }
static INLINE void   StoreU16_LE(uint16 *a, const uint16 v) { *a = v; }
static INLINE void   StoreU32_LE(uint32 *a, const uint32 v) { *a = v; }

#endif /* MSB_FIRST */


/*
 * MultiAccessSizeMem: fixed-size byte buffer that supports aligned
 * 8/16/24/32-bit access against the same backing storage.  Used for
 * MainRAM (2 MB), BIOSROM (512 KB), ScratchRAM (1 KB), PIOMem (64 KB).
 *
 * Plain C struct now - the previous C++ class with member functions
 * has been split into the struct + free functions below.  The
 * constructor pattern (MultiAccessSizeMem_New(N)) returns a calloc-d
 * struct with init'd storage; MultiAccessSizeMem_Attach binds an
 * existing buffer (lightrec mmap path) without taking ownership.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MultiAccessSizeMem
{
   uint8_t  *data8;
   uint32_t  size;
   bool      owned;
} MultiAccessSizeMem;

static INLINE MultiAccessSizeMem *MultiAccessSizeMem_New(uint32_t size)
{
   MultiAccessSizeMem *m = (MultiAccessSizeMem *)calloc(1, sizeof(MultiAccessSizeMem));
   if (!m) return NULL;
   m->data8 = (uint8_t *)calloc(1, size);
   if (!m->data8) { free(m); return NULL; }
   m->size  = size;
   m->owned = true;
   return m;
}

static INLINE MultiAccessSizeMem *MultiAccessSizeMem_Attach(uint8_t *buf, uint32_t size)
{
   MultiAccessSizeMem *m = (MultiAccessSizeMem *)calloc(1, sizeof(MultiAccessSizeMem));
   if (!m) return NULL;
   m->data8 = buf;
   m->size  = size;
   m->owned = false;
   return m;
}

static INLINE void MultiAccessSizeMem_Free(MultiAccessSizeMem *m)
{
   if (!m) return;
   if (m->owned)
      free(m->data8);
   free(m);
}

static INLINE uint32_t *MultiAccessSizeMem_get_data32(MultiAccessSizeMem *m)
{
   return (uint32_t *)m->data8;
}

static INLINE uint8_t MASMEM_ReadU8(MultiAccessSizeMem *m, uint32_t address)
{
   return m->data8[address];
}

static INLINE uint16_t MASMEM_ReadU16(MultiAccessSizeMem *m, uint32_t address)
{
   return LoadU16_LE((uint16_t *)(m->data8 + address));
}

static INLINE uint32_t MASMEM_ReadU32(MultiAccessSizeMem *m, uint32_t address)
{
   return LoadU32_LE((uint32_t *)(m->data8 + address));
}

static INLINE uint32_t MASMEM_ReadU24(MultiAccessSizeMem *m, uint32_t address)
{
   return  m->data8[address]
        | (m->data8[address + 1] << 8)
        | (m->data8[address + 2] << 16);
}

static INLINE void MASMEM_WriteU8(MultiAccessSizeMem *m, uint32_t address, uint8_t value)
{
   m->data8[address] = value;
}

static INLINE void MASMEM_WriteU16(MultiAccessSizeMem *m, uint32_t address, uint16_t value)
{
   StoreU16_LE((uint16_t *)(m->data8 + address), value);
}

static INLINE void MASMEM_WriteU32(MultiAccessSizeMem *m, uint32_t address, uint32_t value)
{
   StoreU32_LE((uint32_t *)(m->data8 + address), value);
}

static INLINE void MASMEM_WriteU24(MultiAccessSizeMem *m, uint32_t address, uint32_t value)
{
   m->data8[address + 0] = (uint8_t)(value >> 0);
   m->data8[address + 1] = (uint8_t)(value >> 8);
   m->data8[address + 2] = (uint8_t)(value >> 16);
}

#ifdef __cplusplus
}
#endif

#endif
