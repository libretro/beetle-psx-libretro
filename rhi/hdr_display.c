#include <math.h>
#include <string.h>

#include "hdr_display.h"

float hdr_display_pq(float nits)
{
   const double m1 = 2610.0 / 16384.0;
   const double m2 = (2523.0 / 4096.0) * 128.0;
   const double c1 = 3424.0 / 4096.0;
   const double c2 = (2413.0 / 4096.0) * 32.0;
   const double c3 = (2392.0 / 4096.0) * 32.0;
   double y        = nits / 10000.0;
   double ym;
   if (y <= 0.0)
      return 0.0f;
   if (y > 1.0)
      y  = 1.0;
   ym    = pow(y, m1);
   return (float)pow((c1 + c2 * ym) / (1.0 + c3 * ym), m2);
}

/* 3x3 row-major helpers, double throughout: the result is uploaded once per
 * panel change, so precision costs nothing. */
static double mat3_det(const double m[9])
{
   return m[0] * (m[4] * m[8] - m[5] * m[7])
        - m[1] * (m[3] * m[8] - m[5] * m[6])
        + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

static bool mat3_inverse(const double m[9], double out[9])
{
   double det = mat3_det(m);
   double inv;
   if (fabs(det) < 1e-12)
      return false;
   inv    = 1.0 / det;
   out[0] =  (m[4] * m[8] - m[5] * m[7]) * inv;
   out[1] = -(m[1] * m[8] - m[2] * m[7]) * inv;
   out[2] =  (m[1] * m[5] - m[2] * m[4]) * inv;
   out[3] = -(m[3] * m[8] - m[5] * m[6]) * inv;
   out[4] =  (m[0] * m[8] - m[2] * m[6]) * inv;
   out[5] = -(m[0] * m[5] - m[2] * m[3]) * inv;
   out[6] =  (m[3] * m[7] - m[4] * m[6]) * inv;
   out[7] = -(m[0] * m[7] - m[1] * m[6]) * inv;
   out[8] =  (m[0] * m[4] - m[1] * m[3]) * inv;
   return true;
}

/* RGB -> XYZ for the given primaries (x,y pairs) and a D65 white. */
static bool rgb_to_xyz(const double xy[6], double out[9])
{
   const double wx = 0.3127;
   const double wy = 0.3290;
   double       p[9];
   double       pinv[9];
   double       w[3];
   double       s[3];
   unsigned     i;

   for (i = 0; i < 3; i++)
   {
      double x = xy[i * 2 + 0];
      double y = xy[i * 2 + 1];
      if (y <= 0.0)
         return false;
      p[0 + i] = x / y;
      p[3 + i] = 1.0;
      p[6 + i] = (1.0 - x - y) / y;
   }
   if (!mat3_inverse(p, pinv))
      return false;

   w[0] = wx / wy;
   w[1] = 1.0;
   w[2] = (1.0 - wx - wy) / wy;
   for (i = 0; i < 3; i++)
      s[i] = pinv[i * 3 + 0] * w[0] + pinv[i * 3 + 1] * w[1]
           + pinv[i * 3 + 2] * w[2];
   for (i = 0; i < 9; i++)
      out[i] = p[i] * s[i % 3];
   return true;
}

bool hdr_display_panel_matrix(const uint16_t primaries_xy[8], float to_panel[9])
{
   static const double bt2020[6] = {
      0.708, 0.292, 0.170, 0.797, 0.131, 0.046 };
   double   panel_xy[6];
   double   m2020[9];
   double   mpanel[9];
   double   mpanel_inv[9];
   bool     known = false;
   unsigned i, r, c;

   for (i = 0; i < 9; i++)
      to_panel[i] = 0.0f;
   for (i = 0; i < 6; i++)
   {
      panel_xy[i] = primaries_xy[i] / 10000.0;
      if (primaries_xy[i])
         known = true;
   }
   if (!known)
      return false;
   if (     !rgb_to_xyz(bt2020, m2020)
         || !rgb_to_xyz(panel_xy, mpanel)
         || !mat3_inverse(mpanel, mpanel_inv))
      return false;

   for (r = 0; r < 3; r++)
      for (c = 0; c < 3; c++)
         to_panel[c * 3 + r] = (float)(
              mpanel_inv[r * 3 + 0] * m2020[0 * 3 + c]
            + mpanel_inv[r * 3 + 1] * m2020[1 * 3 + c]
            + mpanel_inv[r * 3 + 2] * m2020[2 * 3 + c]);
   return true;
}
