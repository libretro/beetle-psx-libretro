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

#include <retro_stat.h>

#include "../mednafen.h"

#include <sys/types.h>

#include <string.h>
#include <errno.h>
#include <time.h>

#include "../general.h"
#include "../FileStream.h"
#include "../MemoryStream.h"

#include "CDAccess.h"
#include "CDAccess_PBP.h"
#include "CDUtility.h"

#include "audioreader.h"

#include "../../libretro.h"

#include "zlib.h"

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

   // FIXME: These are just guesses:
   "MODE2/2336",
   "MODE2/2048",
   "MODE2/2324",
   "MODE2/2352"
};

void CDAccess_PBP::ImageOpen(const char *path, bool image_memcache)
{
log_cb(RETRO_LOG_DEBUG, "[PBP] Opening %s...\n", path);
   if(image_memcache)
      fp = new MemoryStream(new FileStream(path, MODE_READ));
   else
      fp = new FileStream(path, MODE_READ);

   uint8 magic[4];
   char psar_sig[12];

   // check for valid pbp
   if(fp->read(magic, 4, false) != 4 || magic[0] != 0 || magic[1] != 'P' || magic[2] != 'B' || magic[3] != 'P')
      throw(MDFN_Error(0, _("Invalid PBP header: %s"), path));

   // only data.psar is relevant
   fp->seek(0x24, SEEK_SET);
   psar_offset = fp->get_LE<uint32>();
   psisoimg_offset = psar_offset;
   fp->seek(psar_offset, SEEK_SET);

   fp->read(psar_sig, sizeof(psar_sig));
   if(strncmp(psar_sig, "PSTITLEIMG00", sizeof(psar_sig)) == 0)
   {
      // check for multidisk image
      fp->seek(psar_offset + 0x200, SEEK_SET);

      for(int i = 0; i < 6; i++)
      {
         discs_start_offset[i] = fp->get_LE<uint32>();
         if(discs_start_offset[i] == 0)
         {
            disc_count = i;
            break;
         }
log_cb(RETRO_LOG_DEBUG, "[PBP] DISC[%i] offset = %#x\n", i, psar_offset+discs_start_offset[i]);
      }

      if(disc_count == 0)
         throw(MDFN_Error(0, _("Multidisk eboot has 0 images?: %s"), path));

      // TODO: figure out a way to integrate multi-discs with retroarch (just a matter of storing the currently selected disc and seeking to the according offset on Read_TOC)

      // default to first disc on loading
      current_disc = 0;
      psisoimg_offset += discs_start_offset[0];
      fp->seek(psisoimg_offset, SEEK_SET);

      fp->read(psar_sig, sizeof(psar_sig));
   }

   if(strncmp(psar_sig, "PSISOIMG0000", sizeof(psar_sig)) != 0)
      throw(MDFN_Error(0, _("Unexpected psar_sig: %s"), path));

   // check for "\0PGD" @psisoimg_offset+0x400, should indicate whether TOC is encrypted or not?
   fp->seek(psisoimg_offset+0x400, SEEK_SET);
   fp->read(magic, 4);
   if(magic[0] == 0 && magic[1] == 'P' && magic[2] == 'G' && magic[3] == 'D')
      throw(MDFN_Error(0, _("%s seems to contain an encrypted TOC (unsupported atm), bailing out"), path));
}

void CDAccess_PBP::Cleanup(void)
{
   if(fp != NULL)
   {
      fp->close();   // need to manually close for FileStreams?
      delete fp;
   }
   if(index_table != NULL)
      free(index_table);
}

CDAccess_PBP::CDAccess_PBP(const char *path, bool image_memcache) : NumTracks(0), FirstTrack(0), LastTrack(0), total_sectors(0)
{
   index_table = NULL;
   fp = NULL;
   ImageOpen(path, image_memcache);
   // TODO: check for .sbi files in same directory and load them with LoadSBI()
}

CDAccess_PBP::~CDAccess_PBP()
{
   Cleanup();
}

int CDAccess_PBP::uncompress2(void *out, uint32_t *out_size, void *in, uint32_t in_size)
{
   static z_stream z;
   int ret = 0;

   if (z.zalloc == NULL) {
      // XXX: one-time leak here..
      z.next_in = Z_NULL;
      z.avail_in = 0;
      z.zalloc = Z_NULL;
      z.zfree = Z_NULL;
      z.opaque = Z_NULL;
      ret = inflateInit2(&z, -15);
   }
   else
      ret = inflateReset(&z);
   if (ret != Z_OK)
      return ret;

   z.next_in = (Bytef*)in;
   z.avail_in = in_size;
   z.next_out = (Bytef*)out;
   z.avail_out = *out_size;

   ret = inflate(&z, Z_NO_FLUSH);
   //inflateEnd(&z);

   *out_size -= z.avail_out;
   return ret == 1 ? 0 : ret;
}

// Note: this function makes use of the current contents(as in |=) in SubPWBuf.
void CDAccess_PBP::MakeSubPQ(int32 lba, uint8 *SubPWBuf)
{
   unsigned i;
   uint8_t buf[0xC], adr, control;
   int32_t track;
   uint32_t lba_relative;
   uint32_t ma, sa, fa;
   uint32_t m, s, f;
   uint8_t pause_or = 0x00;
   bool track_found = FALSE;

   for(track = FirstTrack; track < (FirstTrack + NumTracks); track++)
   {
      if(lba >= (Tracks[track].LBA - Tracks[track].pregap_dv - Tracks[track].pregap) && lba < (Tracks[track].LBA + Tracks[track].sectors + Tracks[track].postgap))
      {
         track_found = TRUE;
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
}

void CDAccess_PBP::Read_Raw_Sector(uint8 *buf, int32 lba)
{
   uint8_t SimuQ[0xC];

   memset(buf + 2352, 0, 96);
   MakeSubPQ(lba, buf + 2352);
   subq_deinterleave(buf + 2352, SimuQ);

   int32_t block = lba >> 4;
   sector_in_blk = lba & 0xf;

//log_cb(RETRO_LOG_DEBUG, "[PBP] lba = %d, sector_in_blk = %u, block = %d, current_block = %u\n", lba, sector_in_blk, block, current_block);

   if (block == current_block)
   {
      //printf("hit sect %d\n", lba);
   }
   else
   {
      if (lba >= index_len * 16)
      {
         log_cb(RETRO_LOG_ERROR, "[PBP] sector %d is past img end\n", lba);
         return;
      }

      uint32_t start_byte = index_table[block] & 0x7fffffff;
      fp->seek(start_byte, SEEK_SET);

      int32_t is_compressed = !(index_table[block] & 0x80000000);  // this is always != 0, perhaps check the first byte in the image at index_table[block] instead?
      uint32_t size = (index_table[block + 1] & 0x7fffffff) - start_byte;
      if (size > sizeof(buff_compressed))
      {
         log_cb(RETRO_LOG_ERROR, "[PBP] block %d is too large: %u\n", block, size);
         return;
      }

      fp->read(is_compressed ? buff_compressed : buff_raw[0], size);

//log_cb(RETRO_LOG_DEBUG, "block = %u, start_byte = %#x, index_table[%i] = %#x\n", block, start_byte, block, index_table[block]);

      if (is_compressed)
      {
         uint32_t cdbuffer_size_expect = sizeof(buff_raw[0]) << 4;
         uint32_t cdbuffer_size = cdbuffer_size_expect;
         int ret = uncompress2(buff_raw[0], &cdbuffer_size, buff_compressed, size);
         if (ret != 0)
         {
            log_cb(RETRO_LOG_ERROR, "[PBP] uncompress failed with %d for block %d, sector %d (%u)\n", ret, block, lba, size);
            return;
         }
         if (cdbuffer_size != cdbuffer_size_expect)
         {
            log_cb(RETRO_LOG_WARN, "[PBP] cdbuffer_size: %lu != %lu, sector %d\n", cdbuffer_size, cdbuffer_size_expect, lba);
            return;
         }
      }
      current_block = block;
   }
   memcpy(buf, buff_raw[sector_in_blk], CD_FRAMESIZE_RAW);
}

void CDAccess_PBP::Read_TOC(TOC *toc)
{
   struct {
      uint8_t type;
      uint8_t pad0;
      uint8_t track;
      uint8_t index0[3];
      uint8_t pad1;
      uint8_t index1[3];
   } toc_entry;

   struct {
      uint32_t offset;
      uint16_t size;
      uint16_t marker;
      uint8_t checksum[16];
      uint8_t padding[8];
   } index_entry;

   int i;
   TOC_Clear(toc);

   // initialize opposites
   FirstTrack = 99;
   LastTrack = 0;

   // seek to TOC
   fp->seek(psisoimg_offset + 0x800, SEEK_SET);

   // first three entries are special
   fp->seek(sizeof(toc_entry), SEEK_CUR);
   fp->read(&toc_entry, sizeof(toc_entry));
   NumTracks = BCD_to_U8(toc_entry.index1[0]);

   // total length
   fp->read(&toc_entry, sizeof(toc_entry));
   total_sectors = (BCD_to_U8(toc_entry.index1[0])*60 + BCD_to_U8(toc_entry.index1[1])) * 75 + BCD_to_U8(toc_entry.index1[2]);

log_cb(RETRO_LOG_DEBUG, "[PBP] psisoimg_offset = %#x, Numtracks = %d, total_sectors = %d\n", psisoimg_offset, NumTracks, total_sectors);

   // read track info
   for(i = 1; i <= NumTracks; i++)
   {
      fp->read(&toc_entry, sizeof(toc_entry));

      if(toc_entry.track < FirstTrack)
         FirstTrack = BCD_to_U8(toc_entry.track);
      if(toc_entry.track > LastTrack)
         LastTrack = BCD_to_U8(toc_entry.track);

      if(toc_entry.type == 1)
      {
         Tracks[i].DIFormat = DI_FORMAT_AUDIO;
         Tracks[i].subq_control &= ~SUBQ_CTRLF_DATA;
      }
      else  // TOCHECK: are there any psx games that have other formats than AUDIO and MODE2/2352?
      {
         Tracks[i].DIFormat = DI_FORMAT_MODE2_RAW;
         Tracks[i].subq_control |= SUBQ_CTRLF_DATA;
      }

      Tracks[i].index[0] = (BCD_to_U8(toc_entry.index0[0])*60 + BCD_to_U8(toc_entry.index0[1])) * 75 + BCD_to_U8(toc_entry.index0[2]);
      Tracks[i].index[1] = (BCD_to_U8(toc_entry.index1[0])*60 + BCD_to_U8(toc_entry.index1[1])) * 75 + BCD_to_U8(toc_entry.index1[2]);

      // are these correct?
      Tracks[i].LBA = ABA_to_LBA(Tracks[i].index[1]);
      if(i > 1)
         Tracks[i-1].sectors = Tracks[i].LBA - Tracks[i-1].LBA;
#if 0
      Tracks[i].pregap = Tracks[i].index[0];
      if(i > 1)
         Tracks[i-1].postgap = Tracks[i].index[0] - Tracks[i-1].index[1];
#else
      Tracks[i].pregap = Tracks[i].postgap = 0;
#endif
      Tracks[i].pregap_dv = Tracks[i].index[1]-Tracks[i].index[0];
      if(Tracks[i].pregap_dv < 0)
         Tracks[i].pregap_dv = 0;

      if(i == NumTracks)
      {
            Tracks[i].sectors = total_sectors - Tracks[i-1].LBA;
#if 0
            Tracks[i].postgap = total_sectors - Tracks[i-1].index[1];
#endif
      }
      toc->tracks[i].control = Tracks[i].subq_control;
      toc->tracks[i].adr = ADR_CURPOS;
      toc->tracks[i].lba = Tracks[i].LBA;

      log_cb(RETRO_LOG_DEBUG, "[PBP] track[%i]: %s, lba = %i, adr = %i, control = %i, index[0] = %i, index[1] = %i\n", BCD_to_U8(toc_entry.track), DI_CUE_Strings[Tracks[i].DIFormat], toc->tracks[i].lba, toc->tracks[i].adr, toc->tracks[i].control, Tracks[i].index[0], Tracks[i].index[1]);

      if(BCD_to_U8(toc_entry.track) < i || BCD_to_U8(toc_entry.track) > i)
         throw(MDFN_Error(0, _("Tracks out of order")));   // can this happen?
   }

   toc->first_track = FirstTrack;
   toc->last_track = LastTrack;
   toc->disc_type = DISC_TYPE_CD_XA;   // always?

   // seek to ISO disc map table
   fp->seek(psisoimg_offset + 0x4000, SEEK_SET);

   // set class variables
   current_block = (uint32_t)-1;
   index_len = (0x100000 - 0x4000) / sizeof(index_entry);   // disc map table has a fixed size of 0xfc000

   if(index_table != NULL)
      free(index_table);

   index_table = (unsigned int*)malloc((index_len + 1) * sizeof(*index_table));
   if (index_table == NULL)
      throw(MDFN_Error(0, _("Unable to allocate memory")));

   uint32_t cdimg_base = psisoimg_offset + 0x100000;
   for (i = 0; i < index_len; i++)
   {
      // TOCHECK: does struct reading (with entries that could be affected by endianness) work reliably between different platforms?
      fp->read(&index_entry, sizeof(index_entry));

      if (index_entry.size == 0)
         break;

      index_table[i] = cdimg_base + index_entry.offset;
   }
   index_table[i] = cdimg_base + index_entry.offset + index_entry.size;

   toc->tracks[100].lba = total_sectors;
   toc->tracks[100].adr = ADR_CURPOS;
   toc->tracks[100].control = toc->tracks[toc->last_track].control & 0x4;

   // Convenience leadout track duplication.
   if(toc->last_track < 99)
      toc->tracks[toc->last_track + 1] = toc->tracks[100];

log_cb(RETRO_LOG_DEBUG, "[PBP] tracks: first = %i, last = %i, disc_type = %i, total_sectors = %i\n", toc->first_track, toc->last_track, toc->disc_type, total_sectors);
}

int CDAccess_PBP::LoadSBI(const char* sbi_path)
{
   /* Loading SBI file */
   uint8 header[4];
   uint8 ed[4 + 10];
   uint8 tmpq[12];
   FileStream sbis(sbi_path, MODE_READ);

   sbis.read(header, 4);

   if(memcmp(header, "SBI\0", 4))
      return -1;

   while(sbis.read(ed, sizeof(ed), false) == sizeof(ed))
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

   //MDFN_printf(_("Loaded Q subchannel replacements for %zu sectors.\n"), SubQReplaceMap.size());

   return 0;
}

void CDAccess_PBP::Eject(bool eject_status)
{

}
