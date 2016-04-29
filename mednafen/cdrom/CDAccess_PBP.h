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
      uint8_t buff_raw[16][CD_FRAMESIZE_RAW];
      uint8_t buff_compressed[CD_FRAMESIZE_RAW * 16 + 100];
      uint32_t *index_table;
      uint32_t index_len;
      uint32_t current_block;
      uint32_t sector_in_blk;
      ////////////////

      int32_t NumTracks;
      int32_t FirstTrack;
      int32_t LastTrack;
      int32_t total_sectors;
      uint8_t disc_type;

      int32_t disc_count;
      int32_t current_disc;
      uint32_t discs_start_offset[6];
      uint32_t psar_offset, psisoimg_offset;

      void ImageOpen(const char *path, bool image_memcache);
      int LoadSBI(const char* sbi_path);
      void Cleanup(void);

      CDRFILE_TRACK_INFO Tracks[100]; // Track #0(HMM?) through 99
      struct cpp11_array_doodad
      {
         uint8 data[12];
      };
      std::map<uint32, cpp11_array_doodad> SubQReplaceMap;
      void MakeSubPQ(int32 lba, uint8 *SubPWBuf);

      int uncompress2(void *out, uint32_t *out_size, void *in, uint32_t in_size);
};


#endif
