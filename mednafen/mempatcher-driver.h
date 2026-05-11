#ifndef __MDFN_MEMPATCHER_DRIVER_H
#define __MDFN_MEMPATCHER_DRIVER_H

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MemoryPatch describes one cheat entry.  The legacy C++ class had
 * std::string fields for `name` and `conditions`; with the cheat
 * table now in plain C we use fixed char buffers.  Names and
 * conditions are short by construction (libretro composes the name
 * from "cheat_<index>_<part>", and conditions are GameShark D0/D1/etc
 * encodings of at most a few comma-separated terms). */
#define MDFN_MEMPATCH_NAME_LEN       128
#define MDFN_MEMPATCH_CONDITIONS_LEN 256

typedef struct MemoryPatch
{
   char     name      [MDFN_MEMPATCH_NAME_LEN];
   char     conditions[MDFN_MEMPATCH_CONDITIONS_LEN];
   uint32_t addr;
   uint64_t val;
   uint64_t compare;
   uint32_t mltpl_count;
   uint32_t mltpl_addr_inc;
   uint64_t mltpl_val_inc;
   uint32_t copy_src_addr;
   uint32_t copy_src_addr_inc;
   unsigned length;
   bool     bigendian;
   bool     status; /* (in)active */
   unsigned icount;
   char     type; /* 'R' replace, 'S' substitute(GG), 'C' substitute+compare,
                   * 'T' copy/transfer, 'A' add(variant of R) */
} MemoryPatch;

/* Reset a MemoryPatch to its zero default state. */
void MemoryPatch_Init(MemoryPatch *p);

/* Append `patch` to the cheat table.  The table takes a copy. */
void MDFNI_AddCheat(const MemoryPatch *patch);

#ifdef __cplusplus
}
#endif

#endif
