/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Ogg Vorbis audio-track reader on libretro-common's rvorbis, the
 * integer (Q20 fixed-point) clean-room decoder: deterministic s16
 * output across hosts, no float DSP in the audio path.
 *
 * rvorbis decodes from a single in-memory buffer, so AR_Open needs
 * the whole (compressed) stream resident:
 *
 *   - a memory-backed cdstream already holds it: decode straight out
 *     of the stream's buffer, no copy, no extra ownership;
 *   - a file-backed cdstream is slurped through a data_transfer
 *     prefix read on its path: address space reserved up front,
 *     pages committed as the fill advances, stable base for the
 *     reader's lifetime, and a short read reported as failure
 *     rather than a truncated stream.  The resident cost is the
 *     compressed track (typically a small fraction of the decoded
 *     PCM), not the decode.
 *
 * As before, AR_Open never takes ownership of the cdstream itself
 * (it won't free the struct), but assumes exclusive access to it for
 * as long as the AudioReader exists. */

#include <stdlib.h>
#include <string.h>

#include <formats/rvorbis.h>
#include <formats/data_transfer.h>
#include <streams/file_stream.h>

#include "audioreader.h"

struct AudioReader
{
   rvorbis         *vf;
   data_transfer_t *dt;            /* owns the compressed bytes when
                                    * the source was file-backed */
   int64_t          last_read_pos;
   int              channels;      /* stream channel count */
};

AudioReader *AR_Open(cdstream *fp)
{
   AudioReader   *r;
   const uint8_t *data = NULL;
   uint64_t       len  = 0;
   int            verr = 0;

   if (!fp)
      return NULL;

   r = (AudioReader *)calloc(1, sizeof(*r));
   if (!r)
      return NULL;

   if (fp->buf)
   {
      /* Memory-backed: the stream's buffer is the whole file. */
      data = fp->buf;
      len  = fp->size;
   }
   else if (fp->fp)
   {
      const char *path = filestream_get_path(fp->fp);

      if (path && *path)
      {
         size_t dlen = 0;

         r->dt = data_transfer_open_prefix(path, 0);
         if (r->dt)
         {
            while (!data_transfer_complete(r->dt)
                  && !data_transfer_failed(r->dt))
               data_transfer_iterate(r->dt, 0);

            if (data_transfer_complete(r->dt))
            {
               data = data_transfer_ptr(r->dt, &dlen);
               len  = (uint64_t)dlen;
            }
         }
      }
   }

   /* Not an Ogg Vorbis stream (or unreadable): report NULL, exactly
    * as the tremor ov_open failure did - callers treat that as
    * "unsupported audio track format". */
   if (!data || !len || len > 0x7FFFFFFF
         || !(r->vf = rvorbis_open_memory(data, (int)len, &verr, NULL)))
   {
      data_transfer_free(r->dt);
      free(r);
      return NULL;
   }

   r->channels = rvorbis_get_info(r->vf).channels;

   return r;
}

void AR_Close(AudioReader *r)
{
   if (!r)
      return;
   if (r->vf)
      rvorbis_close(r->vf);
   data_transfer_free(r->dt);
   free(r);
}

int64_t AR_FrameCount(AudioReader *r)
{
   if (!r)
      return 0;
   return (int64_t)rvorbis_stream_length_in_samples(r->vf);
}

int64_t AR_Read(AudioReader *r, int64_t frame_offset,
      int16_t *buffer, int64_t frames)
{
   int got;

   if (!r || frames <= 0)
      return 0;

   if (r->last_read_pos != frame_offset)
   {
      if (frame_offset < 0 || frame_offset > 0xFFFFFFFFll)
         return 0;
      rvorbis_seek(r->vf, (unsigned int)frame_offset);
      r->last_read_pos = frame_offset;
   }

   if (r->channels >= 2)
   {
      /* Stereo (or more: extra channels are dropped by the decoder's
       * interleave).  Returns samples per channel - i.e. frames. */
      got = rvorbis_get_samples_s16_interleaved(r->vf, 2, buffer,
            (int)(frames * 2));
   }
   else
   {
      /* Mono: rvorbis zero-fills channels the stream doesn't have,
       * which would play a mono track left-only.  Decode mono into
       * the upper half of the caller's buffer and expand to L=R
       * pairs in place (pair i reads index frames+i, which is always
       * at or ahead of the write cursor 2i+1 for i < frames, so the
       * forward expansion never clobbers unread input).  The old
       * tremor path mishandled mono differently - it assumed stereo
       * and miscounted - so duplication is a fix, not a behaviour
       * change to preserve. */
      int16_t *tmp = buffer + frames;
      int      i;

      got = rvorbis_get_samples_s16_interleaved(r->vf, 1, tmp,
            (int)frames);
      for (i = 0; i < got; i++)
      {
         buffer[2 * i]     = tmp[i];
         buffer[2 * i + 1] = tmp[i];
      }
   }
   if (got < 0)
      got = 0;

   r->last_read_pos += got;
   return got;
}
