/* Regression test for rhi_sampled_vram_rect (rhi/rhi_tt.h), the VRAM area
 * both renderers use to decide whether a textured draw reads rendered
 * content and whether it is framebuffer feedback.
 *
 * Each case is checked against a brute-force oracle that maps every texel
 * the draw can address to its 16-bit VRAM word:
 *
 *  - unmasked windows within the page: the rect must equal the exact word
 *    span of [min_u, max_u] x [min_v, max_v] for every texel depth. This
 *    includes one-column draws (min_u == max_u), whose area must never be
 *    empty, and 4/8bpp spans that end mid-word, which must not grow by an
 *    extra word on the right;
 *  - texels past 255 wrap, so the rect is the whole page;
 *  - masked windows (every GP0(E2h) mask/offset pair): the rect must hold
 *    every word the window can address and stay inside the page.
 *
 * Build and run: tools/sampled_rect/run.sh */

#include <stdio.h>
#include <stdlib.h>

#include "rhi/rhi_tt.h"

retro_log_printf_t log_cb = NULL;

#define PAGE_X 640u
#define PAGE_Y 256u

static unsigned failures = 0;

static void fail(const char *what, unsigned shift,
      unsigned a, unsigned b, unsigned c, unsigned d,
      const struct TTRect *got, const struct TTRect *want)
{
   if (failures < 16)
      fprintf(stderr, "FAIL %s shift=%u args=%u,%u,%u,%u "
            "got {%u,%u,%u,%u} want {%u,%u,%u,%u}\n",
            what, shift, a, b, c, d,
            got->x, got->y, got->width, got->height,
            want->x, want->y, want->width, want->height);
   failures++;
}

static void check_unmasked(void)
{
   unsigned shift;
   for (shift = 0; shift <= 2; shift++)
   {
      unsigned min_u;
      for (min_u = 0; min_u < 256; min_u++)
      {
         unsigned max_u;
         for (max_u = min_u; max_u < 256; max_u++)
         {
            static const unsigned vs[3][2] = { { 0, 0 }, { 7, 8 }, { 3, 255 } };
            unsigned i;
            for (i = 0; i < 3; i++)
            {
               unsigned u;
               unsigned lo = 0xffffffffu;
               unsigned hi = 0;
               struct TTRect want;
               struct TTRect got = rhi_sampled_vram_rect(PAGE_X, PAGE_Y,
                     min_u, vs[i][0], max_u, vs[i][1],
                     0xffu, 0xffu, 0, 0, shift);
               for (u = min_u; u <= max_u; u++)
               {
                  unsigned w = u >> shift;
                  if (w < lo)
                     lo = w;
                  if (w > hi)
                     hi = w;
               }
               want = make_rect(PAGE_X + lo, PAGE_Y + vs[i][0],
                     hi - lo + 1, vs[i][1] - vs[i][0] + 1);
               if (!rect_eq(&got, &want))
                  fail("unmasked", shift, min_u, max_u,
                        vs[i][0], vs[i][1], &got, &want);
            }
         }
      }
   }
}

static void check_wrapped(void)
{
   unsigned shift;
   for (shift = 0; shift <= 2; shift++)
   {
      static const unsigned lim[3][4] = {
         { 0, 0, 256, 10 }, { 200, 3, 300, 3 }, { 5, 250, 5, 260 } };
      unsigned i;
      for (i = 0; i < 3; i++)
      {
         struct TTRect got = rhi_sampled_vram_rect(PAGE_X, PAGE_Y,
               lim[i][0], lim[i][1], lim[i][2], lim[i][3],
               0xffu, 0xffu, 0, 0, shift);
         struct TTRect want = make_rect(PAGE_X, PAGE_Y, 256u >> shift, 256);
         if (!rect_eq(&got, &want))
            fail("wrapped", shift, lim[i][0], lim[i][1],
                  lim[i][2], lim[i][3], &got, &want);
      }
   }
}

/* Same encoding the renderers apply to the GP0(E2h) fields. */
static void window_from_e2(unsigned tw, unsigned to,
      unsigned *mask, unsigned *orv)
{
   *mask = (~(tw << 3)) & 0xffu;
   *orv  = ((to & tw) << 3) & 0xffu;
}

static void check_masked(void)
{
   unsigned shift;
   for (shift = 0; shift <= 2; shift++)
   {
      unsigned tww;
      for (tww = 0; tww < 32; tww++)
      {
         unsigned twx;
         for (twx = 0; twx < 32; twx++)
         {
            static const unsigned ys[3][2] = { { 0, 0 }, { 0x1f, 0x15 }, { 0x08, 0x1f } };
            unsigned i;
            for (i = 0; i < 3; i++)
            {
               unsigned mask_x, or_x, mask_y, or_y;
               unsigned u, v;
               struct TTRect got;
               struct TTRect want;
               window_from_e2(tww, twx, &mask_x, &or_x);
               window_from_e2(ys[i][0], ys[i][1], &mask_y, &or_y);
               if (mask_x == 0xffu && mask_y == 0xffu)
                  continue;
               got = rhi_sampled_vram_rect(PAGE_X, PAGE_Y, 0, 0, 255, 255,
                     mask_x, mask_y, or_x, or_y, shift);
               want = make_rect(PAGE_X, PAGE_Y, 256u >> shift, 256);
               if (got.width == 0 || got.height == 0 ||
                   got.x < PAGE_X || got.y < PAGE_Y ||
                   got.x + got.width  > PAGE_X + (256u >> shift) ||
                   got.y + got.height > PAGE_Y + 256u)
               {
                  fail("masked-bounds", shift, tww, twx,
                        ys[i][0], ys[i][1], &got, &want);
                  continue;
               }
               for (u = 0; u < 256; u++)
               {
                  unsigned wx = PAGE_X + ((((u & mask_x) | or_x) & 0xffu) >> shift);
                  for (v = 0; v < 256; v += 17)
                  {
                     unsigned wy = PAGE_Y + (((v & mask_y) | or_y) & 0xffu);
                     if (wx < got.x || wx >= got.x + got.width ||
                         wy < got.y || wy >= got.y + got.height)
                     {
                        fail("masked-cover", shift, tww, twx,
                              ys[i][0], ys[i][1], &got, &want);
                        u = 256;
                        break;
                     }
                  }
               }
            }
         }
      }
   }
}

int main(void)
{
   check_unmasked();
   check_wrapped();
   check_masked();
   if (failures)
   {
      fprintf(stderr, "sampled_rect_test: %u failure(s)\n", failures);
      return 1;
   }
   printf("sampled_rect_test: OK\n");
   return 0;
}
