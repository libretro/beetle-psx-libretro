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

#ifndef __MDFN_CDROM_CDROMIF_H
#define __MDFN_CDROM_CDROMIF_H

#include <stdint.h>
#include <boolean.h>

#include "CDUtility.h"
#include "../Stream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef TOC CD_TOC;

/* CDIF is opaque.  Concrete struct lives in cdromif.c. */
struct CDIF;
typedef struct CDIF CDIF;

/* Construct a CDIF for the given disc image path.  Selects MT or
 * ST flavour based on image_memcache.  On failure returns NULL and
 * sets *success to false; on success returns a CDIF pointer that
 * must eventually be released with CDIF_Close. */
CDIF *CDIF_Open(bool *success, const char *path,
      const bool is_device, bool image_memcache);

/* Tear down a CDIF (joins the read thread if MT, deletes the
 * underlying CDAccess, frees all resources). */
void CDIF_Close(CDIF *cdif);

/* Copy the disc TOC into *out. */
void CDIF_ReadTOC(CDIF *cdif, TOC *out);

/* Hint that lba is about to be read.  No-op on ST. */
void CDIF_HintReadSector(CDIF *cdif, uint32_t lba);

/* Read 2352 main + 96 subchannel bytes for the sector at lba into
 * buf.  timeout_us is an MT-mode wait budget in microseconds:
 * 0 = non-blocking try, -1 = wait indefinitely, ignored on ST. */
bool CDIF_ReadRawSector(CDIF *cdif, uint8_t *buf, uint32_t lba,
      int64_t timeout_us);

/* Read only the 96 bytes of P+W subchannel for the sector at lba.
 * If hint_fullread is true, also nudge the MT read-ahead. */
bool CDIF_ReadRawSectorPWOnly(CDIF *cdif, uint8_t *buf, uint32_t lba,
      bool hint_fullread);

/* Read nSectors of mode-1 / mode-2-form-1 user data (2048 B per
 * sector) starting at lba into pBuf.  Returns the mode of the
 * first sector on success, 0 on error. */
int CDIF_ReadSector(CDIF *cdif, uint8_t *pBuf, uint32_t lba,
      uint32_t nSectors);

/* Validate a 2352-byte sector via EDC/ECC.  Mode-1 / mode-2-form-1
 * only.  Returns true if intact (or correctable). */
bool CDIF_ValidateRawSector(uint8_t *buf);

/* Eject (true) or insert (false) the disc.  Returns true on
 * success or NOP. */
bool CDIF_Eject(CDIF *cdif, bool eject_status);

/* True iff CDIF_Open or the read thread reported a fatal error. */
bool CDIF_IsUnrecoverable(const CDIF *cdif);

/* Construct a Stream view onto a (start_lba, sector_count) region
 * of the disc.  The stream borrows cdif and must not outlive it. */
struct Stream *CDIF_MakeStream(CDIF *cdif, uint32_t lba, uint32_t sector_count);

#ifdef __cplusplus
}
#endif

#endif
