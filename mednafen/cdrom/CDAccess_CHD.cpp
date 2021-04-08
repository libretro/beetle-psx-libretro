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

#include <mednafen/mednafen.h>
#include <mednafen/general.h>
#include <mednafen/mednafen-endian.h>
#include <mednafen/FileStream.h>

#include "CDAccess_CHD.h"

extern retro_log_printf_t log_cb;


// Disk-image(rip) track/sector formats
enum
{
   DI_FORMAT_AUDIO       = 0x00,
   DI_FORMAT_MODE1       = 0x01,
   DI_FORMAT_MODE1_RAW   = 0x02,
   DI_FORMAT_MODE2       = 0x03,
   DI_FORMAT_MODE2_FORM1 = 0x04,
   DI_FORMAT_MODE2_FORM2 = 0x05,
   DI_FORMAT_MODE2_RAW   = 0x06,
   _DI_FORMAT_COUNT
};

static const char *DI_CUE_Strings[7] = 
{
   "AUDIO",
   "MODE1/2048",
   "MODE1/2352",

   "MODE2/2336",
   "MODE2/2048",
   "MODE2/2324",
   "MODE2/2352"
};

bool CDAccess_CHD::ImageOpen(const char *path, bool image_memcache)
{
   chd_error err = chd_open(path, CHD_OPEN_READ, NULL, &chd);
   if (err != CHDERR_NONE)
      return false;

   if (image_memcache)
   {
      err = chd_precache(chd);
      if (err != CHDERR_NONE)
         return false;
   }

   /* allocate storage for sector reads */
   const chd_header *head = chd_get_header(chd);
   hunkmem = (uint8_t*)malloc(head->hunkbytes);
   oldhunk = -1;
   
   log_cb(RETRO_LOG_INFO, "chd_load '%s' hunkbytes=%d\n", path, head->hunkbytes);

   int plba = -150;
   uint32_t fileOffset = 0;
   //int rlba = 0;

   char type[64], subtype[32], pgtype[32], pgsub[32];

   char meta_entry[256];
   uint32_t meta_entry_size = 0;

   while (1)
   {
      int tkid = 0, frames = 0, pad = 0, pregap = 0, postgap = 0;

      err = chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, NumTracks, meta_entry, sizeof(meta_entry), &meta_entry_size, NULL, NULL);
      if (err == CHDERR_NONE)
      {
         sscanf(meta_entry, CDROM_TRACK_METADATA2_FORMAT,
            &tkid, type, subtype, &frames, &pregap, pgtype, pgsub, &postgap);
      }
      else
      {
         err = chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, NumTracks, meta_entry, sizeof(meta_entry), &meta_entry_size, NULL, NULL);
         if (err == CHDERR_NONE)
         {
            sscanf(meta_entry, CDROM_TRACK_METADATA_FORMAT,
               &tkid, type, subtype, &frames);
         }
         else
         {
            /* if there's no valid metadata, this is the end of the TOC */
            break;
         }
      }

      if (strncmp(type, "MODE2_RAW", 9) != 0 && strncmp(type, "AUDIO", 5) != 0)
      {
         log_cb(RETRO_LOG_ERROR, "chd_parse track type %s unsupported\n", type);
         return false;
      }
      else if (strncmp(subtype, "NONE", 4) != 0)
      {
         log_cb(RETRO_LOG_ERROR, "chd_parse track subtype %s unsupported\n", subtype);
         return false;
      }

      /* add track */
      NumTracks++;

      if (NumTracks != tkid)
      {
         // should this be treated as a fatal error?
         log_cb(RETRO_LOG_WARN, "chd tracks are out of order, missing a track or contain a duplicate!\n");
      }

      if (strncmp(type, "MODE2_RAW", 9) == 0)
      {
         Tracks[tkid].DIFormat = DI_FORMAT_MODE2_RAW;
         Tracks[tkid].subq_control |= SUBQ_CTRLF_DATA;
      }
      else if (strncmp(type, "AUDIO", 5) == 0)
      {
         Tracks[tkid].DIFormat = DI_FORMAT_AUDIO;
         Tracks[tkid].subq_control &= ~SUBQ_CTRLF_DATA;
         Tracks[tkid].RawAudioMSBFirst = true;
      }

      Tracks[tkid].pregap = (tkid == 1) ? 150 : (pgtype[0] == 'V') ? 0 : pregap;
      Tracks[tkid].pregap_dv = (pgtype[0] == 'V') ? pregap : 0;
      plba += Tracks[tkid].pregap + Tracks[tkid].pregap_dv;
      Tracks[tkid].LBA = plba;
      Tracks[tkid].postgap = postgap;
      Tracks[tkid].sectors = frames - Tracks[tkid].pregap_dv;
      Tracks[tkid].SubchannelMode = 0;
      Tracks[tkid].index[0] = -1;
      Tracks[tkid].index[1] = 0;

      fileOffset += Tracks[tkid].pregap_dv;
      //printf("Tracks[%d].fileOffset=%d\n",NumTracks, fileOffset);
      Tracks[tkid].FileOffset = fileOffset;
      fileOffset += frames - Tracks[tkid].pregap_dv;
      fileOffset += Tracks[tkid].postgap;
      fileOffset += ((frames + 3) & ~3) - frames;

      plba += frames - Tracks[tkid].pregap_dv;
      plba += Tracks[tkid].postgap;

      total_sectors += (tkid == 1) ? frames : frames + Tracks[tkid].pregap;

      if (tkid < FirstTrack)
         FirstTrack = tkid;
      if (tkid > LastTrack)
         LastTrack = tkid;

      /*Tracks[tkid].pregap = pregap;
      Tracks[tkid].postgap = postgap;
      Tracks[tkid].index[0] = rlba - pregap;
      Tracks[tkid].index[1] = rlba;
      Tracks[tkid].pregap_dv = Tracks[tkid].index[1] - Tracks[tkid].index[0];
      Tracks[tkid].LBA = rlba;
      Tracks[tkid].sectors = frames;

      Tracks[tkid].FileOffset = rlba * 2352;

      total_sectors += frames;

      if (tkid == 1)
         rlba += frames + 150;
      else
         rlba += frames;*/
   }

   // prepare sbi file path
   std::string base_dir, file_base, file_ext;
   char sbi_ext[4] = { 's', 'b', 'i', 0 };

   MDFN_GetFilePathComponents(path, &base_dir, &file_base, &file_ext);

   if(file_ext.length() == 4 && file_ext[0] == '.')
   {
      for(int i = 0; i < 3; i++)
      {
         if(file_ext[1 + i] >= 'A' && file_ext[1 + i] <= 'Z')
            sbi_ext[i] += 'A' - 'a';
      }
   }
   sbi_path = MDFN_EvalFIP(base_dir, file_base + std::string(".") + std::string(sbi_ext), true);

   return true;
}

void CDAccess_CHD::Cleanup(void)
{
   if(chd != NULL)
      chd_close(chd);

   if (hunkmem != NULL)
      free(hunkmem);
}

CDAccess_CHD::CDAccess_CHD(const char *path, bool image_memcache)
{
   chd = NULL;

   NumTracks = 0;
   total_sectors = 0;
   memset(Tracks, 0, sizeof(Tracks));

   // initialize opposites
   FirstTrack = 99;
   LastTrack = 0;

   if (!ImageOpen(path, image_memcache))
   {
   }
}

CDAccess_CHD::~CDAccess_CHD()
{
   Cleanup();
}

// Note: this function makes use of the current contents(as in |=) in SubPWBuf.
int32_t CDAccess_CHD::MakeSubPQ(int32 lba, uint8 *SubPWBuf)
{
   unsigned i;
   uint8_t buf[0xC], adr, control;
   int32_t track;
   uint32_t lba_relative;
   uint32_t ma, sa, fa;
   uint32_t m, s, f;
   uint8_t pause_or = 0x00;
   bool track_found = false;

   for(track = FirstTrack; track < (FirstTrack + NumTracks); track++)
   {
      if(lba >= (Tracks[track].LBA - Tracks[track].pregap_dv - Tracks[track].pregap) && lba < (Tracks[track].LBA + Tracks[track].sectors + Tracks[track].postgap))
      {
         track_found = true;
         break;
      }
   }

   //printf("%d %d\n", Tracks[1].LBA, Tracks[1].sectors);

   if(!track_found)
   {
      printf("MakeSubPQ error for sector %u!", lba);
      track = FirstTrack;
   }

   lba_relative = abs((int32)lba - Tracks[track].LBA);

   f            = (lba_relative % 75);
   s            = ((lba_relative / 75) % 60);
   m            = (lba_relative / 75 / 60);

   fa           = (lba + 150) % 75;
   sa           = ((lba + 150) / 75) % 60;
   ma           = ((lba + 150) / 75 / 60);

   adr          = 0x1; // Q channel data encodes position
   control      = Tracks[track].subq_control;

   // Handle pause(D7 of interleaved subchannel byte) bit, should be set to 1 when in pregap or postgap.
   if((lba < Tracks[track].LBA) || (lba >= Tracks[track].LBA + Tracks[track].sectors))
   {
      //printf("pause_or = 0x80 --- %d\n", lba);
      pause_or = 0x80;
   }

   // Handle pregap between audio->data track
   {
      int32_t pg_offset = (int32)lba - Tracks[track].LBA;

      // If we're more than 2 seconds(150 sectors) from the real "start" of the track/INDEX 01, and the track is a data track,
      // and the preceding track is an audio track, encode it as audio(by taking the SubQ control field from the preceding track).
      //
      // TODO: Look into how we're supposed to handle subq control field in the four combinations of track types(data/audio).
      //
      if(pg_offset < -150)
      {
         if((Tracks[track].subq_control & SUBQ_CTRLF_DATA) && (FirstTrack < track) && !(Tracks[track - 1].subq_control & SUBQ_CTRLF_DATA))
         {
            //printf("Pregap part 1 audio->data: lba=%d track_lba=%d\n", lba, Tracks[track].LBA);
            control = Tracks[track - 1].subq_control;
         }
      }
   }

   memset(buf, 0, 0xC);
   buf[0] = (adr << 0) | (control << 4);
   buf[1] = U8_to_BCD(track);

   if(lba < Tracks[track].LBA) // Index is 00 in pregap
      buf[2] = U8_to_BCD(0x00);
   else
      buf[2] = U8_to_BCD(0x01);

   /* Track relative MSF address */
   buf[3] = U8_to_BCD(m);
   buf[4] = U8_to_BCD(s);
   buf[5] = U8_to_BCD(f);
   buf[6] = 0;
   /* Absolute MSF address */
   buf[7] = U8_to_BCD(ma);
   buf[8] = U8_to_BCD(sa);
   buf[9] = U8_to_BCD(fa);

   subq_generate_checksum(buf);

   if(!SubQReplaceMap.empty())
   {
      //printf("%d\n", lba);
      std::map<uint32, cpp11_array_doodad>::const_iterator it = SubQReplaceMap.find(LBA_to_ABA(lba));

      if(it != SubQReplaceMap.end())
      {
         //printf("Replace: %d\n", lba);
         memcpy(buf, it->second.data, 12);
      }
   }

   for (i = 0; i < 96; i++)
      SubPWBuf[i] |= (((buf[i >> 3] >> (7 - (i & 0x7))) & 1) ? 0x40 : 0x00) | pause_or;

   return track;
}

bool CDAccess_CHD::Read_Raw_Sector(uint8 *buf, int32 lba)
{
   uint8_t SimuQ[0xC];
   int32_t track;
   CDRFILE_TRACK_INFO *ct;

   //
   // Leadout synthesis
   //
   if (lba >= total_sectors)
   {
      uint8_t data_synth_mode = 0x01; // Default for DISC_TYPE_CDDA_OR_M1, would be 0x02 for DISC_TYPE_CD_XA

      switch (Tracks[LastTrack].DIFormat)
      {
         case DI_FORMAT_AUDIO:
            break;

         case DI_FORMAT_MODE1_RAW:
         case DI_FORMAT_MODE1:
            data_synth_mode = 0x01;
            break;

         case DI_FORMAT_MODE2_RAW:
         case DI_FORMAT_MODE2_FORM1:
         case DI_FORMAT_MODE2_FORM2:
         case DI_FORMAT_MODE2:
            data_synth_mode = 0x02;
            break;
      }
      synth_leadout_sector_lba(data_synth_mode, ptoc, lba, buf);
   }

   memset(buf + 2352, 0, 96);
   track = MakeSubPQ(lba, buf + 2352);
   subq_deinterleave(buf + 2352, SimuQ);

   ct = &Tracks[track];

   //
   // Handle pregap and postgap reading
   //
   if (lba < (ct->LBA - ct->pregap_dv) || lba >= (ct->LBA + ct->sectors))
   {
      int32_t pg_offset = lba - ct->LBA;
      CDRFILE_TRACK_INFO *et = ct;

      if (pg_offset < -150)
      {
         if ((Tracks[track].subq_control & SUBQ_CTRLF_DATA) && (FirstTrack < track) && !(Tracks[track - 1].subq_control & SUBQ_CTRLF_DATA))
         et = &Tracks[track - 1];
      }

      memset(buf, 0, 2352);
      switch (et->DIFormat)
      {
         case DI_FORMAT_AUDIO:
            break;

         case DI_FORMAT_MODE1_RAW:
         case DI_FORMAT_MODE1:
            encode_mode1_sector(lba + 150, buf);
            break;

         case DI_FORMAT_MODE2_RAW:
         case DI_FORMAT_MODE2_FORM1:
         case DI_FORMAT_MODE2_FORM2:
         case DI_FORMAT_MODE2:
            buf[12 + 6] = 0x20;
            buf[12 + 10] = 0x20;
            encode_mode2_form2_sector(lba + 150, buf);
            // TODO: Zero out optional(?) checksum bytes?
            break;
      }
      printf("Pre/post-gap read, LBA=%d(LBA-track_start_LBA=%d)\n", lba, lba - ct->LBA);
   }
   else
   {
      // read CHD hunk
      const chd_header *head = chd_get_header(chd);
      //int cad = (((lba - ct->LBA) * 2352) + ct->FileOffset) / 2352;
      int cad = lba - ct->LBA + ct->FileOffset;
      int sph = head->hunkbytes / (2352 + 96);
      int hunknum = cad / sph; //(cad * head->unitbytes) / head->hunkbytes;
      int hunkofs = cad % sph; //(cad * head->unitbytes) % head->hunkbytes;
      int err = CHDERR_NONE;

      /* each hunk holds ~8 sectors, optimize when reading contiguous sectors */
      if (hunknum != oldhunk)
      {
         err = chd_read(chd, hunknum, hunkmem);
         if (err != CHDERR_NONE)
            log_cb(RETRO_LOG_ERROR, "chd_read_sector failed lba=%d error=%d\n", lba, err);
         else
            oldhunk = hunknum;
      }

      memcpy(buf, hunkmem + hunkofs * (2352 + 96), 2352);

      if (ct->DIFormat == DI_FORMAT_AUDIO && ct->RawAudioMSBFirst)
         Endian_A16_Swap(buf, 588 * 2);
   }
   return true;
}

bool CDAccess_CHD::Read_Raw_PW(uint8_t *buf, int32_t lba)
{
   memset(buf, 0, 96);
   MakeSubPQ(lba, buf);
   return true;
}

bool CDAccess_CHD::Read_TOC(TOC *toc)
{
   TOC_Clear(toc);

   toc->first_track = FirstTrack;
   toc->last_track = LastTrack;
   toc->disc_type = DISC_TYPE_CD_XA;   // always?

   // read track info
   for(int i = 1; i <= NumTracks; i++)
   {
      toc->tracks[i].control = Tracks[i].subq_control;
      toc->tracks[i].adr = ADR_CURPOS;
      toc->tracks[i].lba = Tracks[i].LBA;
   }

   toc->tracks[100].lba = total_sectors;
   toc->tracks[100].adr = ADR_CURPOS;
   toc->tracks[100].control = toc->tracks[toc->last_track].control & 0x4;

   // Convenience leadout track duplication.
   if (toc->last_track < 99)
      toc->tracks[toc->last_track + 1] = toc->tracks[100];

   if (!SubQReplaceMap.empty())
      SubQReplaceMap.clear();

   // Load SBI file, if present
   if (filestream_exists(sbi_path.c_str()))
      LoadSBI(sbi_path.c_str());

   ptoc = toc;
   log_cb(RETRO_LOG_INFO, "chd_read_toc: finished\n");
   return true;
}

int CDAccess_CHD::LoadSBI(const char* sbi_path)
{
   /* Loading SBI file */
   uint8 header[4];
   uint8 ed[4 + 10];
   uint8 tmpq[12];
   FileStream sbis(sbi_path, MODE_READ);

   sbis.read(header, 4);

   if(memcmp(header, "SBI\0", 4))
      return -1;

   while(sbis.read(ed, sizeof(ed)) == sizeof(ed))
   {
      /* Bad BCD MSF offset in SBI file. */
      if(!BCD_is_valid(ed[0]) || !BCD_is_valid(ed[1]) || !BCD_is_valid(ed[2]))
         return -1;

      /* Unrecognized boogly oogly in SBI file */
      if(ed[3] != 0x01)
         return -1;

      memcpy(tmpq, &ed[4], 10);

      subq_generate_checksum(tmpq);
      tmpq[10] ^= 0xFF;
      tmpq[11] ^= 0xFF;

      uint32 aba = AMSF_to_ABA(BCD_to_U8(ed[0]), BCD_to_U8(ed[1]), BCD_to_U8(ed[2]));

      memcpy(SubQReplaceMap[aba].data, tmpq, 12);
   }

#if 0
   MDFN_printf("Loaded Q subchannel replacements for %zu sectors.\n", SubQReplaceMap.size());
#endif
   log_cb(RETRO_LOG_INFO, "[CHD] Loaded SBI file %s\n", sbi_path);
   return 0;
}

void CDAccess_CHD::Eject(bool eject_status)
{

}
