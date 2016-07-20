#ifndef __MDFN_CDACCESS_PBP_H
#define __MDFN_CDACCESS_PBP_H

#include <boolean.h>

#include <map>
#include "CDAccess_Image.h"

class Stream;

class CDAccess_PBP : public CDAccess
{
   public:

      CDAccess_PBP(const char *path, bool image_memcache);
      virtual ~CDAccess_PBP();

      virtual bool Read_Raw_Sector(uint8_t *buf, int32_t lba);

      virtual void Read_TOC(TOC *toc);

      virtual void Eject(bool eject_status);

   private:
      Stream* fp;

      enum PBP_FILES{
         PARAM_SFO,
         ICON0_PNG,
         ICON1_PMF,
         PIC0_PNG,
         PIC1_PNG,
         SND0_AT3,
         DATA_PSP,
         DATA_PSAR,

         PBP_NUM_FILES
      };
      uint32_t pbp_file_offsets[PBP_NUM_FILES];

      ////////////////
      uint8_t buff_raw[16][2352];
      uint8_t buff_compressed[2352 * 16];
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

      std::string sbi_path;
      uint32_t discs_start_offset[5];
      uint32_t psisoimg_offset;

      uint16_t fixed_sectors;
      bool is_official;    // TODO: find more consistent ways to check for used compression algorithm, compressed (and/or encrypted?) audio tracks and messed up sectors

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

      int decompress2(void *out, uint32_t *out_size, void *in, uint32_t in_size);

      int decode_range(unsigned int *range, unsigned int *code, unsigned char **src);
      int decode_bit(unsigned int *range, unsigned int *code, int *index, unsigned char **src, unsigned char *c);
      int decode_word(unsigned char *ptr, int index, int *bit_flag, unsigned int *range, unsigned int *code, unsigned char **src);
      int decode_number(unsigned char *ptr, int index, int *bit_flag, unsigned int *range, unsigned int *code, unsigned char **src);
      int decompress(unsigned char *out, unsigned char *in, unsigned int size);

      int decrypt_pgd(unsigned char* pgd_data, int pgd_size);
      int fix_sector(uint8_t* sector, int32_t lba);
};


#endif
