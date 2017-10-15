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

#include "CDUtility.h"
#include "../Stream.h"

#include <queue>

typedef TOC CD_TOC;

class CDIF
{
   public:

      CDIF();
      virtual ~CDIF();

      inline void ReadTOC(TOC *read_target)
      {
         *read_target = disc_toc;
      }

      virtual void HintReadSector(uint32_t lba) = 0;
      virtual bool ReadRawSector(uint8_t *buf, uint32_t lba, bool async = false) = 0;
      virtual bool ReadRawSectorPWOnly(uint8_t *buf, uint32_t lba, bool hint_fullread) = 0;

      // Call for mode 1 or mode 2 form 1 only.
      bool ValidateRawSector(uint8_t *buf);

      // Utility/Wrapped functions
      // Reads mode 1 and mode2 form 1 sectors(2048 bytes per sector returned)
      // Will return the type(1, 2) of the first sector read to the buffer supplied, 0 on error
      int ReadSector(uint8_t *pBuf, uint32_t lba, uint32_t nSectors);

      // Return true if operation succeeded or it was a NOP(either due to not being implemented, or the current status matches eject_status).
      // Returns false on failure(usually drive error of some kind; not completely fatal, can try again).
      virtual bool Eject(bool eject_status) = 0;

      // For Mode 1, or Mode 2 Form 1.
      // No reference counting or whatever is done, so if you destroy the CDIF object before you destroy the returned Stream, things will go BOOM.
      Stream *MakeStream(uint32_t lba, uint32_t sector_count);

   protected:
      bool UnrecoverableError;
      TOC disc_toc;
      bool DiscEjected;
};

CDIF *CDIF_Open(bool *success, const char *path, const bool is_device, bool image_memcache);

#endif
