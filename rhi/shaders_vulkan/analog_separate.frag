#version 450
precision highp float;
precision highp int;

/* Pass 2: separate luma and chroma.
 *
 * Composite carries Y and C summed on one wire, and the only handle on them is
 * that the carrier inverts every line while luma mostly does not. Averaging the
 * neighbours cancels chroma and leaves luma; subtracting leaves chroma:
 *
 *     est = 0.5 * (line - 0.5 * (line_above + line_below))
 *
 * scaled by AN_COMB_GAIN, which differs by region: NTSC puts the neighbours in
 * antiphase with the centre (gain 0.5), PAL puts them in antiphase with each
 * other so they cancel outright (gain 1.0). PAL only works this cleanly because
 * the PSX omits the broadcast 1/625-per-line offset; see analog.h.
 *
 * That is exact where chroma is constant vertically and wrong at every
 * horizontal colour edge, which is where hanging dots come from. It is an
 * artifact of the cable, not an error to filter away.
 *
 * The comb is inlined into the band-pass rather than run as its own pass: the
 * band-pass needs the estimate at neighbouring samples, so splitting them would
 * cost a full-width intermediate for three texture reads of work. Inlined it is
 * 3 fetches per tap, and the pass after this one gets a finished chroma signal
 * it can demodulate without re-running any of it.
 *
 * S-Video skips the comb entirely - Y and C arrive on separate pins, nothing to
 * separate. That is the entire difference between the two cables, and it is why
 * a 1px checker survives S-Video and turns to rainbow on composite. */

#include "analog.h"

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uSignal;

layout(push_constant, std430) uniform Registers
{
	vec2 sig_size;
	int  svideo;
} reg;

highp vec2 fetch(highp float x, highp float row)
{
	highp float k = clamp(x, 0.0, reg.sig_size.x - 1.0);
	return textureLod(uSignal, vec2((k + 0.5) / reg.sig_size.x, row), 0.0).xy;
}

/* Composite = Y + C on one wire. */
highp float comb_est(highp float x, highp float row, highp float dy)
{
	highp vec2 c = fetch(x, row);
	highp vec2 u = fetch(x, max(row - dy, 0.0));
	highp vec2 d = fetch(x, min(row + dy, 1.0));
	return AN_COMB_GAIN * ((c.x + c.y) - 0.5 * ((u.x + u.y) + (d.x + d.y)));
}

void main()
{
	highp float n   = floor(gl_FragCoord.x);
	highp float row = vUV.y;

	if (reg.svideo != 0)
	{
		highp vec2 yc = fetch(n, row);
		FragColor = vec4(yc.x, yc.y, 0.0, 1.0);
		return;
	}

	highp float dy = 1.0 / reg.sig_size.y;

	/* Chroma should not exist outside its band, so band-pass the comb
	 * estimate before trusting it. */
	highp float c = 0.0;
	for (int t = -AN_BANDPASS_N; t <= AN_BANDPASS_N; t++)
		c += AN_BANDPASS[t < 0 ? -t : t] * comb_est(n + float(t), row, dy);

	highp vec2  here = fetch(n, row);
	highp float y    = (here.x + here.y) - c;

	FragColor = vec4(y, c, 0.0, 1.0);
}
