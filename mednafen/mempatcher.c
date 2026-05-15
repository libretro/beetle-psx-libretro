/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <compat/msvc.h>
#endif

#include <boolean.h>

#include "mednafen.h"

#include "settings.h"
#include "mempatcher.h"

#include <libretro.h>
#include "psx/psx_c.h"

extern retro_log_printf_t log_cb;

static uint8_t **RAMPtrs = NULL;
static uint32_t  PageSize;
static uint32_t  NumPages;

/* Cheat table.
 *
 * Used to be std::vector<MemoryPatch> + 8 std::vector<SUBCHEAT>;
 * the only consumer paths are libretro.cpp's retro_cheat_set
 * (push) / retro_cheat_reset (clear) and the per-frame
 * MDFNMP_ApplyPeriodicCheats / RebuildSubCheats sweeps, all of
 * which want sequential access.  A grow-on-demand C array is the
 * simplest fit. */
static MemoryPatch *cheats;
static size_t       cheats_count;
static size_t       cheats_cap;
static bool         CheatsActive = true;

/* SubCheats is the per-byte expansion of the cheat table, bucketed
 * by (addr & 0x7) for fast lookup in the (currently no-op) read-side
 * patch path.  Eight independent dyn-arrays. */
typedef struct
{
   SUBCHEAT *items;
   size_t    count;
   size_t    cap;
} SubCheatBucket;

static SubCheatBucket SubCheats[8];

static int subcheat_push(SubCheatBucket *b, const SUBCHEAT *sc)
{
   if (b->count == b->cap)
   {
      size_t new_cap     = b->cap ? b->cap * 2 : 8;
      SUBCHEAT *new_items = (SUBCHEAT *)realloc(b->items, new_cap * sizeof(*new_items));
      if (!new_items)
         return -1;
      b->items = new_items;
      b->cap   = new_cap;
   }
   b->items[b->count++] = *sc;
   return 0;
}

static void subcheat_clear_all(void)
{
   int i;
   for (i = 0; i < 8; i++)
      SubCheats[i].count = 0;
}

static int cheats_push(const MemoryPatch *p)
{
   if (cheats_count == cheats_cap)
   {
      size_t new_cap        = cheats_cap ? cheats_cap * 2 : 8;
      MemoryPatch *new_items = (MemoryPatch *)realloc(cheats, new_cap * sizeof(*new_items));
      if (!new_items)
         return -1;
      cheats     = new_items;
      cheats_cap = new_cap;
   }
   cheats[cheats_count++] = *p;
   return 0;
}

static void cheats_free(void)
{
   int i;
   free(cheats);
   cheats       = NULL;
   cheats_count = 0;
   cheats_cap   = 0;
   for (i = 0; i < 8; i++)
   {
      free(SubCheats[i].items);
      SubCheats[i].items = NULL;
      SubCheats[i].count = 0;
      SubCheats[i].cap   = 0;
   }
}

void MemoryPatch_Init(MemoryPatch *p)
{
   memset(p, 0, sizeof(*p));
   p->mltpl_count = 1;
}

static void RebuildSubCheats(void)
{
   size_t ci;

   subcheat_clear_all();

   if (!CheatsActive)
      return;

   for (ci = 0; ci < cheats_count; ci++)
   {
      MemoryPatch *chit = &cheats[ci];
      unsigned     x;

      if (!chit->status || chit->type == 'R')
         continue;

      for (x = 0; x < chit->length; x++)
      {
         SUBCHEAT     tmpsub;
         unsigned int shiftie = chit->bigendian ? (chit->length - 1 - x) * 8 : x * 8;

         tmpsub.addr  = chit->addr + x;
         tmpsub.value = (chit->val >> shiftie) & 0xFF;
         if (chit->type == 'C')
            tmpsub.compare = (chit->compare >> shiftie) & 0xFF;
         else
            tmpsub.compare = -1;

         subcheat_push(&SubCheats[(chit->addr + x) & 0x7], &tmpsub);
      }
   }
}

bool MDFNMP_Init(uint32_t ps, uint32_t numpages)
{
   PageSize = ps;
   NumPages = numpages;

   RAMPtrs = (uint8_t **)calloc(numpages, sizeof(uint8_t *));

   CheatsActive = MDFN_GetSettingB("cheats");
   return true;
}

void MDFNMP_Kill(void)
{
   if (RAMPtrs)
   {
      free(RAMPtrs);
      RAMPtrs = NULL;
   }
   cheats_free();
}


void MDFNMP_AddRAM(uint32_t size, uint32_t A, uint8_t *RAM)
{
   uint32_t AB = A / PageSize;
   unsigned int x;

   size /= PageSize;

   for (x = 0; x < size; x++)
   {
      RAMPtrs[AB + x] = RAM;
      if (RAM) /* Don't increment the RAM pointer if we're passed a NULL pointer */
         RAM += PageSize;
   }
}

void MDFNMP_RegSearchable(uint32_t addr, uint32_t size)
{
   MDFNMP_AddRAM(size, addr, NULL);
}

/* These are placeholders for read-side cheat patches that PSX doesn't
 * actually wire up - the emulator core uses MDFN_MemRead* directly
 * rather than going through a per-address callback. The functions are
 * still called from cheat-management paths, so we keep them as
 * no-ops rather than removing the call sites. */
void MDFNMP_InstallReadPatches(void) { }
void MDFNMP_RemoveReadPatches(void)  { }

void MDFN_LoadGameCheats(void *override_ptr)
{
   (void)override_ptr;
   RebuildSubCheats();
}

void MDFN_FlushGameCheats(int nosave)
{
   (void)nosave;
   cheats_count = 0;
   RebuildSubCheats();
}

void MDFNI_AddCheat(const MemoryPatch *patch)
{
   cheats_push(patch);

   MDFNMP_RemoveReadPatches();
   RebuildSubCheats();
   MDFNMP_InstallReadPatches();
}

/*
 Condition format(ws = white space):

  <variable size><ws><endian><ws><address><ws><operation><ws><value>
	  [,second condition...etc.]

  Value should be unsigned integer, hex(with a 0x prefix) or
  base-10.

  Operations: >=, <=, >, <, ==, !=, & (AND nonzero), !& (AND zero),
              ^, !^, |, !|

  Full example:

  2 L 0xADDE == 0xDEAD, 1 L 0xC000 == 0xA0
*/
static bool TestConditions(const char *string)
{
   char         address[64];
   char         operation[64];
   char         value[64];
   char         endian;
   unsigned int bytelen;
   bool         passed = true;

   while (   passed
          && sscanf(string, "%u %c %63s %63s %63s",
                   &bytelen, &endian, address, operation, value) == 5)
   {
      uint32_t v_address;
      uint64_t v_value;
      uint64_t value_at_address;
      unsigned int x;

      if (address[0] == '0' && address[1] == 'x')
         v_address = strtoul(address + 2, NULL, 16);
      else
         v_address = strtoul(address, NULL, 10);

      if (value[0] == '0' && value[1] == 'x')
         v_value = strtoull(value + 2, NULL, 16);
      else
         v_value = strtoull(value, NULL, 0);

      value_at_address = 0;
      for (x = 0; x < bytelen; x++)
      {
         unsigned int shiftie = (endian == 'B') ? (bytelen - 1 - x) * 8 : x * 8;
         value_at_address |= (uint64_t)PSX_MemPeek8(v_address + x) << shiftie;
      }

      if      (!strcmp(operation, ">="))  { if (!(value_at_address >= v_value)) passed = false; }
      else if (!strcmp(operation, "<="))  { if (!(value_at_address <= v_value)) passed = false; }
      else if (!strcmp(operation, ">"))   { if (!(value_at_address >  v_value)) passed = false; }
      else if (!strcmp(operation, "<"))   { if (!(value_at_address <  v_value)) passed = false; }
      else if (!strcmp(operation, "==")) { if (!(value_at_address == v_value)) passed = false; }
      else if (!strcmp(operation, "!=")) { if (!(value_at_address != v_value)) passed = false; }
      else if (!strcmp(operation, "&"))   { if (!(value_at_address &  v_value)) passed = false; }
      else if (!strcmp(operation, "!&"))  { if  ((value_at_address &  v_value)) passed = false; }
      else if (!strcmp(operation, "^"))   { if (!(value_at_address ^  v_value)) passed = false; }
      else if (!strcmp(operation, "!^"))  { if  ((value_at_address ^  v_value)) passed = false; }
      else if (!strcmp(operation, "|"))   { if (!(value_at_address |  v_value)) passed = false; }
      else if (!strcmp(operation, "!|"))  { if  ((value_at_address |  v_value)) passed = false; }

      string = strchr(string, ',');
      if (!string)
         break;
      string++;
   }

   return passed;
}

void MDFNMP_ApplyPeriodicCheats(void)
{
   size_t ci;

   if (!CheatsActive)
      return;

   for (ci = 0; ci < cheats_count; ci++)
   {
      MemoryPatch *chit = &cheats[ci];

      if (!chit->status)
         continue;
      if (chit->type != 'R' && chit->type != 'A' && chit->type != 'T')
         continue;
      if (chit->conditions[0] != 0 && !TestConditions(chit->conditions))
         continue;

      {
         uint32_t mltpl_count   = chit->mltpl_count;
         uint32_t mltpl_addr    = chit->addr;
         uint64_t mltpl_val     = chit->val;
         uint32_t copy_src_addr = chit->copy_src_addr;

         while (mltpl_count--)
         {
            uint8_t carry = 0;
            unsigned int x;

            for (x = 0; x < chit->length; x++)
            {
               const uint32_t tmpaddr = chit->bigendian
                  ? (mltpl_addr + chit->length - 1 - x)
                  : (mltpl_addr + x);
               const uint8_t  tmpval  = mltpl_val >> (x * 8);

               if (chit->type == 'A')
               {
                  const unsigned t = PSX_MemPeek8(tmpaddr) + tmpval + carry;
                  carry = t >> 8;
                  PSX_MemPoke8(tmpaddr, (uint8_t)t);
               }
               else if (chit->type == 'T')
               {
                  const uint8_t cv = PSX_MemPeek8(chit->bigendian
                        ? (copy_src_addr + chit->length - 1 - x)
                        : (copy_src_addr + x));
                  PSX_MemPoke8(tmpaddr, cv);
               }
               else
                  PSX_MemPoke8(tmpaddr, tmpval);
            }
            mltpl_addr    += chit->mltpl_addr_inc;
            mltpl_val     += chit->mltpl_val_inc;
            copy_src_addr += chit->copy_src_addr_inc;
         }
      }
   }
}
