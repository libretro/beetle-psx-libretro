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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "MemoryStream.h"
#include "error.h"

static struct MemoryStream *ms_from_stream(struct Stream *s)
{
   return (struct MemoryStream *)s;
}

static uint64_t ms_read(struct Stream *s, void *data, uint64_t count)
{
   struct MemoryStream *ms = ms_from_stream(s);
   uint64_t avail;

   if (!ms->data_buffer || ms->position < 0
         || (uint64_t)ms->position >= ms->data_buffer_size)
      return 0;

   avail = ms->data_buffer_size - (uint64_t)ms->position;
   if (count > avail)
      count = avail;

   memcpy(data, &ms->data_buffer[ms->position], (size_t)count);
   ms->position += count;
   return count;
}

static void ms_seek(struct Stream *s, int64_t offset, int whence)
{
   struct MemoryStream *ms = ms_from_stream(s);
   int64_t new_position;

   switch (whence)
   {
      case SEEK_SET:
         new_position = offset;
         break;
      case SEEK_CUR:
         new_position = ms->position + offset;
         break;
      case SEEK_END:
         new_position = (int64_t)ms->data_buffer_size + offset;
         break;
      default:
         return;
   }

   if (new_position < 0)
      return;
   /* Read-only stream: seeking past EOF used to extend the buffer
    * via grow_if_necessary; that path is gone with the write/grow
    * removal. Subsequent reads at an out-of-range position return 0
    * (EOF), which is the correct behaviour for a read-only stream. */
   ms->position = new_position;
}

static uint64_t ms_tell(struct Stream *s)
{
   return (uint64_t)ms_from_stream(s)->position;
}

static uint64_t ms_size(struct Stream *s)
{
   return ms_from_stream(s)->data_buffer_size;
}

static void ms_close(struct Stream *s)
{
   struct MemoryStream *ms = ms_from_stream(s);
   if (ms->data_buffer)
   {
      free(ms->data_buffer);
      ms->data_buffer = NULL;
   }
   ms->data_buffer_size = 0;
   ms->position         = 0;
}

static void ms_destroy(struct Stream *s)
{
   if (!s)
      return;
   ms_close(s);
   free(ms_from_stream(s));
}

/* Memory-stream fast path for line reads. Avoids the byte-at-a-time
 * dispatcher overhead, which mattered noticeably to the cue-sheet
 * parser when this was inherited from Mednafen and worth preserving
 * even after the C conversion. */
static int ms_get_line(struct Stream *s, char *out, size_t cap)
{
   struct MemoryStream *ms = ms_from_stream(s);
   size_t n      = 0;
   bool   got_any = false;

   if (ms->position < 0)
      return -1;

   while ((uint64_t)ms->position < ms->data_buffer_size)
   {
      uint8_t c = ms->data_buffer[ms->position++];
      got_any   = true;

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

static const struct StreamOps memstream_ops_heap =
{
   ms_read,
   ms_seek,
   ms_tell,
   ms_size,
   ms_close,
   ms_destroy,
   ms_get_line,
};

static const struct StreamOps memstream_ops_stack =
{
   ms_read,
   ms_seek,
   ms_tell,
   ms_size,
   ms_close,
   NULL,    /* destroy: stack-allocated; caller must stream_close */
   ms_get_line,
};

/* Common slurp logic. Reads all of `src` into a fresh malloc'd buffer
 * stored in `ms`, then destroys `src`. Caller has already pointed
 * ms->base.ops at the appropriate vtable. */
static void ms_load_from(struct MemoryStream *ms, struct Stream *src)
{
   uint64_t size;
   uint64_t got;

   ms->data_buffer      = NULL;
   ms->data_buffer_size = 0;
   ms->position         = 0;

   if (!src)
      return;

   size = stream_size(src);
   if (size == (uint64_t)-1 || size == 0)
      goto done;

   ms->data_buffer = (uint8_t *)malloc((size_t)size);
   if (!ms->data_buffer)
   {
      MDFN_Error(0,
            "MemoryStream: out of memory (%llu bytes)",
            (unsigned long long)size);
      goto done;
   }

   ms->data_buffer_size = size;

   /* Source is always at position 0 in this libretro core - the
    * historical "preserve source position" branch was never
    * exercised. Read the whole thing in one go. */
   got = stream_read(src, ms->data_buffer, size);
   if (got != size)
   {
      MDFN_Error(0,
            "MemoryStream: short read (%llu of %llu)",
            (unsigned long long)got,
            (unsigned long long)size);
      free(ms->data_buffer);
      ms->data_buffer      = NULL;
      ms->data_buffer_size = 0;
   }

done:
   stream_destroy(src);
}

struct MemoryStream *mdfn_memstream_new_from_stream(struct Stream *src)
{
   struct MemoryStream *ms = (struct MemoryStream *)malloc(sizeof(*ms));
   if (!ms)
   {
      /* Even on alloc failure, take ownership of src to keep the
       * caller's contract simple (src is always consumed). */
      stream_destroy(src);
      return NULL;
   }

   ms->base.ops = &memstream_ops_heap;
   ms_load_from(ms, src);
   return ms;
}

void mdfn_memstream_init_from_stream(struct MemoryStream *dst,
      struct Stream *src)
{
   dst->base.ops = &memstream_ops_stack;
   ms_load_from(dst, src);
}
