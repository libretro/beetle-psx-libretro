#ifndef __MDFN_AUDIOREADER_H
#define __MDFN_AUDIOREADER_H

#include "../Stream.h"

class AudioReader
{
   public:
      AudioReader();
      virtual ~AudioReader();

      virtual int64_t FrameCount(void);
      INLINE int64_t Read(int64_t frame_offset, int16_t *buffer, int64_t frames)
      {
         int64_t ret;

         if(LastReadPos != frame_offset)
         {
            //puts("SEEK");
            if(!Seek_(frame_offset))
               return(0);
            LastReadPos = frame_offset;
         }

         ret = Read_(buffer, frames);
         LastReadPos += ret;
         return(ret);
      }

   private:
      virtual int64_t Read_(int16_t *buffer, int64_t frames);
      virtual bool Seek_(int64_t frame_offset);

      int64_t LastReadPos;
};

// AR_Open(), and AudioReader, will NOT take "ownership" of the Stream object(IE it won't ever delete it).  Though it does assume it has exclusive access
// to it for as long as the AudioReader object exists.
AudioReader *AR_Open(Stream *fp);

#endif
