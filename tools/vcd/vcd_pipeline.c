/* End-to-end check of the VCD decode path.
 *
 * Wraps a real MPEG-1 program stream in Mode 2 Form 2 sectors exactly as a
 * Video CD carries it, pushes them through VCD_FeedSector, and verifies that
 * pictures and audio come back out. This exercises the seam the unit tests
 * for rmpeg1_ps / rmpeg1_video / rmp3 do not: the sector tap, the subheader
 * filtering, the packet routing, and the colour conversion.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../mednafen/psx/vcd.h"

/* vcd.c reaches CDIF only from VCD_ProbeDisc, which this test does not call. */
struct CDIF;
int CDIF_ReadSector(struct CDIF *c, uint8_t *b, uint32_t l, uint32_t n)
{ (void)c; (void)b; (void)l; (void)n; return 0; }

int main(int argc, char **argv)
{
   FILE     *f;
   long      len;
   uint8_t  *mpg;
   size_t    off = 0;
   unsigned  sectors = 0, frames = 0;
   size_t    audio_frames = 0;
   int16_t   abuf[8192 * 2];

   if (argc < 2) { fprintf(stderr, "usage: %s file.mpg\n", argv[0]); return 2; }
   f = fopen(argv[1], "rb");
   if (!f) { perror("open"); return 2; }
   fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
   mpg = (uint8_t *)malloc(len);
   if (fread(mpg, 1, len, f) != (size_t)len) return 2;
   fclose(f);

   VCD_Init();
   VCD_SetMode(VCD_MODE_HLE);
   VCD_SetAVSwitch(true);          /* arms the decoder and sets PLAY */

   while (off < (size_t)len)
   {
      uint8_t sec[2352];
      size_t  n = (size_t)len - off;

      if (n > VCD_FORM2_PAYLOAD) n = VCD_FORM2_PAYLOAD;

      memset(sec, 0, sizeof(sec));
      /* 12 sync + 4 header + 8 subheader, then payload at offset 24.
       * Submode bit5 = Form 2, bit1 = video, bit2 = real-time. */
      sec[16 + 2] = 0x20 | 0x02 | 0x04;
      sec[16 + 6] = sec[16 + 2];   /* subheader is duplicated */
      memcpy(sec + 24, mpg + off, n);

      VCD_FeedSector(sec, 150 + sectors);
      sectors++;
      off += n;

      if (VCD_RunFrame())
         frames++;
      audio_frames += VCD_GetAudio(abuf, 8192);
   }

   /* Drain whatever the decoders still hold. */
   { int guard = 0;
     while (VCD_RunFrame() && guard++ < 4096) frames++;
     audio_frames += VCD_GetAudio(abuf, 8192); }

   {
      unsigned    w = 0, h = 0;
      size_t      pitch = 0;
      const void *fb = VCD_GetVideo(&w, &h, &pitch);

      printf("sectors fed   : %u\n", sectors);
      printf("pictures out  : %u\n", frames);
      printf("audio frames  : %zu  (%u Hz)\n", audio_frames, VCD_GetSampleRate());
      printf("frame rate    : %.4f\n", VCD_GetFrameRate());
      printf("last picture  : %ux%u pitch %zu %s\n", w, h, pitch,
             fb ? "present" : "MISSING");

      /* A decoded picture must not be uniformly one value: that is what an
       * all-black or all-grey frame looks like when the pipeline runs but
       * decodes nothing. */
      if (fb && w && h)
      {
         const uint16_t *px = (const uint16_t *)fb;
         size_t stride16 = pitch / 2, x, y;
         uint16_t first = px[0];
         int varied = 0;
         for (y = 0; y < h && !varied; y++)
            for (x = 0; x < w; x++)
               if (px[y * stride16 + x] != first) { varied = 1; break; }
         printf("picture varied: %s\n", varied ? "yes" : "NO (uniform)");
         if (!varied) { VCD_Kill(); free(mpg); return 1; }
      }

      VCD_Kill();
      free(mpg);
      if (!fb || frames < 10 || audio_frames < 10000) {
         printf("RESULT: FAIL\n"); return 1; }
   }
   printf("RESULT: PASS\n");
   return 0;
}
