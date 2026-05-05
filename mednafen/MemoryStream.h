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

#ifndef _MEMORY_STREAM_H
#define _MEMORY_STREAM_H

#include "Stream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only in-memory stream. Used by the CD layer to slurp small
 * cue/toc/PBP/CCD index files into RAM up-front so the cue-sheet
 * parser isn't doing tens of thousands of small filesystem reads.
 *
 * The historical default and size_hint constructors and the entire
 * write/grow path were never used by any caller in this libretro
 * core and have been removed.
 *
 * The single supported construction is mdfn_memstream_new_from_stream()
 * which reads the entirety of an existing source stream into a fresh
 * heap buffer, then destroys the source. */
struct MemoryStream
{
   struct Stream  base;          /* must be first */
   uint8_t       *data_buffer;
   uint64_t       data_buffer_size;
   int64_t        position;
};

/* Heap-allocate a new MemoryStream and read all of `src` into it.
 * `src` is destroyed (closed and freed) before this function returns,
 * matching the historical MemoryStream(Stream*) ownership semantics
 * - the caller's pointer to `src` is dangling on return.
 *
 * Returns:
 *  - NULL if the MemoryStream allocation itself failed (rare); `src`
 *    is still destroyed in this case to keep ownership unambiguous.
 *  - A non-NULL pointer otherwise. The caller MUST check
 *    mdfn_memstream_is_valid() - if false, the buffer allocation or
 *    the underlying read failed, and the stream is an empty shell. */
struct MemoryStream *mdfn_memstream_new_from_stream(struct Stream *src);

/* Stack-allocated equivalent. Initializes `dst` in place; caller
 * must stream_close(&dst->base) when done (NOT stream_destroy,
 * which would free a stack address). */
void mdfn_memstream_init_from_stream(struct MemoryStream *dst,
      struct Stream *src);

static INLINE bool mdfn_memstream_is_valid(const struct MemoryStream *ms)
{
   return ms && ms->data_buffer != NULL;
}

#ifdef __cplusplus
}
#endif

#endif
