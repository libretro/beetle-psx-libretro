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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <boolean.h>
#include <compat/strl.h>

#include <streams/file_stream.h>
#include <libretro.h>

#include "../mednafen.h"
#include "../error.h"
#include "../general_c.h"

#include "CDAccess.h"
#include "CDAccess_CHD.h"
#include "cdaccess_track.h"
#include "CDUtility.h"

#include <libchdr/chd.h>

extern retro_log_printf_t log_cb;

#define CHD_PATH_BUF 4096

/* ------------------------------------------------------------------
 * Concrete struct.  std::string sbi_path is gone (replaced with a
 * fixed char buffer); std::map<uint32, 12-byte> SubQReplaceMap is
 * replaced with a sorted-array subq_map (cdaccess_track.h).
 * ------------------------------------------------------------------ */

struct CDAccess_CHD
{
   CDAccess     base;

   RFILE              *fp;
   chd_file    *chd;
   uint8_t     *hunkmem;        /* hunk-data cache */
   int          oldhunk;        /* last hunknum read, -1 sentinel */

   int32_t      NumTracks;
   int32_t      FirstTrack;
   int32_t      LastTrack;
   int32_t      total_sectors;
   TOC         *ptoc;

   char         sbi_path[CHD_PATH_BUF];

   CDRFILE_TRACK_INFO Tracks[100];   /* Tracks #0 (HMM?) through 99 */

   subq_map     SubQReplaceMap;
};

/* Disk-image (rip) track/sector formats - kept file-static. */
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

/* libchdr file-IO callbacks - operate directly on a libretro-common
 * RFILE pointer.  The user_data we hand chd_open_core_file_callbacks
 * IS a RFILE *, not a wrapper. */

static uint64_t Callback_fsize(void *user_data)
{
   RFILE  *fp = (RFILE *)user_data;
   int64_t sz = filestream_get_size(fp);
   if (sz < 0)
      return 0;
   return (uint64_t)sz;
}

static size_t Callback_fread(void *buffer, size_t size, size_t count,
      void *user_data)
{
   RFILE  *fp = (RFILE *)user_data;
   int64_t got;
   if (size == 0 || count == 0)
      return 0;

   got = filestream_read(fp, buffer, (int64_t)(count * size));
   if (got < 0)
      return 0;
   return (size_t)got / size;
}

static int Callback_fclose(void *user_data)
{
   (void)user_data;
   return 0;
}

static int Callback_fseek(void *user_data, int64_t offset, int whence)
{
   RFILE *fp           = (RFILE *)user_data;
   int    seek_position = RETRO_VFS_SEEK_POSITION_START;
   switch (whence)
   {
      case SEEK_SET: seek_position = RETRO_VFS_SEEK_POSITION_START;   break;
      case SEEK_CUR: seek_position = RETRO_VFS_SEEK_POSITION_CURRENT; break;
      case SEEK_END: seek_position = RETRO_VFS_SEEK_POSITION_END;     break;
   }
   filestream_seek(fp, offset, seek_position);
   return 0;
}

static const core_file_callbacks chd_callbacks =
{
   Callback_fsize,
   Callback_fread,
   Callback_fclose,
   Callback_fseek
};

/* Forward declaration - LoadSBI is called from Read_TOC. */
static int CDAccess_CHD_LoadSBI(struct CDAccess_CHD *self,
      const char *sbi_path);

/* ------------------------------------------------------------------
 * Body methods.
 * ------------------------------------------------------------------ */

static bool CDAccess_CHD_ImageOpen(struct CDAccess_CHD *self,
      const char *path, bool image_memcache)
{
   const chd_header *head;
   chd_error         err;
   int               plba       = -150;
   uint32_t          fileOffset = 0;
   char              type[64];
   char              subtype[32];
   char              pgtype[32];
   char              pgsub[32];
   char              meta_entry[256];
   uint32_t          meta_entry_size = 0;
   char              base_dir[CHD_PATH_BUF];
   char              file_base[CHD_PATH_BUF];
   char              file_ext[CHD_PATH_BUF];
   char              sbi_ext[4];
   size_t            ext_len;
   int               i;
   char              sbi_basename[CHD_PATH_BUF];

   err = chd_open_core_file_callbacks(&chd_callbacks, self->fp,
         CHD_OPEN_READ, NULL, &self->chd);
   if (err != CHDERR_NONE)
      return false;

   if (image_memcache)
   {
      err = chd_precache(self->chd);
      if (err != CHDERR_NONE)
         return false;
   }

   head = chd_get_header(self->chd);
   self->hunkmem = (uint8_t *)malloc(head->hunkbytes);
   self->oldhunk = -1;

   log_cb(RETRO_LOG_INFO, "chd_load '%s' hunkbytes=%d\n", path,
         head->hunkbytes);

   for (;;)
   {
      int tkid    = 0;
      int frames  = 0;
      int pad     = 0;
      int pregap  = 0;
      int postgap = 0;

      err = chd_get_metadata(self->chd, CDROM_TRACK_METADATA2_TAG,
            self->NumTracks, meta_entry, sizeof(meta_entry),
            &meta_entry_size, NULL, NULL);
      if (err == CHDERR_NONE)
      {
         sscanf(meta_entry, CDROM_TRACK_METADATA2_FORMAT,
               &tkid, type, subtype, &frames, &pregap, pgtype, pgsub,
               &postgap);
      }
      else
      {
         err = chd_get_metadata(self->chd, CDROM_TRACK_METADATA_TAG,
               self->NumTracks, meta_entry, sizeof(meta_entry),
               &meta_entry_size, NULL, NULL);
         if (err == CHDERR_NONE)
         {
            sscanf(meta_entry, CDROM_TRACK_METADATA_FORMAT,
                  &tkid, type, subtype, &frames);
         }
         else
            break;   /* end of TOC */
      }

      if (strncmp(type, "MODE2_RAW", 9) != 0
            && strncmp(type, "AUDIO", 5) != 0)
      {
         log_cb(RETRO_LOG_ERROR, "chd_parse track type %s unsupported\n",
               type);
         return false;
      }
      else if (strncmp(subtype, "NONE", 4) != 0)
      {
         log_cb(RETRO_LOG_ERROR, "chd_parse track subtype %s unsupported\n",
               subtype);
         return false;
      }

      self->NumTracks++;

      if (self->NumTracks != tkid)
         log_cb(RETRO_LOG_WARN,
               "chd tracks are out of order, missing a track or contain a duplicate!\n");

      if (strncmp(type, "MODE2_RAW", 9) == 0)
      {
         self->Tracks[tkid].DIFormat      = DI_FORMAT_MODE2_RAW;
         self->Tracks[tkid].subq_control |= SUBQ_CTRLF_DATA;
      }
      else if (strncmp(type, "AUDIO", 5) == 0)
      {
         self->Tracks[tkid].DIFormat        = DI_FORMAT_AUDIO;
         self->Tracks[tkid].subq_control   &= ~SUBQ_CTRLF_DATA;
         self->Tracks[tkid].RawAudioMSBFirst = true;
      }

      self->Tracks[tkid].pregap    = (tkid == 1) ? 150
                                                 : (pgtype[0] == 'V') ? 0 : pregap;
      self->Tracks[tkid].pregap_dv = (pgtype[0] == 'V') ? pregap : 0;
      plba += self->Tracks[tkid].pregap + self->Tracks[tkid].pregap_dv;
      self->Tracks[tkid].LBA       = plba;
      self->Tracks[tkid].postgap   = postgap;
      self->Tracks[tkid].sectors   = frames - self->Tracks[tkid].pregap_dv;
      self->Tracks[tkid].SubchannelMode = 0;
      self->Tracks[tkid].index[0]  = -1;
      self->Tracks[tkid].index[1]  = 0;

      fileOffset                  += self->Tracks[tkid].pregap_dv;
      self->Tracks[tkid].FileOffset = fileOffset;
      fileOffset                  += frames - self->Tracks[tkid].pregap_dv;
      fileOffset                  += self->Tracks[tkid].postgap;
      fileOffset                  += ((frames + 3) & ~3) - frames;

      plba += frames - self->Tracks[tkid].pregap_dv;
      plba += self->Tracks[tkid].postgap;

      self->total_sectors += (tkid == 1)
            ? frames
            : frames + self->Tracks[tkid].pregap;

      if (tkid < self->FirstTrack)
         self->FirstTrack = tkid;
      if (tkid > self->LastTrack)
         self->LastTrack = tkid;

      (void)pad;
      (void)pgsub;
   }

   /* Build sbi file path: <dir>/<base>.<sbi_ext> where sbi_ext
    * matches the original disc-image extension's case. */
   sbi_ext[0] = 's';
   sbi_ext[1] = 'b';
   sbi_ext[2] = 'i';
   sbi_ext[3] = 0;

   MDFN_GetFilePathComponents_c(path,
         base_dir,  sizeof(base_dir),
         file_base, sizeof(file_base),
         file_ext,  sizeof(file_ext));

   ext_len = strlen(file_ext);
   if (ext_len == 4 && file_ext[0] == '.')
   {
      for (i = 0; i < 3; i++)
      {
         if (file_ext[1 + i] >= 'A' && file_ext[1 + i] <= 'Z')
            sbi_ext[i] += 'A' - 'a';
      }
   }

   /* sbi_basename = file_base + "." + sbi_ext
    * sbi_ext is always 3 chars (case-folded "sbi"); reserve room for
    * ".sbi" + NUL (5 bytes) at the tail so that an oversized file_base
    * gets truncated rather than the suffix. */
   {
      size_t cap  = sizeof(sbi_basename) - 5; /* room for ".sbi\0" */
      size_t blen = strlcpy(sbi_basename, file_base, cap);
      if (blen >= cap)
         blen = cap - 1;
      sbi_basename[blen]     = '.';
      memcpy(sbi_basename + blen + 1, sbi_ext, 4); /* 3 chars + NUL */
   }
   MDFN_EvalFIP_c(base_dir, sbi_basename,
         self->sbi_path, sizeof(self->sbi_path));

   return true;
}

static void CDAccess_CHD_Cleanup(struct CDAccess_CHD *self)
{
   if (self->chd)
      chd_close(self->chd);

   if (self->hunkmem)
      free(self->hunkmem);

   if (self->fp)
   {
      filestream_close(self->fp);
      self->fp = NULL;
   }
}

/* MakeSubPQ ORs the simulated P and Q subchannel data into SubPWBuf. */
static int32_t CDAccess_CHD_MakeSubPQ(struct CDAccess_CHD *self,
      int32_t lba, uint8_t *SubPWBuf)
{
   unsigned        i;
   uint8_t         buf[0xC];
   uint8_t         adr;
   uint8_t         control;
   int32_t         track;
   uint32_t        lba_relative;
   uint32_t        ma, sa, fa;
   uint32_t        m, s, f;
   uint8_t         pause_or = 0x00;
   bool            track_found = false;
   const uint8_t  *sbi_replacement;

   for (track = self->FirstTrack;
         track < (self->FirstTrack + self->NumTracks); track++)
   {
      if (lba >= (self->Tracks[track].LBA - self->Tracks[track].pregap_dv
                  - self->Tracks[track].pregap)
            && lba < (self->Tracks[track].LBA + self->Tracks[track].sectors
                      + self->Tracks[track].postgap))
      {
         track_found = true;
         break;
      }
   }

   if (!track_found)
      track = self->FirstTrack;

   lba_relative = abs((int32_t)lba - self->Tracks[track].LBA);

   f = (lba_relative % 75);
   s = ((lba_relative / 75) % 60);
   m = (lba_relative / 75 / 60);

   fa = (lba + 150) % 75;
   sa = ((lba + 150) / 75) % 60;
   ma = ((lba + 150) / 75 / 60);

   adr     = 0x1;   /* Q channel data encodes position */
   control = self->Tracks[track].subq_control;

   /* Pause bit (D7) - set when in pregap or postgap. */
   if (lba < self->Tracks[track].LBA
         || lba >= self->Tracks[track].LBA + self->Tracks[track].sectors)
      pause_or = 0x80;

   /* Pregap between audio->data track. */
   {
      int32_t pg_offset = (int32_t)lba - self->Tracks[track].LBA;
      if (pg_offset < -150)
      {
         if ((self->Tracks[track].subq_control & SUBQ_CTRLF_DATA)
               && (self->FirstTrack < track)
               && !(self->Tracks[track - 1].subq_control & SUBQ_CTRLF_DATA))
            control = self->Tracks[track - 1].subq_control;
      }
   }

   memset(buf, 0, 0xC);
   buf[0] = (adr << 0) | (control << 4);
   buf[1] = U8_to_BCD(track);

   if (lba < self->Tracks[track].LBA)   /* Index is 00 in pregap */
      buf[2] = U8_to_BCD(0x00);
   else
      buf[2] = U8_to_BCD(0x01);

   /* Track-relative MSF address */
   buf[3] = U8_to_BCD(m);
   buf[4] = U8_to_BCD(s);
   buf[5] = U8_to_BCD(f);
   buf[6] = 0;
   /* Absolute MSF address */
   buf[7] = U8_to_BCD(ma);
   buf[8] = U8_to_BCD(sa);
   buf[9] = U8_to_BCD(fa);

   subq_generate_checksum(buf);

   sbi_replacement = subq_map_find(&self->SubQReplaceMap, LBA_to_ABA(lba));
   if (sbi_replacement)
      memcpy(buf, sbi_replacement, 12);

   for (i = 0; i < 96; i++)
      SubPWBuf[i] |= (((buf[i >> 3] >> (7 - (i & 0x7))) & 1) ? 0x40 : 0x00)
                     | pause_or;

   return track;
}

static bool CDAccess_CHD_Read_Raw_Sector(CDAccess *base_self, uint8_t *buf,
      int32_t lba)
{
   struct CDAccess_CHD *self = (struct CDAccess_CHD *)base_self;
   uint8_t              SimuQ[0xC];
   int32_t              track;
   CDRFILE_TRACK_INFO  *ct;

   /* Leadout synthesis */
   if (lba >= self->total_sectors)
   {
      uint8_t data_synth_mode = 0x01;
      switch (self->Tracks[self->LastTrack].DIFormat)
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
      synth_leadout_sector_lba(data_synth_mode, self->ptoc, lba, buf);
   }

   memset(buf + 2352, 0, 96);
   track = CDAccess_CHD_MakeSubPQ(self, lba, buf + 2352);
   subq_deinterleave(buf + 2352, SimuQ);

   ct = &self->Tracks[track];

   /* Pregap and postgap synthesis */
   if (lba < (ct->LBA - ct->pregap_dv) || lba >= (ct->LBA + ct->sectors))
   {
      int32_t              pg_offset = lba - ct->LBA;
      CDRFILE_TRACK_INFO  *et         = ct;

      if (pg_offset < -150)
      {
         if ((self->Tracks[track].subq_control & SUBQ_CTRLF_DATA)
               && (self->FirstTrack < track)
               && !(self->Tracks[track - 1].subq_control & SUBQ_CTRLF_DATA))
            et = &self->Tracks[track - 1];
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
            buf[12 + 6]  = 0x20;
            buf[12 + 10] = 0x20;
            encode_mode2_form2_sector(lba + 150, buf);
            break;
      }
   }
   else
   {
      const chd_header *head    = chd_get_header(self->chd);
      int               cad     = lba - ct->LBA + ct->FileOffset;
      int               sph     = head->hunkbytes / (2352 + 96);
      int               hunknum = cad / sph;
      int               hunkofs = cad % sph;
      int               err     = CHDERR_NONE;

      /* Each hunk holds ~8 sectors; cache the most-recently-read one. */
      if (hunknum != self->oldhunk)
      {
         err = chd_read(self->chd, hunknum, self->hunkmem);
         if (err != CHDERR_NONE)
            log_cb(RETRO_LOG_ERROR,
                  "chd_read_sector failed lba=%d error=%d\n", lba, err);
         else
            self->oldhunk = hunknum;
      }

      memcpy(buf, self->hunkmem + hunkofs * (2352 + 96), 2352);

      /* Path 2 contract: buf holds host-endian int16 stereo
       * samples. Swap iff source byte order differs from host
       * byte order. CHD AUDIO tracks are always BE-stored
       * (RawAudioMSBFirst = true). */
      if (ct->DIFormat == DI_FORMAT_AUDIO
#ifdef MSB_FIRST
            && !ct->RawAudioMSBFirst
#else
            && ct->RawAudioMSBFirst
#endif
         )
      {
         /* 32-bit-chunked A16 swap; see CDAccess_Image.c
          * counterpart for the rationale. 588 iterations
          * over 2352 bytes of stereo 16-bit samples. */
         uint8_t *_s = (uint8_t *)buf;
         int32_t  _i;
         for (_i = 0; _i + 3 < 588 * 2 * 2; _i += 4)
         {
            uint32_t _v;
            memcpy(&_v, _s + _i, 4);
            _v = ((_v & 0xFF00FF00U) >> 8) | ((_v & 0x00FF00FFU) << 8);
            memcpy(_s + _i, &_v, 4);
         }
      }
   }
   return true;
}

static bool CDAccess_CHD_Read_Raw_PW(CDAccess *base_self, uint8_t *buf,
      int32_t lba)
{
   struct CDAccess_CHD *self = (struct CDAccess_CHD *)base_self;
   memset(buf, 0, 96);
   CDAccess_CHD_MakeSubPQ(self, lba, buf);
   return true;
}

static bool CDAccess_CHD_Read_TOC(CDAccess *base_self, TOC *toc)
{
   struct CDAccess_CHD *self = (struct CDAccess_CHD *)base_self;
   int                  i;

   TOC_Clear(toc);

   toc->first_track = self->FirstTrack;
   toc->last_track  = self->LastTrack;
   toc->disc_type   = DISC_TYPE_CD_XA;

   for (i = 1; i <= self->NumTracks; i++)
   {
      toc->tracks[i].control = self->Tracks[i].subq_control;
      toc->tracks[i].adr     = ADR_CURPOS;
      toc->tracks[i].lba     = self->Tracks[i].LBA;
   }

   toc->tracks[100].lba     = self->total_sectors;
   toc->tracks[100].adr     = ADR_CURPOS;
   toc->tracks[100].control = toc->tracks[toc->last_track].control & 0x4;

   /* Convenience leadout track duplication. */
   if (toc->last_track < 99)
      toc->tracks[toc->last_track + 1] = toc->tracks[100];

   subq_map_clear(&self->SubQReplaceMap);

   /* Load SBI file, if present. */
   if (filestream_exists(self->sbi_path))
      CDAccess_CHD_LoadSBI(self, self->sbi_path);

   self->ptoc = toc;
   log_cb(RETRO_LOG_INFO, "chd_read_toc: finished\n");
   return true;
}

static int CDAccess_CHD_LoadSBI(struct CDAccess_CHD *self,
      const char *sbi_path)
{
   uint8_t header[4];
   uint8_t ed[4 + 10];
   uint8_t tmpq[12];
   RFILE  *sbis;
   int     ret = 0;

   sbis = filestream_open(sbi_path,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!sbis)
      return -1;

   filestream_read(sbis, header, 4);

   if (memcmp(header, "SBI\0", 4))
   {
      ret = -1;
      goto cleanup;
   }

   while (filestream_read(sbis, ed, sizeof(ed)) == sizeof(ed))
   {
      uint32_t aba;

      if (!BCD_is_valid(ed[0]) || !BCD_is_valid(ed[1])
            || !BCD_is_valid(ed[2]))
      {
         ret = -1;
         goto cleanup;
      }

      if (ed[3] != 0x01)
      {
         ret = -1;
         goto cleanup;
      }

      memcpy(tmpq, &ed[4], 10);

      subq_generate_checksum(tmpq);
      tmpq[10] ^= 0xFF;
      tmpq[11] ^= 0xFF;

      aba = AMSF_to_ABA(BCD_to_U8(ed[0]),
            BCD_to_U8(ed[1]), BCD_to_U8(ed[2]));
      subq_map_insert(&self->SubQReplaceMap, aba, tmpq);
   }

   subq_map_finalize(&self->SubQReplaceMap);

   log_cb(RETRO_LOG_INFO, "[CHD] Loaded SBI file %s\n", sbi_path);
cleanup:
   filestream_close(sbis);
   return ret;
}

static void CDAccess_CHD_Eject(CDAccess *base_self, bool eject_status)
{
   (void)base_self;
   (void)eject_status;
}

static void CDAccess_CHD_destroy(CDAccess *base_self)
{
   struct CDAccess_CHD *self = (struct CDAccess_CHD *)base_self;
   CDAccess_CHD_Cleanup(self);
   free(self);
}

/* ------------------------------------------------------------------
 * Factory.
 * ------------------------------------------------------------------ */

CDAccess *CDAccess_CHD_New(bool *success, const char *path,
      bool image_memcache)
{
   struct CDAccess_CHD *self =
      (struct CDAccess_CHD *)calloc(1, sizeof(*self));
   if (!self)
   {
      *success = false;
      return NULL;
   }

   /* Vtable */
   self->base.Read_Raw_Sector = CDAccess_CHD_Read_Raw_Sector;
   self->base.Read_Raw_PW     = CDAccess_CHD_Read_Raw_PW;
   self->base.Read_TOC        = CDAccess_CHD_Read_TOC;
   self->base.Eject           = CDAccess_CHD_Eject;
   self->base.destroy         = CDAccess_CHD_destroy;

   self->chd           = NULL;
   self->NumTracks     = 0;
   self->total_sectors = 0;
   self->FirstTrack    = 99;   /* opposites for min/max init */
   self->LastTrack     = 0;

   self->fp = filestream_open(path,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);

   if (!self->fp)
   {
      MDFN_Error(0, "CHD: failed to open \"%s\"", path);
      *success = false;
      return &self->base;
   }

   if (!CDAccess_CHD_ImageOpen(self, path, image_memcache))
      *success = false;

   return &self->base;
}
