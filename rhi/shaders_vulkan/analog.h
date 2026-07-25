/* ---- PSX analog video path, shared math ---------------------------------
 *
 * Models the composite / S-Video / RGB output of the PSX video DAC and the
 * Y/C separator at the far end of the cable. Runs entirely in the signal
 * (gamma) domain, because a real encoder modulates R'G'B', not linear light,
 * so the result is handed to the display encode exactly where the plain
 * framebuffer value would otherwise have gone.
 *
 * Region is a compile-time choice (-DPAL): the subcarrier frequency, the
 * colour space, the modulation axes and the comb gain all differ, and none of
 * that wants a per-pixel branch.
 *
 * Sample rate is the CRTC base clock, not a resampled grid:
 *
 *   NTSC base = 15 * fsc  (15 * 315/88 MHz     = 53.6932 MHz)
 *   PAL  base = 12 * fsc  (12 * 4.43361875 MHz = 53.2034 MHz)
 *
 * Both ratios are exact, so the subcarrier advances a rational 1/15 (or 1/12)
 * of a cycle per base sample and the phase never drifts. Framebuffer -> signal
 * is then an integer zero-order hold of `div` samples per pixel, which is what
 * the DAC does, so nothing is resampled on the way in.
 *
 * Line period is 3412.5 base clocks NTSC (227.5 fsc, the textbook value) and
 * 3405 PAL (283.75 fsc). Note PAL: broadcast is 283.7516 = 283 + 3/4 + 1/625,
 * and the PSX emits exactly 283 + 3/4. The missing 1/625 offset is what makes
 * the plain three-line comb usable here - see AN_COMB_GAIN below.
 *
 * Phase at the first *visible* sample is not zero. GP1(06h).X1 is counted in
 * video clocks from HSYNC so it enters the phase directly, and the standard X1
 * values differ per mode (0, 120, 336 deg ... on NTSC). It has to come from the
 * CRTC register; assuming zero is wrong for most modes, and games such as
 * Chrono Cross move X1 at runtime for screen shake. */
#ifndef ANALOG_H
#define ANALOG_H

#include "analog_taps.h"

/* cable: 0 = RGB (bypass), 1 = S-Video, 2 = composite.
 * RGB is the PSX's top tier - the multi-out carries analog RGB, not the YPbPr
 * component the PS2 added - and is a straight passthrough. */
#define AN_CABLE_RGB       0
#define AN_CABLE_SVIDEO    1
#define AN_CABLE_COMPOSITE 2
#define AN_CABLE_RF        3

/* Intercarrier beat. The PSX has no RF modulator of its own - the rear jack is
 * a 2.5 mm DC feed for an external one - so this models a modulator, not a
 * console output. Its defining artifact is the sound carrier beating against
 * the chroma subcarrier: 4.5 MHz - fsc = 920.455 kHz on NTSC-M, 5.5 MHz - fsc
 * = 1.066 MHz on PAL-B/G. Both land well inside the luma passband, so the beat
 * survives demodulation and shows as a fine pattern, exactly as it does on a
 * real set.
 *
 * NTSC works out to exactly 3/175 of the base clock and half a cycle per line,
 * so it alternates line to line like the carrier itself. PAL is 0.248 per
 * line, close to but not exactly a quarter.
 *
 * Amplitude tracks chroma magnitude because the beat is a product of the two
 * carriers: greyscale is clean, saturated colour is where it bites. */
#if defined(PAL)
#define AN_BEAT_RATIO    0.020043470
#define AN_BEAT_LINE_ADV 0.248
#else
#define AN_BEAT_RATIO    0.017142857
#define AN_BEAT_LINE_ADV 0.5
#endif
#define AN_BEAT_AMPLITUDE 0.18

/* Gain the three-line comb needs to recover C(l) from
 *     comp(l) - 0.5 * (comp(l+1) + comp(l-1))
 *
 * NTSC advances the carrier half a cycle per line, so C(l+-1) = -C(l), the
 * bracket evaluates to 2*C(l) and the result needs halving.
 *
 * PAL advances exactly three quarters of a cycle, which puts the two
 * neighbours in antiphase with each other rather than with the centre:
 * C(l-1) = -C(l+1), so they cancel and the bracket leaves C(l) untouched.
 * Gain is therefore 1. This only works because the PSX omits the broadcast
 * 1/625-per-line offset - at the standard 283.7516 the neighbours leak
 * cos(2*pi*283.7516) = 1.01% of chroma into luma, which is what forces real
 * PAL decoders onto a delay line or a 2D cross filter instead.
 *
 * Vertically constant luma is rejected exactly in both regions. */
#if defined(PAL)
#define AN_COMB_GAIN 1.0
#else
#define AN_COMB_GAIN 0.5
#endif

/* Colour space. NTSC uses YIQ (the I axis rotated 33 degrees off R-Y to give
 * skin tones the wider band); PAL uses YUV. Both share the BT.601 luma row.
 * The reverse matrices are exact inverses of the forward ones rather than the
 * rounded textbook constants, which invert a slightly different forward matrix
 * and leave round-trip error for nothing. */
highp vec3 an_rgb_to_yc(highp vec3 c)
{
#if defined(PAL)
	return vec3(
		 0.299000000 * c.r +  0.587000000 * c.g +  0.114000000 * c.b,
		-0.147141189 * c.r + -0.288869157 * c.g +  0.436010346 * c.b,
		 0.614975383 * c.r + -0.514965121 * c.g + -0.100010262 * c.b);
#else
	return vec3(
		 0.299000000 * c.r +  0.587000000 * c.g +  0.114000000 * c.b,
		 0.595900000 * c.r + -0.274600000 * c.g + -0.321300000 * c.b,
		 0.211500000 * c.r + -0.522700000 * c.g +  0.311200000 * c.b);
#endif
}

highp vec3 an_yc_to_rgb(highp vec3 c)
{
#if defined(PAL)
	return vec3(
		c.x +  0.000000000 * c.y +  1.139883025 * c.z,
		c.x + -0.394642340 * c.y + -0.580621848 * c.z,
		c.x +  2.032061872 * c.y +  0.000000000 * c.z);
#else
	return vec3(
		c.x +  0.956050200 * c.y +  0.620754900 * c.z,
		c.x + -0.272052300 * c.y + -0.647205700 * c.z,
		c.x + -1.106704300 * c.y +  1.704421300 * c.z);
#endif
}

/* Subcarrier phase, in cycles, at base-clock sample `n` of visible line `line`.
 *
 *   x1          GP1(06h).X1, video clocks from HSYNC to the first visible
 *               sample. Enters the phase directly.
 *   inv_ratio   1/15 NTSC, 1/12 PAL - cycles of subcarrier per base clock.
 *   line_adv    fractional cycles gained per line: 0.5 NTSC (227.5),
 *               0.75 PAL (283.75).
 *   field_adv   fractional cycles gained per field, folded in by the caller
 *               from the field counter. In 240p this is 0.5 in both regions,
 *               so the pattern simply alternates frame to frame instead of
 *               running the broadcast 4-field (NTSC) or 8-field (PAL) cycle;
 *               480i restores those.
 *
 * Only the fraction matters, and each term is reduced before summing so a long
 * line cannot grind the mantissa away. */
highp float an_phase(highp float n, highp float line, highp float x1,
                     highp float inv_ratio, highp float line_adv,
                     highp float field_adv)
{
	highp float p = fract((x1 + n) * inv_ratio)
	              + fract(line * line_adv)
	              + field_adv;
	return fract(p);
}

const highp float AN_TAU = 6.28318530718;

highp vec2 an_carrier(highp float cycles)
{
	highp float a = AN_TAU * cycles;
	return vec2(cos(a), sin(a));
}

/* PAL alternates the sign of V every line - the Phase Alternate Line the
 * standard is named for. NTSC does not. Kept here so encode and decode cannot
 * disagree about it. */
highp float an_v_sign(highp float line)
{
#if defined(PAL)
	return (fract(line * 0.5) > 0.25) ? -1.0 : 1.0;
#else
	return 1.0;
#endif
}

/* Modulate the two chroma axes onto the carrier.
 *   NTSC:  C = I * cos + Q * sin
 *   PAL:   C = U * sin + sign * V * cos
 * The axis-to-quadrature assignment genuinely differs between the two; it is
 * not just a renaming. */
highp float an_modulate(highp vec2 c2, highp vec2 carrier, highp float vsign)
{
#if defined(PAL)
	return c2.x * carrier.y + vsign * c2.y * carrier.x;
#else
	return c2.x * carrier.x + c2.y * carrier.y;
#endif
}

/* Inverse of the above, per tap. Multiplying by 2*carrier lands the wanted
 * axis at baseband and its image at 2*fsc, which the chroma low-pass removes. */
highp vec2 an_demodulate(highp float c, highp vec2 carrier, highp float vsign)
{
#if defined(PAL)
	return 2.0 * c * vec2(carrier.y, vsign * carrier.x);
#else
	return 2.0 * c * vec2(carrier.x, carrier.y);
#endif
}

#endif /* ANALOG_H */
