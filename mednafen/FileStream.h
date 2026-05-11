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

#ifndef __MDFN_FILESTREAM_H
#define __MDFN_FILESTREAM_H

#include <streams/file_stream.h>

#include "Stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only wrapper around libretro-common's filestream_* API,
 * exposed through the generic struct Stream interface.
 *
 * `mdfn_filestream_*` rather than `filestream_*` because
 * libretro-common already owns the filestream_ prefix. */
struct FileStream
{
   struct Stream  base;   /* must be first - vtable upcasts via &fs->base */
   RFILE         *fp;
};

/* Heap-allocate and open a file for reading. Returns NULL on
 * allocation failure; on file-open failure returns a valid
 * FileStream * with !mdfn_filestream_is_open() (caller must
 * check is_open before using).
 *
 * Free with stream_destroy(&fs->base) - this both closes the file
 * and frees the FileStream. */
struct FileStream *mdfn_filestream_new(const char *path);

/* Stack-allocated equivalent. Initializes `fs` in place; caller
 * must stream_close(&fs->base) when done (NOT stream_destroy,
 * which would free a stack address). On open failure
 * mdfn_filestream_is_open(fs) returns false, but stream_close is
 * still safe to call (idempotent). */
void mdfn_filestream_init(struct FileStream *fs, const char *path);

/* True iff the underlying RFILE was opened successfully. The
 * constructor never fails-fatally - callers must check this. */
static INLINE bool mdfn_filestream_is_open(const struct FileStream *fs)
{
   return fs && fs->fp != NULL;
}

#ifdef __cplusplus
}
#endif

#endif
