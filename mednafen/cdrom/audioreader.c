/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* AR_Open(), and AudioReader, will NOT take "ownership" of the Stream
 * object (it won't ever free it).  Though it does assume it has
 * exclusive access to it for as long as the AudioReader exists. */

#include <stdlib.h>
#include <string.h>

#include "audioreader.h"

#include "../tremor/ivorbisfile.h"

struct AudioReader
{
   OggVorbis_File ovfile;
   int64_t        last_read_pos;
};

static size_t iov_read_func(void *ptr, size_t size, size_t nmemb, void *user_data)
{
   cdstream *fw = (cdstream *)user_data;

   if(!size || !fw)
      return 0;

   return cdstream_read(fw, ptr, size * nmemb) / size;
}

static int iov_seek_func(void *user_data, int64_t offset, int whence)
{
   cdstream *fw = (cdstream *)user_data;

   if (fw)
      cdstream_seek(fw, offset, whence);
   return 0;
}

static int iov_close_func(void *user_data)
{
   cdstream *fw = (cdstream *)user_data;

   if (fw)
      cdstream_close(fw);
   return 0;
}

static long iov_tell_func(void *user_data)
{
   cdstream *fw = (cdstream *)user_data;

   if (!fw)
      return -1;

   return (long)cdstream_tell(fw);
}

AudioReader *AR_Open(cdstream *fp)
{
   ov_callbacks cb;
   AudioReader *r;

   if (!fp)
      return NULL;

   r = (AudioReader *)calloc(1, sizeof(*r));
   if (!r)
      return NULL;

   memset(&cb, 0, sizeof(cb));
   cb.read_func  = iov_read_func;
   cb.seek_func  = iov_seek_func;
   cb.close_func = iov_close_func;
   cb.tell_func  = iov_tell_func;

   cdstream_seek(fp, 0, SEEK_SET);
   if (ov_open_callbacks(fp, &r->ovfile, NULL, 0, cb) != 0)
   {
      free(r);
      return NULL;
   }

   return r;
}

void AR_Close(AudioReader *r)
{
   if (!r)
      return;
   ov_clear(&r->ovfile);
   free(r);
}

int64_t AR_FrameCount(AudioReader *r)
{
   if (!r)
      return 0;
   return ov_pcm_total(&r->ovfile, -1);
}

int64_t AR_Read(AudioReader *r, int64_t frame_offset,
      int16_t *buffer, int64_t frames)
{
   uint8_t *tw_buf;
   int      cursection = 0;
   long     toread;
   int64_t  ret;

   if (!r)
      return 0;

   if (r->last_read_pos != frame_offset)
   {
      ov_pcm_seek(&r->ovfile, frame_offset);
      r->last_read_pos = frame_offset;
   }

   tw_buf = (uint8_t *)buffer;
   toread = frames * (long)sizeof(int16_t) * 2;

   while (toread > 0)
   {
      long didread = ov_read(&r->ovfile, (char *)tw_buf, toread, &cursection);

      if (didread == 0)
         break;

      tw_buf += didread;
      toread -= didread;
   }

   ret = frames - toread / (int64_t)sizeof(int16_t) / 2;
   r->last_read_pos += ret;
   return ret;
}
