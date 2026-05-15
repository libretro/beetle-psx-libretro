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

#ifndef _CDROM_CDACCESS_CCD_H_
#define _CDROM_CDACCESS_CCD_H_

#include "CDAccess.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Factory: returns a CDAccess* whose vtable dispatches to the CCD
 * backend.  *success is set to true on success, false on failure;
 * a non-NULL pointer may still be returned on failure and must be
 * destroyed with `cda->destroy(cda)`. */
CDAccess *CDAccess_CCD_New(bool *success, const char *path,
      bool image_memcache);

#ifdef __cplusplus
}
#endif

#endif
