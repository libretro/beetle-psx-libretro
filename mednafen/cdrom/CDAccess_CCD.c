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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include <boolean.h>
#include <compat/msvc.h>
#include <compat/strl.h>

#include "../mednafen.h"
#include "../error.h"
#include "../general_c.h"
#include "../cdstream.h"

#include "CDAccess.h"
#include "CDAccess_CCD.h"
#include "CDUtility.h"

extern retro_log_printf_t log_cb;

#define CCD_PATH_BUF      4096
#define CCD_LINE_BUF      512
#define CCD_KEY_BUF       64
#define CCD_VAL_BUF       64
#define CCD_SECTION_BUF   64
#define CCD_MAX_ENTRIES   1024

/* ------------------------------------------------------------------
 * CCD parser: flat list of (section, key, value) tuples.
 *
 * Replaces the std::map<std::string, std::map<std::string, std::string>>
 * the C++ version used.  CCD files in the wild rarely exceed 200
 * entries; linear scan over a contiguous array is small on both
 * code-size and cache-locality grounds at that scale.
 * ------------------------------------------------------------------ */

typedef struct ccd_kv
{
   char section[CCD_SECTION_BUF];
   char key    [CCD_KEY_BUF];
   char value  [CCD_VAL_BUF];
} ccd_kv;

typedef struct ccd_kv_list
{
   ccd_kv   entries[CCD_MAX_ENTRIES];
   unsigned count;
} ccd_kv_list;

/* Find a value by (section, key).  Both compared verbatim - caller
 * is responsible for already having uppercased the lookup key.
 * Returns NULL if not found. */
static const char *ccd_kv_find(const ccd_kv_list *kv,
      const char *section, const char *key)
{
   unsigned i;
   for (i = 0; i < kv->count; i++)
   {
      if (strcmp(kv->entries[i].section, section) == 0
            && strcmp(kv->entries[i].key, key) == 0)
         return kv->entries[i].value;
   }
   return NULL;
}

/* Append a (section, key, value) tuple.  Truncates over-long
 * components to fit the fixed buffers. */
static void ccd_kv_set(ccd_kv_list *kv, const char *section,
      const char *key, const char *value)
{
   ccd_kv *e;
   if (kv->count >= CCD_MAX_ENTRIES)
      return;
   e = &kv->entries[kv->count++];
   strlcpy(e->section, section, sizeof(e->section));
   strlcpy(e->key,     key,     sizeof(e->key));
   strlcpy(e->value,   value,   sizeof(e->value));
}

/* CCD_ReadInt: parse an integer from a CCD section.
 *
 * The C++ version was a template parameterised on T (unsigned,
 * uint8_t, signed); the only thing that varied with T was the
 * signedness of the strtoul/strtol call.  Replaced with two
 * non-template C functions, one signed and one unsigned, both
 * returning long.  Callers narrow to the type they need at the
 * use site (the original code already did the narrowing too,
 * just implicitly).
 *
 * Sets *ok to false on error (missing property, malformed value).
 * Callers MUST initialize *ok to true before a sequence of calls
 * and check it after the sequence; once *ok is false, subsequent
 * calls short-circuit return 0. */

static long ccd_read_long(bool *ok, const ccd_kv_list *kv,
      const char *section, const char *propname,
      bool is_signed, bool have_defval, long defval)
{
   const char *v;
   const char *vp;
   char       *ep         = NULL;
   int         scan_base  = 10;
   long        ret        = 0;

   if (!*ok)
      return 0;

   v = ccd_kv_find(kv, section, propname);
   if (!v)
   {
      if (have_defval)
         return defval;
      MDFN_Error(0, "Missing property: %s", propname);
      *ok = false;
      return 0;
   }

   vp = v;
   if (v[0] == '0' && v[1] == 'x' && v[2])
   {
      scan_base = 16;
      vp        = v + 2;
   }

   if (is_signed)
      ret = strtol(vp, &ep, scan_base);
   else
      ret = (long)strtoul(vp, &ep, scan_base);

   if (!vp[0] || ep[0])
   {
      MDFN_Error(0, "Property %s: Malformed integer: %s", propname, v);
      *ok = false;
      return 0;
   }

   return ret;
}

#define CCD_READ_U(ok, kv, section, prop) \
   ccd_read_long((ok), (kv), (section), (prop), false, false, 0)
#define CCD_READ_S(ok, kv, section, prop) \
   ccd_read_long((ok), (kv), (section), (prop), true,  false, 0)

/* ------------------------------------------------------------------
 * Concrete struct.
 * ------------------------------------------------------------------ */

struct CDAccess_CCD
{
   CDAccess       base;

   cdstream       img_stream;
   cdstream       sub_stream;
   size_t         img_numsectors;
   TOC            tocd;
};

/* ------------------------------------------------------------------
 * Body methods.
 * ------------------------------------------------------------------ */

static bool CDAccess_CCD_CheckSubQSanity(struct CDAccess_CCD *self)
{
   /* Checks for Q subchannel mode 1 (current time) data that has a
    * correct checksum but is nonsensical or corrupted; this is the
    * case for some bad rips floating around on the Internet.
    * Allowing them through would cause emulation problems. */
   size_t   s;
   int      prev_lba   = INT_MAX;
   uint8_t  prev_track = 0;

   (void)prev_lba;

   for (s = 0; s < self->img_numsectors; s++)
   {
      uint8_t adr;
      union
      {
         uint8_t full[96];
         struct
         {
            uint8_t pbuf[12];
            uint8_t qbuf[12];
         } parts;
      } buf;

      cdstream_seek(&self->sub_stream, s * 96, SEEK_SET);
      cdstream_read(&self->sub_stream, buf.full, 96);

      if (!subq_check_checksum(buf.parts.qbuf))
         continue;

      adr = buf.parts.qbuf[0] & 0xF;

      if (adr == 0x01)
      {
         int     lba;
         uint8_t track;
         uint8_t track_bcd = buf.parts.qbuf[1];
         uint8_t index_bcd = buf.parts.qbuf[2];
         uint8_t rm_bcd    = buf.parts.qbuf[3];
         uint8_t rs_bcd    = buf.parts.qbuf[4];
         uint8_t rf_bcd    = buf.parts.qbuf[5];
         uint8_t am_bcd    = buf.parts.qbuf[7];
         uint8_t as_bcd    = buf.parts.qbuf[8];
         uint8_t af_bcd    = buf.parts.qbuf[9];

         (void)index_bcd;

         if (!BCD_is_valid(track_bcd) || !BCD_is_valid(index_bcd)
               || !BCD_is_valid(rm_bcd) || !BCD_is_valid(rs_bcd)
               || !BCD_is_valid(rf_bcd) || !BCD_is_valid(am_bcd)
               || !BCD_is_valid(as_bcd) || !BCD_is_valid(af_bcd)
               || rs_bcd > 0x59 || rf_bcd > 0x74
               || as_bcd > 0x59 || af_bcd > 0x74)
         {
            MDFN_Error(0,
                  "Garbage subchannel Q data detected (bad BCD/out of range): %02x:%02x:%02x %02x:%02x:%02x",
                  rm_bcd, rs_bcd, rf_bcd, am_bcd, as_bcd, af_bcd);
            return false;
         }

         lba   = ((BCD_to_U8(am_bcd) * 60 + BCD_to_U8(as_bcd)) * 75
                  + BCD_to_U8(af_bcd)) - 150;
         track = BCD_to_U8(track_bcd);

         prev_lba = lba;

         if (track < prev_track)
         {
            MDFN_Error(0, "Garbage subchannel Q data detected (bad track number)");
            return false;
         }

         prev_track = track;
      }
   }

   return true;
}

/* In-place transform: anything from .ccd extension's case dictates
 * the case of the .img and .sub partner files we look for. */
static void apply_extension_case(const char *file_ext,
      char *img_ext, char *sub_ext)
{
   int          i;
   signed char  av        = -1;
   signed char  extupt[3];

   extupt[0] = -1;
   extupt[1] = -1;
   extupt[2] = -1;

   if (strlen(file_ext) != 4 || file_ext[0] != '.')
      return;

   for (i = 1; i < 4; i++)
   {
      if (file_ext[i] >= 'A' && file_ext[i] <= 'Z')
         extupt[i - 1] = (signed char)('A' - 'a');
      else if (file_ext[i] >= 'a' && file_ext[i] <= 'z')
         extupt[i - 1] = 0;
   }

   for (i = 0; i < 3; i++)
   {
      if (extupt[i] != -1)
         av = extupt[i];
      else
         extupt[i] = av;
   }

   if (av == -1)
      av = 0;

   for (i = 0; i < 3; i++)
   {
      if (extupt[i] == -1)
         extupt[i] = av;
   }

   for (i = 0; i < 3; i++)
   {
      img_ext[i] = (char)(img_ext[i] + extupt[i]);
      sub_ext[i] = (char)(sub_ext[i] + extupt[i]);
   }
}

static bool CDAccess_CCD_Load(struct CDAccess_CCD *self, const char *path,
      bool image_memcache)
{
   /* All return paths past cdstream_open pass through cleanup, which
    * closes cf. */
   cdstream           cf;
   bool               ok = true;
   ccd_kv_list       *kv;
   char               linebuf[CCD_LINE_BUF];
   char               cur_section[CCD_SECTION_BUF];
   char               dir_path  [CCD_PATH_BUF];
   char               file_base [CCD_PATH_BUF];
   char               file_ext  [CCD_PATH_BUF];
   char               img_extsd[4];
   char               sub_extsd[4];
   unsigned           te;
   unsigned           toc_entries;
   unsigned           num_sessions;
   bool               data_tracks_scrambled;
   bool               inner_ok = true;

   /* Stack would push the kv list past typical size limits; allocate. */
   kv = (ccd_kv_list *)calloc(1, sizeof(*kv));
   if (!kv)
   {
      MDFN_Error(0, "CCD: out of memory");
      return false;
   }

   img_extsd[0] = 'i'; img_extsd[1] = 'm'; img_extsd[2] = 'g'; img_extsd[3] = 0;
   sub_extsd[0] = 's'; sub_extsd[1] = 'u'; sub_extsd[2] = 'b'; sub_extsd[3] = 0;

   cur_section[0] = 0;

   if (!cdstream_open(&cf, path))
   {
      MDFN_Error(0, "CCD: failed to open \"%s\"", path);
      ok = false;
      goto cleanup;
   }

   MDFN_GetFilePathComponents_c(path,
         dir_path,  sizeof(dir_path),
         file_base, sizeof(file_base),
         file_ext,  sizeof(file_ext));

   apply_extension_case(file_ext, img_extsd, sub_extsd);

   /* Section/key/value parser.  CCD files are flat .ini-style:
    *    [SECTION]
    *    Key=Value
    * Comments and stray whitespace are not part of the format
    * (just blank lines between sections). */
   while (cdstream_get_line(&cf, linebuf, sizeof(linebuf)) >= 0)
   {
      size_t llen;

      MDFN_trim_c(linebuf);
      llen = strlen(linebuf);

      if (llen == 0)
         continue;

      if (linebuf[0] == '[')
      {
         size_t i;
         if (llen < 3 || linebuf[llen - 1] != ']')
         {
            MDFN_Error(0, "Malformed section specifier: %s", linebuf);
            ok = false;
            goto cleanup;
         }
         /* Copy "NAME" out of "[NAME]" into cur_section, uppercased. */
         if (llen - 2 >= sizeof(cur_section))
            llen = sizeof(cur_section) + 1;
         for (i = 0; i + 2 < llen && i < sizeof(cur_section) - 1; i++)
            cur_section[i] = linebuf[i + 1];
         cur_section[i] = 0;
         MDFN_strtoupper_c(cur_section);
      }
      else
      {
         char  *eq;
         char  *eq_check;
         size_t klen;

         eq = strchr(linebuf, '=');
         if (!eq)
         {
            MDFN_Error(0, "Malformed value pair specifier: %s", linebuf);
            ok = false;
            goto cleanup;
         }
         /* Reject lines with multiple '=' (matches the C++ feqpos !=
          * leqpos check). */
         eq_check = strchr(eq + 1, '=');
         if (eq_check)
         {
            MDFN_Error(0, "Malformed value pair specifier: %s", linebuf);
            ok = false;
            goto cleanup;
         }

         {
            char k_buf[CCD_KEY_BUF];
            char v_buf[CCD_VAL_BUF];

            klen = (size_t)(eq - linebuf);
            if (klen >= sizeof(k_buf))
               klen = sizeof(k_buf) - 1;
            memcpy(k_buf, linebuf, klen);
            k_buf[klen] = 0;
            MDFN_trim_c(k_buf);
            MDFN_strtoupper_c(k_buf);

            strncpy(v_buf, eq + 1, sizeof(v_buf) - 1);
            v_buf[sizeof(v_buf) - 1] = 0;
            MDFN_trim_c(v_buf);

            ccd_kv_set(kv, cur_section, k_buf, v_buf);
         }
      }
   }

   /* DISC section. */
   toc_entries           = (unsigned)CCD_READ_U(&inner_ok, kv, "DISC", "TOCENTRIES");
   num_sessions          = (unsigned)CCD_READ_U(&inner_ok, kv, "DISC", "SESSIONS");
   data_tracks_scrambled =
         CCD_READ_U(&inner_ok, kv, "DISC", "DATATRACKSSCRAMBLED") != 0;

   if (!inner_ok)
   {
      ok = false;
      goto cleanup;
   }

   if (num_sessions != 1)
   {
      MDFN_Error(0, "Unsupported number of sessions: %u", num_sessions);
      ok = false;
      goto cleanup;
   }

   if (data_tracks_scrambled)
   {
      MDFN_Error(0, "Scrambled CCD data tracks currently not supported.");
      ok = false;
      goto cleanup;
   }

   /* TOC entries. */
   for (te = 0; te < toc_entries; te++)
   {
      char     section[CCD_SECTION_BUF];
      uint8_t  point;
      uint8_t  adr;
      uint8_t  control;
      uint8_t  pmin;
      uint8_t  psec;
      uint8_t  pframe;
      signed   plba;
      unsigned session;

      snprintf(section, sizeof(section), "ENTRY %u", te);

      session = (unsigned)CCD_READ_U(&inner_ok, kv, section, "SESSION");
      point   = (uint8_t) CCD_READ_U(&inner_ok, kv, section, "POINT");
      adr     = (uint8_t) CCD_READ_U(&inner_ok, kv, section, "ADR");
      control = (uint8_t) CCD_READ_U(&inner_ok, kv, section, "CONTROL");
      pmin    = (uint8_t) CCD_READ_U(&inner_ok, kv, section, "PMIN");
      psec    = (uint8_t) CCD_READ_U(&inner_ok, kv, section, "PSEC");
      pframe  = (uint8_t) CCD_READ_U(&inner_ok, kv, section, "PFRAME");
      plba    = (signed)  CCD_READ_S(&inner_ok, kv, section, "PLBA");
      (void)pframe;

      if (!inner_ok)
      {
         ok = false;
         goto cleanup;
      }

      if (session != 1)
      {
         MDFN_Error(0, "Unsupported TOC entry Session value: %u", session);
         ok = false;
         goto cleanup;
      }

      /* Reference: ECMA-394, page 5-14 */
      if (point >= 1 && point <= 99)
      {
         self->tocd.tracks[point].adr     = adr;
         self->tocd.tracks[point].control = control;
         self->tocd.tracks[point].lba     = plba;
      }
      else
      {
         switch (point)
         {
            default:
               MDFN_Error(0, "Unsupported TOC entry Point value: %u", point);
               ok = false;
               goto cleanup;
            case 0xA0:
               self->tocd.first_track = pmin;
               self->tocd.disc_type   = psec;
               break;
            case 0xA1:
               self->tocd.last_track  = pmin;
               break;
            case 0xA2:
               self->tocd.tracks[100].adr     = adr;
               self->tocd.tracks[100].control = control;
               self->tocd.tracks[100].lba     = plba;
               break;
         }
      }
   }

   /* Convenience leadout track duplication. */
   if (self->tocd.last_track < 99)
      self->tocd.tracks[self->tocd.last_track + 1] = self->tocd.tracks[100];

   /* Open image stream (.img sibling). */
   {
      char    image_basename[CCD_PATH_BUF];
      char    image_path    [CCD_PATH_BUF];
      int64_t ss;

      /* image_basename = file_base + "." + img_extsd ("img", 3 chars).
       * Reserve 5 bytes (".img\0") so an oversized file_base gets
       * truncated rather than the extension. */
      {
         size_t cap  = sizeof(image_basename) - 5;
         size_t blen = strlcpy(image_basename, file_base, cap);
         if (blen >= cap)
            blen = cap - 1;
         image_basename[blen] = '.';
         memcpy(image_basename + blen + 1, img_extsd, 4); /* 3 + NUL */
      }
      MDFN_EvalFIP_c(dir_path, image_basename,
            image_path, sizeof(image_path));

      if (!cdstream_open(&self->img_stream, image_path))
      {
         MDFN_Error(0, "Could not open CCD image \"%s\"", image_path);
         ok = false;
         goto cleanup;
      }

      if (image_memcache && !cdstream_memcache_in_place(&self->img_stream))
      {
         MDFN_Error(0, "Could not load CCD image \"%s\" into memory",
               image_path);
         /* cdstream_memcache_in_place closed self->img_stream on
          * failure - cleanup's cdstream_close is a safe no-op. */
         ok = false;
         goto cleanup;
      }

      ss = (int64_t)cdstream_size(&self->img_stream);

      if (ss % 2352)
      {
         MDFN_Error(0, "CCD image size is not evenly divisible by 2352.");
         ok = false;
         goto cleanup;
      }

      self->img_numsectors = ss / 2352;
   }

   /* Open subchannel stream (.sub sibling). */
   {
      char sub_basename[CCD_PATH_BUF];
      char sub_path    [CCD_PATH_BUF];

      /* sub_basename = file_base + "." + sub_extsd ("sub", 3 chars).
       * Reserve 5 bytes (".sub\0") so an oversized file_base gets
       * truncated rather than the extension. */
      {
         size_t cap  = sizeof(sub_basename) - 5;
         size_t blen = strlcpy(sub_basename, file_base, cap);
         if (blen >= cap)
            blen = cap - 1;
         sub_basename[blen] = '.';
         memcpy(sub_basename + blen + 1, sub_extsd, 4); /* 3 + NUL */
      }
      MDFN_EvalFIP_c(dir_path, sub_basename,
            sub_path, sizeof(sub_path));

      if (!cdstream_open(&self->sub_stream, sub_path))
      {
         MDFN_Error(0, "Could not open CCD subchannel \"%s\"", sub_path);
         ok = false;
         goto cleanup;
      }

      if (image_memcache && !cdstream_memcache_in_place(&self->sub_stream))
      {
         MDFN_Error(0, "Could not load CCD subchannel \"%s\" into memory",
               sub_path);
         ok = false;
         goto cleanup;
      }

      if (cdstream_size(&self->sub_stream)
            != (uint64_t)self->img_numsectors * 96)
      {
         MDFN_Error(0, "CCD SUB file size mismatch.");
         ok = false;
         goto cleanup;
      }
   }

   if (!CDAccess_CCD_CheckSubQSanity(self))
   {
      ok = false;
      goto cleanup;
   }

cleanup:
   cdstream_close(&cf);
   free(kv);
   return ok;
}

static void CDAccess_CCD_Cleanup(struct CDAccess_CCD *self)
{
   cdstream_close(&self->img_stream);
   cdstream_close(&self->sub_stream);
}

static bool CDAccess_CCD_Read_Raw_Sector(CDAccess *base_self, uint8_t *buf,
      int32_t lba)
{
   struct CDAccess_CCD *self = (struct CDAccess_CCD *)base_self;
   uint8_t              sub_buf[96];

   if (lba < 0 || (size_t)lba >= self->img_numsectors)
   {
      MDFN_Error(0, "LBA out of range.");
      return false;
   }

   cdstream_seek(&self->img_stream, lba * 2352, SEEK_SET);
   cdstream_read(&self->img_stream, buf, 2352);

   cdstream_seek(&self->sub_stream, lba * 96, SEEK_SET);
   cdstream_read(&self->sub_stream, sub_buf, 96);

   subpw_interleave(sub_buf, buf + 2352);

   return true;
}

static bool CDAccess_CCD_Read_Raw_PW(CDAccess *base_self, uint8_t *buf,
      int32_t lba)
{
   struct CDAccess_CCD *self = (struct CDAccess_CCD *)base_self;
   uint8_t              sub_buf[96];

   if (lba < 0 || (size_t)lba >= self->img_numsectors)
   {
      MDFN_Error(0, "LBA out of range.");
      return false;
   }

   cdstream_seek(&self->sub_stream, lba * 96, SEEK_SET);
   cdstream_read(&self->sub_stream, sub_buf, 96);

   subpw_interleave(sub_buf, buf);

   return true;
}

static bool CDAccess_CCD_Read_TOC(CDAccess *base_self, TOC *toc)
{
   struct CDAccess_CCD *self = (struct CDAccess_CCD *)base_self;
   *toc = self->tocd;
   return true;
}

static void CDAccess_CCD_Eject(CDAccess *base_self, bool eject_status)
{
   (void)base_self;
   (void)eject_status;
}

static void CDAccess_CCD_destroy(CDAccess *base_self)
{
   struct CDAccess_CCD *self = (struct CDAccess_CCD *)base_self;
   CDAccess_CCD_Cleanup(self);
   free(self);
}

/* ------------------------------------------------------------------
 * Factory.
 * ------------------------------------------------------------------ */

CDAccess *CDAccess_CCD_New(bool *success, const char *path,
      bool image_memcache)
{
   struct CDAccess_CCD *self =
      (struct CDAccess_CCD *)calloc(1, sizeof(*self));

   if (!self)
   {
      *success = false;
      return NULL;
   }

   self->base.Read_Raw_Sector = CDAccess_CCD_Read_Raw_Sector;
   self->base.Read_Raw_PW     = CDAccess_CCD_Read_Raw_PW;
   self->base.Read_TOC        = CDAccess_CCD_Read_TOC;
   self->base.Eject           = CDAccess_CCD_Eject;
   self->base.destroy         = CDAccess_CCD_destroy;

   /* self came from calloc - img_stream and sub_stream are already
    * zero-initialised (closed cdstream). */
   self->img_numsectors = 0;
   TOC_Clear(&self->tocd);

   if (!CDAccess_CCD_Load(self, path, image_memcache))
      *success = false;

   return &self->base;
}
