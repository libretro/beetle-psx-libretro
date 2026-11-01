/* Regression test for the software rasteriser's upscaled U/V sampling
 * (mednafen/psx/gpu_uv_phase.h), the seed and slope DrawTriangle uses.
 *
 * Sub-pixels sample at their top-left corner and step by the per-pixel
 * slope from the seed, exactly as DrawSpan walks a span. Checked:
 *
 *  - 2D (sprite phase): every 1:1 span, forward and flipped, on either
 *    axis, at every width and upscale factor, samples on each sub-pixel
 *    the texel its native pixel samples. A native half-texel seed shifts
 *    these sprites by half a pixel and bleeds the texel past the edge;
 *  - 3D phase: integer magnifications keep native texel phase on every
 *    sub-pixel, and both phases equal the native seed at 1x;
 *  - slope: an upscaled triangle's slope is exactly the native slope
 *    divided by the scale, up to the largest triangle the decoder keeps.
 *    Truncating at native precision, or int32 products, breaks this.
 *
 * Build and run: tools/uv_phase/run.sh */

#include <stdio.h>
#include <stdlib.h>

#include "mednafen/psx/gpu_uv_phase.h"

#define MAX_SHIFT 4

static unsigned failures = 0;

static void fail(const char *what, unsigned shift, int a, int b, int w,
      int sub, unsigned want, unsigned got)
{
   if (failures < 16)
      fprintf(stderr, "FAIL %s shift=%u u %d->%d width=%d sub-pixel %d: "
            "native texel %u, got %u\n", what, shift, a, b, w, sub, want, got);
   failures++;
}

static unsigned texel(uint32_t u)
{
   return (u >> (COORD_FBS + COORD_POST_PADDING)) & 0xffu;
}

/* Slope of the attribute along the span axis for the right triangle
 * (0,0,a) (w,0,b) (0,h,a), scaled by 1 << shift, as CALCIS computes it.
 * The cross axis is the same expression with x and y exchanged. */
static uint32_t span_delta(int a, int b, int w, unsigned shift)
{
   const int32_t ws = (int32_t)w << shift;
   const int32_t hs = (int32_t)8 << shift;
   const int32_t cis = (b - a) * hs;
   const int32_t denom = ws * hs;
   return gpu_uv_delta(cis, denom, shift);
}

static void check_span(const char *what, int a, int b, int w,
      unsigned shift, bool sprite_phase, bool flipped)
{
   const uint32_t nd = span_delta(a, b, w, 0);
   const uint32_t nseed = gpu_uv_seed(a, 0, sprite_phase, flipped);
   const uint32_t d = span_delta(a, b, w, shift);
   const uint32_t seed = gpu_uv_seed(a, shift, sprite_phase, flipped);
   int x;
   for (x = 0; x < (w << shift); x++)
   {
      unsigned want = texel(nseed + nd * (uint32_t)(x >> shift));
      unsigned got = texel(seed + d * (uint32_t)x);
      if (got != want)
      {
         fail(what, shift, a, b, w, x, want, got);
         return;
      }
   }
}

static void check_sprites(void)
{
   unsigned shift;
   for (shift = 1; shift <= MAX_SHIFT; shift++)
   {
      int w;
      for (w = 1; w <= 255; w++)
      {
         int u0;
         for (u0 = 0; u0 + w <= 255; u0 += 37)
         {
            check_span("2d forward", u0, u0 + w, w, shift, true, false);
            check_span("2d flipped", u0 + w, u0, w, shift, true, true);
         }
      }
   }
}

static void check_magnified(void)
{
   unsigned shift;
   for (shift = 1; shift <= MAX_SHIFT; shift++)
   {
      int m;
      for (m = 2; m <= 8; m *= 2)
      {
         int w;
         for (w = m; w <= 255; w += m)
            check_span("3d magnified", 0, w / m, w, shift, false, false);
      }
   }
}

static void check_native_seed(void)
{
   int u;
   for (u = 0; u < 256; u++)
   {
      uint32_t want = (uint32_t)(COORD_MF_INT(u) + (1 << (COORD_FBS - 1)))
            << COORD_POST_PADDING;
      if (gpu_uv_seed(u, 0, true, true) != want ||
          gpu_uv_seed(u, 0, true, false) != want ||
          gpu_uv_seed(u, 0, false, false) != want)
         fail("native seed", 0, u, u, 0, 0, texel(want),
               texel(gpu_uv_seed(u, 0, true, true)));
   }
}

/* CALCIS for an arbitrary triangle, as the decoder evaluates it. */
static int32_t calcis(const int32_t *x, const int32_t *y)
{
   return ((x[1] - x[0]) * (y[2] - y[1])) - ((x[2] - x[1]) * (y[1] - y[0]));
}

static void check_slope_case(const int32_t *px, const int32_t *py,
      const int32_t *pu)
{
   int32_t denom0 = calcis(px, py);
   int32_t cis0 = calcis(pu, py);
   uint32_t native;
   unsigned shift;
   if (!denom0)
      return;
   native = gpu_uv_delta(cis0, denom0, 0);
   for (shift = 1; shift <= MAX_SHIFT; shift++)
   {
      int32_t x[3], y[3];
      int i;
      uint32_t got;
      for (i = 0; i < 3; i++)
      {
         x[i] = px[i] << shift;
         y[i] = py[i] << shift;
      }
      got = gpu_uv_delta(calcis(pu, y), calcis(x, y), shift);
      if ((uint32_t)(got << shift) != native)
      {
         fail("slope", shift, pu[0], pu[1], px[1] - px[0], 0,
               (unsigned)(native >> 12), (unsigned)((got << shift) >> 12));
         return;
      }
   }
}

static void check_slopes(void)
{
   /* Largest extents the decoder keeps: x spans < 1024, y spans < 512. */
   static const int32_t big_x[3] = { 0, 1023, 0 };
   static const int32_t big_y[3] = { 0, 0, 511 };
   static const int32_t big_u[3] = { 0, 255, 0 };
   static const int32_t big_ur[3] = { 255, 0, 255 };
   unsigned seed = 12345u;
   int n;
   check_slope_case(big_x, big_y, big_u);
   check_slope_case(big_x, big_y, big_ur);
   for (n = 0; n < 200000; n++)
   {
      int32_t x[3], y[3], u[3];
      int i;
      for (i = 0; i < 3; i++)
      {
         seed = seed * 1103515245u + 12345u;
         x[i] = (int32_t)((seed >> 8) % 1024u);
         seed = seed * 1103515245u + 12345u;
         y[i] = (int32_t)((seed >> 8) % 512u);
         seed = seed * 1103515245u + 12345u;
         u[i] = (int32_t)((seed >> 8) & 0xffu);
      }
      check_slope_case(x, y, u);
   }
}

int main(void)
{
   check_native_seed();
   check_sprites();
   check_magnified();
   check_slopes();
   if (failures)
   {
      fprintf(stderr, "uv_phase: %u failure(s)\n", failures);
      return EXIT_FAILURE;
   }
   printf("uv_phase: all checks passed\n");
   return EXIT_SUCCESS;
}
