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

// AR_Open(), and AudioReader, will NOT take "ownership" of the Stream object(IE it won't ever delete it).  Though it does assume it has exclusive access
// to it for as long as the AudioReader object exists.

// Don't allow exceptions to propagate into the vorbis/musepack/etc. libraries, as it could easily leave the state of the library's decoder "object" in an
// inconsistent state, which would cause all sorts of unfun when we try to destroy it while handling the exception farther up.

#include "../mednafen.h"
#include "audioreader.h"

#include "../tremor/ivorbisfile.h"

#include <string.h>
#include <errno.h>
#include <time.h>

#include "../general.h"
#include "../mednafen-endian.h"

AudioReader::AudioReader() : LastReadPos(0)
{

}

AudioReader::~AudioReader()
{

}

int64_t AudioReader::Read_(int16_t *buffer, int64_t frames)
{
   abort();
   return(false);
}

bool AudioReader::Seek_(int64_t frame_offset)
{
   abort();
   return(false);
}

int64_t AudioReader::FrameCount(void)
{
   abort();
   return(0);
}

class OggVorbisReader : public AudioReader
{
   public:
      OggVorbisReader(Stream *fp);
      ~OggVorbisReader();

      int64_t Read_(int16_t *buffer, int64_t frames);
      bool Seek_(int64_t frame_offset);
      int64_t FrameCount(void);

   private:
      OggVorbis_File ovfile;
};


static size_t iov_read_func(void *ptr, size_t size, size_t nmemb, void *user_data)
{
   Stream *fw = (Stream*)user_data;

   if(!size || !fw)
      return(0);

   return fw->read(ptr, size * nmemb) / size;
}

static int iov_seek_func(void *user_data, int64_t offset, int whence)
{
   Stream *fw = (Stream*)user_data;

   if (fw)
      fw->seek(offset, whence);
   return(0);
}

static int iov_close_func(void *user_data)
{
   Stream *fw = (Stream*)user_data;

   if (fw)
      fw->close();
   return(0);
}

static long iov_tell_func(void *user_data)
{
   Stream *fw = (Stream*)user_data;

   if (!fw)
      return -1;

   return fw->tell();
}

OggVorbisReader::OggVorbisReader(Stream *fp)
{
   ov_callbacks cb;

   memset(&cb, 0, sizeof(cb));
   cb.read_func = iov_read_func;
   cb.seek_func = iov_seek_func;
   cb.close_func = iov_close_func;
   cb.tell_func = iov_tell_func;

   fp->seek(0, SEEK_SET);
   if(ov_open_callbacks(fp, &ovfile, NULL, 0, cb))
      throw(0);
}

OggVorbisReader::~OggVorbisReader()
{
   ov_clear(&ovfile);
}

int64_t OggVorbisReader::Read_(int16_t *buffer, int64_t frames)
{
   uint8 *tw_buf = (uint8 *)buffer;
   int cursection = 0;
   long toread = frames * sizeof(int16_t) * 2;

   while(toread > 0)
   {
      long didread = ov_read(&ovfile, (char*)tw_buf, toread, &cursection);

      if(didread == 0)
         break;

      tw_buf = (uint8 *)tw_buf + didread;
      toread -= didread;
   }

   return(frames - toread / sizeof(int16_t) / 2);
}

bool OggVorbisReader::Seek_(int64_t frame_offset)
{
   ov_pcm_seek(&ovfile, frame_offset);
   return(true);
}

int64_t OggVorbisReader::FrameCount(void)
{
   return(ov_pcm_total(&ovfile, -1));
}

AudioReader *AR_Open(Stream *fp)
{
   return new OggVorbisReader(fp);
}
