/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Sorted-array implementation of the SubQ replacement table used
 * by the CD-image backends.  Replaces the std::map<uint32, 12-byte>
 * the C++ codebase used; lookup is bsearch instead of red-black
 * tree, which is both smaller and friendlier to cache locality at
 * the few-dozen entries SBI files typically have.
 *
 * Workflow: clear -> insert N times -> finalize (sorts) -> find
 * repeatedly during sector reads.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>

#include "cdaccess_track.h"

void subq_map_clear(subq_map *m)
{
   m->count  = 0;
   m->sorted = false;
}

bool subq_map_empty(const subq_map *m)
{
   return m->count == 0;
}

void subq_map_insert(subq_map *m, uint32_t aba, const uint8_t data[12])
{
   if (m->count >= SUBQ_MAP_MAX)
      return;
   m->entries[m->count].aba = aba;
   memcpy(m->entries[m->count].val.data, data, 12);
   m->count++;
   m->sorted = false;
}

static int subq_entry_cmp(const void *a, const void *b)
{
   uint32_t aa = ((const subq_map_entry *)a)->aba;
   uint32_t bb = ((const subq_map_entry *)b)->aba;
   if (aa < bb) return -1;
   if (aa > bb) return  1;
   return 0;
}

void subq_map_finalize(subq_map *m)
{
   if (m->sorted)
      return;
   if (m->count > 1)
      qsort(m->entries, m->count, sizeof(subq_map_entry), subq_entry_cmp);
   m->sorted = true;
}

const uint8_t *subq_map_find(const subq_map *m, uint32_t aba)
{
   /* Binary search.  Caller is expected to have called finalize;
    * if the map isn't sorted we degrade to linear scan rather than
    * silently returning the wrong answer. */
   if (!m->sorted)
   {
      unsigned i;
      for (i = 0; i < m->count; i++)
         if (m->entries[i].aba == aba)
            return m->entries[i].val.data;
      return NULL;
   }
   else
   {
      int lo = 0;
      int hi = (int)m->count - 1;
      while (lo <= hi)
      {
         int      mid = lo + ((hi - lo) >> 1);
         uint32_t mid_aba = m->entries[mid].aba;
         if (mid_aba == aba)
            return m->entries[mid].val.data;
         if (mid_aba < aba)
            lo = mid + 1;
         else
            hi = mid - 1;
      }
      return NULL;
   }
}
