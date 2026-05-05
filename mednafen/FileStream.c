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
#include "Stream.h"
#include "FileStream.h"

/* The base member is always the first field, so a `struct Stream *`
 * passed to a vtable op can be safely cast to a `struct FileStream *`. */
static struct FileStream *fs_from_stream(struct Stream *s)
{
   return (struct FileStream *)s;
}

static uint64_t fs_read(struct Stream *s, void *data, uint64_t count)
{
   struct FileStream *fs = fs_from_stream(s);
   int64_t got;

   if (!fs->fp)
      return 0;

   /* filestream_read returns int64_t; -1 indicates error. Don't wrap
    * a negative result up into UINT64_MAX - return 0 ("read nothing")
    * so callers comparing against the requested count behave sanely. */
   got = filestream_read(fs->fp, data, count);
   if (got < 0)
      return 0;
   return (uint64_t)got;
}

static void fs_seek(struct Stream *s, int64_t offset, int whence)
{
   struct FileStream *fs = fs_from_stream(s);
   int seek_position = -1;

   if (!fs->fp)
      return;
   switch (whence)
   {
      case SEEK_SET:
         seek_position = RETRO_VFS_SEEK_POSITION_START;
         break;
      case SEEK_CUR:
         seek_position = RETRO_VFS_SEEK_POSITION_CURRENT;
         break;
      case SEEK_END:
         seek_position = RETRO_VFS_SEEK_POSITION_END;
         break;
   }
   filestream_seek(fs->fp, offset, seek_position);
}

static uint64_t fs_tell(struct Stream *s)
{
   struct FileStream *fs = fs_from_stream(s);
   int64_t pos;

   if (!fs->fp)
      return (uint64_t)-1;

   pos = filestream_tell(fs->fp);
   if (pos < 0)
      return (uint64_t)-1;
   return (uint64_t)pos;
}

static uint64_t fs_size(struct Stream *s)
{
   struct FileStream *fs = fs_from_stream(s);
   int64_t sz;

   if (!fs->fp)
      return (uint64_t)-1;

   sz = filestream_get_size(fs->fp);
   if (sz < 0)
      return (uint64_t)-1;
   return (uint64_t)sz;
}

static void fs_close(struct Stream *s)
{
   struct FileStream *fs = fs_from_stream(s);
   if (!fs->fp)
      return;
   filestream_close(fs->fp);
   fs->fp = NULL;
}

static void fs_destroy(struct Stream *s)
{
   if (!s)
      return;
   fs_close(s);
   free(fs_from_stream(s));
}

static const struct StreamOps filestream_ops_heap =
{
   fs_read,
   fs_seek,
   fs_tell,
   fs_size,
   fs_close,
   fs_destroy,
   NULL,   /* get_line: byte-at-a-time fallback is fine for files */
};

static const struct StreamOps filestream_ops_stack =
{
   fs_read,
   fs_seek,
   fs_tell,
   fs_size,
   fs_close,
   NULL,    /* destroy: stack-allocated; caller must stream_close */
   NULL,
};

void mdfn_filestream_init(struct FileStream *fs, const char *path)
{
   fs->base.ops = &filestream_ops_stack;
   fs->fp       = filestream_open(path,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
}

struct FileStream *mdfn_filestream_new(const char *path)
{
   struct FileStream *fs = (struct FileStream *)malloc(sizeof(*fs));
   if (!fs)
      return NULL;

   fs->base.ops = &filestream_ops_heap;
   fs->fp       = filestream_open(path,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   /* Caller checks mdfn_filestream_is_open(fs); on failure they
    * stream_destroy(&fs->base) to free the empty shell. */
   return fs;
}
