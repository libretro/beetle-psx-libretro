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

#include <stdlib.h>
#include <string.h>

#include "mednafen.h"
#include "cdstream.h"
#include "error.h"

bool cdstream_open(cdstream *out, const char *path)
{
   memset(out, 0, sizeof(*out));
   out->fp = filestream_open(path,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   return out->fp != NULL;
}

/* Fill a prefix transfer to the end of the file.  Returns the transfer
 * with its full-length buffer on success (complete, non-empty), NULL
 * otherwise.  The transfer's buffer has the whole file at a stable
 * base: address space is reserved up front and pages are committed as
 * the fill advances, so there is no up-front allocation spike for
 * large track images, and a short read (I/O error, file shrank) is
 * reported as failure rather than a silently truncated buffer. */
static data_transfer_t *cdstream_slurp_dt(const char *path,
      const uint8_t **out_base, uint64_t *out_len)
{
   size_t           len = 0;
   const uint8_t   *base;
   data_transfer_t *dt = data_transfer_open_prefix(path, 0);

   if (!dt)
      return NULL;

   /* Budget 0 = fill to the end; loop defensively in case the
    * backend chunks anyway. */
   while (!data_transfer_complete(dt) && !data_transfer_failed(dt))
      data_transfer_iterate(dt, 0);

   base = data_transfer_ptr(dt, &len);

   /* A zero-length file is "open and empty", not success: the
    * historical MemoryStream path reported is_valid=false for an
    * empty source, making callers treat zero-byte files as "could
    * not load".  Preserve that behaviour. */
   if (!data_transfer_complete(dt) || !base || len == 0)
   {
      data_transfer_free(dt);
      return NULL;
   }

   *out_base = base;
   *out_len  = (uint64_t)len;
   return dt;
}

bool cdstream_open_memcached(cdstream *out, const char *path)
{
   const uint8_t *base = NULL;
   uint64_t       len  = 0;

   memset(out, 0, sizeof(*out));

   out->dt = cdstream_slurp_dt(path, &base, &len);
   if (!out->dt)
      return false;

   out->buf  = (uint8_t *)base;
   out->size = len;
   out->pos  = 0;
   return true;
}

bool cdstream_memcache_in_place(cdstream *src)
{
   const uint8_t *base = NULL;
   uint64_t       len  = 0;
   int64_t        cur;
   const char    *path;
   data_transfer_t *dt;

   if (!src->fp)
   {
      /* Already memory-backed (or closed); nothing to do. */
      return src->buf != NULL;
   }

   /* The transfer reads the whole file from its path; the view we
    * expose starts at the stream's current position, matching
    * MemoryStream's historical "rest of the file from here"
    * behaviour (callers only ever invoke this at pos 0). */
   cur  = filestream_tell(src->fp);
   path = filestream_get_path(src->fp);
   if (cur < 0 || !path || !*path)
   {
      cdstream_close(src);
      return false;
   }

   dt = cdstream_slurp_dt(path, &base, &len);
   if (!dt || (uint64_t)cur >= len)
   {
      /* Missing, empty, unreadable, or already at/past EOF - all of
       * which the historical path reported as failure. */
      if (dt)
         data_transfer_free(dt);
      cdstream_close(src);
      return false;
   }

   filestream_close(src->fp);
   src->fp   = NULL;
   src->dt   = dt;
   src->buf  = (uint8_t *)base + cur;
   src->size = len - (uint64_t)cur;
   src->pos  = 0;
   return true;
}

int cdstream_get_line(cdstream *s, char *out, size_t cap)
{
   size_t  n       = 0;
   bool    got_any = false;
   uint8_t c;

   /* Memory-backed fast path: tight inline scan over s->buf. */
   if (s->buf)
   {
      if (s->pos < 0)
         return -1;

      while ((uint64_t)s->pos < s->size)
      {
         c        = s->buf[s->pos++];
         got_any  = true;

         if (c == '\r' || c == '\n' || c == 0)
         {
            if (cap > 0)
               out[n] = '\0';
            return c;
         }

         if (cap > 0 && n + 1 < cap)
            out[n++] = (char)c;
      }

      if (cap > 0)
         out[n] = '\0';
      return got_any ? 0 : -1;
   }

   /* File-backed fallback: byte-at-a-time over filestream_read.
    * Always NUL-terminate out.  cap == 0 means "no buffer"; still
    * drain the line and report line-end / EOF correctly. */
   if (!s->fp)
      return -1;

   for (;;)
   {
      if (cdstream_read(s, &c, 1) == 0)
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

cdstream *cdstream_new(const char *path)
{
   cdstream *s = (cdstream *)malloc(sizeof(*s));
   if (!s)
      return NULL;
   if (!cdstream_open(s, path))
   {
      free(s);
      return NULL;
   }
   return s;
}

cdstream *cdstream_new_memcached(const char *path)
{
   cdstream *s = (cdstream *)malloc(sizeof(*s));
   if (!s)
      return NULL;
   if (!cdstream_open_memcached(s, path))
   {
      free(s);
      return NULL;
   }
   return s;
}
