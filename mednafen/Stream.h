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

#ifndef __MDFN_STREAM_H
#define __MDFN_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>     /* SEEK_SET / SEEK_CUR / SEEK_END */
#include <boolean.h>

#include "mednafen.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The streams layer in this libretro core is read-only. The historical
 * MODE_WRITE / MODE_WRITE_SAFE / MODE_WRITE_INPLACE constants and the
 * write()/put_*() helpers were never exercised by any code path - all
 * streams are CD images opened for reading - and have been removed
 * along with the C++ -> C conversion. Likewise map()/unmap()/flush()
 * were unused virtual abstraction tax and are gone. */

struct Stream;

/* Stream vtable. One static instance per concrete stream type
 * (FileStream, MemoryStream, CDIF_Stream_Thing). The `self` argument
 * to each op is the same struct Stream * the dispatcher was called
 * with - concrete impls upcast to their own type via structure
 * embedding (struct Stream is the first member of each concrete type).
 *
 *   read     : returns bytes actually read; 0 on error or EOF
 *   seek     : SEEK_SET / SEEK_CUR / SEEK_END whence; out-of-range
 *              clamps or no-ops, no error reporting
 *   tell     : returns (uint64_t)-1 on error
 *   size     : returns (uint64_t)-1 on error
 *   close    : release any underlying resource (file handle, buffer);
 *              idempotent. Does NOT free the containing struct.
 *   destroy  : release resources AND free the containing struct.
 *              May be NULL for stack-allocated streams - callers in
 *              that case must use stream_close() and never
 *              stream_destroy().
 *   get_line : optional fast path for line-oriented reads. May be
 *              NULL; the generic byte-at-a-time fallback in
 *              stream_get_line() handles that case. Reads until
 *              \r/\n/\0 or EOF, writes up to cap-1 bytes to out
 *              (always NUL-terminating), and returns the line-end
 *              char (the line-end is NOT written to out), or -1 on
 *              EOF with an empty line.
 */
struct StreamOps
{
   uint64_t (*read)(struct Stream *self, void *data, uint64_t count);
   void     (*seek)(struct Stream *self, int64_t offset, int whence);
   uint64_t (*tell)(struct Stream *self);
   uint64_t (*size)(struct Stream *self);
   void     (*close)(struct Stream *self);
   void     (*destroy)(struct Stream *self);
   int      (*get_line)(struct Stream *self, char *out, size_t cap);
};

struct Stream
{
   const struct StreamOps *ops;
};

/* Dispatcher inlines - the public C API. All consumers call these
 * rather than poking ops directly. */

static INLINE uint64_t stream_read(struct Stream *s, void *data, uint64_t count)
{
   return s->ops->read(s, data, count);
}

static INLINE void stream_seek(struct Stream *s, int64_t offset, int whence)
{
   s->ops->seek(s, offset, whence);
}

static INLINE uint64_t stream_tell(struct Stream *s)
{
   return s->ops->tell(s);
}

static INLINE uint64_t stream_size(struct Stream *s)
{
   return s->ops->size(s);
}

static INLINE void stream_close(struct Stream *s)
{
   s->ops->close(s);
}

/* Destroy a heap-allocated stream. Calls close internally and frees
 * the containing struct. Caller's pointer is dangling on return.
 *
 * NULL is allowed (no-op) - matches the historical `delete (Stream*)NULL;`
 * tolerance. If the concrete type has no destroy op installed (i.e. the
 * stream is stack-allocated), this falls back to a close-only call;
 * callers should use stream_close() in that case. */
static INLINE void stream_destroy(struct Stream *s)
{
   if (!s)
      return;
   if (s->ops->destroy)
      s->ops->destroy(s);
   else
      s->ops->close(s);
}

/* Single-byte read helper. Equivalent to the historical
 * Stream::get_u8 inline. */
static INLINE uint8_t stream_get_u8(struct Stream *s)
{
   uint8_t ret = 0;
   stream_read(s, &ret, 1);
   return ret;
}

/* Little-endian 32-bit read - currently the only multi-byte typed
 * helper any consumer needs (CDAccess_PBP.cpp). Add more variants
 * here when call sites grow them, rather than reintroducing the
 * full C++ get_LE<T>/get_BE<T>/get_NE<T>/get_RE<T> template family. */
static INLINE uint32_t stream_get_le_u32(struct Stream *s)
{
   uint8_t tmp[4];
   stream_read(s, tmp, 4);
   return  (uint32_t)tmp[0]
       |  ((uint32_t)tmp[1] << 8)
       |  ((uint32_t)tmp[2] << 16)
       |  ((uint32_t)tmp[3] << 24);
}

/* Read a line into out (cap bytes max, always NUL-terminated). Returns
 * the line-end char ('\r', '\n', or '\0') consumed from the stream,
 * or -1 on EOF with an empty line. The line-end char is NOT written
 * to out. Callers handling DOS-style \r\n must consume the trailing
 * \n themselves.
 *
 * Uses the concrete stream's fast path (get_line op) if present,
 * otherwise falls back to byte-at-a-time reads. */
int stream_get_line(struct Stream *s, char *out, size_t cap);

#ifdef __cplusplus
}

/* C++ shim for callers (CDAccess_Image, CDAccess_CCD) whose cue/toc
 * parsers do extensive std::string post-processing on each line.
 * Reads into an internal buffer, then assigns to `str`. Returns the
 * same value as stream_get_line. */
#include <string>
static INLINE int stream_get_line_string(struct Stream *s, std::string &str)
{
   /* CD-image cue/toc lines are short in practice; 4 KB is a generous
    * cap that exceeds anything seen in real images. Lines that exceed
    * this are silently truncated, matching the original (unbounded
    * std::string growth was the only "protection" before, which was
    * itself an OOM hazard on malformed input). */
   char buf[4096];
   int  ret = stream_get_line(s, buf, sizeof(buf));
   if (ret < 0)
   {
      str.clear();
      return ret;
   }
   str.assign(buf);
   return ret;
}

#endif

#endif
