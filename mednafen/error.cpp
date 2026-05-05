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

#include "mednafen.h"
#include "error.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <libretro.h>

extern retro_log_printf_t log_cb;

/* Format and log an error message via the libretro logging callback.
 * No allocation, no exceptions, no hidden control flow - just log and
 * return so the caller can react to whatever error condition they
 * detected by propagating their own status code. */
void MDFN_Error(int errno_code, const char *format, ...)
{
   char buf[1024];
   va_list ap;

   (void)errno_code;

   if (!format)
      return;

   va_start(ap, format);
   vsnprintf(buf, sizeof(buf), format, ap);
   va_end(ap);

   if (log_cb)
      log_cb(RETRO_LOG_ERROR, "%s\n", buf);
}
