#ifndef __MDFN_PSX_MASMEM_H
#define __MDFN_PSX_MASMEM_H

#include <retro_inline.h>

#include "mednafen-types.h"

/*
 * Endian model: a single MSB_FIRST define determines host endianness
 * for the entire file. Defined => big-endian host (PowerPC consoles -
 * PS3, Xenon/Xbox 360, GameCube, Wii, ppc OSX); undefined => little-
 * endian host (everything else, which is the overwhelming majority of
 * libretro frontends today).
 *
 * The data the PS1 emulator stores in MainRAM / BIOSROM / ScratchRAM /
 * PIOMem is always laid out little-endian on disk and at runtime, so
 * the LE host case is a straight pointer dereference and the BE host
 * case is a byte-swap.
 *
 * Previously this file had three layers: a public LoadU*_LE / StoreU*_LE
 * pair that #ifdef'd to call LoadU*_RBO / StoreU*_RBO, which themselves
 * #ifdef'd between hand-written shifts and PowerPC lhbrx/lwbrx
 * instructions. That collapsed at -O2 but the source obscured the fact
 * that the LE path is one mov. Each operation is now defined directly
 * for its host with no internal branching.
 */

#ifdef MSB_FIRST

/* Big-endian host: PS1 little-endian data needs to be byte-swapped on
 * every load and store. PowerPC has direct byte-reversed load/store
 * instructions; other (rare) BE hosts use a portable shift sequence. */

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

/* Little-endian host: PS1 little-endian data is already in native
 * byte order. Every helper compiles to a single load/store. */

static INLINE uint16 LoadU16_LE(const uint16 *a)  { return *a; }
static INLINE uint32 LoadU32_LE(const uint32 *a)  { return *a; }
static INLINE void   StoreU16_LE(uint16 *a, const uint16 v) { *a = v; }
static INLINE void   StoreU32_LE(uint32 *a, const uint32 v) { *a = v; }

#endif /* MSB_FIRST */


/*
 * Mednafen's MultiAccessSizeMem is a fixed-size byte buffer that
 * supports aligned 8/16/24/32-bit access against the same backing
 * storage. Used for MainRAM, BIOSROM, ScratchRAM, and PIOMem.
 *
 * The template originally took (size, max_unit_type, big_endian)
 * parameters but every instantiation in the codebase passes
 * (size, uint32, false) - the last two never vary - so the only live
 * parameter is the size. The "false" big_endian means "the data is
 * stored little-endian regardless of host", which collapses to:
 *   - LE host: direct dereference
 *   - BE host: byte-swap
 *
 * which is exactly what LoadU*_LE / StoreU*_LE already do, so the
 * read/write helpers below just call into them.
 *
 * The template body remains C++ because it's still a template (size
 * is a template parameter). C-callable wrappers exist on the .cpp
 * side where any C consumer needs them.
 */
#ifdef __cplusplus
template<unsigned size>
struct MultiAccessSizeMem
{
   union
   {
      uint8  data8 [size];
      uint16 data16[size / sizeof(uint16)];
      uint32 data32[size / sizeof(uint32)];
   };

   INLINE uint8 ReadU8(uint32 address)
   {
      return data8[address];
   }

   INLINE uint16 ReadU16(uint32 address)
   {
      return LoadU16_LE((uint16 *)(data8 + address));
   }

   INLINE uint32 ReadU32(uint32 address)
   {
      return LoadU32_LE((uint32 *)(data8 + address));
   }

   INLINE uint32 ReadU24(uint32 address)
   {
      return  ReadU8(address)
           | (ReadU8(address + 1) << 8)
           | (ReadU8(address + 2) << 16);
   }

   INLINE void WriteU8(uint32 address, uint8 value)
   {
      data8[address] = value;
   }

   INLINE void WriteU16(uint32 address, uint16 value)
   {
      StoreU16_LE((uint16 *)(data8 + address), value);
   }

   INLINE void WriteU32(uint32 address, uint32 value)
   {
      StoreU32_LE((uint32 *)(data8 + address), value);
   }

   INLINE void WriteU24(uint32 address, uint32 value)
   {
      WriteU8(address + 0, value >> 0);
      WriteU8(address + 1, value >> 8);
      WriteU8(address + 2, value >> 16);
   }

   template<typename T>
   INLINE T Read(uint32 address)
   {
      if (sizeof(T) == 4)
         return ReadU32(address);
      if (sizeof(T) == 2)
         return ReadU16(address);
      return ReadU8(address);
   }

   template<typename T>
   INLINE void Write(uint32 address, T value)
   {
      if (sizeof(T) == 4)
         WriteU32(address, value);
      else if (sizeof(T) == 2)
         WriteU16(address, value);
      else
         WriteU8(address, value);
   }
};
#endif /* __cplusplus */

#endif
