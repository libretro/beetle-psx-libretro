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

#include <formats/rchd.h>

extern retro_log_printf_t log_cb;

#define CHD_PATH_BUF 4096

/* A child CHD references unchanged data in a parent file and a parent
 * can itself be a child; this bounds how many images one chain holds. */
#define CHD_MAX_PARENTS 8

/* Largest single read handed to the decoder when the file is neither
 * mapped nor cached; rchd accepts short supplies and asks again. */
#define CHD_IO_CHUNK 65536

/* One image of a chain: the decoder and the file it is fed from. When
 * the whole file is resident - mapped by the VFS (the open passes
 * FREQUENT_ACCESS) or read in under image_memcache - @base points at
 * it and hunk payloads are lent to the decoder in place rather than
 * copied; otherwise each request is read through the filestream. */
typedef struct
{
   rchd_t        *chd;
   RFILE         *fp;
   const uint8_t *base;
   uint8_t       *owned;   /* the image_memcache copy, when @base is it */
   int64_t        len;
} chd_src;

/* ------------------------------------------------------------------
 * Concrete struct.  std::string sbi_path is gone (replaced with a
 * fixed char buffer); std::map<uint32, 12-byte> SubQReplaceMap is
 * replaced with a sorted-array subq_map (cdaccess_track.h).
 * ------------------------------------------------------------------ */

struct CDAccess_CHD
{
   CDAccess     base;

   /* chain[0] is the image itself, chain[i + 1] the parent of
    * chain[i]. Every level is owned here and closed in Cleanup. */
   chd_src      chain[CHD_MAX_PARENTS + 1];
   uint8_t     *hunkmem;        /* hunk-data cache */
   uint8_t     *io_buf;         /* CHD_IO_CHUNK bytes of read staging */
   uint32_t     chain_len;
   uint32_t     hunkbytes;
   int          oldhunk;        /* last hunknum read, -1 sentinel */

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

/* Field-width-limited parse formats for the CD track metadata
 * ('CHT2' and 'CHTR' entries). A bare %s with no width would let a crafted CHD whose
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

/* ------------------------------------------------------------------
 * Image I/O.  rchd does no I/O of its own: every step that needs bytes
 * returns RCHD_PENDING with the range it wants, and the functions below
 * satisfy it from the image's file.
 * ------------------------------------------------------------------ */

static void chd_src_close(chd_src *src)
{
   if (src->chd)
      rchd_free(src->chd);
   /* closing the RFILE releases a VFS mapping with it */
   if (src->fp)
      filestream_close(src->fp);
   free(src->owned);
   src->chd   = NULL;
   src->fp    = NULL;
   src->base  = NULL;
   src->owned = NULL;
   src->len   = 0;
}

/* Satisfies one request from @src. @reading selects the read-time feed,
 * which lends resident bytes in place; the open sequence always copies. */
static bool chd_src_supply(chd_src *src, const rchd_request_t *rq,
      uint8_t *io_buf, bool reading)
{
   int64_t got;

   if (src->base)
   {
      const uint8_t *p;
      size_t         n;
      if (rq->offset >= (uint64_t)src->len)
         return false;
      p = src->base + (size_t)rq->offset;
      n = rq->length;
      if ((uint64_t)n > (uint64_t)src->len - rq->offset)
         n = (size_t)((uint64_t)src->len - rq->offset);
      if (reading)
         return rchd_feed_borrow(src->chd, rq->offset, rq->source,
               p, n) == RCHD_OK;
      return rchd_feed(src->chd, p, n) == RCHD_OK;
   }

   if (filestream_seek(src->fp, (int64_t)rq->offset,
            RETRO_VFS_SEEK_POSITION_START) < 0)
      return false;
   got = filestream_read(src->fp, io_buf,
         rq->length < CHD_IO_CHUNK ? rq->length : CHD_IO_CHUNK);
   if (got <= 0)
      return false;
   if (reading)
      return rchd_feed_at(src->chd, rq->offset, rq->source,
            io_buf, (size_t)got) == RCHD_OK;
   return rchd_feed(src->chd, io_buf, (size_t)got) == RCHD_OK;
}

/* Opens @path into @src and runs the decoder's open sequence (header,
 * map, metadata). On failure @src is left closed. */
static bool chd_src_open(chd_src *src, const char *path,
      bool image_memcache, uint8_t *io_buf)
{
   rchd_request_t rq;
   int            err;

   src->fp = filestream_open(path,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_FREQUENT_ACCESS);
   if (!src->fp)
      return false;

   src->base = (const uint8_t *)filestream_get_mapped_ptr(src->fp,
         &src->len);
   if (src->base && src->len <= 0)
      src->base = NULL;

   if (!src->base && image_memcache)
   {
      int64_t size = filestream_get_size(src->fp);
      if (size > 0 && (uint64_t)size == (uint64_t)(size_t)size
            && (src->owned = (uint8_t *)malloc((size_t)size)))
      {
         if (filestream_seek(src->fp, 0, RETRO_VFS_SEEK_POSITION_START) >= 0
               && filestream_read(src->fp, src->owned, size) == size)
         {
            src->base = src->owned;
            src->len  = size;
         }
         else
         {
            free(src->owned);
            src->owned = NULL;
         }
      }
      if (!src->base)
      {
         chd_src_close(src);
         return false;
      }
   }

   if (!(src->chd = rchd_new()))
   {
      chd_src_close(src);
      return false;
   }

   while ((err = rchd_open_step(src->chd, &rq)) == RCHD_PENDING)
   {
      if (!chd_src_supply(src, &rq, io_buf, false))
         break;
   }

   if (err != RCHD_OK)
   {
      log_cb(RETRO_LOG_ERROR, "CHD: \"%s\" failed to open (%d)\n",
            path, err);
      chd_src_close(src);
      return false;
   }
   return true;
}

/* Combined SHA-1 of the CHD at @path, from its header alone. That is the
 * hash a child names its parent by, so this is all a parent search has
 * to read of each candidate. Versions 1 and 2 carry no SHA-1. */
static bool chd_peek_sha1(const char *path, uint8_t *sha1)
{
   uint8_t  h[124];
   uint32_t version;
   size_t   at;
   int64_t  got;
   RFILE   *fp = filestream_open(path,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);

   if (!fp)
      return false;
   got = filestream_read(fp, h, sizeof(h));
   filestream_close(fp);

   if (got < 16 || memcmp(h, "MComprHD", 8))
      return false;

   version = ((uint32_t)h[12] << 24) | ((uint32_t)h[13] << 16)
           | ((uint32_t)h[14] <<  8) |  (uint32_t)h[15];
   switch (version)
   {
      case 3:  at = 80; break;
      case 4:  at = 48; break;
      case 5:  at = 84; break;
      default: return false;
   }
   if ((size_t)got < at + 20)
      return false;
   memcpy(sha1, h + at, 20);
   return true;
}

/* Search @dir for the parent of @child, matching each candidate's
 * combined SHA-1 against the parent SHA-1 the child records, and open
 * the match into @out. The child's own hash never equals the parent
 * hash it records, so the child is passed over without a path check. */
static bool chd_find_parent_in_dir(const char *dir, const rchd_t *child,
      chd_src *out, bool image_memcache, uint8_t *io_buf)
{
   struct RDIR *rdir = retro_opendir(dir);
   bool         ok   = false;
   char        *cand;

   if (!rdir)
      return false;
   if (!(cand = (char *)malloc(CHD_PATH_BUF)))
   {
      retro_closedir(rdir);
      return false;
   }

   while (retro_readdir(rdir))
   {
      const char *name = retro_dirent_get_name(rdir);
      const char *ext;
      uint8_t     sha1[20];

      if (!name || retro_dirent_is_dir(rdir, NULL))
         continue;

      ext = path_get_extension(name);
      if (!ext || strcasecmp(ext, "chd"))
         continue;

      fill_pathname_join(cand, dir, name, CHD_PATH_BUF);

      if (!chd_peek_sha1(cand, sha1)
            || !rchd_parent_sha1_matches(child, sha1))
         continue;

      if (chd_src_open(out, cand, image_memcache, io_buf))
      {
         log_cb(RETRO_LOG_INFO, "CHD: using parent \"%s\"\n", cand);
         ok = true;
      }
      break;
   }

   free(cand);
   retro_closedir(rdir);
   return ok;
}

/* Opens @path as chain[0], then finds, opens and binds each parent the
 * chain needs from @base_dir. */
static bool chd_open_chain(struct CDAccess_CHD *self, const char *path,
      const char *base_dir, bool image_memcache)
{
   if (!chd_src_open(&self->chain[0], path, image_memcache, self->io_buf))
      return false;
   self->chain_len = 1;

   while (rchd_info(self->chain[self->chain_len - 1].chd)->has_parent)
   {
      rchd_t *child = self->chain[self->chain_len - 1].chd;

      if (self->chain_len > CHD_MAX_PARENTS
            || !chd_find_parent_in_dir(base_dir, child,
               &self->chain[self->chain_len], image_memcache,
               self->io_buf))
      {
         log_cb(RETRO_LOG_ERROR,
               "CHD: \"%s\" needs a parent CHD that was not found in %s\n",
               path, base_dir);
         return false;
      }
      if (rchd_set_parent(child,
               self->chain[self->chain_len].chd) != RCHD_OK)
         return false;
      self->chain_len++;
   }
   return true;
}

/* Decodes hunk @hunknum into hunkmem. Requests for a hunk that a child
 * shares with its parent are made by the parent's decoder, so the
 * level whose request is outstanding is the one whose file is read. */
static bool CDAccess_CHD_ReadHunk(struct CDAccess_CHD *self,
      uint32_t hunknum)
{
   rchd_request_t rq;
   int            err = rchd_read_hunk_begin(self->chain[0].chd, hunknum,
         self->hunkmem);

   while (err == RCHD_OK
         && (err = rchd_read_step(self->chain[0].chd, &rq)) == RCHD_PENDING)
   {
      uint32_t lvl;

      for (lvl = 0; lvl < self->chain_len; lvl++)
         if (rchd_read_pending(self->chain[lvl].chd, &rq, 1))
            break;

      if (lvl == self->chain_len)
         err = RCHD_ERROR_STATE;
      else if (!chd_src_supply(&self->chain[lvl], &rq, self->io_buf, true))
         err = RCHD_ERROR_DATA;
      else
         err = RCHD_OK;
   }

   if (err != RCHD_OK)
   {
      log_cb(RETRO_LOG_ERROR, "CHD: hunk %u failed to decode (%d)\n",
            (unsigned)hunknum, err);
      return false;
   }
   return true;
}

/* Copies the @n'th @tag metadata entry, NUL-terminated, into @out. */
static bool chd_get_track_meta(const rchd_t *chd, uint32_t tag, uint32_t n,
      char *out, size_t out_size)
{
   const rchd_metadata_t *m = rchd_metadata_find(chd, tag, n);
   size_t                 len;

   if (!m)
      return false;
   len = m->length < out_size - 1 ? m->length : out_size - 1;
   memcpy(out, m->data, len);
   out[len] = '\0';
   return true;
}

/* Forward declaration - LoadSBI is called from Read_TOC. */
static int CDAccess_CHD_LoadSBI(struct CDAccess_CHD *self,
      const char *sbi_path);

/* ------------------------------------------------------------------
 * Body methods.
 * ------------------------------------------------------------------ */

static bool CDAccess_CHD_ImageOpen(struct CDAccess_CHD *self,
      const char *path, bool image_memcache)
{
   rchd_t           *chd;
   int               plba       = -150;
   uint32_t          fileOffset = 0;
   char              type[64];
   char              subtype[32];
   char              pgtype[32];
   char              pgsub[32];
   char              meta_entry[256];
   char              base_dir[CHD_PATH_BUF];
   char              file_base[CHD_PATH_BUF];
   char              file_ext[CHD_PATH_BUF];
   char              sbi_ext[4];
   size_t            ext_len;
   int               i;
   char              sbi_basename[CHD_PATH_BUF];

   if (!(self->io_buf = (uint8_t *)malloc(CHD_IO_CHUNK)))
      return false;

   base_dir[0] = '\0';
   fill_pathname_basedir(base_dir, path, sizeof(base_dir));
   if (!chd_open_chain(self, path, base_dir, image_memcache))
      return false;

   chd             = self->chain[0].chd;
   self->hunkbytes = rchd_info(chd)->hunk_bytes;
   if (self->hunkbytes < 2352 + 96
         || !(self->hunkmem = (uint8_t *)malloc(self->hunkbytes)))
      return false;
   self->oldhunk = -1;

   log_cb(RETRO_LOG_INFO, "chd_load '%s' hunkbytes=%u\n", path,
         (unsigned)self->hunkbytes);

   for (;;)
   {
      int tkid    = 0;
      int frames  = 0;
      int pad     = 0;
      int pregap  = 0;
      int postgap = 0;

      type[0]    = '\0';
      subtype[0] = '\0';
      pgtype[0]  = '\0';
      pgsub[0]   = '\0';

      if (chd_get_track_meta(chd, RCHD_META_CDROM_TRACK2,
               (uint32_t)self->NumTracks, meta_entry, sizeof(meta_entry)))
         sscanf(meta_entry, CHD_TRACK_METADATA2_FMT_SAFE,
               &tkid, type, subtype, &frames, &pregap, pgtype, pgsub,
               &postgap);
      else if (chd_get_track_meta(chd, RCHD_META_CDROM_TRACK,
               (uint32_t)self->NumTracks, meta_entry, sizeof(meta_entry)))
         sscanf(meta_entry, CHD_TRACK_METADATA_FMT_SAFE,
               &tkid, type, subtype, &frames);
      else
         break;   /* end of TOC */

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
   uint32_t i;

   /* A child holds its parent, so the chain closes child first. */
   for (i = 0; i <= CHD_MAX_PARENTS; i++)
      chd_src_close(&self->chain[i]);
   self->chain_len = 0;

   free(self->hunkmem);
   free(self->io_buf);
   self->hunkmem = NULL;
   self->io_buf  = NULL;
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

   /* (uint32_t)lba + 150u wraps by definition and converts back to the
    * same int32_t that -fwrapv already produces here, so the signed
    * division and remainder below - which yield negative values for
    * pregap LBAs, and the uint32_t results depend on that - are
    * unchanged. lba comes from the image's own tables, so a malformed
    * sheet reaches this with values at the extremes of int32_t and the
    * bare addition is undefined. */
   {
      int32_t lba_a = (int32_t)((uint32_t)lba + 150u);
      fa = lba_a % 75;
      sa = (lba_a / 75) % 60;
      ma = (lba_a / 75 / 60);
   }

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
      int               cad     = lba - ct->LBA + ct->FileOffset;
      int               sph     = (int)(self->hunkbytes / (2352 + 96));
      int               hunknum = cad / sph;
      int               hunkofs = cad % sph;

      /* Each hunk holds ~8 sectors; cache the most-recently-read one. */
      if (hunknum != self->oldhunk)
      {
         if (!CDAccess_CHD_ReadHunk(self, (uint32_t)hunknum))
            log_cb(RETRO_LOG_ERROR,
                  "chd_read_sector failed lba=%d\n", lba);
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

   self->NumTracks     = 0;
   self->total_sectors = 0;
   self->FirstTrack    = 99;   /* opposites for min/max init */
   self->LastTrack     = 0;

   /* The file itself is opened inside chd_open_chain; probe for
    * existence up front only to keep the historical error message for
    * the common failure. */
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
