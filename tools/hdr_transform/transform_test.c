/* Regression test for the Display Transform (rhi/hdr_transform.c,
 * rhi/hdr_aces2.c), the constants and tables both HDR backends upload.
 *
 *  - every per-channel curve pins paper white (1.0 -> 1.0), is monotonic,
 *    joins its shoulder with matching slope and runs out to the display
 *    peak H (GT, which has no shoulder, stays bounded);
 *  - the knee (curve 0) fills a block that selects nothing, so the hdr.h
 *    roll-off - the default - is untouched;
 *  - ACES 2.0 keeps greys neutral, rises with exposure and stays under the
 *    display peak, for every limiting gamut and surround;
 *  - Filmic Log's terms put scene 1.0 back on 1.0 through the shipped
 *    desaturation cube and contrast curve;
 *  - the per-frame fill never re-solves the ACES 2.0 tables while nothing
 *    changed (a re-solve costs milliseconds), and does when the peak,
 *    gamut or surround move;
 *  - the unorm16 -> half conversion GLES uploads is within half an ULP.
 *
 * Build and run: tools/hdr_transform/run.sh */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rhi/hdr_transform.h"
#include "rhi/hdr_aces2.h"

static unsigned failures;
static const uint16_t no_panel[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

static void fail(const char *what, int curve, double at, double got, double want)
{
   if (failures < 24)
      fprintf(stderr, "FAIL %s: curve %d at %g got %.6f want %.6f\n",
            what, curve, at, got, want);
   failures++;
}

static void defaults(struct hdr_dt_settings *s, int curve)
{
   memset(s, 0, sizeof(*s));
   s->curve           = curve;
   s->aces2_surround  = 1;
   s->aces2_gamut     = 3;
   s->gt_toe          = 0.22f;
   s->gt_linear       = 0.40f;
   s->filmic_contrast = 3;
   s->filmic_max_ev   = 4.026068811f;
}

static void check_curves(void)
{
   static const float peaks[3] = { 400.0f, 1000.0f, 4000.0f };
   int curve;
   unsigned k;
   for (curve = 1; curve <= 17; curve++)
   {
      for (k = 0; k < 3; k++)
      {
         struct hdr_dt_settings s;
         struct hdr_dt_block    b;
         /* 7-digit normalisation constants leave paper white within 1e-6
          * of 1.0, so slopes are compared over a step much wider. */
         double H, v, prev = -1.0, d = 1e-3;
         defaults(&s, curve);
         hdr_dt_configure(&s);
         hdr_dt_fill(&b, 200.0f, peaks[k], 0.0f, no_panel);
         H = b.mode[1];
         if (fabs(b.mode[0] - curve) > 0.0f)
            fail("block curve", curve, 0, b.mode[0], curve);
         if (fabs(hdr_dt_curve_cpu(curve, 1.0, &b) - 1.0) > 2e-3)
            fail("paper white", curve, 1.0, hdr_dt_curve_cpu(curve, 1.0, &b), 1.0);
         for (v = 0.0; v <= 64.0; v += (v < 2.0 ? 1.0 / 512.0 : 1.0 / 16.0))
         {
            double y = hdr_dt_curve_cpu(curve, v, &b);
            if (y < prev - 1e-9 || y != y)
            {
               fail("monotonic", curve, v, y, prev);
               break;
            }
            prev = y;
         }
         if (curve == HDR_DT_GT)
         {
            if (hdr_dt_curve_cpu(curve, 1e6, &b) > 2.0)
               fail("GT bounded", curve, 1e6, hdr_dt_curve_cpu(curve, 1e6, &b), 2.0);
            continue;
         }
         {
            double left  = (hdr_dt_curve_cpu(curve, 1.0, &b)
                          - hdr_dt_curve_cpu(curve, 1.0 - d, &b)) / d;
            double right = (hdr_dt_curve_cpu(curve, 1.0 + d, &b)
                          - hdr_dt_curve_cpu(curve, 1.0, &b)) / d;
            if (fabs(left - right) > 0.01 * left + 1e-3)
               fail("C1 at paper white", curve, 1.0, right, left);
            if (fabs(hdr_dt_curve_cpu(curve, 1.0 + 1e-9, &b)
                     - hdr_dt_curve_cpu(curve, 1.0, &b)) > 1e-5)
               fail("continuous at paper white", curve, 1.0,
                     hdr_dt_curve_cpu(curve, 1.0 + 1e-9, &b), hdr_dt_curve_cpu(curve, 1.0, &b));
         }
         if (fabs(hdr_dt_curve_cpu(curve, 1e7, &b) - H) > 0.01 * H)
            fail("reaches the peak", curve, 1e7, hdr_dt_curve_cpu(curve, 1e7, &b), H);
         if (hdr_dt_curve_cpu(curve, 1e7, &b) > H * 1.0001)
            fail("stays under the peak", curve, 1e7, hdr_dt_curve_cpu(curve, 1e7, &b), H);
      }
   }
}

static void check_knee_default(void)
{
   struct hdr_dt_settings s;
   struct hdr_dt_block    b;
   defaults(&s, HDR_DT_KNEE);
   hdr_dt_configure(&s);
   hdr_dt_fill(&b, 200.0f, 1000.0f, 0.0f, no_panel);
   if (b.mode[0] != 0.0f || hdr_dt_enabled())
      fail("knee selects nothing", 0, 0, b.mode[0], 0);
}

/* Scene Rec.709 -> AP0, the shader's DT_709_TO_AP0 (row-major here). */
static void to_ap0(const double c[3], double out[3])
{
   static const double m[9] = {
      0.4397010, 0.3829780, 0.1773350,
      0.0897923, 0.8134230, 0.0967616,
      0.0175440, 0.1115440, 0.8707040 };
   int i;
   for (i = 0; i < 3; i++)
      out[i] = m[i * 3] * c[0] + m[i * 3 + 1] * c[1] + m[i * 3 + 2] * c[2];
}

static void check_aces2(void)
{
   static const double prims[3][8] = {
      { 0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290 },
      { 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290 },
      { 0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290 } };
   static const double peaks[2] = { 1000.0, 4000.0 };
   aces2Params_t *p = (aces2Params_t*)calloc(1, sizeof(*p));
   int g, sur;
   unsigned k;
   if (!p)
   {
      failures++;
      return;
   }
   for (sur = 0; sur < 3; sur++)
      for (g = 0; g < 3; g++)
         for (k = 0; k < 2; k++)
         {
            double prev = -1.0, e;
            ACES2_SetSurround(sur);
            ACES2_Init(prims[g], peaks[k], p);
            for (e = 0.01; e <= 64.0; e *= 1.5)
            {
               double grey[3], ap0[3], rgb[3], lum;
               grey[0] = grey[1] = grey[2] = e * 2.0; /* 1.0 = 100 nits at 200 paper */
               to_ap0(grey, ap0);
               ACES2_OutputTransformFwd(ap0, p, rgb);
               lum = (rgb[0] + rgb[1] + rgb[2]) / 3.0;
               if (fabs(rgb[0] - rgb[1]) > 2e-3 * (lum + 1e-3)
                     || fabs(rgb[1] - rgb[2]) > 2e-3 * (lum + 1e-3))
                  fail("ACES 2.0 grey neutral", g * 10 + sur, e, rgb[0] - rgb[2], 0.0);
               if (lum < prev - 1e-9)
                  fail("ACES 2.0 monotonic", g * 10 + sur, e, lum, prev);
               if (rgb[0] > peaks[k] / 100.0 * 1.001)
                  fail("ACES 2.0 under peak", g * 10 + sur, e, rgb[0], peaks[k] / 100.0);
               prev = lum;
            }
         }
   ACES2_SetSurround(1);
   free(p);
}

static void check_filmic(void)
{
   struct hdr_dt_settings s;
   struct hdr_dt_block    b;
   int c;
   for (c = 0; c < 7; c++)
   {
      const uint16_t *lut, *desat;
      double t, o, pos;
      int i, r;
      defaults(&s, HDR_DT_FILMICLOG);
      s.filmic_contrast = c;
      hdr_dt_configure(&s);
      hdr_dt_fill(&b, 200.0f, 1000.0f, 0.0f, no_panel);
      lut   = hdr_dt_film_lut();
      desat = hdr_dt_film_desat();
      if (!lut || !desat)
      {
         fail("filmic tables", c, 0, 0, 1);
         continue;
      }
      /* Scene 1.0 grey, through the shader's steps with nearest lookups
       * on the grey axis, where the cube is the identity. */
      t   = (log(1.0) / log(2.0) + 12.4739312) * 0.04;
      r   = (int)(t * 32.0 + 0.5);
      i   = (r * 33 + r) + r * HDR_DT_DESAT_WIDTH;
      t   = desat[i * 4] / 65535.0 * b.film[0];
      pos = t * (HDR_DT_FILM_LUT_WIDTH - 1);
      i   = (int)pos;
      o   = (lut[i] + (lut[i + 1] - lut[i]) * (pos - i)) / 65535.0;
      o   = pow(o, 2.2) * b.film[1];
      if (fabs(o - 1.0) > 0.03)
         fail("filmic pins paper white", c, 1.0, o, 1.0);
   }
}

static void check_rebuilds(void)
{
   struct hdr_dt_settings s;
   struct hdr_dt_block    b;
   unsigned gen;
   int i;
   defaults(&s, HDR_DT_ACES2FULL);
   hdr_dt_configure(&s);
   hdr_dt_fill(&b, 200.0f, 1000.0f, 0.0f, no_panel);
   if (!hdr_dt_aces2_lut() || b.mode[0] != (float)HDR_DT_ACES2FULL)
      fail("ACES 2.0 tables built", HDR_DT_ACES2FULL, 0, b.mode[0], HDR_DT_ACES2FULL);
   gen = hdr_dt_generation();
   for (i = 0; i < 100; i++)
      hdr_dt_fill(&b, 200.0f, 1000.0f + (float)(i % 3) * 0.1f, 0.0f, no_panel);
   if (hdr_dt_generation() != gen)
      fail("no per-frame re-solve", HDR_DT_ACES2FULL, 1000.0, hdr_dt_generation(), gen);
   hdr_dt_fill(&b, 200.0f, 1500.0f, 0.0f, no_panel);
   if (hdr_dt_generation() == gen)
      fail("peak change re-solves", HDR_DT_ACES2FULL, 1500.0, hdr_dt_generation(), gen + 1);
   gen = hdr_dt_generation();
   s.aces2_gamut = 2;
   hdr_dt_configure(&s);
   hdr_dt_fill(&b, 200.0f, 1500.0f, 0.0f, no_panel);
   if (hdr_dt_generation() == gen)
      fail("gamut change re-solves", HDR_DT_ACES2FULL, 0, hdr_dt_generation(), gen + 1);
   gen = hdr_dt_generation();
   s.aces2_surround = 0;
   hdr_dt_configure(&s);
   hdr_dt_fill(&b, 200.0f, 1500.0f, 0.0f, no_panel);
   if (hdr_dt_generation() == gen)
      fail("surround change re-solves", HDR_DT_ACES2FULL, 0, hdr_dt_generation(), gen + 1);
}

static double half_to_double(uint16_t h)
{
   int e = (h >> 10) & 31, m = h & 1023;
   if (e == 0)
      return ldexp((double)m, -24);
   return ldexp(1.0 + m / 1024.0, e - 15);
}

static void check_half(void)
{
   unsigned v;
   for (v = 0; v < 65536; v++)
   {
      double want = v / 65535.0;
      double got  = half_to_double(hdr_dt_unorm16_to_half((uint16_t)v));
      int    e;
      double ulp;
      frexp(want > 6.103515625e-05 ? want : 6.103515625e-05, &e);
      ulp = ldexp(1.0, e - 11);
      if (fabs(got - want) > 0.5 * ulp + 1e-12)
      {
         fail("half conversion", 0, want, got, want);
         break;
      }
   }
}

/* The post effects' per-frame parameters: off means nothing runs, the two
 * bloom modes carry the same total energy, the pyramid stops before a level
 * gets smaller than 4x4, the curve knee follows Neutral and GT, and the
 * aberration and halation steps are monotonic. */
static void check_post(void)
{
   struct hdr_post_settings p;
   struct hdr_post_frame    f;
   struct hdr_dt_settings   s;
   unsigned                 w, h, bw, bh;
   int                      i, n;

   memset(&p, 0, sizeof(p));
   p.firefly_knee = 1.0f;
   hdr_post_configure(&p);
   if (hdr_post_enabled())
      fail("post effects on by default", 0, 0.0, 1.0, 0.0);
   hdr_post_frame_params(&f, 320, 240);
   if (f.bloom || f.hal_bloom[3] != 0.0f || f.ca != 0.0f)
      fail("post parameters with everything off", 0, 0.0, f.hal_bloom[3], 0.0);

   p.bloom = 1.0f;
   hdr_post_configure(&p);
   hdr_post_frame_params(&f, 320, 240);
   if (!f.bloom || f.levels != 0)
      fail("two-band bloom", 0, 0.0, f.levels, 0.0);
   if (fabs(f.band_w[0] + f.band_w[1] - 0.36) > 1e-6)
      fail("two-band bloom energy", 0, 0.0, f.band_w[0] + f.band_w[1], 0.36);
   if (f.hal_bloom[0] != 1.0f || f.hal_bloom[1] != 1.0f || f.hal_bloom[2] != 1.0f)
      fail("bloom tinted without halation", 0, 0.0, f.hal_bloom[2], 1.0);
   if (f.bright[0] != 0.70f || f.bright[2] != 1.0f)
      fail("classic bright pass", 0, 0.0, f.bright[0], 0.70);

   p.convolution = 1;
   hdr_post_configure(&p);
   for (w = 16; w <= 7680; w = w * 3 / 2)
   {
      h = w * 3 / 4;
      hdr_post_frame_params(&f, w, h);
      n  = 0;
      bw = w / 4;
      bh = h / 4;
      while (n < HDR_POST_CONV_MAX && bw >= 4 && bh >= 4)
      {
         n++;
         bw >>= 1;
         bh >>= 1;
      }
      if (n < 1)
         n = 1;
      if (f.levels != n || f.levels > HDR_POST_CONV_MAX)
         fail("pyramid levels", 0, w, f.levels, n);
      if (fabs(f.band_w[0] * f.levels + f.band_w[1] - 0.36) > 1e-5)
         fail("pyramid bloom energy", 0, w, f.band_w[0] * f.levels, 0.36);
   }

   p.bloom_knee = 1;
   hdr_post_configure(&p);
   defaults(&s, HDR_DT_NEUTRAL);
   hdr_dt_configure(&s);
   hdr_post_frame_params(&f, 320, 240);
   if (f.bright[0] != 0.76f)
      fail("curve-knee threshold under Neutral", HDR_DT_NEUTRAL, 0.0, f.bright[0], 0.76);
   defaults(&s, HDR_DT_GT);
   hdr_dt_configure(&s);
   hdr_post_frame_params(&f, 320, 240);
   if (!(f.bright[0] > 0.3f && f.bright[0] < 0.95f) || f.bright[0] == 0.70f)
      fail("curve-knee threshold under GT", HDR_DT_GT, 0.0, f.bright[0], 0.55);
   defaults(&s, HDR_DT_ACES);
   hdr_dt_configure(&s);
   hdr_post_frame_params(&f, 320, 240);
   if (f.bright[0] != 0.70f)
      fail("curve-knee threshold without a knee", HDR_DT_ACES, 0.0, f.bright[0], 0.70);
   defaults(&s, HDR_DT_KNEE);
   hdr_dt_configure(&s);

   {
      float last_ca = 0.0f, last_g = 1.0f;
      for (i = 1; i <= 3; i++)
      {
         p.chromatic = i;
         p.halation  = i < 3 ? i : 2;
         hdr_post_configure(&p);
         hdr_post_frame_params(&f, 320, 240);
         if (!(f.ca > last_ca))
            fail("aberration steps", i, 0.0, f.ca, last_ca);
         if (i < 3 && !(f.hal_bloom[1] < last_g && f.hal_bloom[2] < f.hal_bloom[1]))
            fail("halation tint", i, 0.0, f.hal_bloom[1], last_g);
         last_ca = f.ca;
         last_g  = f.hal_bloom[1];
      }
   }

   memset(&p, 0, sizeof(p));
   p.firefly_knee = 1.0f;
   hdr_post_configure(&p);
}

int main(void)
{
   check_knee_default();
   check_curves();
   check_aces2();
   check_filmic();
   check_rebuilds();
   check_half();
   check_post();
   hdr_dt_free();
   if (failures)
   {
      fprintf(stderr, "hdr_transform: %u failure(s)\n", failures);
      return EXIT_FAILURE;
   }
   printf("hdr_transform: all checks passed\n");
   return EXIT_SUCCESS;
}
