#ifndef __MDFN_CDACCESS_CHD_H
#define __MDFN_CDACCESS_CHD_H

#include "CDAccess.h"
#include "CDAccess_Image.h"

#include "chd.h"

class CDAccess_CHD : public CDAccess
{
   public:

      CDAccess_CHD(bool *success, const char *path, bool image_memcache);
      virtual ~CDAccess_CHD();

      virtual bool Read_Raw_Sector(uint8_t *buf, int32_t lba);

      virtual bool Read_Raw_PW(uint8_t *buf, int32_t lba);

      virtual bool Read_TOC(TOC *toc);

      virtual void Eject(bool eject_status);

   private:
      chd_file *chd;
      chd_file *parent_chd;
      /* hunk data cache */
      uint8_t *hunkmem;
      /* last hunknum read */
      int oldhunk;

      int32_t NumTracks;
      int32_t FirstTrack;
      int32_t LastTrack;
      int32_t total_sectors;
      TOC* ptoc;

      std::string sbi_path;

      bool ImageOpen(const char *path, bool image_memcache);
      int LoadSBI(const char* sbi_path);
      void Cleanup(void);

      CDRFILE_TRACK_INFO Tracks[100]; // Track #0(HMM?) through 99
      struct cpp11_array_doodad
      {
         uint8 data[12];
      };
      std::map<uint32, cpp11_array_doodad> SubQReplaceMap;
      int32_t MakeSubPQ(int32 lba, uint8 *SubPWBuf);
};


#endif
