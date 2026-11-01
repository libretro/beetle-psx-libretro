#ifndef RHI_ANALOG_TRAP_H__
#define RHI_ANALOG_TRAP_H__

/* Coefficients of the analog chain's chroma trap (analog_notch.comp), for
 * the Vulkan and GL backends alike, so the two cannot disagree on them. The
 * shader runs
 *
 *    y[n] = b0*x[n] + b1*x[n-1] + b2*x[n-2] - a1*y[n-1] - a2*y[n-2]
 *
 * with zeros on the subcarrier at radius 0.99 (the null) and poles at the
 * same angle, radius 0.85 (which keep the stop band narrow), normalised to
 * 0 dB at DC. inv_ratio is subcarrier cycles per base-clock sample. */

#include <math.h>

#define ANALOG_TRAP_ZERO_RADIUS 0.99
#define ANALOG_TRAP_POLE_RADIUS 0.85

/* out: b0, b1, b2, a1, a2 */
static void analog_trap_coeffs(double inv_ratio, double out[5])
{
   const double w  = 2.0 * 3.14159265358979323846 * inv_ratio;
   const double fq = ANALOG_TRAP_ZERO_RADIUS;
   const double iq = ANALOG_TRAP_POLE_RADIUS;
   const double a1 = -2.0 * iq * cos(w);
   const double a2 = iq * iq;
   double       b0 = 1.0;
   double       b1 = -2.0 * fq * cos(w);
   double       b2 = fq * fq;
   const double g  = (1.0 + a1 + a2) / (b0 + b1 + b2);
   out[0] = b0 * g;
   out[1] = b1 * g;
   out[2] = b2 * g;
   out[3] = a1;
   out[4] = a2;
}

#endif
