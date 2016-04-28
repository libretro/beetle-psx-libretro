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

#include <map>


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
log_cb(RETRO_LOG_DEBUG, "[PBP] ACCESSING %s\n", path);
   fp = new MemoryStream(new FileStream(path, MODE_READ));

   unsigned int i;
   uint8 magic[4];
   char psar_sig[12];

   // check for valid pbp
   if(fp->read(magic, 4, false) != 4 || magic[3] != 'P' || magic[2] != 'B' || magic[1] != 'P' || magic[0] != 0)
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

      for(i = 0; i < 5; i++)
      {
         discs_start_offset[i] = fp->get_LE<uint32>();
         if(discs_start_offset[i] == 0)
            break;
log_cb(RETRO_LOG_DEBUG, "[PBP] DISC[%i] offset = %#x\n", i, psar_offset+discs_start_offset[i]);
      }
      if(i == 0)
         throw(MDFN_Error(0, _("Multidisk eboot has 0 images?: %s"), path));

      // default to first disc on loading
      psisoimg_offset += discs_start_offset[0];
      fp->seek(psisoimg_offset, SEEK_SET);

      fp->read(psar_sig, sizeof(psar_sig));
   }

   if(strncmp(psar_sig, "PSISOIMG0000", sizeof(psar_sig)) != 0)
      throw(MDFN_Error(0, _("Unexpected psar_sig: %s"), path));

log_cb(RETRO_LOG_DEBUG, "[PBP] Done with ImageOpen()\n");
}

void CDAccess_PBP::Cleanup(void)
{
   if(fp != NULL)
      delete fp;
   if(index_table != NULL)
      free(index_table);
}

CDAccess_PBP::CDAccess_PBP(const char *path, bool image_memcache) : NumTracks(0), FirstTrack(0), LastTrack(0), total_sectors(0)
{
   ImageOpen(path, image_memcache);
}

CDAccess_PBP::~CDAccess_PBP()
{
   Cleanup();
}

int CDAccess_PBP::uncompress2(void *out, unsigned long *out_size, void *in, unsigned long in_size)
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

void CDAccess_PBP::Read_Raw_Sector(uint8 *buf, int32 lba)
{
   memset(buf + 2352, 0, 96);

   int block = lba >> 4;
   sector_in_blk = lba & 0xf;

log_cb(RETRO_LOG_DEBUG, "[PBP] lba = %d, sector_in_blk = %u, block = %d, current_block = %u\n", lba, sector_in_blk, block, current_block);

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

      unsigned int start_byte = index_table[block] & 0x7fffffff;
      fp->seek(start_byte, SEEK_SET);

      int is_compressed = !(index_table[block] & 0x80000000);  // this is always != 0, perhaps check the first byte in the image at index_table[block] instead?
      unsigned int size = (index_table[block + 1] & 0x7fffffff) - start_byte;
      if (size > sizeof(buff_compressed))
      {
         log_cb(RETRO_LOG_ERROR, "[PBP] block %d is too large: %u\n", block, size);
         return;
      }

      fp->read(is_compressed ? buff_compressed : buff_raw[0], size);

log_cb(RETRO_LOG_DEBUG, "block = %u, start_byte = %#x, index_table[%i] = %#x\n", block, start_byte, block, index_table[block]);

      if (is_compressed)
      {
         unsigned long cdbuffer_size_expect = sizeof(buff_raw[0]) << 4;
         unsigned long cdbuffer_size = cdbuffer_size_expect;
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

      // done at last!
      current_block = block;
   }
   memcpy(buf, buff_raw[sector_in_blk], CD_FRAMESIZE_RAW);
}

void CDAccess_PBP::Read_TOC(TOC *toc)
{
log_cb(RETRO_LOG_DEBUG, "[PBP] Read_TOC() was called\n");

struct {
   unsigned char type;
   unsigned char pad0;
   unsigned char track;
   unsigned char index0[3];
   unsigned char pad1;
   unsigned char index1[3];
} toc_entry;
struct {
   unsigned int offset;
   unsigned short size;
   unsigned short marker;
   unsigned char checksum[16];
   unsigned char padding[8];
} index_entry;

uint32_t DIFormat;
int i;
TOC_Clear(toc);

   // initialize opposites
   toc->first_track = 99;
   toc->last_track = 0;

   toc->disc_type = DISC_TYPE_CD_XA;   // always?

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

      if(toc_entry.track < toc->first_track)
         FirstTrack = BCD_to_U8(toc_entry.track);
      if(toc_entry.track > toc->last_track)
         LastTrack = BCD_to_U8(toc_entry.track);

      if(toc_entry.type == 1)
      {
         DIFormat = DI_FORMAT_AUDIO;
         toc->tracks[i].control &= ~SUBQ_CTRLF_DATA;
      }
      else  // TOCHECK: are there any psx games that have other formats than AUDIO and MODE2/2352?
      {
         DIFormat = DI_FORMAT_MODE2_RAW;
         toc->tracks[i].control |= SUBQ_CTRLF_DATA;
      }
      toc->tracks[i].adr = ADR_CURPOS;  // is this correct?

      int32 index[2];
      index[0] = (BCD_to_U8(toc_entry.index0[0])*60 + BCD_to_U8(toc_entry.index0[1])) * 75 + BCD_to_U8(toc_entry.index0[2]);
      index[1] = (BCD_to_U8(toc_entry.index1[0])*60 + BCD_to_U8(toc_entry.index1[1])) * 75 + BCD_to_U8(toc_entry.index1[2]);

      // is index0 required for something?
      toc->tracks[i].lba = ABA_to_LBA(index[1]);

log_cb(RETRO_LOG_DEBUG, "[PBP] track[%i]: %s, lba = %i, adr = %i, control = %i\n", BCD_to_U8(toc_entry.track), DI_CUE_Strings[DIFormat], toc->tracks[i].lba, toc->tracks[i].adr, toc->tracks[i].control);
      if(BCD_to_U8(toc_entry.track) < i || BCD_to_U8(toc_entry.track) > i)
         throw(MDFN_Error(0, _("Tracks out of order")));   // can this happen?
   }
   toc->first_track = FirstTrack;
   toc->last_track = LastTrack;

   // seek to ISO disc map table
   fp->seek(psisoimg_offset + 0x4000, SEEK_SET);

   // set class variables
   current_block = (unsigned int)-1;

   // number of indices (disc map table has a fixed size of 0xfc000)
   index_len = (0x100000 - 0x4000) / sizeof(index_entry);
   index_table = (unsigned int*)malloc((index_len + 1) * sizeof(*index_table));
   if (index_table == NULL)
      throw(MDFN_Error(0, _("Unable to allocate memory")));

   uint32 cdimg_base = psisoimg_offset + 0x100000;
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

void CDAccess_PBP::Eject(bool eject_status)
{

}
