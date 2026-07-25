#version 450
precision highp float;
precision highp int;

/* Pass 1 of the analog path: framebuffer -> modulated signal.
 *
 * Output is one base-clock sample per fragment, at width = native_pixels * div.
 *
 * The filters are evaluated against the CRTC's zero-order hold rather than
 * against individual taps. The source is piecewise constant over `div` base
 * samples - that is what the hold is - so every run of taps landing on the same
 * native pixel collapses into one weighted fetch, the weight being a difference
 * of two prefix-sum entries. A 95-tap luma filter plus a 31-tap chroma filter
 * becomes 11 to 25 texture reads depending on the dot clock instead of 126, and
 * the arithmetic is identical, not approximated.
 *
 * Luma and chroma share the loop: chroma's tap span is a strict subset of
 * luma's, so one fetch feeds both and the chroma weight is simply zero outside
 * its own span.
 *
 * Emits vec2: .x luma, .y modulated chroma. Composite sums the two on one wire
 * and the decoder has to separate them again; S-Video keeps them apart, which
 * is exactly what distinguishes the cables. RGB never reaches this pass. */

#include "analog.h"

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant, std430) uniform Registers
{
	vec2  src_size;      /* native framebuffer size, texels */
	float div;           /* base clocks per pixel: 10,8,7,5,4 */
	float x1;            /* GP1(06h).X1, video clocks from HSYNC */
	float inv_ratio;     /* 1/15 NTSC, 1/12 PAL */
	float line_adv;
	float line_split;    /* 1 progressive, 2 interlaced (woven frame) */
	float field_off;     /* phase between the two fields of a woven frame */      /* 0.5 NTSC, 0.75 PAL */
	float field_adv;     /* from the field counter */
	int   cable;         /* AN_CABLE_*: picks the luma tier, enables the beat */
} reg;

/* Summed weight of every tap falling on one held pixel, from the prefix table.
 * `lo`/`hi` are tap offsets relative to the filter centre. */
highp float span_weight_luma(int lo, int hi)
{
	int a = clamp(lo + AN_LUMA_HALF,     0, AN_LUMA_N);
	int b = clamp(hi + AN_LUMA_HALF + 1, 0, AN_LUMA_N);
	if (reg.cable == AN_CABLE_SVIDEO)
		return AN_LUMA_WIDE_P[b] - AN_LUMA_WIDE_P[a];
	if (reg.cable == AN_CABLE_RF)
		return AN_LUMA_RF_P[b] - AN_LUMA_RF_P[a];
	return AN_LUMA_P[b] - AN_LUMA_P[a];
}

highp float span_weight_chroma(int lo, int hi)
{
	int a = clamp(lo + AN_CHROMA_HALF,     0, AN_CHROMA_N);
	int b = clamp(hi + AN_CHROMA_HALF + 1, 0, AN_CHROMA_N);
	return AN_CHROMA_P[b] - AN_CHROMA_P[a];
}

void main()
{
	highp float n    = floor(gl_FragCoord.x);
	highp float row  = vUV.y;
	highp float line = floor(gl_FragCoord.y);

	int   idiv = int(reg.div);
	int   ni   = int(n);
	int   maxp = int(reg.src_size.x) - 1;

	/* Native pixels touched by the luma span. */
	int q0 = (ni - AN_LUMA_HALF) / idiv - 1;
	int q1 = (ni + AN_LUMA_HALF) / idiv + 1;

	highp float y  = 0.0;
	highp vec2  iq = vec2(0.0);
	int q;

	for (q = q0; q <= q1; q++)
	{
		/* Tap offsets covered by native pixel q, relative to the centre. */
		int lo = q * idiv - ni;
		int hi = lo + idiv - 1;

		highp float wl = span_weight_luma(lo, hi);
		highp float wc = span_weight_chroma(lo, hi);
		if (wl == 0.0 && wc == 0.0)
			continue;

		highp float px  = float(clamp(q, 0, maxp));
		highp vec3  yc  = an_rgb_to_yc(
			textureLod(uSource, vec2((px + 0.5) / reg.src_size.x, row), 0.0).rgb);

		y  += wl * yc.x;
		iq += wc * yc.yz;
	}

	highp float ph = an_phase(n, line, reg.x1, reg.inv_ratio,
	                          reg.line_adv, reg.line_split, reg.field_off,
	                          reg.field_adv);
	highp vec2  cs = an_carrier(ph);
	highp float vs = an_v_sign(an_field_line(line, reg.line_split));

	highp float chroma = an_modulate(iq, cs, vs);

	/* RF only: the sound carrier beating against the subcarrier. Added before
	 * separation, which is where it lands on real hardware - it sits far below
	 * the chroma band, so the comb leaves it in luma. */
	if (reg.cable == AN_CABLE_RF)
	{
		highp float bph = fract(n * AN_BEAT_RATIO + fract(line * AN_BEAT_LINE_ADV));
		chroma += AN_BEAT_AMPLITUDE * length(iq) * cos(AN_TAU * bph);
	}

	FragColor = vec4(y, chroma, 0.0, 1.0);
}
