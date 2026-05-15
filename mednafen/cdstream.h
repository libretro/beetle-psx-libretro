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

#ifndef __MDFN_CDSTREAM_H
#define __MDFN_CDSTREAM_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>     /* SEEK_SET / SEEK_CUR / SEEK_END */
#include <string.h>
#include <boolean.h>

#include <streams/file_stream.h>

#include "mednafen.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only stream used by the CD layer.  One concrete type, two
 * possible backends:
 *
 *   - File-backed: `fp` is a libretro-common RFILE *, `buf` is NULL.
 *     read / seek / tell / size delegate straight to filestream_*.
 *
 *   - Memory-backed: `buf` is a malloc'd buffer owned by the cdstream
 *     and freed on close, `fp` is NULL.  Used for the "memcache"
 *     mode where small (cue/toc/ccd/pbp) files - and optionally full
 *     track images - are slurped into RAM up front so the parser
 *     isn't doing tens of thousands of tiny filesystem reads.
 *
 * The two backends are mutually exclusive: exactly one of (fp, buf)
 * is non-NULL on an open stream; both are NULL on a closed/zeroed
 * stream.  Operations branch once on `fp != NULL`.
 *
 * Replaces the historical struct Stream + StreamOps vtable plus its
 * three concrete subtypes (FileStream, MemoryStream,
 * CDIF_Stream_Thing) - all of which are now gone.  The disc-sector-
 * backed CDIF_Stream case had a single consumer
 * (CalcDiscSCEx_BySYSTEMCNF) which now talks to CDIF_ReadSector
 * directly without an intervening stream abstraction. */
typedef struct cdstream
{
   RFILE   *fp;     /* file-backed: libretro-common RFILE handle */
   uint8_t *buf;    /* memory-backed: owned buffer, freed on close */
   uint64_t size;   /* memory-backed: buffer size in bytes */
   int64_t  pos;    /* memory-backed: current read position */
} cdstream;

/* Open `path` as a file-backed stream.  Returns true on success;
 * on failure `out` is zeroed and false is returned.  Safe to call
 * cdstream_close on a failed-open stream. */
bool cdstream_open(cdstream *out, const char *path);

/* Open `path` and slurp its entire contents into RAM as a
 * memory-backed stream.  Returns true on success; on failure (file
 * missing, OOM, short read) `out` is zeroed and false is returned. */
bool cdstream_open_memcached(cdstream *out, const char *path);

/* Convert an open file-backed stream into a memory-backed one by
 * reading the rest of the file from the current position into a
 * fresh buffer.  On success the source RFILE is closed and `src`
 * is converted in place to memory-backed.  On failure `src` is
 * closed and zeroed.  Returns true on success.
 *
 * Used by call sites that need to decide memcache-vs-not after
 * opening (to share an error path on open failure). */
bool cdstream_memcache_in_place(cdstream *src);

/* Read up to `count` bytes into `data`.  Returns the number of
 * bytes actually read; 0 on EOF or error. */
static INLINE uint64_t cdstream_read(cdstream *s, void *data, uint64_t count)
{
   if (s->fp)
   {
      int64_t got = filestream_read(s->fp, data, (int64_t)count);
      /* filestream_read returns -1 on error.  Don't wrap a negative
       * up to UINT64_MAX - return 0 so callers comparing against the
       * requested count behave sanely. */
      if (got < 0)
         return 0;
      return (uint64_t)got;
   }
   if (s->buf)
   {
      uint64_t avail;
      if (s->pos < 0 || (uint64_t)s->pos >= s->size)
         return 0;
      avail = s->size - (uint64_t)s->pos;
      if (count > avail)
         count = avail;
      memcpy(data, s->buf + s->pos, (size_t)count);
      s->pos += (int64_t)count;
      return count;
   }
   return 0;
}

static INLINE void cdstream_seek(cdstream *s, int64_t offset, int whence)
{
   if (s->fp)
   {
      int seek_position = RETRO_VFS_SEEK_POSITION_START;
      switch (whence)
      {
         case SEEK_SET: seek_position = RETRO_VFS_SEEK_POSITION_START;   break;
         case SEEK_CUR: seek_position = RETRO_VFS_SEEK_POSITION_CURRENT; break;
         case SEEK_END: seek_position = RETRO_VFS_SEEK_POSITION_END;     break;
      }
      filestream_seek(s->fp, offset, seek_position);
      return;
   }
   if (s->buf)
   {
      int64_t new_position;
      switch (whence)
      {
         case SEEK_SET: new_position = offset;                          break;
         case SEEK_CUR: new_position = s->pos + offset;                 break;
         case SEEK_END: new_position = (int64_t)s->size + offset;       break;
         default:       return;
      }
      if (new_position < 0)
         return;
      /* Reads past EOF return 0 - that's the correct read-only
       * behaviour; the historical grow-on-write path is gone. */
      s->pos = new_position;
   }
}

static INLINE uint64_t cdstream_tell(cdstream *s)
{
   if (s->fp)
   {
      int64_t pos = filestream_tell(s->fp);
      if (pos < 0)
         return (uint64_t)-1;
      return (uint64_t)pos;
   }
   if (s->buf)
      return (uint64_t)s->pos;
   return (uint64_t)-1;
}

static INLINE uint64_t cdstream_size(cdstream *s)
{
   if (s->fp)
   {
      int64_t sz = filestream_get_size(s->fp);
      if (sz < 0)
         return (uint64_t)-1;
      return (uint64_t)sz;
   }
   if (s->buf)
      return s->size;
   return (uint64_t)-1;
}

/* Release backing resources (RFILE handle or buffer) and zero the
 * stream.  Idempotent; safe to call on a stream that failed to
 * open. */
static INLINE void cdstream_close(cdstream *s)
{
   if (s->fp)
   {
      filestream_close(s->fp);
      s->fp = NULL;
   }
   if (s->buf)
   {
      free(s->buf);
      s->buf  = NULL;
   }
   s->size = 0;
   s->pos  = 0;
}

/* Heap-stream variant: close + free.  No-op on NULL (matches the
 * historical `stream_destroy((Stream*)NULL)` tolerance). */
static INLINE void cdstream_destroy(cdstream *s)
{
   if (!s)
      return;
   cdstream_close(s);
   free(s);
}

/* True iff the stream is open (file handle or buffer present). */
static INLINE bool cdstream_is_open(const cdstream *s)
{
   return s && (s->fp != NULL || s->buf != NULL);
}

/* Single-byte read.  Returns 0 on error / EOF. */
static INLINE uint8_t cdstream_read_u8(cdstream *s)
{
   uint8_t ret = 0;
   cdstream_read(s, &ret, 1);
   return ret;
}

/* Little-endian u32 read.  Used by the PBP header parser. */
static INLINE uint32_t cdstream_read_le_u32(cdstream *s)
{
   uint8_t tmp[4];
   cdstream_read(s, tmp, 4);
   return  (uint32_t)tmp[0]
       |  ((uint32_t)tmp[1] << 8)
       |  ((uint32_t)tmp[2] << 16)
       |  ((uint32_t)tmp[3] << 24);
}

/* Read a line into `out` (cap bytes max, always NUL-terminated).
 * Returns the line-end char ('\r', '\n', or '\0') consumed from
 * the stream, or -1 on EOF with an empty line.  The line-end char
 * is NOT written to out.  Callers handling DOS-style \r\n must
 * consume the trailing \n themselves.
 *
 * Memory-backed streams use an inline tight loop; file-backed
 * streams use a byte-at-a-time fallback over filestream_read. */
int cdstream_get_line(cdstream *s, char *out, size_t cap);

/* Heap-allocate a new cdstream and open `path` as file-backed.
 * Returns NULL on alloc or open failure.  Free with
 * cdstream_destroy. */
cdstream *cdstream_new(const char *path);

/* Heap-allocate a new cdstream and slurp `path` into RAM as
 * memory-backed.  Returns NULL on alloc, open, or read failure. */
cdstream *cdstream_new_memcached(const char *path);

#ifdef __cplusplus
}
#endif

#endif
