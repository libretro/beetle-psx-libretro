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
#include "Stream.h"

int stream_get_line(struct Stream *s, char *out, size_t cap)
{
   size_t n = 0;
   bool   got_any = false;
   uint8_t c;

   if (s->ops->get_line)
      return s->ops->get_line(s, out, cap);

   /* Fallback: byte-at-a-time. Always NUL-terminate out.
    * cap == 0 means "no buffer"; still drain the line and report
    * line-end / EOF correctly. */
   for (;;)
   {
      if (stream_read(s, &c, 1) == 0)
      {
         if (cap > 0)
            out[n] = '\0';
         return got_any ? 0 : -1;
      }
      got_any = true;

      if (c == '\r' || c == '\n' || c == 0)
      {
         if (cap > 0)
            out[n] = '\0';
         return c;
      }

      /* Cap-1 to leave room for NUL. Once full, silently drop further
       * bytes until we hit the line-end. */
      if (cap > 0 && n + 1 < cap)
         out[n++] = (char)c;
   }
}
