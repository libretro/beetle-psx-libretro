#version 450
precision highp float;
precision highp int;

/* Pass 2: three-line comb.
 *
 * Composite carries Y and C summed on one wire, and the only handle on them is
 * that the carrier inverts every line while luma mostly does not. Averaging the
 * neighbours cancels chroma and leaves luma; subtracting leaves chroma:
 *
 *     est = AN_COMB_GAIN * (line - 0.5 * (line_above + line_below))
 *
 * Gain differs by region: NTSC advances the carrier half a cycle per line so
 * the neighbours land in antiphase with the centre (0.5); PAL advances exactly
 * three quarters, putting the neighbours in antiphase with each other so they
 * cancel outright (1.0). PAL only works this cleanly because the PSX omits the
 * broadcast 1/625-per-line offset - see analog.h.
 *
 * Exact where chroma is constant vertically, wrong at every horizontal colour
 * edge, which is where hanging dots come from. That is the cable, not an error.
 *
 * This used to be inlined into a 95-tap band-pass loop, which meant three line
 * fetches per tap - 285 reads per sample to produce a value that depends on
 * three. It is its own pass now and the band-pass is gone entirely.
 *
 * S-Video short-circuits: Y and C arrive on separate pins, nothing to separate.
 * That is the whole difference between the cables, and why a 1px checker
 * survives S-Video and turns to rainbow on composite. */

#include "analog.h"

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uSignal;

layout(push_constant, std430) uniform Registers
{
	vec2 sig_size;
	int  svideo;
} reg;

void main()
{
	highp vec2 uv  = vUV;
	highp vec2 cur = textureLod(uSignal, uv, 0.0).xy;

	if (reg.svideo != 0)
	{
		/* .x luma, .y chroma, already separate. */
		FragColor = vec4(cur.x, cur.y, 0.0, 1.0);
		return;
	}

	highp float dy = 1.0 / reg.sig_size.y;
	highp vec2  up = textureLod(uSignal, vec2(uv.x, max(uv.y - dy, 0.0)), 0.0).xy;
	highp vec2  dn = textureLod(uSignal, vec2(uv.x, min(uv.y + dy, 1.0)), 0.0).xy;

	highp float c_cur = cur.x + cur.y;
	highp float est   = AN_COMB_GAIN * (c_cur - 0.5 * ((up.x + up.y) + (dn.x + dn.y)));

	/* .x is the full composite so the next pass can form luma by subtraction. */
	FragColor = vec4(c_cur, est, 0.0, 1.0);
}
