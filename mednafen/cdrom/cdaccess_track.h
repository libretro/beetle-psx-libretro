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

#ifndef __MDFN_CDACCESS_TRACK_H
#define __MDFN_CDACCESS_TRACK_H

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations - keeps this header light.  The full
 * `struct Stream` lives in mednafen/Stream.h; AudioReader is a
 * still-C++ class declared in mednafen/cdrom/audioreader.h. */
struct Stream;
struct AudioReader;

/* Per-track state shared by the CDAccess_Image / _CHD / _PBP
 * backends.  Holds CD-image metadata + a borrowed Stream pointer
 * for backends that read sectors via a Stream layer (Image, PBP).
 * CHD doesn't use the fp / AReader / FileOffset fields - those are
 * left zero. */
typedef struct CDRFILE_TRACK_INFO
{
   int32_t       LBA;
   uint32_t      DIFormat;
   uint8_t       subq_control;

   int32_t       pregap;
   int32_t       pregap_dv;
   int32_t       postgap;

   int32_t       index[2];
   int32_t       sectors;          /* Not including pregap */

   struct Stream *fp;
   bool          FirstFileInstance;
   bool          RawAudioMSBFirst;
   long          FileOffset;
   unsigned      SubchannelMode;

   uint32_t      LastSamplePos;

   struct AudioReader *AReader;
} CDRFILE_TRACK_INFO;

/* 12-byte SBI replace map value.  The legacy name preserves the
 * original "C++11 wants a class type as std::map value, can't take
 * a raw array" workaround comment from upstream Mednafen. */
typedef struct cpp11_array_doodad
{
   uint8_t data[12];
} cpp11_array_doodad;

/* Sorted-array SubQ replacement table.  Used by CDAccess_CHD,
 * _Image, _PBP to remember per-LBA SubQ overrides loaded from
 * an .sbi file.  Typical SBI files have a few dozen entries; the
 * cap of 1024 entries is generous (largest known SBI files in
 * the wild are well under 200).
 *
 * Entries are inserted in arbitrary order via subq_map_insert,
 * then made searchable with subq_map_finalize (which sorts by
 * aba).  Lookup is binary search via subq_map_find. */
#define SUBQ_MAP_MAX 1024

typedef struct subq_map_entry
{
   uint32_t           aba;
   cpp11_array_doodad val;
} subq_map_entry;

typedef struct subq_map
{
   subq_map_entry entries[SUBQ_MAP_MAX];
   unsigned       count;
   bool           sorted;
} subq_map;

void subq_map_clear   (subq_map *m);
bool subq_map_empty   (const subq_map *m);
void subq_map_insert  (subq_map *m, uint32_t aba, const uint8_t data[12]);
void subq_map_finalize(subq_map *m);

/* Returns NULL if not found, else pointer to the 12-byte value. */
const uint8_t *subq_map_find(const subq_map *m, uint32_t aba);

#ifdef __cplusplus
}
#endif

#endif
