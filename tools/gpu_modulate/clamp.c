/* Where texture modulation clamps, and what the counters count.
 *
 * ModTexel multiplies the 5-bit texel channel by the 8-bit vertex colour
 * with 0x80 as unity, then looks the product up in DitherLUT, which adds the
 * dither offset, shifts down by 3 and clamps to 0x1F. So the LUT's input is
 * an 8-bit-scale value and an input above 255 is exactly a channel the
 * hardware discards.
 *
 * This rebuilds the LUT from gpu.c's own construction and checks that claim
 * directly, rather than taking the arithmetic on trust: where the saturation
 * boundary actually falls, that unity really is unity, and what the largest
 * reachable overshoot is. It needs no GPU, no content and no core.
 */
#include <stdio.h>
#include <stdint.h>

/* gpu.c's dither table and LUT construction. */
static const int dither_table[4][4] =
{
   { -4,  0, -3,  1 },
   {  2, -2,  3, -1 },
   { -3,  1, -4,  0 },
   {  3, -1,  2, -2 },
};

static uint8_t lut[4][512][4];

static void build(void)
{
   int y, x, v;
   for (y = 0; y < 4; y++)
      for (x = 0; x < 4; x++)
         for (v = 0; v < 512; v++)
         {
            int value = v + dither_table[y][x];
            value >>= 3;
            if (value < 0)    value = 0;
            if (value > 0x1F) value = 0x1F;
            lut[y][v][x] = (uint8_t)value;
         }
}

#define IDX_R(texel, r) ((((texel) & 0x1F) * (r)) >> 4)

static int fails = 0;
static void chk(const char *what, int cond)
{ printf("  %-54s %s\n", what, cond ? "ok" : "FAIL"); if (!cond) fails++; }

int main(void)
{
   int v, y, x;
   int first_sat = -1;

   build();

   /* Unity: vertex colour 0x80 must return the texel, or the "0x80 is 1.0"
    * premise the whole headroom argument rests on is wrong.
    *
    * Exactly, before dither: the index is (texel * 0x80) >> 4 = texel * 8,
    * and the LUT shifts down by 3, so texel * 8 >> 3 == texel. With dither
    * the result moves by at most one step, which is what dithering is for --
    * asserting equality there would be asserting that dithering does not
    * happen. */
   {
      int exact = 1, within_one = 1;
      for (v = 0; v <= 0x1F; v++)
      {
         if ((IDX_R(v, 0x80) >> 3) != v)
            exact = 0;
         for (y = 0; y < 4; y++)
            for (x = 0; x < 4; x++)
            {
               int got = lut[y][IDX_R(v, 0x80)][x];
               if (got < v - 1 || got > v + 1)
                  within_one = 0;
            }
      }
      chk("vertex colour 0x80 is unity before dither", exact);
      chk("dither moves it by at most one step", within_one);
   }

   /* The saturation boundary. Above it the LUT cannot represent the value. */
   for (v = 0; v < 512; v++)
   {
      int all_max = 1;
      for (y = 0; y < 4 && all_max; y++)
         for (x = 0; x < 4; x++)
            if (lut[y][v][x] != 0x1F) { all_max = 0; break; }
      if (all_max) { first_sat = v; break; }
   }
   printf("  (every dither phase clamps from index %d upward)\n", first_sat);
   chk("saturation begins in the 248..256 region", first_sat >= 248 && first_sat <= 256);
   chk("index 255 is at or past the ceiling", lut[0][255][0] == 0x1F);

   /* The reachable range. 0x1F * 0xFF >> 4 = 494 is the ceiling of the
    * modulate, so the largest overshoot is 494/255, just under 2x. */
   chk("largest reachable index is 494", IDX_R(0x1F, 0xFF) == 494);
   chk("largest overshoot is under 2x", (494.0 / 255.0) < 2.0);
   chk("largest overshoot is over 1.9x", (494.0 / 255.0) > 1.9);

   /* And what the counters key on: an index above 255 is a discarded value,
    * an index at or below it is representable. */
   chk("an index of 256 is discarded", lut[0][256][0] == 0x1F && lut[0][255][0] == 0x1F);
   chk("a mid-range index is not clamped", lut[0][128][0] < 0x1F);

   printf("\n  headroom above the ceiling: %.2fx  (index %d..494)\n",
          494.0 / 255.0, first_sat);
   printf(fails ? "\nRESULT: FAIL\n" : "\nRESULT: PASS\n");
   return fails ? 1 : 0;
}
