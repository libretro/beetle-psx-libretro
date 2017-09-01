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


#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "../mednafen.h"

#include "CDAccess.h"
#include "CDAccess_Image.h"
#include "CDAccess_CCD.h"
#ifdef HAVE_PBP
#include "CDAccess_PBP.h"
#endif
#ifdef HAVE_CHD
#include "CDAccess_CHD.h"
#endif

CDAccess::CDAccess()
{

}

CDAccess::~CDAccess()
{

}

CDAccess *cdaccess_open_image(bool *success, const char *path, bool image_memcache)
{
   if(strlen(path) >= 4 && !strcasecmp(path + strlen(path) - 4, ".ccd"))
      return new CDAccess_CCD(success, path, image_memcache);
#ifdef HAVE_PBP
   else if(strlen(path) >= 4 && !strcasecmp(path + strlen(path) - 4, ".pbp"))
      return new CDAccess_PBP(path, image_memcache);
#endif
#ifdef HAVE_CHD
   else if (strlen(path) >= 4 && !strcasecmp(path + strlen(path) - 4, ".chd"))
      return new CDAccess_CHD(path, image_memcache);
#endif
   return new CDAccess_Image(success, path, image_memcache);
}
