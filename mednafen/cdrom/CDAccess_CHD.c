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
#include <retro_dirent.h>
#include <file/file_path.h>
#include <libretro.h>

#include "../mednafen.h"
#include "../error.h"
#include "../general.h"

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

   chd_file    *chd;
   uint8_t     *hunkmem;        /* hunk-data cache */
   int          oldhunk;        /* last hunknum read, -1 sentinel */

   /* Parent (clone) CHD chain depth guard. A child CHD references
    * unchanged data in a parent file; a parent can itself be a child.
    * Parent chd_files and their backing files are owned by the child
    * chd_file (libchdr closes the whole chain in chd_close), so no
    * handle tracking lives here - only the recursion bound. */
#define CHD_MAX_PARENTS 8

   int32_t      NumTracks;
   int32_t      FirstTrack;
   int32_t      LastTrack;
   int32_t      total_sectors;
   TOC         *ptoc;

   char         sbi_path[CHD_PATH_BUF];

   /* Set when the CHD carries real subchannel data (RW / RW_RAW); when
    * true the per-sector read uses the recorded subchannel instead of
    * the synthesized one, which is what LibCrypt-protected discs need. */
   bool         has_subchannel;

   CDRFILE_TRACK_INFO Tracks[100];   /* Tracks #0 (HMM?) through 99 */

   subq_map     SubQReplaceMap;
};

/* Disk-image (rip) track/sector formats - kept file-static. */
enum
{
   CDRF_SUBM_NONE = 0,
   CDRF_SUBM_RW,
   CDRF_SUBM_RW_RAW
};

/* Field-width-limited copies of libchdr's CDROM_TRACK_METADATA*_FORMAT.
 * The upstream macros use bare %s with no width, so a crafted CHD whose
 * TYPE/SUBTYPE/PGTYPE/PGSUB metadata strings exceed the destination
 * buffers (type[64], subtype/pgtype/pgsub[32]) overflows them. The
 * widths below are sizeof(dest)-1 and keep sscanf from writing past the
 * buffers regardless of the metadata contents. */
#define CHD_TRACK_METADATA_FMT_SAFE \
   "TRACK:%d TYPE:%63s SUBTYPE:%31s FRAMES:%d"
#define CHD_TRACK_METADATA2_FMT_SAFE \
   "TRACK:%d TYPE:%63s SUBTYPE:%31s FRAMES:%d PREGAP:%d PGTYPE:%31s PGSUB:%31s POSTGAP:%d"

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

/* libchdr file IO - a heap-allocated core_file whose argp is a
 * libretro-common RFILE.  Ownership is linear and fully delegated:
 * every chd_open_core_file() call consumes the core_file (and any
 * parent chd_file handed in) whether it succeeds or fails - on
 * success the chd_file owns them and chd_close() releases the whole
 * chain (libchdr closes parents recursively and core_fclose()s each
 * file); on failure libchdr's cleanup path has already released
 * them.  The fclose shim therefore closes the RFILE and frees the
 * wrapper itself, and nothing here tracks parent handles. */

static uint64_t Callback_fsize(core_file *cf)
{
   RFILE  *fp = (RFILE *)cf->argp;
   int64_t sz = filestream_get_size(fp);
   if (sz < 0)
      return (uint64_t)-1;
   return (uint64_t)sz;
}

static size_t Callback_fread(void *buffer, size_t size, size_t count,
      core_file *cf)
{
   RFILE  *fp = (RFILE *)cf->argp;
   int64_t got;
   if (size == 0 || count == 0)
      return 0;

   got = filestream_read(fp, buffer, (int64_t)(count * size));
   if (got < 0)
      return 0;
   return (size_t)got / size;
}

static int Callback_fclose(core_file *cf)
{
   filestream_close((RFILE *)cf->argp);
   free(cf);
   return 0;
}

static int Callback_fseek(core_file *cf, int64_t offset, int whence)
{
   RFILE *fp            = (RFILE *)cf->argp;
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

/* Open `path` through the VFS and wrap it as a core_file.  NULL on
 * open failure.  Successful callers hand the result to libchdr,
 * which releases it via the fclose shim; a caller done with it
 * before that must core_fclose() it. */
static core_file *chd_core_rfile_open(const char *path)
{
   core_file *cf;
   RFILE     *fp = filestream_open(path,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);
   if (!fp)
      return NULL;

   cf = (core_file *)calloc(1, sizeof(*cf));
   if (!cf)
   {
      filestream_close(fp);
      return NULL;
   }
   cf->argp   = fp;
   cf->fsize  = Callback_fsize;
   cf->fread  = Callback_fread;
   cf->fclose = Callback_fclose;
   cf->fseek  = Callback_fseek;
   return cf;
}

/* Forward declaration - LoadSBI is called from Read_TOC. */
static int CDAccess_CHD_LoadSBI(struct CDAccess_CHD *self,
      const char *sbi_path);

/* ------------------------------------------------------------------
 * Body methods.
 * ------------------------------------------------------------------ */

/* Read just the CHD header of a file (no full open, no parent needed),
 * so we can inspect its SHA1 / parent-SHA1 while hunting for a parent. */
static bool chd_peek_header(const char *path, chd_header *out)
{
   chd_error  err;
   core_file *cf = chd_core_rfile_open(path);

   if (!cf)
      return false;

   /* chd_read_header_core_file does not consume the file. */
   err = chd_read_header_core_file(cf, out);
   core_fclose(cf);
   return err == CHDERR_NONE;
}

/* True if the header indicates the file is a child that needs a parent. */
static bool chd_header_needs_parent(const chd_header *h)
{
   static const uint8_t nullsha1[CHD_SHA1_BYTES] = { 0 };
   if (h->version < 5)
      return (h->flags & CHDFLAGS_HAS_PARENT) != 0;
   return memcmp(nullsha1, h->parentsha1, sizeof(h->parentsha1)) != 0;
}

/* Search dir for a .chd whose own SHA1 matches want_parentsha1 (the
 * child's parentsha1). Returns true and fills found_path on a match.
 * skip_path is the child itself, never a candidate for its own parent. */
static bool chd_find_parent_in_dir(const char *dir,
      const uint8_t *want_parentsha1, const char *skip_path,
      char *found_path, size_t found_path_len)
{
   struct RDIR *rdir = retro_opendir(dir);
   bool         ok   = false;

   if (!rdir)
      return false;

   while (retro_readdir(rdir))
   {
      const char *name = retro_dirent_get_name(rdir);
      const char *ext;
      char        cand[CHD_PATH_BUF];
      chd_header  ch;

      if (!name || retro_dirent_is_dir(rdir, NULL))
         continue;

      ext = path_get_extension(name);
      if (!ext || strcasecmp(ext, "chd"))
         continue;

      fill_pathname_join(cand, dir, name, sizeof(cand));

      /* Don't consider the child file itself. */
      if (!strcmp(cand, skip_path))
         continue;

      if (!chd_peek_header(cand, &ch))
         continue;

      /* libchdr validates a parent by comparing the child's parentsha1
       * against the parent's (combined) sha1; match the same way. */
      if (!memcmp(ch.sha1, want_parentsha1, CHD_SHA1_BYTES))
      {
         strlcpy(found_path, cand, found_path_len);
         ok = true;
         break;
      }
   }

   retro_closedir(rdir);
   return ok;
}

/* Open a CHD, resolving any parent chain by searching base_dir. On
 * success returns CHDERR_NONE with *out_chd set; the parent chd_files
 * (and every backing file) are owned by the returned child and are
 * released by a single chd_close on it. depth guards against cycles /
 * absurd chains.
 *
 * Ownership through libchdr is linear: chd_open_core_file consumes
 * its core_file AND its parent argument on failure as well as on
 * success (its cleanup path chd_closes the partially built handle,
 * which core_fcloses the file and recursively closes the parent).
 * That is why the first no-parent attempt cannot reuse its file for
 * the re-open - the failed attempt already closed it - and why no
 * error path here releases anything already handed to libchdr. */
static chd_error chd_open_resolving_parents(struct CDAccess_CHD *self,
      const char *path, const char *base_dir, int depth,
      chd_file **out_chd)
{
   chd_header  hdr;
   chd_error   err;
   chd_file   *parent = NULL;
   core_file  *cf;

   *out_chd = NULL;

   if (depth >= CHD_MAX_PARENTS)
      return CHDERR_REQUIRES_PARENT;

   cf = chd_core_rfile_open(path);
   if (!cf)
      return CHDERR_FILE_NOT_FOUND;

   /* First, try a plain open. If the file needs no parent this
    * succeeds. Either way cf is consumed. */
   err = chd_open_core_file(cf, CHD_OPEN_READ, NULL, out_chd);
   if (err != CHDERR_REQUIRES_PARENT)
      return err;

   /* It's a child: read its header to learn the parent SHA1, find the
    * parent file in the same directory, open it (recursively), then
    * re-open this file with the parent handle. */
   if (!chd_peek_header(path, &hdr) || !chd_header_needs_parent(&hdr))
      return CHDERR_REQUIRES_PARENT;

   {
      char parent_path[CHD_PATH_BUF];

      if (!chd_find_parent_in_dir(base_dir, hdr.parentsha1, path,
               parent_path, sizeof(parent_path)))
      {
         log_cb(RETRO_LOG_ERROR,
               "CHD: \"%s\" needs a parent CHD that was not found in %s\n",
               path, base_dir);
         return CHDERR_REQUIRES_PARENT;
      }

      err = chd_open_resolving_parents(self, parent_path, base_dir,
            depth + 1, &parent);
      if (err != CHDERR_NONE)
         return err;

      log_cb(RETRO_LOG_INFO, "CHD: \"%s\" using parent \"%s\"\n",
            path, parent_path);
   }

   /* Re-open the child now that we have its parent. cf2 and parent
    * are consumed whether this succeeds or fails. */
   cf = chd_core_rfile_open(path);
   if (!cf)
   {
      chd_close(parent);
      return CHDERR_FILE_NOT_FOUND;
   }
   err = chd_open_core_file(cf, CHD_OPEN_READ, parent, out_chd);
   return err;
}

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

   {
      char base_dir[CHD_PATH_BUF];
      base_dir[0] = '\0';
      fill_pathname_basedir(base_dir, path, sizeof(base_dir));

      err = chd_open_resolving_parents(self, path, base_dir,
            0, &self->chd);
   }
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
         sscanf(meta_entry, CHD_TRACK_METADATA2_FMT_SAFE,
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
            sscanf(meta_entry, CHD_TRACK_METADATA_FMT_SAFE,
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
      /* "NONE" = no subchannel. "RW_RAW" = raw (interleaved) R-W
       * subchannel recorded in the image, used by LibCrypt-protected
       * discs; the data trails each 2352-byte sector in the hunk. "RW"
       * (deinterleaved) is accepted as well. Anything else is unknown. */
      else if (strncmp(subtype, "NONE", 4) != 0
            && strncmp(subtype, "RW_RAW", 6) != 0
            && strncmp(subtype, "RW", 2) != 0)
      {
         log_cb(RETRO_LOG_ERROR, "chd_parse track subtype %s unsupported\n",
               subtype);
         return false;
      }

      /* tkid is parsed straight from attacker-controlled CHD metadata
       * and is used unchecked as Tracks[tkid]. Valid CD track numbers
       * are 1..99 (Tracks[] has 100 slots, index 0 is reserved); reject
       * anything outside that range instead of writing out of bounds. */
      if (tkid < 1 || tkid >= 100)
      {
         log_cb(RETRO_LOG_ERROR,
               "chd_parse track number %d out of range (1-99)\n", tkid);
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
      if (strncmp(subtype, "RW_RAW", 6) == 0)
         self->Tracks[tkid].SubchannelMode = CDRF_SUBM_RW_RAW;
      else if (strncmp(subtype, "RW", 2) == 0)
         self->Tracks[tkid].SubchannelMode = CDRF_SUBM_RW;
      else
         self->Tracks[tkid].SubchannelMode = CDRF_SUBM_NONE;
      if (self->Tracks[tkid].SubchannelMode != CDRF_SUBM_NONE)
         self->has_subchannel = true;
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
   /* chd_close releases the whole chain: it recursively closes the
    * parent chd_files and core_fclose()s every backing file, which in
    * our shims closes the RFILE and frees the core_file wrapper. */
   if (self->chd)
   {
      chd_close(self->chd);
      self->chd = NULL;
   }

   if (self->hunkmem)
   {
      free(self->hunkmem);
      self->hunkmem = NULL;
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

      /* If the disc carries real subchannel data (LibCrypt), use the
       * recorded 96 bytes that trail the sector in the hunk instead of
       * the synthesized subchannel placed in buf+2352 above. RW_RAW is
       * interleaved P-W exactly as the rest of the pipeline expects, so
       * it is copied verbatim; a deinterleaved "RW" image is rare and
       * is interleaved into the same layout. The SBI replacement map,
       * applied during synthesis, is intentionally not re-applied here
       * because the recorded subchannel already contains the protection
       * data SBI would otherwise patch in. */
      if (self->has_subchannel
            && ct->SubchannelMode != CDRF_SUBM_NONE)
      {
         const uint8_t *sub = self->hunkmem + hunkofs * (2352 + 96) + 2352;

         if (ct->SubchannelMode == CDRF_SUBM_RW_RAW)
            memcpy(buf + 2352, sub, 96);
         else /* CDRF_SUBM_RW: deinterleaved on disc, re-interleave it. */
            subpw_interleave(sub, buf + 2352);
      }

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

   /* The file itself is opened (as a core_file wrapper) inside
    * chd_open_resolving_parents; probe for existence up front only to
    * keep the historical error message for the common failure. */
   {
      RFILE *probe = filestream_open(path,
            RETRO_VFS_FILE_ACCESS_READ,
            RETRO_VFS_FILE_ACCESS_HINT_NONE);
      if (!probe)
      {
         MDFN_Error(0, "CHD: failed to open \"%s\"", path);
         *success = false;
         return &self->base;
      }
      filestream_close(probe);
   }

   if (!CDAccess_CHD_ImageOpen(self, path, image_memcache))
      *success = false;

   return &self->base;
}
