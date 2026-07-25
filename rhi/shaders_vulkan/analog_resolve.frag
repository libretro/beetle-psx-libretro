#version 450
precision highp float;
precision highp int;

/* Pass 3: demodulate chroma, return to RGB, resample base clock -> display.
 *
 * Output is a gamma-domain R'G'B' triple, exactly what the display quad would
 * otherwise have produced, so the HDR10 encode here is the same one the normal
 * path uses.
 *
 * Under HDR this is where the analog path pays off. Band-limiting rings and the
 * comb leaks, so the decoded signal genuinely overshoots 1.0 at sharp edges -
 * on PSX-like content around 0.5% of samples, peaking near 1.7. In 24-bit those
 * clip flat to white. Handed to encode_hdr10 they land in the roll-off and read
 * as highlights instead, at no extra cost because the overshoot path already
 * exists. Undershoot still clamps: negative light is not a thing. */

#include "analog.h"

#if defined(HDR)
#include "hdr.h"
#endif

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uSeparated;

layout(push_constant, std430) uniform Registers
{
	vec2  sig_size;      /* base-clock signal: native_w*div by native_h */
	vec2  out_size;      /* display resolution, native * internal scale */
	float x1;
	float inv_ratio;
	float line_adv;
	float field_adv;
#if defined(HDR)
	float paper_white_nits;
	int   expand_gamut;
	int   shoulder;
	int   sdr_eotf;
#endif
} reg;

highp vec2 fetch(highp float x, highp float row)
{
	highp float k = clamp(x, 0.0, reg.sig_size.x - 1.0);
	return textureLod(uSeparated, vec2((k + 0.5) / reg.sig_size.x, row), 0.0).xy;
}

highp vec3 decode_at(highp float n, highp float row, highp float line)
{
	highp float vs = an_v_sign(line);
	highp float y  = fetch(n, row).x;

	/* Multiplying by 2*carrier lands the wanted axis at baseband and its
	 * image at 2*fsc, which the chroma low-pass then removes. */
	highp vec2 iq = vec2(0.0);
	for (int t = -AN_CHROMA_N; t <= AN_CHROMA_N; t++)
	{
		highp float w  = AN_CHROMA[t < 0 ? -t : t];
		highp float k  = n + float(t);
		highp float cc = fetch(k, row).y;
		highp float ph = an_phase(k, line, reg.x1, reg.inv_ratio,
		                          reg.line_adv, reg.field_adv);
		highp vec2  cs = an_carrier(ph);
		iq += w * an_demodulate(cc, cs, vs);
	}

	return an_yc_to_rgb(vec3(y, iq));
}

void main()
{
	/* The scanline index drives the subcarrier phase, so it has to be the
	 * signal's line and not the output row - those differ the moment internal
	 * resolution scales the output vertically. The signal has exactly one
	 * sample per scanline; there is no vertical detail beyond that to find. */
	highp float line = floor(vUV.y * reg.sig_size.y);
	highp float row  = vUV.y;

	/* Base samples per output pixel. At 1x internal resolution this is `div`
	 * and the box is the exact inverse of the CRTC hold. Above 1x the output
	 * grid is finer than native, the window narrows, and the sub-native
	 * structure the cable produces - ringing, chroma smear, dot crawl, all of
	 * it at base-clock scale - survives instead of being averaged flat. Below
	 * 1 the signal is simply being interpolated, which is legitimate: it is
	 * band-limited, so there is nothing between the samples to miss. */
	highp float bpo = reg.sig_size.x / max(reg.out_size.x, 1.0);
	highp float n0  = floor(gl_FragCoord.x) * bpo;
	highp int   num = int(clamp(floor(bpo + 0.5), 1.0, 16.0));
	highp vec3  acc = vec3(0.0);
	highp float cnt = 0.0;
	for (int i = 0; i < 16; i++)
	{
		if (i >= num)
			break;
		acc += decode_at(n0 + float(i), row, line);
		cnt += 1.0;
	}

	highp vec3 rgb = max(acc / max(cnt, 1.0), vec3(0.0));

#if defined(HDR)
	FragColor = vec4(encode_hdr10(rgb, reg.paper_white_nits, reg.expand_gamut,
	                              reg.shoulder, reg.sdr_eotf), 1.0);
#else
	FragColor = vec4(min(rgb, vec3(1.0)), 1.0);
#endif
}
