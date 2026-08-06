/* End-to-end Video CD test through the real disc path.
 *
 * Every other Video CD test here works on elementary streams, or hands
 * synthesised sectors straight to VCD_FeedSector. This one opens an actual
 * CUE with CDIF_Open, reads the TOC, runs VCD_ProbeDisc against the image,
 * then walks track 2 with CDIF_ReadRawSector and feeds what the drive
 * returns -- the same call cdc.c's sector tap uses.
 *
 * That covers what the others structurally cannot: the CUE parse, the TOC,
 * Mode 2 sector framing as CDIF hands it over, whether the probe finds the
 * control sectors at their mandated LBAs on a real image, and whether the
 * subheader filtering in VCD_FeedSector agrees with what a disc actually
 * carries.
 *
 * Build it against an image from tools/vcd/make_vcd.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../mednafen/psx/vcd.h"
#include "../../mednafen/cdrom/cdromif.h"

static int fails = 0;

static void chk(const char *what, int cond)
{
   printf("  %-48s %s\n", what, cond ? "ok" : "FAIL");
   if (!cond)
      fails++;
}

static const char *disc_name(VCD_DiscType t)
{
   switch (t)
   {
      case VCD_DISC_VCD11: return "VCD 1.1";
      case VCD_DISC_VCD20: return "VCD 2.0";
      case VCD_DISC_SVCD:  return "SVCD";
      case VCD_DISC_HQVCD: return "HQ-VCD";
      default:             return "none";
   }
}

int main(int argc, char **argv)
{
   CDIF        *cdif;
   bool         ok = false;
   TOC          toc;
   VCD_DiscInfo di;
   VCD_DiscType type;
   uint32_t     t2_lba, t2_end, lba;
   unsigned     fed = 0, frames = 0;
   size_t       audio = 0;
   int16_t      abuf[8192 * 2];

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s image.cue\n", argv[0]);
      return 2;
   }

   cdif = CDIF_Open(&ok, argv[1], false, true);
   chk("CDIF_Open on the CUE", ok && cdif != NULL);
   if (!ok || !cdif)
   {
      printf("RESULT: FAIL\n");
      return 1;
   }

   CDIF_ReadTOC(cdif, &toc);
   printf("TOC: first=%d last=%d  track2 lba=%u  leadout=%u\n",
          toc.first_track, toc.last_track,
          toc.tracks[2].lba, toc.tracks[100].lba);

   chk("two tracks in the TOC",
       toc.first_track == 1 && toc.last_track == 2);
   chk("track 2 is a data track",
       (toc.tracks[2].control & 0x04) != 0);

   /* --- the probe, against a real image ---------------------------- */
   type = VCD_ProbeDisc(cdif, &di);
   printf("probe: %s, %u entries, %s, PBC %s, album \"%s\"\n",
          disc_name(type), di.num_entries, di.pal ? "PAL" : "NTSC",
          di.has_pbc ? "present" : "absent", di.album_id);

   chk("probe identifies a Video CD", type != VCD_DISC_NONE);
   chk("probe found the chapter list", di.num_entries >= 1);
   chk("chapter 0 is on track 2",
       di.num_entries >= 1 && di.entries[0].track == 2);
   chk("chapter 0 LBA agrees with the TOC",
       di.num_entries >= 1 && di.entries[0].lba == toc.tracks[2].lba);

   /* --- play it, through the drive --------------------------------- */
   VCD_Init();
   VCD_SetMode(VCD_MODE_HLE);
   VCD_SetAVSwitch(true);
   if (di.num_entries)
      VCD_Play(0);

   t2_lba = toc.tracks[2].lba;
   t2_end = toc.tracks[100].lba;      /* lead-out */

   for (lba = t2_lba; lba < t2_end; lba++)
   {
      uint8_t raw[2352 + 96];

      if (!CDIF_ReadRawSector(cdif, raw, lba, -1))
         break;

      VCD_FeedSector(raw, lba);
      fed++;

      if (VCD_RunFrame())
         frames++;
      audio += VCD_GetAudio(abuf, 8192);
   }

   {
      int guard = 0;
      while (VCD_RunFrame() && guard++ < 4096)
         frames++;
      audio += VCD_GetAudio(abuf, 8192);
   }

   printf("read %u sectors from track 2: %u pictures, %zu audio frames @ %u Hz\n",
          fed, frames, audio, VCD_GetSampleRate());

   /* Enough to be sure the walk happened; the shortest test stream is
    * under a hundred sectors. */
   chk("sectors were read off the image", fed > 50);
   chk("pictures decoded from the disc", frames > 10);
   chk("audio decoded from the disc", audio > 10000);

   {
      unsigned    w = 0, h = 0;
      size_t      pitch = 0;
      const void *fb = VCD_GetVideo(&w, &h, &pitch);

      printf("last picture: %ux%u  rate %.4f\n", w, h, VCD_GetFrameRate());
      chk("a picture is available", fb != NULL);
      chk("geometry is sane", w >= 320 && h >= 240);

      /* A frame that decoded but is a flat colour means the pipeline ran and
       * produced nothing -- the failure that looks like success. */
      if (fb && w && h)
      {
         const uint16_t *px = (const uint16_t *)fb;
         size_t   stride16 = pitch / 2, x, y;
         uint16_t first = px[0];
         int      varied = 0;

         for (y = 0; y < h && !varied; y++)
            for (x = 0; x < w; x++)
               if (px[y * stride16 + x] != first) { varied = 1; break; }

         chk("picture is not a flat colour", varied);
      }
   }

   VCD_Kill();
   CDIF_Close(cdif);

   printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
   return fails ? 1 : 0;
}
