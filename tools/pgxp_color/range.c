/* Over-range counter check for the PGXP precise-colour path.
 *
 * oracle.c proves an accepted shadow is correct; transport.c proves one
 * arrives. Neither says whether recovering it would change any pixel, and
 * for an HDR renderer slice that is the deciding question: a colour whose
 * channels all sit at or below 255 requantizes to the byte the
 * architectural path already carried, so a wide framebuffer has nothing to
 * hold. Hit rate is a precondition; over-white frequency is the go/no-go.
 *
 * This checks the counters that measure it, by feeding MAC values whose
 * over-range classification is known by construction and comparing against
 * PGXP_GetColorRangeStats. It says nothing about real content -- no game
 * has been run against it -- only that the number the measurement slice
 * reports is the number it claims to report.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "pgxp_gpu.h"
#include "pgxp_value.h"
#include "pgxp_types.h"

extern PGXP_value CB[64];

static int fails = 0;

static void chk(const char *what, int cond)
{
   printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
   if (!cond)
      fails++;
}

/* MAC -> architectural byte, the Lm_C(MACn >> 4) saturation from
 * MAC_to_RGB_FIFO in gte.c. */
static uint32_t lm_c(int32_t mac)
{
   int32_t v = mac >> 4;
   if (v < 0)   return 0;
   if (v > 255) return 255;
   return (uint32_t)v;
}

static uint32_t gte_pack(int32_t m1, int32_t m2, int32_t m3)
{
   return lm_c(m1) | (lm_c(m2) << 8) | (lm_c(m3) << 16);
}

static void push(int32_t m1, int32_t m2, int32_t m3, uint32_t word)
{
   CB[0].x     = (float)m1 / 16.0f;
   CB[0].y     = (float)m2 / 16.0f;
   CB[0].z     = (float)m3 / 16.0f;
   CB[0].value = word;
   CB[0].flags = VALID_ALL;
}

/* One accepted colour at the given peak channel, in 8-bit scale. */
static void feed(double peak8)
{
   int32_t  mac  = (int32_t)(peak8 * 16.0);
   uint32_t word = gte_pack(mac, 0, 0);
   push(mac, 0, 0, word);
   PGXP_GetColor(0, &word, NULL);
}

int main(void)
{
   uint32_t over = 0, buckets[4] = {0,0,0,0};
   float    peak = 0.0f;
   uint32_t stats[4];
   int      i;

   printf("PGXP precise-colour over-range counters\n\n");

   /* Nothing above white yet. 255 exactly is not over: it is the ceiling,
    * and it requantizes to the byte the architectural path already had. */
   for (i = 0; i < 100; i++)
      feed(200.0);
   feed(255.0);
   PGXP_GetColorRangeStats(&over, buckets, &peak);
   chk("in-range colours do not count as over-white", over == 0);
   chk("peak tracks the largest in-range channel", peak == 255.0f);

   /* One in each bucket. Ratios are peak/255. */
   feed(255.0 * 1.10);   /* bucket 0 */
   feed(255.0 * 1.40);   /* bucket 1 */
   feed(255.0 * 1.90);   /* bucket 2 */
   feed(255.0 * 3.00);   /* bucket 3 */
   PGXP_GetColorRangeStats(&over, buckets, &peak);
   chk("four over-white colours counted", over == 4);
   chk("bucket <=1.25x", buckets[0] == 1);
   chk("bucket <=1.5x",  buckets[1] == 1);
   chk("bucket <=2x",    buckets[2] == 1);
   chk("bucket >2x",     buckets[3] == 1);
   chk("peak follows the brightest", peak > 255.0f * 2.9f);

   /* Boundaries land in the lower bucket, not the higher one. */
   feed(255.0 * 1.25);
   feed(255.0 * 1.50);
   feed(255.0 * 2.00);
   PGXP_GetColorRangeStats(&over, buckets, NULL);
   chk("1.25x boundary is bucket 0", buckets[0] == 2);
   chk("1.50x boundary is bucket 1", buckets[1] == 2);
   chk("2.00x boundary is bucket 2", buckets[2] == 2);

   /* A refused shadow must not be counted however bright it is: it cannot
    * be used, so counting it would overstate the case for the feature. */
   {
      uint32_t before = over;
      int32_t  mac    = (int32_t)(255.0 * 4.0 * 16.0);
      uint32_t word   = gte_pack(mac, 0, 0) ^ 0x00000001u; /* altered */
      push(mac, 0, 0, gte_pack(mac, 0, 0));
      PGXP_GetColor(0, &word, NULL);
      PGXP_GetColorRangeStats(&over, NULL, NULL);
      chk("a refused shadow is not counted as over-white", over == before);
   }

   PGXP_GetColorStats(stats);
   printf("\n  attempts=%u hits=%u over-white=%u peak=%.1f (%.2fx)\n",
          stats[0], stats[1], over, peak, (double)peak / 255.0);

   printf(fails ? "\nRESULT: FAIL\n" : "\nRESULT: PASS\n");
   return fails ? 1 : 0;
}
