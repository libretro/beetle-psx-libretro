#version 450
precision highp float;
precision highp int;

/* Pass 4: resample base clock -> display, and hand off to the display encode.
 *
 * Everything upstream now emits RGB at base rate, so this is a box filter and
 * nothing else. It used to carry the chroma demodulator, which meant running a
 * 31-tap filter once per base sample *per output pixel* - the same work
 * repeated at every internal-resolution multiple. Demodulating once at base
 * rate and resampling afterwards makes this pass essentially free and makes the
 * chain's cost independent of internal resolution.
 *
 * Output is gamma-domain R'G'B', exactly what the display quad would otherwise
 * have produced, so the HDR10 encode here is the one the normal path uses.
 *
 * Clamped to [0,1]. Band-limiting rings and the comb leaks, so the decoded
 * signal does overshoot at sharp edges - but that is filter ringing, not light.
 * A television clips it, and treating it as emissive content makes every edge
 * glow. */

#if defined(HDR)
#include "hdr.h"
#endif

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uDecoded;

layout(push_constant, std430) uniform Registers
{
	vec2  sig_size;      /* base-clock signal: native_w*div by native_h */
	vec2  out_size;      /* display resolution, native * internal scale */
#if defined(HDR)
	float paper_white_nits;
	int   expand_gamut;
	int   shoulder;
	int   sdr_eotf;
	int   src_primaries;  /* authoring display gamut: 0 709, 1 SMPTE-C, 2 EBU, 3 NTSC1953 */
#endif
} reg;

void main()
{
	highp float row = vUV.y;

	/* Base samples per output pixel. At 1x this is `div` and the box is the
	 * exact inverse of the CRTC hold. Above 1x the window narrows and the
	 * sub-native structure the cable produces - ringing, chroma smear, dot
	 * crawl, all at base-clock rate - survives instead of averaging flat.
	 * Below 1 the band-limited signal is simply interpolated. */
	highp float bpo = reg.sig_size.x / max(reg.out_size.x, 1.0);
	highp float n0  = floor(gl_FragCoord.x) * bpo;
	highp int   num = int(clamp(floor(bpo + 0.5), 1.0, 16.0));

	highp vec3  acc = vec3(0.0);
	highp float cnt = 0.0;
	int i;
	for (i = 0; i < 16; i++)
	{
		if (i >= num)
			break;
		highp float k = clamp(n0 + float(i), 0.0, reg.sig_size.x - 1.0);
		acc += textureLod(uDecoded, vec2((k + 0.5) / reg.sig_size.x, row), 0.0).rgb;
		cnt += 1.0;
	}

	highp vec3 rgb = clamp(acc / max(cnt, 1.0), vec3(0.0), vec3(1.0));

#if defined(HDR)
	FragColor = vec4(encode_hdr10(rgb, reg.paper_white_nits, reg.expand_gamut,
	                              reg.shoulder, reg.sdr_eotf, reg.src_primaries), 1.0);
#else
	FragColor = vec4(rgb, 1.0);
#endif
}
