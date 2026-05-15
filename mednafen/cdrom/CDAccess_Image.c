/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/*
 Notes and TODO:

	POSTGAP in CUE sheets may not be handled properly, should the directive automatically increment the index number?

	INDEX nn where 02 <= nn <= 99 is not supported in CUE sheets.

	TOC reading code is extremely barebones, leaving out support for more esoteric features.

	A PREGAP statement in the first track definition in a CUE sheet may not work properly(depends on what is proper);
	it will be added onto the implicit default 00:02:00 of pregap.

	Trying to read sectors at an LBA of less than 0 is not supported.  TODO: support it(at least up to -150).
*/

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>

#include <boolean.h>
#include <compat/strl.h>
#include <streams/file_stream.h>

#include "../mednafen.h"
#include "../error.h"

#include <sys/types.h>

#include "../general_c.h"
#include "../cdstream.h"

#include "CDAccess.h"
#include "cdaccess_track.h"
#include "CDAccess_Image.h"
#include "CDUtility.h"

#include "audioreader.h"

#include <libretro.h>

extern retro_log_printf_t log_cb;

enum
{
   CDRF_SUBM_NONE = 0,
   CDRF_SUBM_RW,
   CDRF_SUBM_RW_RAW
};

/* Disk-image(rip) track/sector formats */
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

static const int32_t DI_Size_Table[7] =
{
   2352, /* Audio */
   2048, /* MODE1 */
   2352, /* MODE1 RAW */
   2336, /* MODE2 */
   2048, /* MODE2 Form 1 */
   2324, /* Mode 2 Form 2 */
   2352
};

static const char *DI_CDRDAO_Strings[7] =
{
   "AUDIO",
   "MODE1",
   "MODE1_RAW",
   "MODE2",
   "MODE2_FORM1",
   "MODE2_FORM2",
   "MODE2_RAW"
};

static const char *DI_CUE_Strings[7] =
{
   "AUDIO",
   "MODE1/2048",
   "MODE1/2352",

   /* FIXME: These are just guesses: */
   "MODE2/2336",
   "MODE2/2048",
   "MODE2/2324",
   "MODE2/2352"
};

/* Tokenize one whitespace-separated argument out of `src` starting at
 * source_offset.  Writes up to destlen-1 characters into dest, NUL-
 * terminated.  Returns the offset to the start of the next argument
 * (past trailing whitespace), or src_len if there isn't one.
 *
 * If parse_quotes is true, double-quoted spans are treated as a
 * single argument with the quotes stripped. */
static size_t UnQuotify(const char *src, size_t source_len, size_t source_offset,
      char *dest, size_t destlen, bool parse_quotes)
{
   bool   in_quote       = false;
   bool   already_normal = false;
   size_t dest_pos       = 0;

   if (destlen)
      dest[0] = 0;

   while (source_offset < source_len)
   {
      char c = src[source_offset];

      if (c == ' ' || c == '\t')
      {
         if (!in_quote)
         {
            if (already_normal)
               break; /* Trailing whitespace - argument complete */
            else
            {
               /* Leading whitespace, ignore it. */
               source_offset++;
               continue;
            }
         }
      }

      if (c == '"' && parse_quotes)
      {
         if (in_quote)
         {
            source_offset++;
            break;
         }
         else
            in_quote = true;
      }
      else
      {
         if (dest_pos + 1 < destlen)
         {
            dest[dest_pos++] = c;
            dest[dest_pos]   = 0;
         }
         already_normal = true;
      }
      source_offset++;
   }

   /* Skip trailing whitespace before next arg */
   while (source_offset < source_len)
   {
      char c = src[source_offset];
      if (c != ' ' && c != '\t')
         break;
      source_offset++;
   }

   return source_offset;
}

/* Tiny dynamic array of {filename, fp} pairs used by the TOC parser
 * to dedupe DATAFILE/AUDIOFILE references that point at the same
 * underlying file.  Tracks share the Stream instance in that case;
 * the first track to reference the file owns it (FirstFileInstance =
 * true), subsequent ones don't and won't free it.
 *
 * Linear search is fine - cue/toc images rarely have more than a
 * handful of distinct file references. */
struct toc_streamcache_entry
{
   char     *filename;
   cdstream *fp;
};

struct toc_streamcache
{
   struct toc_streamcache_entry *items;
   size_t                        count;
   size_t                        cap;
};

static cdstream *toc_streamcache_find(const struct toc_streamcache *c,
      const char *filename)
{
   size_t i;
   for (i = 0; i < c->count; i++)
   {
      if (!strcmp(c->items[i].filename, filename))
         return c->items[i].fp;
   }
   return NULL;
}

static int toc_streamcache_set(struct toc_streamcache *c,
      const char *filename, cdstream *fp)
{
   size_t i;
   /* Replace if already present (matches std::map's operator[] semantics). */
   for (i = 0; i < c->count; i++)
   {
      if (!strcmp(c->items[i].filename, filename))
      {
         c->items[i].fp = fp;
         return 0;
      }
   }
   if (c->count == c->cap)
   {
      size_t new_cap = c->cap ? c->cap * 2 : 4;
      struct toc_streamcache_entry *new_items =
         (struct toc_streamcache_entry *)realloc(c->items,
               new_cap * sizeof(*new_items));
      if (!new_items)
         return -1;
      c->items = new_items;
      c->cap   = new_cap;
   }
   c->items[c->count].filename = strdup(filename);
   if (!c->items[c->count].filename)
      return -1;
   c->items[c->count].fp = fp;
   c->count++;
   return 0;
}

static void toc_streamcache_free(struct toc_streamcache *c)
{
   size_t i;
   for (i = 0; i < c->count; i++)
      free(c->items[i].filename);
   free(c->items);
   c->items = NULL;
   c->count = 0;
   c->cap   = 0;
}

#define IMAGE_PATH_BUF 4096

struct CDAccess_Image
{
   CDAccess  base;

   int32_t   NumTracks;
   int32_t   FirstTrack;
   int32_t   LastTrack;
   int32_t   total_sectors;
   uint8_t   disc_type;
   CDRFILE_TRACK_INFO Tracks[100];

   subq_map  SubQReplaceMap;
   char      base_dir[IMAGE_PATH_BUF];
};
typedef struct CDAccess_Image CDAccess_Image;

/* Forward declarations - methods reference each other regardless of
 * source order. */
static uint32_t CDAccess_Image_GetSectorCount(CDAccess_Image *self, CDRFILE_TRACK_INFO *track);
static bool   CDAccess_Image_ParseTOCFileLineInfo(CDAccess_Image *self, CDRFILE_TRACK_INFO *track, const int tracknum, const char *filename, const char *binoffset, const char *msfoffset, const char *length, bool image_memcache, struct toc_streamcache *cache);
static int    CDAccess_Image_LoadSBI(CDAccess_Image *self, const char *sbi_path);
static bool   CDAccess_Image_ImageOpen(CDAccess_Image *self, const char *path, bool image_memcache);
static void   CDAccess_Image_Cleanup(CDAccess_Image *self);
static void   CDAccess_Image_MakeSubPQ(CDAccess_Image *self, int32_t lba, uint8_t *SubPWBuf);



static uint32_t CDAccess_Image_GetSectorCount(CDAccess_Image *self, CDRFILE_TRACK_INFO *track){
   int64_t size;

   if(track->DIFormat == DI_FORMAT_AUDIO)
   {
      if(track->AReader)
         return(((AR_FrameCount(track->AReader) * 4) - track->FileOffset) / 2352);

      size = cdstream_size(track->fp);

      if(track->SubchannelMode)
         return((size - track->FileOffset) / (2352 + 96));
      return((size - track->FileOffset) / 2352);
   }

   size = cdstream_size(track->fp);

   return((size - track->FileOffset) / DI_Size_Table[track->DIFormat]);
}

static bool CDAccess_Image_ParseTOCFileLineInfo(CDAccess_Image *self, CDRFILE_TRACK_INFO *track, const int tracknum,
      const char *filename, const char *binoffset, const char *msfoffset,
      const char *length, bool image_memcache, struct toc_streamcache *cache){
   long offset = 0; /* In bytes! */
   long tmp_long;
   int m, s, f;
   uint32_t sector_mult;
   long sectors;
   cdstream *cached;
   size_t flen;

   cached = toc_streamcache_find(cache, filename);

   if (cached)
   {
      track->FirstFileInstance = 0;
      track->fp                = cached;
   }
   else
   {
      char efn[IMAGE_PATH_BUF];
      cdstream *file;

      track->FirstFileInstance = 1;

      MDFN_EvalFIP_c(self->base_dir, filename, efn, sizeof(efn));

      file = cdstream_new(efn);
      if (!file)
      {
         MDFN_Error(0, "Could not open track file \"%s\"", efn);
         return false;
      }

      if (image_memcache && !cdstream_memcache_in_place(file))
      {
         /* memcache_in_place closed the stream on failure; free the
          * shell. */
         free(file);
         return false;
      }

      track->fp = file;
      toc_streamcache_set(cache, filename, track->fp);
   }

   flen = strlen(filename);
   if (flen >= 4 && !strcasecmp(filename + flen - 4, ".wav"))
   {
      track->AReader = AR_Open(track->fp);

      if (!track->AReader)
      {
         MDFN_Error(0, "Failed to open audio track \"%s\" as Ogg Vorbis", filename);
         return false;
      }
   }

   sector_mult = DI_Size_Table[track->DIFormat];

   if(track->SubchannelMode)
      sector_mult += 96;

   if(binoffset && sscanf(binoffset, "%ld", &tmp_long) == 1)
   {
      offset += tmp_long;
   }

   if(msfoffset && sscanf(msfoffset, "%d:%d:%d", &m, &s, &f) == 3)
   {
      offset += ((m * 60 + s) * 75 + f) * sector_mult;
   }

   track->FileOffset = offset; /* Make sure this is set before calling CDAccess_Image_GetSectorCount(self)! */
   sectors = CDAccess_Image_GetSectorCount(self, track);

   if(length)
   {
      tmp_long = sectors;

      if(sscanf(length, "%d:%d:%d", &m, &s, &f) == 3)
         tmp_long = (m * 60 + s) * 75 + f;
      else if(track->DIFormat == DI_FORMAT_AUDIO)
      {
         char *endptr = NULL;

         tmp_long = strtol(length, &endptr, 10);

         /* Error? */
         if(endptr == length)
         {
            tmp_long = sectors;
         }
         else
            tmp_long /= 588;

      }

      if(tmp_long > sectors)
      {
         MDFN_Error(0, "Length specified in TOC file for track %d is too large by %ld sectors!\n", tracknum, (long)(tmp_long - sectors));
         return false;
      }
      sectors = tmp_long;
   }

   track->sectors = sectors;
   return true;
}

static int CDAccess_Image_LoadSBI(CDAccess_Image *self, const char* sbi_path){
   /* Loading SBI file */
   uint8_t header[4];
   uint8_t ed[4 + 10];
   uint8_t tmpq[12];
   RFILE *sbis      = filestream_open(sbi_path,
         RETRO_VFS_FILE_ACCESS_READ,
         RETRO_VFS_FILE_ACCESS_HINT_NONE);

   if (!sbis)
      return -1;

   filestream_read(sbis, header, 4);

   if(memcmp(header, "SBI\0", 4))
      goto error;

   while(filestream_read(sbis, ed, sizeof(ed)) == sizeof(ed))
   {
      uint32_t aba;

      /* Bad BCD MSF offset in SBI file. */
      if(!BCD_is_valid(ed[0]) || !BCD_is_valid(ed[1]) || !BCD_is_valid(ed[2]))
         goto error;

      /* Unrecognized boogly oogly in SBI file */
      if(ed[3] != 0x01)
         goto error;

      memcpy(tmpq, &ed[4], 10);

      subq_generate_checksum(tmpq);
      tmpq[10] ^= 0xFF;
      tmpq[11] ^= 0xFF;

      aba = AMSF_to_ABA(BCD_to_U8(ed[0]), BCD_to_U8(ed[1]), BCD_to_U8(ed[2]));

      subq_map_insert(&self->SubQReplaceMap, aba, tmpq);
   }

   subq_map_finalize(&self->SubQReplaceMap);

   log_cb(RETRO_LOG_INFO, "[Image] Loaded SBI file %s\n", sbi_path);
   filestream_close(sbis);
   return 0;

error:
   if (sbis)
      filestream_close(sbis);
   return -1;
}

/* In-place ASCII uppercase. */
static void str_toupper(char *s)
{
   for (; *s; s++)
      if (*s >= 'a' && *s <= 'z')
         *s = (char)(*s - 'a' + 'A');
}

/* In-place trim of leading and trailing whitespace. */
static void str_trim(char *s)
{
   size_t len, i, di;
   if (!s)
      return;
   len = strlen(s);
   while (len > 0 && (s[len - 1] == ' '  || s[len - 1] == '\r'
                  || s[len - 1] == '\n' || s[len - 1] == '\t'
                  || s[len - 1] == 0x0b))
      s[--len] = 0;
   i = 0;
   while (s[i] == ' ' || s[i] == '\r' || s[i] == '\n'
       || s[i] == '\t' || s[i] == 0x0b)
      i++;
   if (i > 0)
   {
      di = 0;
      while (s[i])
         s[di++] = s[i++];
      s[di] = 0;
   }
}

static bool CDAccess_Image_ImageOpen(CDAccess_Image *self, const char *path, bool image_memcache){
   cdstream fp;
   bool ok;
   /* Hoisted from mid-function so the `goto cleanup` paths above don't
    * cross their initialization. They were locals to the post-parse
    * track-fixup loop. */
   int32_t RunningLBA = 0;
   int32_t LastIndex  = 0;
   long  FileOffset = 0;
   const unsigned max_args = 4;
   char  linebuf[4096];
   char  cmdbuf[256];
   char  args[4][1024];
   bool  IsTOC = false;
   int32_t active_track = -1;
   int32_t AutoTrackInc = 1; /* For TOC */
   CDRFILE_TRACK_INFO TmpTrack;
   char  file_base_buf[IMAGE_PATH_BUF];
   char  file_ext_buf [IMAGE_PATH_BUF];
   struct toc_streamcache cache;

   /* Open the cue/toc sheet itself and slurp it into RAM up-front.
    * The parser does line-at-a-time reads; doing those against the
    * filesystem would be tens of thousands of small reads on a
    * complex multi-track image.  fp is a stack cdstream cleaned up
    * by the single cdstream_close at the cleanup label - all error
    * paths below set ok = false and fall through. */
   if (!cdstream_open(&fp, path))
   {
      MDFN_Error(0, "Could not open \"%s\"", path);
      return false;
   }
   if (!cdstream_memcache_in_place(&fp))
   {
      MDFN_Error(0, "Could not load \"%s\" into memory", path);
      /* memcache_in_place closed fp on failure. */
      return false;
   }

   ok = true;
   /* Silence GCC warning - LastIndex is assigned but only conditionally read */
   (void)LastIndex;

   memset(&cache, 0, sizeof(cache));

   self->disc_type = DISC_TYPE_CDDA_OR_M1;
   memset(&TmpTrack, 0, sizeof(TmpTrack));

   MDFN_GetFilePathComponents_c(path,
         self->base_dir, sizeof(self->base_dir),
         file_base_buf,  sizeof(file_base_buf),
         file_ext_buf,   sizeof(file_ext_buf));

   if(!strcasecmp(file_ext_buf, ".toc"))
   {
      log_cb(RETRO_LOG_INFO, "TOC file detected.\n");
      IsTOC = true;
   }

   /* Check for annoying UTF-8 BOM. */
   if(!IsTOC)
   {
      uint8_t bom_tmp[3];

      if(cdstream_read(&fp, bom_tmp, 3) == 3 && bom_tmp[0] == 0xEF && bom_tmp[1] == 0xBB && bom_tmp[2] == 0xBF)
      {
         log_cb(RETRO_LOG_ERROR, "UTF-8 BOM detected at start of CUE sheet.\n");
      }
      else
         cdstream_seek(&fp, 0, SEEK_SET);
   }


   /* Assign opposite maximum values so our tests will work! */
   self->FirstTrack = 99;
   self->LastTrack  = 0;

   while (cdstream_get_line(&fp, linebuf, sizeof(linebuf)) >= 0)
   {
      unsigned argcount = 0;
      size_t   linelen;

      if (IsTOC)
      {
         /* Handle TOC format comments */
         char *ss_loc = strstr(linebuf, "//");
         if (ss_loc)
            *ss_loc = 0;
      }

      /* Trim AFTER comment removal so trailing ws on lines like
       * "MONKEY  // BABIES" gets stripped after the comment. */
      str_trim(linebuf);
      linelen = strlen(linebuf);

      if (linelen == 0)
         continue;

      /* Grab command and arguments. */
      {
         size_t offs = 0;

         offs = UnQuotify(linebuf, linelen, offs, cmdbuf, sizeof(cmdbuf), false);
         for (argcount = 0; argcount < max_args && offs < linelen; argcount++)
            offs = UnQuotify(linebuf, linelen, offs, args[argcount], sizeof(args[0]), true);

         /* Make sure unused arguments are cleared out so we don't have inter-line leaks! */
         {
            unsigned x;
            for (x = argcount; x < max_args; x++)
               args[x][0] = 0;
         }

         str_toupper(cmdbuf);
      }

      if (IsTOC)
      {
         if (!strcmp(cmdbuf, "TRACK"))
         {
            int format_lookup;

            if (active_track >= 0)
            {
               memcpy(&self->Tracks[active_track], &TmpTrack, sizeof(TmpTrack));
               memset(&TmpTrack, 0, sizeof(TmpTrack));
               active_track = -1;
            }

            if (AutoTrackInc > 99)
            {
               MDFN_Error(0, "Invalid track number: %d", AutoTrackInc);
               { ok = false; goto cleanup; }
            }

            active_track = AutoTrackInc++;
            if (active_track < self->FirstTrack)
               self->FirstTrack = active_track;
            if (active_track > self->LastTrack)
               self->LastTrack = active_track;

            for (format_lookup = 0; format_lookup < _DI_FORMAT_COUNT; format_lookup++)
            {
               if (!strcasecmp(args[0], DI_CDRDAO_Strings[format_lookup]))
               {
                  TmpTrack.DIFormat = format_lookup;
                  break;
               }
            }

            if (format_lookup == _DI_FORMAT_COUNT)
            {
               MDFN_Error(0, "Invalid track format: %s", args[0]);
               { ok = false; goto cleanup; }
            }

            if (TmpTrack.DIFormat == DI_FORMAT_AUDIO)
               TmpTrack.RawAudioMSBFirst = true; /* Silly cdrdao... */

            if (!strcasecmp(args[1], "RW"))
            {
               TmpTrack.SubchannelMode = CDRF_SUBM_RW;
               MDFN_Error(0, "\"RW\" format subchannel data not supported, only \"RW_RAW\" is!");
               { ok = false; goto cleanup; }
            }
            else if (!strcasecmp(args[1], "RW_RAW"))
               TmpTrack.SubchannelMode = CDRF_SUBM_RW_RAW;

         } /* end to TRACK */
         else if (!strcmp(cmdbuf, "SILENCE"))
         {
            /* Unsupported but tolerated */
         }
         else if (!strcmp(cmdbuf, "ZERO"))
         {
            /* Unsupported but tolerated */
         }
         else if (!strcmp(cmdbuf, "FIFO"))
         {
            MDFN_Error(0, "Unsupported directive: %s", cmdbuf);
            { ok = false; goto cleanup; }
         }
         else if (!strcmp(cmdbuf, "FILE") || !strcmp(cmdbuf, "AUDIOFILE"))
         {
            const char *binoffset = NULL;
            const char *msfoffset = NULL;
            const char *length    = NULL;

            if (args[1][0] == '#')
            {
               binoffset = args[1] + 1;
               msfoffset = args[2];
               length    = args[3];
            }
            else
            {
               msfoffset = args[1];
               length    = args[2];
            }
            CDAccess_Image_ParseTOCFileLineInfo(self, &TmpTrack, active_track,
                  args[0], binoffset, msfoffset, length, image_memcache, &cache);
         }
         else if (!strcmp(cmdbuf, "DATAFILE"))
         {
            const char *binoffset = NULL;
            const char *length    = NULL;

            if (args[1][0] == '#')
            {
               binoffset = args[1] + 1;
               length    = args[2];
            }
            else
               length = args[1];

            CDAccess_Image_ParseTOCFileLineInfo(self, &TmpTrack, active_track,
                  args[0], binoffset, NULL, length, image_memcache, &cache);
         }
         else if (!strcmp(cmdbuf, "INDEX"))
         {
         }
         else if (!strcmp(cmdbuf, "PREGAP"))
         {
            int m, s, f;
            if (active_track < 0)
            {
               MDFN_Error(0, "Command %s is outside of a TRACK definition!\n", cmdbuf);
               { ok = false; goto cleanup; }
            }
            sscanf(args[0], "%d:%d:%d", &m, &s, &f);
            TmpTrack.pregap = (m * 60 + s) * 75 + f;
         }
         else if (!strcmp(cmdbuf, "START"))
         {
            int m, s, f;
            if (active_track < 0)
            {
               MDFN_Error(0, "Command %s is outside of a TRACK definition!\n", cmdbuf);
               { ok = false; goto cleanup; }
            }
            sscanf(args[0], "%d:%d:%d", &m, &s, &f);
            TmpTrack.pregap = (m * 60 + s) * 75 + f;
         }
         else if (!strcmp(cmdbuf, "TWO_CHANNEL_AUDIO"))
            TmpTrack.subq_control &= ~SUBQ_CTRLF_4CH;
         else if (!strcmp(cmdbuf, "FOUR_CHANNEL_AUDIO"))
            TmpTrack.subq_control |= SUBQ_CTRLF_4CH;
         else if (!strcmp(cmdbuf, "NO"))
         {
            str_toupper(args[0]);

            if (!strcmp(args[0], "COPY"))
               TmpTrack.subq_control &= ~SUBQ_CTRLF_DCP;
            else if (!strcmp(args[0], "PRE_EMPHASIS"))
               TmpTrack.subq_control &= ~SUBQ_CTRLF_PRE;
            else
            {
               MDFN_Error(0, "Unsupported argument to \"NO\" directive: %s", args[0]);
               { ok = false; goto cleanup; }
            }
         }
         else if (!strcmp(cmdbuf, "COPY"))
            TmpTrack.subq_control |= SUBQ_CTRLF_DCP;
         else if (!strcmp(cmdbuf, "PRE_EMPHASIS"))
            TmpTrack.subq_control |= SUBQ_CTRLF_PRE;
         /* TODO: Confirm that these are taken from the TOC of the disc, and not synthesized by cdrdao. */
         else if (!strcmp(cmdbuf, "CD_DA"))
            self->disc_type = DISC_TYPE_CDDA_OR_M1;
         else if (!strcmp(cmdbuf, "CD_ROM"))
            self->disc_type = DISC_TYPE_CDDA_OR_M1;
         else if (!strcmp(cmdbuf, "CD_ROM_XA"))
            self->disc_type = DISC_TYPE_CD_XA;
         else
         {
            /* Unrecognized but ignored - matches old behaviour */
         }
         /* TODO: CATALOG */

      } /*********** END TOC HANDLING ************/
      else /* now for CUE sheet handling */
      {
         if (!strcmp(cmdbuf, "FILE"))
         {
            char efn[IMAGE_PATH_BUF];

            if (active_track >= 0)
            {
               memcpy(&self->Tracks[active_track], &TmpTrack, sizeof(TmpTrack));
               memset(&TmpTrack, 0, sizeof(TmpTrack));
               active_track = -1;
            }

            if (!strstr(args[0], "cdrom://"))
               MDFN_EvalFIP_c(self->base_dir, args[0], efn, sizeof(efn));
            else
               strlcpy(efn, args[0], sizeof(efn));

            {
               cdstream *probe2 = cdstream_new(efn);
               if (!probe2)
               {
                  MDFN_Error(0, "Could not open track file \"%s\"", efn);
                  { ok = false; goto cleanup; }
               }
               TmpTrack.fp = probe2;
            }
            TmpTrack.FirstFileInstance = 1;

            if (image_memcache && !cdstream_memcache_in_place(TmpTrack.fp))
            {
               /* memcache_in_place closed the stream on failure; free
                * the shell.  Clear TmpTrack.fp so cleanup doesn't
                * double-free. */
               free(TmpTrack.fp);
               TmpTrack.fp = NULL;
               { ok = false; goto cleanup; }
            }

            if (!strcasecmp(args[1], "BINARY"))
            {
               /* nothing extra */
            }
            else if (   !strcasecmp(args[1], "OGG")    || !strcasecmp(args[1], "VORBIS")
                     || !strcasecmp(args[1], "WAVE")   || !strcasecmp(args[1], "WAV")
                     || !strcasecmp(args[1], "PCM")    || !strcasecmp(args[1], "MPC")
                     || !strcasecmp(args[1], "MP+"))
            {
               TmpTrack.AReader = AR_Open(TmpTrack.fp);

               if (!TmpTrack.AReader)
               {
                  MDFN_Error(0, "Unsupported audio track file format: %s\n", args[0]);
                  { ok = false; goto cleanup; }
               }
            }
            else
            {
               MDFN_Error(0, "Unsupported track format: %s\n", args[1]);
               { ok = false; goto cleanup; }
            }
         }
         else if (!strcmp(cmdbuf, "TRACK"))
         {
            int format_lookup;

            if (active_track >= 0)
            {
               memcpy(&self->Tracks[active_track], &TmpTrack, sizeof(TmpTrack));
               TmpTrack.FirstFileInstance = 0;
               TmpTrack.pregap            = 0;
               TmpTrack.pregap_dv         = 0;
               TmpTrack.postgap           = 0;
               TmpTrack.index[0]          = -1;
               TmpTrack.index[1]          = 0;
            }
            active_track = atoi(args[0]);

            if (active_track < self->FirstTrack)
               self->FirstTrack = active_track;
            if (active_track > self->LastTrack)
               self->LastTrack = active_track;

            for (format_lookup = 0; format_lookup < _DI_FORMAT_COUNT; format_lookup++)
            {
               if (!strcasecmp(args[1], DI_CUE_Strings[format_lookup]))
               {
                  TmpTrack.DIFormat = format_lookup;
                  break;
               }
            }

            if (format_lookup == _DI_FORMAT_COUNT)
            {
               MDFN_Error(0, "Invalid track format: %s\n", args[1]);
               { ok = false; goto cleanup; }
            }

            if (active_track < 0 || active_track > 99)
            {
               MDFN_Error(0, "Invalid track number: %d\n", active_track);
               { ok = false; goto cleanup; }
            }
         }
         else if (!strcmp(cmdbuf, "INDEX"))
         {
            if (active_track >= 0)
            {
               unsigned int m, s, f;

               if (sscanf(args[1], "%u:%u:%u", &m, &s, &f) != 3)
               {
                  MDFN_Error(0, "Malformed m:s:f time in \"%s\" directive: %s", cmdbuf, args[0]);
                  { ok = false; goto cleanup; }
               }

               if (!strcasecmp(args[0], "01") || !strcasecmp(args[0], "1"))
                  TmpTrack.index[1] = (m * 60 + s) * 75 + f;
               else if (!strcasecmp(args[0], "00") || !strcasecmp(args[0], "0"))
                  TmpTrack.index[0] = (m * 60 + s) * 75 + f;
            }
         }
         else if (!strcmp(cmdbuf, "PREGAP"))
         {
            if (active_track >= 0)
            {
               unsigned int m, s, f;

               if (sscanf(args[0], "%u:%u:%u", &m, &s, &f) != 3)
               {
                  MDFN_Error(0, "Malformed m:s:f time in \"%s\" directive: %s", cmdbuf, args[0]);
                  { ok = false; goto cleanup; }
               }

               TmpTrack.pregap = (m * 60 + s) * 75 + f;
            }
         }
         else if (!strcmp(cmdbuf, "POSTGAP"))
         {
            if (active_track >= 0)
            {
               unsigned int m, s, f;

               if (sscanf(args[0], "%u:%u:%u", &m, &s, &f) != 3)
               {
                  MDFN_Error(0, "Malformed m:s:f time in \"%s\" directive: %s", cmdbuf, args[0]);
                  { ok = false; goto cleanup; }
               }

               TmpTrack.postgap = (m * 60 + s) * 75 + f;
            }
         }
         else if (!strcmp(cmdbuf, "REM"))
         {
         }
         else if (!strcmp(cmdbuf, "FLAGS"))
         {
            unsigned i;
            TmpTrack.subq_control &= ~(SUBQ_CTRLF_PRE | SUBQ_CTRLF_DCP | SUBQ_CTRLF_4CH);
            for (i = 0; i < argcount; i++)
            {
               if (!strcmp(args[i], "DCP"))
                  TmpTrack.subq_control |= SUBQ_CTRLF_DCP;
               else if (!strcmp(args[i], "4CH"))
                  TmpTrack.subq_control |= SUBQ_CTRLF_4CH;
               else if (!strcmp(args[i], "PRE"))
                  TmpTrack.subq_control |= SUBQ_CTRLF_PRE;
               else if (!strcmp(args[i], "SCMS"))
               {
                  /* Not implemented, likely pointless. */
               }
               else
               {
                  MDFN_Error(0, "Unknown CUE sheet \"FLAGS\" directive flag \"%s\".\n", args[i]);
                  { ok = false; goto cleanup; }
               }
            }
         }
         else if (   !strcmp(cmdbuf, "CDTEXTFILE")
                  || !strcmp(cmdbuf, "CATALOG")
                  || !strcmp(cmdbuf, "ISRC")
                  || !strcmp(cmdbuf, "TITLE")
                  || !strcmp(cmdbuf, "PERFORMER")
                  || !strcmp(cmdbuf, "SONGWRITER"))
            log_cb(RETRO_LOG_ERROR, "Unsupported CUE sheet directive: \"%s\".\n", cmdbuf);
         else
         {
            MDFN_Error(0, "Unknown CUE sheet directive \"%s\".\n", cmdbuf);
            { ok = false; goto cleanup; }
         }
      } /* end of CUE sheet handling */
   } /* end of fgets() loop */

   if (active_track >= 0)
      memcpy(&self->Tracks[active_track], &TmpTrack, sizeof(TmpTrack));

   if (self->FirstTrack > self->LastTrack)
   {
      MDFN_Error(0, "No tracks found!\n");
      { ok = false; goto cleanup; }
   }

   self->NumTracks  = 1 + self->LastTrack - self->FirstTrack;

   {
      int x;
      for (x = self->FirstTrack; x < (self->FirstTrack + self->NumTracks); x++)
      {
         if (self->Tracks[x].DIFormat == DI_FORMAT_AUDIO)
            self->Tracks[x].subq_control &= ~SUBQ_CTRLF_DATA;
         else
            self->Tracks[x].subq_control |= SUBQ_CTRLF_DATA;

         if (!IsTOC) /* TOC-format self->disc_type calculation is handled differently. */
         {
            switch (self->Tracks[x].DIFormat)
            {
               case DI_FORMAT_MODE2:
               case DI_FORMAT_MODE2_FORM1:
               case DI_FORMAT_MODE2_FORM2:
               case DI_FORMAT_MODE2_RAW:
                  self->disc_type = DISC_TYPE_CD_XA;
                  break;
               default:
                  break;
            }
         }

         if (IsTOC)
         {
            RunningLBA            += self->Tracks[x].pregap;
            self->Tracks[x].LBA    = RunningLBA;
            RunningLBA            += self->Tracks[x].sectors;
            RunningLBA            += self->Tracks[x].postgap;
         }
         else /* else handle CUE sheet... */
         {
            if (self->Tracks[x].FirstFileInstance)
            {
               LastIndex  = 0;
               FileOffset = 0;
            }

            RunningLBA               += self->Tracks[x].pregap;

            self->Tracks[x].pregap_dv = 0;

            if (self->Tracks[x].index[0] != -1)
               self->Tracks[x].pregap_dv = self->Tracks[x].index[1] - self->Tracks[x].index[0];

            FileOffset               += self->Tracks[x].pregap_dv * DI_Size_Table[self->Tracks[x].DIFormat];

            RunningLBA               += self->Tracks[x].pregap_dv;

            self->Tracks[x].LBA       = RunningLBA;

            self->Tracks[x].FileOffset = FileOffset;
            self->Tracks[x].sectors    = CDAccess_Image_GetSectorCount(self, &self->Tracks[x]);

            if ((x + 1) >= (self->FirstTrack + self->NumTracks) || self->Tracks[x + 1].FirstFileInstance)
            {
            }
            else
            {
               /* Fix the sector count if we have multiple tracks per one binary image file. */
               if (self->Tracks[x + 1].index[0] == -1)
                  self->Tracks[x].sectors = self->Tracks[x + 1].index[1] - self->Tracks[x].index[1];
               else
                  self->Tracks[x].sectors = self->Tracks[x + 1].index[0] - self->Tracks[x].index[1];
            }

            RunningLBA  += self->Tracks[x].sectors;
            RunningLBA  += self->Tracks[x].postgap;

            FileOffset  += self->Tracks[x].sectors * DI_Size_Table[self->Tracks[x].DIFormat];
         } /* end to cue sheet handling */
      } /* end to track loop */
   }

   self->total_sectors = RunningLBA;

   /* Load SBI file, if present */
   if (!IsTOC)
   {
      char     sbi_path    [IMAGE_PATH_BUF];
      char     sbi_basename[IMAGE_PATH_BUF];
      char     sbi_ext[4] = { 's', 'b', 'i', 0 };
      size_t   ext_len    = strlen(file_ext_buf);

      if (ext_len == 4 && file_ext_buf[0] == '.')
      {
         unsigned i;
         for (i = 0; i < 3; i++)
         {
            if (file_ext_buf[1 + i] >= 'A' && file_ext_buf[1 + i] <= 'Z')
               sbi_ext[i] += 'A' - 'a';
         }
      }

      snprintf(sbi_basename, sizeof(sbi_basename), "%.*s.%s",
            (int)(sizeof(sbi_basename) - 6), file_base_buf, sbi_ext);
      MDFN_EvalFIP_c(self->base_dir, sbi_basename,
            sbi_path, sizeof(sbi_path));

      if (filestream_exists(sbi_path))
         CDAccess_Image_LoadSBI(self, sbi_path);
   }

cleanup:
   toc_streamcache_free(&cache);
   cdstream_close(&fp);
   return ok;
}

static void CDAccess_Image_Cleanup(CDAccess_Image *self){
   int32_t track;

   for (track = 0; track < 100; track++)
   {
      CDRFILE_TRACK_INFO *this_track = &self->Tracks[track];

      if (this_track->FirstFileInstance)
      {
         if (this_track->AReader)
         {
            AR_Close(this_track->AReader);
            this_track->AReader = NULL;
         }

         if (this_track->fp)
         {
            cdstream_destroy(this_track->fp);
            this_track->fp = NULL;
         }
      }
   }
}


static bool CDAccess_Image_Read_Raw_Sector(CDAccess *base_self, uint8_t *buf, int32_t lba){
   CDAccess_Image *self = (CDAccess_Image *)base_self;
   int32_t track;
   uint8_t SimuQ[0xC];
   bool TrackFound = false;

   memset(buf + 2352, 0, 96);

   CDAccess_Image_MakeSubPQ(self, lba, buf + 2352);

   subq_deinterleave(buf + 2352, SimuQ);

   for(track = self->FirstTrack; track < (self->FirstTrack + self->NumTracks); track++)
   {
      CDRFILE_TRACK_INFO *ct = &self->Tracks[track];

      if(lba >= (ct->LBA - ct->pregap_dv - ct->pregap) && lba < (ct->LBA + ct->sectors + ct->postgap))
      {
         TrackFound = true;

         /* Handle pregap and postgap reading */
         if(lba < (ct->LBA - ct->pregap_dv) || lba >= (ct->LBA + ct->sectors))
         {
            memset(buf, 0, 2352);	/* Null sector data, per spec */
         }
         else
         {
            if(ct->AReader)
            {
               /* AR_Read writes host-endian int16 samples; every
                * caller of CDAccess_Image_Read_Raw_Sector provides a
                * buf with at least 4-byte alignment - it's either
                * CDIF_Sector_Buffer.data (offset 12 of a malloc'd
                * struct) or a stack uint8 array of >=2352 bytes
                * (gcc/clang default-align such arrays to 16 in the
                * caller's frame). Reading AR_Read straight into buf
                * removes the 2352 B memcpy + 2352 B stack scratch
                * the original code paid per CDDA sector.
                *
                * The (void*) intermediate cast keeps -Wcast-align
                * quiet on strict-alignment targets; the underlying
                * alignment is safe per above. */
               int64_t frames_read = AR_Read(ct->AReader,
                     (ct->FileOffset / 4) + (lba - ct->LBA) * 588,
                     (int16_t *)(void *)buf, 588);

               ct->LastSamplePos += frames_read;

               if(frames_read < 0 || frames_read > 588)	/* This shouldn't happen. */
                  frames_read = 0;

               if(frames_read < 588)
                  memset(buf + frames_read * 2 * sizeof(int16_t), 0, (588 - frames_read) * 2 * sizeof(int16_t));

               /* Path 2 contract: buf holds host-endian int16
                * stereo samples. AR_Read already wrote host-endian
                * int16 into buf, so we're done - no swap on any
                * host. */
            }
            else	/* Binary, woo. */
            {
               long SeekPos   = ct->FileOffset;
               long LBARelPos = lba - ct->LBA;

               SeekPos += LBARelPos * DI_Size_Table[ct->DIFormat];

               if(ct->SubchannelMode)
                  SeekPos += 96 * (lba - ct->LBA);

               cdstream_seek(ct->fp, SeekPos, SEEK_SET);

               switch(ct->DIFormat)
               {
                  case DI_FORMAT_AUDIO:
                     cdstream_read(ct->fp, buf, 2352);

                     /* Path 2 contract: buf holds host-endian int16
                      * stereo samples. Swap iff source byte order
                      * differs from host byte order:
                      *   LE host + LE source (default BIN)   -> no swap
                      *   LE host + BE source (cdrdao TOC)    -> swap
                      *   BE host + LE source                 -> swap
                      *   BE host + BE source                 -> no swap
                      * Compile-time #ifdef collapses to a single
                      * runtime branch on RawAudioMSBFirst. */
#ifdef MSB_FIRST
                     if(!ct->RawAudioMSBFirst)
#else
                     if(ct->RawAudioMSBFirst)
#endif
                     {
                        /* 32-bit-chunked A16 swap: 2352 bytes / 4 =
                         * 588 iterations, each handling two 16-bit
                         * samples via a 32-bit load + bit-mask shuffle
                         * + 32-bit store. Halves the loop count vs
                         * the per-pair byte swap. */
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
                     break;

                  case DI_FORMAT_MODE1:
                     cdstream_read(ct->fp, buf + 12 + 3 + 1, 2048);
                     encode_mode1_sector(lba + 150, buf);
                     break;

                  case DI_FORMAT_MODE1_RAW:
                  case DI_FORMAT_MODE2_RAW:
                     cdstream_read(ct->fp, buf, 2352);
                     break;

                  case DI_FORMAT_MODE2:
                     cdstream_read(ct->fp, buf + 16, 2336);
                     encode_mode2_sector(lba + 150, buf);
                     break;


                     /* FIXME: M2F1, M2F2, does sub-header come before or after user data(standards say before, but I wonder
                      * about cdrdao...). */
                  case DI_FORMAT_MODE2_FORM1:
                     cdstream_read(ct->fp, buf + 24, 2048);
                     /*encode_mode2_form1_sector(lba + 150, buf);*/
                     break;

                  case DI_FORMAT_MODE2_FORM2:
                     cdstream_read(ct->fp, buf + 24, 2324);
                     /*encode_mode2_form2_sector(lba + 150, buf);*/
                     break;

               }

               if(ct->SubchannelMode)
                  cdstream_read(ct->fp, buf + 2352, 96);
            }
         } /* end if audible part of audio track read. */
         break;
      } /* End if LBA is in range */
   } /* end track search loop */

   if(!TrackFound)
   {
      MDFN_Error(0, "Could not find track for sector %u!", lba);
      return false;
   }

   return true;
}

/* Note: this function makes use of the current contents(as in |=) in SubPWBuf. */
static void CDAccess_Image_MakeSubPQ(CDAccess_Image *self, int32_t lba, uint8_t *SubPWBuf){
   unsigned i;
   uint8_t buf[0xC], adr, control;
   int32_t track;
   uint32_t lba_relative;
   uint32_t ma, sa, fa;
   uint32_t m, s, f;
   uint8_t pause_or = 0x00;
   bool track_found = false;
   const uint8_t *replace;

   for(track = self->FirstTrack; track < (self->FirstTrack + self->NumTracks); track++)
   {
      if(lba >= (self->Tracks[track].LBA - self->Tracks[track].pregap_dv - self->Tracks[track].pregap)
            && lba < (self->Tracks[track].LBA + self->Tracks[track].sectors + self->Tracks[track].postgap))
      {
         track_found = true;
         break;
      }
   }

   if(!track_found)
      track = self->FirstTrack;

   lba_relative = abs((int32_t)lba - self->Tracks[track].LBA);

   f            = (lba_relative % 75);
   s            = ((lba_relative / 75) % 60);
   m            = (lba_relative / 75 / 60);

   fa           = (lba + 150) % 75;
   sa           = ((lba + 150) / 75) % 60;
   ma           = ((lba + 150) / 75 / 60);

   adr          = 0x1; /* Q channel data encodes position */
   control      = self->Tracks[track].subq_control;

   /* Handle pause(D7 of interleaved subchannel byte) bit, should be set to 1 when in pregap or postgap. */
   if((lba < self->Tracks[track].LBA) || (lba >= self->Tracks[track].LBA + self->Tracks[track].sectors))
      pause_or = 0x80;

   /* Handle pregap between audio->data track */
   {
      int32_t pg_offset = (int32_t)lba - self->Tracks[track].LBA;

      if(pg_offset < -150)
      {
         if((self->Tracks[track].subq_control & SUBQ_CTRLF_DATA) && (self->FirstTrack < track) && !(self->Tracks[track - 1].subq_control & SUBQ_CTRLF_DATA))
            control = self->Tracks[track - 1].subq_control;
      }
   }

   memset(buf, 0, 0xC);
   buf[0] = (adr << 0) | (control << 4);
   buf[1] = U8_to_BCD(track);

   if(lba < self->Tracks[track].LBA) /* Index is 00 in pregap */
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

   if (!subq_map_empty(&self->SubQReplaceMap))
   {
      replace = subq_map_find(&self->SubQReplaceMap, LBA_to_ABA(lba));
      if (replace)
         memcpy(buf, replace, 12);
   }

   for (i = 0; i < 96; i++)
      SubPWBuf[i] |= (((buf[i >> 3] >> (7 - (i & 0x7))) & 1) ? 0x40 : 0x00) | pause_or;
}

static bool CDAccess_Image_Read_Raw_PW(CDAccess *base_self, uint8_t *buf, int32_t lba){
   CDAccess_Image *self = (CDAccess_Image *)base_self;
   memset(buf, 0, 96);
   CDAccess_Image_MakeSubPQ(self, lba, buf);
   return true;
}

static bool CDAccess_Image_Read_TOC(CDAccess *base_self, TOC *toc){
   CDAccess_Image *self = (CDAccess_Image *)base_self;
   unsigned i;

   TOC_Clear(toc);

   toc->first_track = self->FirstTrack;
   toc->last_track  = self->FirstTrack + self->NumTracks - 1;
   toc->disc_type   = self->disc_type;

   for(i = toc->first_track; i <= toc->last_track; i++)
   {
      toc->tracks[i].lba     = self->Tracks[i].LBA;
      toc->tracks[i].adr     = ADR_CURPOS;
      toc->tracks[i].control = self->Tracks[i].subq_control;
   }

   toc->tracks[100].lba     = self->total_sectors;
   toc->tracks[100].adr     = ADR_CURPOS;
   toc->tracks[100].control = toc->tracks[toc->last_track].control & 0x4;

   /* Convenience leadout track duplication. */
   if(toc->last_track < 99)
      toc->tracks[toc->last_track + 1] = toc->tracks[100];

   return true;
}

static void CDAccess_Image_Eject(CDAccess *base_self, bool eject_status){
   (void)base_self;
   (void)eject_status;
}

/* ------------------------------------------------------------------
 * destroy and factory.
 * ------------------------------------------------------------------ */

static void CDAccess_Image_destroy(CDAccess *base_self)
{
   CDAccess_Image *self = (CDAccess_Image *)base_self;
   CDAccess_Image_Cleanup(self);
   subq_map_clear(&self->SubQReplaceMap);
   free(self);
}

CDAccess *CDAccess_Image_New(bool *success, const char *path,
      bool image_memcache)
{
   CDAccess_Image *self = (CDAccess_Image *)calloc(1, sizeof(*self));
   if (!self)
   {
      *success = false;
      return NULL;
   }

   /* Vtable */
   self->base.Read_Raw_Sector = CDAccess_Image_Read_Raw_Sector;
   self->base.Read_Raw_PW     = CDAccess_Image_Read_Raw_PW;
   self->base.Read_TOC        = CDAccess_Image_Read_TOC;
   self->base.Eject           = CDAccess_Image_Eject;
   self->base.destroy         = CDAccess_Image_destroy;

   self->NumTracks     = 0;
   self->FirstTrack    = 0;
   self->LastTrack     = 0;
   self->total_sectors = 0;

   if (!CDAccess_Image_ImageOpen(self, path, image_memcache))
      *success = false;

   return &self->base;
}

