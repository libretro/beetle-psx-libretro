/* VCD_StateAction round-trip.
 *
 * Drives the transport into a non-default state, saves, perturbs everything,
 * loads, and checks the transport came back -- and that the decoders were
 * dropped and re-primed rather than surviving with stale contents.
 *
 * Uses the real SFORMAT machinery through a minimal StateMem, so this
 * exercises the same path a frontend save does.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../mednafen/psx/vcd.h"

struct CDIF;
int CDIF_ReadSector(struct CDIF *c, uint8_t *b, uint32_t lba, uint32_t n)
{ (void)c; (void)b; (void)lba; (void)n; return 0; }

/* The core's StateMem, as mednafen/state.h declares it. */
#include "../../mednafen/state.h"

static int fails = 0;
static void chk(const char *what, int cond)
{ printf("  %-46s %s\n", what, cond ? "ok" : "FAIL"); if (!cond) fails++; }

int main(int argc, char **argv)
{
   FILE    *f;
   long     len;
   uint8_t *mpg;
   size_t   off = 0, sectors = 0;
   StateMem sm;
   uint32_t saved_lba;

   if (argc < 2) { fprintf(stderr, "usage: %s file.mpg\n", argv[0]); return 2; }
   f = fopen(argv[1], "rb");
   if (!f) { perror("open"); return 2; }
   fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
   mpg = (uint8_t *)malloc(len);
   if (fread(mpg, 1, len, f) != (size_t)len) return 2;
   fclose(f);

   VCD_Init();
   VCD_SetMode(VCD_MODE_HLE);
   VCD_SetAVSwitch(true);

   /* Push some sectors through so the decoders hold real state. */
   while (off < (size_t)len && sectors < 80)
   {
      uint8_t sec[2352];
      size_t  n = (size_t)len - off;
      if (n > VCD_FORM2_PAYLOAD) n = VCD_FORM2_PAYLOAD;
      memset(sec, 0, sizeof(sec));
      sec[16 + 2] = 0x20 | 0x02 | 0x04;
      sec[16 + 6] = sec[16 + 2];
      memcpy(sec + 24, mpg + off, n);
      VCD_FeedSector(sec, 5000 + (uint32_t)sectors);
      VCD_RunFrame();
      sectors++; off += n;
   }

   saved_lba = VCD_GetPositionLBA();
   printf("before save: lba=%u xport=%d\n", saved_lba, (int)VCD_GetTransport());
   chk("decoder is primed before save", VCD_GetVideo(NULL, NULL, NULL) != NULL);

   memset(&sm, 0, sizeof(sm));
   sm.data     = (uint8_t *)malloc(1 << 16);
   sm.malloced = 1 << 16;
   sm.loc      = 0;
   sm.len      = 0;
   chk("save succeeds", VCD_StateAction(&sm, 0, 1) != 0);
   chk("save wrote something", sm.loc > 0);

   /* Perturb everything the state is supposed to own. */
   VCD_Stop();
   VCD_SetMode(VCD_MODE_OFF);
   VCD_SetPadState(0xFFFF);

   sm.len = sm.loc;
   sm.loc = 0;
   chk("load succeeds", VCD_StateAction(&sm, 1, 1) != 0);

   printf("after load : lba=%u xport=%d mode=%d\n",
          VCD_GetPositionLBA(), (int)VCD_GetTransport(), (int)VCD_GetMode());

   chk("position restored",  VCD_GetPositionLBA() == saved_lba);
   chk("transport restored", VCD_GetTransport()   == VCD_XPORT_PLAY);
   chk("mode restored",      VCD_GetMode()        == VCD_MODE_HLE);
   /* The decoders must have been dropped, not carried across. */
   chk("decoder dropped on load", VCD_GetVideo(NULL, NULL, NULL) == NULL);

   /* And must re-prime from the restored position. */
   off = 0; sectors = 0;
   while (off < (size_t)len && sectors < 80)
   {
      uint8_t sec[2352];
      size_t  n = (size_t)len - off;
      if (n > VCD_FORM2_PAYLOAD) n = VCD_FORM2_PAYLOAD;
      memset(sec, 0, sizeof(sec));
      sec[16 + 2] = 0x20 | 0x02 | 0x04;
      sec[16 + 6] = sec[16 + 2];
      memcpy(sec + 24, mpg + off, n);
      VCD_FeedSector(sec, 5000 + (uint32_t)sectors);
      VCD_RunFrame();
      sectors++; off += n;
   }
   chk("decoder re-primes after load", VCD_GetVideo(NULL, NULL, NULL) != NULL);

   free(sm.data);
   free(mpg);
   VCD_Kill();
   printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
   return fails ? 1 : 0;
}

