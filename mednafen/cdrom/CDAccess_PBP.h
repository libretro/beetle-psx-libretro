#ifndef __MDFN_CDACCESS_PBP_H
#define __MDFN_CDACCESS_PBP_H

#include <map>
#include "CDAccess_Image.h"

class Stream;
class AudioReader;

#define CD_FRAMESIZE_RAW		2352

class CDAccess_PBP : public CDAccess
{
   public:

      CDAccess_PBP(const char *path, bool image_memcache);
      virtual ~CDAccess_PBP();

      virtual void Read_Raw_Sector(uint8_t *buf, int32_t lba);

      virtual void Read_TOC(TOC *toc);

      virtual void Eject(bool eject_status);
   private:
      Stream* fp;

      ////////////////
      unsigned char buff_raw[16][CD_FRAMESIZE_RAW];
      unsigned char buff_compressed[CD_FRAMESIZE_RAW * 16 + 100];
      unsigned int *index_table;
      unsigned int index_len;
      unsigned int current_block;
      unsigned int sector_in_blk;
      ////////////////

      int32_t NumTracks;
      int32_t FirstTrack;
      int32_t LastTrack;
      int32_t total_sectors;
      uint8_t disc_type;

      uint32 discs_start_offset[5];
      uint32 psar_offset, psisoimg_offset;

      void ImageOpen(const char *path, bool image_memcache);
      int LoadSBI(const char* sbi_path);
      void Cleanup(void);

      int uncompress2(void *out, unsigned long *out_size, void *in, unsigned long in_size);
};


#endif
