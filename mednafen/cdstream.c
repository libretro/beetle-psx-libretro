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

bool cdstream_open_memcached(cdstream *out, const char *path)
{
   void   *buf = NULL;
   int64_t len = 0;

   memset(out, 0, sizeof(*out));

   /* filestream_read_file returns non-zero on success and allocates
    * buf with malloc; we own it from this point on. */
   if (!filestream_read_file(path, &buf, &len) || !buf)
   {
      if (buf)
         free(buf);
      return false;
   }

   /* Defensive: a zero-length file is "open and empty", not failure.
    * The historical MemoryStream path treated an empty source as a
    * NULL buffer with size 0 and reported is_valid=false, which made
    * the caller treat zero-byte files as "could not load".  Preserve
    * that behaviour to avoid surprising any existing call site. */
   if (len <= 0)
   {
      free(buf);
      return false;
   }

   out->buf  = (uint8_t *)buf;
   out->size = (uint64_t)len;
   out->pos  = 0;
   return true;
}

bool cdstream_memcache_in_place(cdstream *src)
{
   uint64_t size;
   uint64_t got;
   uint8_t *buf;

   if (!src->fp)
   {
      /* Already memory-backed (or closed); nothing to do. */
      return src->buf != NULL;
   }

   /* Read from current position, matching MemoryStream's historical
    * behaviour (the libretro core only ever called this at pos 0, so
    * the "preserve source position" branch never mattered, but the
    * resulting buffer represented "the rest of the file from here"). */
   {
      int64_t sz   = filestream_get_size(src->fp);
      int64_t cur  = filestream_tell(src->fp);
      if (sz < 0 || cur < 0 || cur > sz)
      {
         cdstream_close(src);
         return false;
      }
      size = (uint64_t)(sz - cur);
   }

   if (size == 0)
   {
      cdstream_close(src);
      return false;
   }

   buf = (uint8_t *)malloc((size_t)size);
   if (!buf)
   {
      MDFN_Error(0, "cdstream: out of memory (%llu bytes)",
            (unsigned long long)size);
      cdstream_close(src);
      return false;
   }

   got = (uint64_t)filestream_read(src->fp, buf, (int64_t)size);
   if (got != size)
   {
      MDFN_Error(0, "cdstream: short read (%llu of %llu)",
            (unsigned long long)got,
            (unsigned long long)size);
      free(buf);
      cdstream_close(src);
      return false;
   }

   filestream_close(src->fp);
   src->fp   = NULL;
   src->buf  = buf;
   src->size = size;
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
