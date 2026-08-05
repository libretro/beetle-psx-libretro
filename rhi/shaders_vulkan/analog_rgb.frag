#version 450
precision highp float;
precision highp int;

/* The RGB/SCART cable, in one pass.
 *
 * Nothing is modulated on this wire: the PSX multi-out carries analog R, G and
 * B straight off the video DAC, so there is no subcarrier to separate back out
 * and none of the encoded tiers' artifacts - no dot crawl, no rainbow, no
 * cross-luma, no comb residue - can exist. That is why this path skips the
 * encode/comb/demod/notch chain entirely and writes the RGB the resolve wants
 * directly.
 *
 * What survives is the band limit. A real RGB path is still a video amplifier
 * driving a receiver's input stage, so it is not infinitely sharp; it is just
 * limited far more generously than anything that has to share a wire with
 * chroma, and per channel rather than on a luma/chroma split. Applying that
 * limit is the entire simulation, and it is what distinguishes this from 'No
 * Simulation', which hands the framebuffer over untouched.
 *
 * Sampling matches the encoded tiers exactly: output is one base-clock sample
 * per fragment at width = native_pixels * div, evaluated against the CRTC's
 * zero-order hold, so every run of taps landing on one native pixel collapses
 * into a single weighted fetch off the prefix table. The three channels share
 * that fetch - one filter, applied three times - so this costs a third of what
 * running it per channel separately would.
 *
 * The phase/carrier machinery in analog.h is deliberately unused here. It has
 * no meaning on a wire that never modulated anything, and feeding a subcarrier
 * phase into an RGB path would be inventing an artifact rather than modelling
 * one. */

#include "analog.h"

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant, std430) uniform Registers
{
	vec2  src_size;      /* native framebuffer size, texels */
	float div;           /* base clocks per pixel: 10,8,7,5,4 */
} reg;

/* Summed weight of every tap falling on one held pixel, from the prefix table.
 * `lo`/`hi` are tap offsets relative to the filter centre. */
highp float span_weight_rgb(int lo, int hi)
{
	int a = clamp(lo + AN_RGB_HALF,     0, AN_RGB_N);
	int b = clamp(hi + AN_RGB_HALF + 1, 0, AN_RGB_N);
	return AN_RGB_P[b] - AN_RGB_P[a];
}

void main()
{
	highp float n   = floor(gl_FragCoord.x);
	highp float row = vUV.y;

	int   idiv = int(reg.div);
	int   ni   = int(n);
	int   maxp = int(reg.src_size.x) - 1;

	/* Native pixels touched by the span. */
	int q0 = (ni - AN_RGB_HALF) / idiv - 1;
	int q1 = (ni + AN_RGB_HALF) / idiv + 1;

	highp vec3 rgb = vec3(0.0);
	int q;

	for (q = q0; q <= q1; q++)
	{
		/* Tap offsets covered by native pixel q, relative to the centre. */
		int lo = q * idiv - ni;
		int hi = lo + idiv - 1;

		highp float w = span_weight_rgb(lo, hi);
		if (w == 0.0)
			continue;

		highp float px = float(clamp(q, 0, maxp));
		rgb += w * textureLod(uSource,
			vec2((px + 0.5) / reg.src_size.x, row), 0.0).rgb;
	}

	/* The filter has negative lobes, so a hard edge undershoots below black.
	 * A real amplifier cannot drive below its own black level either, and the
	 * resolve that follows converts to light with a transfer curve that is not
	 * defined for negatives. Clamp low only: overshoot above white is real
	 * ringing and the HDR path has headroom for it. */
	FragColor = vec4(max(rgb, vec3(0.0)), 1.0);
}
