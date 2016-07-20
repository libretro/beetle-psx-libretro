#ifndef __MDFN_CDACCESS_IMAGE_H
#define __MDFN_CDACCESS_IMAGE_H

#include <map>

class Stream;
class AudioReader;

struct CDRFILE_TRACK_INFO
{
   int32_t LBA;

   uint32_t DIFormat;
   uint8_t subq_control;

   int32_t pregap;
   int32_t pregap_dv;

   int32_t postgap;

   int32_t index[2];

   int32_t sectors;	// Not including pregap sectors!
   Stream *fp;
   bool FirstFileInstance;
   bool RawAudioMSBFirst;
   long FileOffset;
   unsigned int SubchannelMode;

   uint32_t LastSamplePos;

   AudioReader *AReader;
};

class CDAccess_Image : public CDAccess
{
   public:

      CDAccess_Image(bool *success, const char *path, bool image_memcache);
      virtual ~CDAccess_Image();

      virtual bool Read_Raw_Sector(uint8_t *buf, int32_t lba);

      virtual bool Read_TOC(TOC *toc);

      virtual void Eject(bool eject_status);
   private:

      int32_t NumTracks;
      int32_t FirstTrack;
      int32_t LastTrack;
      int32_t total_sectors;
      uint8_t disc_type;
      CDRFILE_TRACK_INFO Tracks[100]; // Track #0(HMM?) through 99

      struct cpp11_array_doodad
      {
         uint8 data[12];
      };

      std::map<uint32, cpp11_array_doodad> SubQReplaceMap;

      std::string base_dir;

      bool ImageOpen(const char *path, bool image_memcache);
      int LoadSBI(const char* sbi_path);
      void Cleanup(void);

      // MakeSubPQ will OR the simulated P and Q subchannel data into SubPWBuf.
      void MakeSubPQ(int32_t lba, uint8_t *SubPWBuf);

      bool ParseTOCFileLineInfo(CDRFILE_TRACK_INFO *track, const int tracknum,
            const std::string &filename, const char *binoffset, const char *msfoffset,
            const char *length, bool image_memcache, std::map<std::string, Stream*> &toc_streamcache);
      uint32_t GetSectorCount(CDRFILE_TRACK_INFO *track);
};


#endif
