/* Checks the chroma trap coefficients both renderers take from
 * rhi/analog_trap.h, run through the recurrence exactly as
 * analog_notch.comp runs it:
 *
 *    y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 *
 * for NTSC (15 base clocks per subcarrier cycle) and PAL (12): both poles
 * inside the unit circle, 0 dB at DC, a notch of roughly -23 dB on the
 * subcarrier, luma detail well below it left essentially intact, and a
 * step that settles at 1 without blowing up. Coefficients with the feedback
 * signs flipped - the GL backend's old derivation - put a pole outside the
 * unit circle and fail every one of these. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "rhi/analog_trap.h"

static unsigned failures;

static void check(int ok, const char *what, const char *region, double got)
{
   if (!ok)
   {
      fprintf(stderr, "FAIL %s %s: %g\n", region, what, got);
      failures++;
   }
}

/* |H(e^jw)| in dB. */
static double gain_db(const double c[5], double w)
{
   double nr = c[0] + c[1] * cos(w) + c[2] * cos(2.0 * w);
   double ni =      - c[1] * sin(w) - c[2] * sin(2.0 * w);
   double dr = 1.0  + c[3] * cos(w) + c[4] * cos(2.0 * w);
   double di =      - c[3] * sin(w) - c[4] * sin(2.0 * w);
   return 10.0 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

static void check_region(const char *region, double inv_ratio)
{
   const double pi = 3.14159265358979323846;
   double c[5];
   double disc, r, y1 = 0.0, y2 = 0.0, x1 = 0.0, x2 = 0.0, peak = 0.0, y = 0.0;
   int    n;

   analog_trap_coeffs(inv_ratio, c);

   /* Poles: roots of z^2 + a1 z + a2. */
   disc = c[3] * c[3] - 4.0 * c[4];
   if (disc < 0.0)
      r = sqrt(c[4]);
   else
   {
      double r1 = fabs((-c[3] + sqrt(disc)) * 0.5);
      double r2 = fabs((-c[3] - sqrt(disc)) * 0.5);
      r = r1 > r2 ? r1 : r2;
   }
   check(r < 0.95, "pole radius", region, r);
   check(fabs(gain_db(c, 0.0)) < 1e-9, "gain at DC (dB)", region, gain_db(c, 0.0));
   {
      double at_fsc = gain_db(c, 2.0 * pi * inv_ratio);
      check(at_fsc < -18.0 && at_fsc > -30.0, "gain at the subcarrier (dB)", region, at_fsc);
   }
   /* A quarter of the subcarrier frequency is ordinary luma detail. */
   {
      double low = gain_db(c, 2.0 * pi * inv_ratio * 0.25);
      check(fabs(low) < 0.6, "gain at a quarter of the subcarrier (dB)", region, low);
   }

   for (n = 0; n < 4096; n++)
   {
      const double x = 1.0;
      y = c[0] * x + c[1] * x1 + c[2] * x2 - c[3] * y1 - c[4] * y2;
      x2 = x1; x1 = x;
      y2 = y1; y1 = y;
      if (fabs(y) > peak)
         peak = fabs(y);
   }
   check(peak < 2.0, "step response peak", region, peak);
   check(fabs(y - 1.0) < 1e-6, "step response settles at", region, y);
}

int main(void)
{
   check_region("NTSC", 1.0 / 15.0);
   check_region("PAL", 1.0 / 12.0);
   if (failures)
   {
      fprintf(stderr, "analog_trap: %u failure(s)\n", failures);
      return EXIT_FAILURE;
   }
   printf("analog_trap: all checks passed\n");
   return EXIT_SUCCESS;
}
