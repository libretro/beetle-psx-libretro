#version 450
precision highp float;
precision highp int;

/* Pass 3: demodulate chroma, recover luma, return to RGB. Base rate.
 *
 * There is no chroma band-pass. Multiplying the comb estimate by 2*carrier
 * lands the wanted axis at baseband and its image at 2*fsc, and the chroma
 * low-pass removes the image - which makes the low-pass a matched band-pass
 * centred exactly on fsc. Running a separate 95-tap band-pass first was both
 * three times the cost and measurably worse, because a designed band-pass has
 * ripple and wider skirts than the matched one.
 *
 * Luma is then the composite minus the carrier rebuilt from the recovered I/Q,
 * which is the same subtraction the band-pass version performed, using a
 * cleaner estimate of what to subtract.
 *
 * S-Video skips the reconstruction: luma arrived on its own pin, so subtracting
 * anything from it would be inventing a correction for crosstalk that never
 * happened - and for the same reason it skips the chroma trap downstream. */

#include "analog.h"

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uComb;

layout(push_constant, std430) uniform Registers
{
	vec2  sig_size;
	float x1;
	float inv_ratio;
	float line_adv;
	float field_adv;
	int   svideo;
} reg;

highp vec2 fetch(highp float x, highp float row)
{
	highp float k = clamp(x, 0.0, reg.sig_size.x - 1.0);
	return textureLod(uComb, vec2((k + 0.5) / reg.sig_size.x, row), 0.0).xy;
}

void main()
{
	highp float n    = floor(gl_FragCoord.x);
	highp float row  = vUV.y;
	highp float line = floor(gl_FragCoord.y);
	highp float vs   = an_v_sign(line);

	highp vec2 iq = vec2(0.0);
	int t;
	for (t = -AN_CHROMA_HALF; t <= AN_CHROMA_HALF; t++)
	{
		highp float w  = AN_CHROMA_P[t + AN_CHROMA_HALF + 1] - AN_CHROMA_P[t + AN_CHROMA_HALF];
		highp float k  = n + float(t);
		highp float cc = fetch(k, row).y;
		highp float ph = an_phase(k, line, reg.x1, reg.inv_ratio,
		                          reg.line_adv, reg.field_adv);
		iq += w * an_demodulate(cc, an_carrier(ph), vs);
	}

	highp vec2  here = fetch(n, row);
	highp float y;

	if (reg.svideo != 0)
	{
		y = here.x;
	}
	else
	{
		highp float ph = an_phase(n, line, reg.x1, reg.inv_ratio,
		                          reg.line_adv, reg.field_adv);
		y = here.x - an_modulate(iq, an_carrier(ph), vs);
	}

	/* Emits (luma, C1, C2), not RGB: the chroma trap runs on luma alone and
	 * does the conversion itself once the trap has been applied. */
	FragColor = vec4(y, iq, 1.0);
}
