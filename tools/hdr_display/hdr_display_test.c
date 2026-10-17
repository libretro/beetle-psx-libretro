/* Checks rhi/hdr_display.c: the PQ encode and the Rec.2020 -> panel matrix
 * the HDR shaders compress against.
 *
 *   - PQ at the curve's fixed points and at 100 nits (0.50808, ST 2084).
 *   - Rec.2020 primaries give the identity.
 *   - P3-D65 primaries give the published Rec.2020 -> P3-D65 matrix.
 *   - White (1,1,1) maps to white for any panel, since the panel matrix is
 *     built against D65 whatever white the panel reports.
 *   - Unknown (all zero) and degenerate (collinear) primaries are refused
 *     and leave a zero matrix, which is what disables the shader path. */
#include <math.h>
#include <stdio.h>

#include "../../rhi/hdr_display.h"

static int failures;

static void check(int ok, const char *what)
{
   if (!ok)
   {
      printf("FAIL: %s\n", what);
      failures++;
   }
}

static int near(double a, double b, double tol)
{
   return fabs(a - b) <= tol;
}

int main(void)
{
   static const uint16_t bt2020[8] = { 7080, 2920, 1700, 7970, 1310,  460, 3127, 3290 };
   static const uint16_t p3d65[8]  = { 6800, 3200, 2650, 6900, 1500,  600, 3127, 3290 };
   static const uint16_t p3_dci_white[8] = { 6800, 3200, 2650, 6900, 1500,  600, 3140, 3510 };
   static const uint16_t unknown[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
   static const uint16_t collinear[8] = { 2000, 2000, 3000, 3000, 4000, 4000, 3127, 3290 };
   /* Rec.2020 -> P3-D65, row-major (each row sums to 1: white maps to white). */
   static const double p3_ref[9] = {
       1.343578, -0.282180, -0.061399,
      -0.065297,  1.075788, -0.010490,
       0.002822, -0.019598,  1.016777 };
   float    m[9];
   unsigned r, c;

   check(hdr_display_pq(0.0f) == 0.0f, "PQ(0) == 0");
   check(near(hdr_display_pq(10000.0f), 1.0, 1e-6), "PQ(10000) == 1");
   check(near(hdr_display_pq(20000.0f), 1.0, 1e-6), "PQ clamps above 10000");
   check(near(hdr_display_pq(100.0f), 0.50808, 1e-4), "PQ(100) == 0.50808");

   check(hdr_display_panel_matrix(bt2020, m), "Rec.2020 panel accepted");
   for (r = 0; r < 3; r++)
      for (c = 0; c < 3; c++)
         check(near(m[c * 3 + r], r == c ? 1.0 : 0.0, 1e-5), "Rec.2020 panel is the identity");

   check(hdr_display_panel_matrix(p3d65, m), "P3-D65 panel accepted");
   for (r = 0; r < 3; r++)
      for (c = 0; c < 3; c++)
         check(near(m[c * 3 + r], p3_ref[r * 3 + c], 2e-4), "Rec.2020 -> P3-D65 matches the published matrix");

   check(hdr_display_panel_matrix(p3_dci_white, m), "P3 panel with a DCI white accepted");
   for (r = 0; r < 3; r++)
      check(near(m[0 * 3 + r] + m[1 * 3 + r] + m[2 * 3 + r], 1.0, 1e-5),
            "white maps to white regardless of the reported panel white");

   m[0] = 1.0f;
   check(!hdr_display_panel_matrix(unknown, m), "unknown primaries refused");
   check(m[0] == 0.0f, "unknown primaries leave a zero matrix");
   m[0] = 1.0f;
   check(!hdr_display_panel_matrix(collinear, m), "collinear primaries refused");
   check(m[0] == 0.0f, "collinear primaries leave a zero matrix");

   if (failures)
   {
      printf("hdr_display_test: %d failure(s)\n", failures);
      return 1;
   }
   printf("hdr_display_test: OK\n");
   return 0;
}
