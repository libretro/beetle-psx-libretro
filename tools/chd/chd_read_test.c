/* CHD reader check against the plain image it was made from.
 *
 *    chd_read_test <reference.cue> <image.chd> [memcache]
 *
 * Opens both through cdaccess_open_image -- the CUE through the plain
 * image backend, the CHD through CDAccess_CHD -- and requires the two to
 * agree on the TOC and on every raw sector of the program area, 2352
 * bytes of sector plus 96 of subchannel. The pregap before LBA 0 and the
 * leadout are synthesised by each backend rather than read from the
 * image, and the plain image backend synthesises neither, so they are
 * not compared.
 *
 * The CHDs run.sh feeds this are compressed with one codec family each
 * (cdzs, cdlz, cdzl, cdfl), plus a child image that differences against
 * a parent and a grandchild that differences against the child, so a
 * codec the reader cannot decode, or a hunk resolved from the wrong
 * image of a chain, shows up as a sector mismatch. "memcache" opens the
 * CHD with image_memcache set, which reads it from memory rather than
 * through the file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../mednafen/cdrom/CDAccess.h"

#define RAW_BYTES (2352 + 96)

int main(int argc, char **argv)
{
   CDAccess *ref;
   CDAccess *chd;
   bool      ok_ref = true;
   bool      ok_chd = true;
   bool      memcache;
   TOC       toc_ref;
   TOC       toc_chd;
   int32_t   lba;
   int32_t   end;
   int       t;
   unsigned  bad   = 0;
   unsigned  count = 0;
   uint8_t   a[RAW_BYTES];
   uint8_t   b[RAW_BYTES];

   if (argc < 3)
   {
      fprintf(stderr, "usage: %s reference.cue image.chd [memcache]\n",
            argv[0]);
      return 2;
   }
   memcache = argc > 3 && !strcmp(argv[3], "memcache");

   ref = cdaccess_open_image(&ok_ref, argv[1], false);
   chd = cdaccess_open_image(&ok_chd, argv[2], memcache);
   if (!ok_ref || !ok_chd)
   {
      printf("%s: open failed (reference %d, chd %d)\nRESULT: FAIL\n",
            argv[2], ok_ref, ok_chd);
      if (ref)
         ref->destroy(ref);
      if (chd)
         chd->destroy(chd);
      return 1;
   }

   ref->Read_TOC(ref, &toc_ref);
   chd->Read_TOC(chd, &toc_chd);

   if (toc_ref.first_track != toc_chd.first_track
         || toc_ref.last_track != toc_chd.last_track)
   {
      printf("TOC track range differs: %d-%d vs %d-%d\n",
            toc_ref.first_track, toc_ref.last_track,
            toc_chd.first_track, toc_chd.last_track);
      bad++;
   }
   for (t = toc_ref.first_track; t <= toc_ref.last_track; t++)
   {
      if (toc_ref.tracks[t].lba != toc_chd.tracks[t].lba
            || toc_ref.tracks[t].control != toc_chd.tracks[t].control)
      {
         printf("TOC track %d differs: lba %u/%u control %u/%u\n", t,
               (unsigned)toc_ref.tracks[t].lba,
               (unsigned)toc_chd.tracks[t].lba,
               (unsigned)toc_ref.tracks[t].control,
               (unsigned)toc_chd.tracks[t].control);
         bad++;
      }
   }
   if (toc_ref.tracks[100].lba != toc_chd.tracks[100].lba)
   {
      printf("leadout differs: %u vs %u\n",
            (unsigned)toc_ref.tracks[100].lba,
            (unsigned)toc_chd.tracks[100].lba);
      bad++;
   }

   end = (int32_t)toc_ref.tracks[100].lba;
   for (lba = 0; lba < end; lba++)
   {
      memset(a, 0xA5, sizeof(a));
      memset(b, 0x5A, sizeof(b));
      ref->Read_Raw_Sector(ref, a, lba);
      chd->Read_Raw_Sector(chd, b, lba);
      count++;
      if (memcmp(a, b, RAW_BYTES))
      {
         if (bad < 8)
         {
            int i = 0;
            while (a[i] == b[i])
               i++;
            printf("sector %d differs at byte %d (%02x vs %02x)\n",
                  (int)lba, i, a[i], b[i]);
         }
         bad++;
      }
   }

   ref->destroy(ref);
   chd->destroy(chd);

   printf("%s%s: %u sectors, %u mismatches\nRESULT: %s\n", argv[2],
         memcache ? " (memcache)" : "", count, bad, bad ? "FAIL" : "PASS");
   return bad ? 1 : 0;
}
