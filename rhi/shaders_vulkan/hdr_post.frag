#version 450
precision highp float;
precision highp int;

/* HDR post composite, after boom3's hdr_composite: chromatic aberration,
 * the (halation-tinted) bloom, then the grade and encode the display quad
 * would have run. Reads the scene-linear image the quad wrote with
 * HDR_LINEAR_OUT (1.0 = paper white, authoring primaries) and the two bloom
 * bands. Always built with -DHDR. */

#include "hdr.h"

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uBloomT;
layout(set = 0, binding = 2) uniform sampler2D uBloomW;

layout(push_constant, std430) uniform Registers
{
	vec4  hal_bloom;      /* xyz halation tint, w bloom amount (0 off) */
	vec2  band_w;         /* tight band, wide band weights */
	float ca;             /* chromatic aberration, 0 off */
	float paper_white_nits;
	float peak_nits;
	int   expand_gamut;
	int   shoulder;
	int   src_primaries;
	float black_pq;
	int   scrgb;
	mat3  to_panel;
} reg;

void main()
{
	highp vec3 lin = textureLod(uScene, vUV, 0.0).rgb;

	/* Transverse chromatic aberration. A lens focuses long and short
	 * wavelengths at slightly different magnifications, so red and blue
	 * land at slightly different distances from the axis. It grows with
	 * the square of the field height: nothing at the centre, most at the
	 * corners. A fixed fraction of the frame, so it is the same artifact at
	 * every internal resolution. */
	if (reg.ca != 0.0)
	{
		highp vec2  d = vUV - 0.5;
		highp float r = dot(d, d) * reg.ca;
		lin.r = textureLod(uScene, vUV - d * r, 0.0).r;
		lin.b = textureLod(uScene, vUV + d * r, 0.0).b;
	}

	/* Bloom in, before the roll-off, so glow lands in the headroom. The
	 * wide band is fetched only when it is weighted. Halation tints the
	 * bloom toward red-orange, the way light scattered back off a film's
	 * base comes back reddened. */
	if (reg.hal_bloom.w > 0.0)
	{
		highp vec3 bl = reg.band_w.x * textureLod(uBloomT, vUV, 0.0).rgb;
		if (reg.band_w.y > 0.0)
			bl += reg.band_w.y * textureLod(uBloomW, vUV, 0.0).rgb;
		lin += (reg.hal_bloom.w * reg.hal_bloom.xyz) * bl;
	}

	FragColor = vec4(encode_hdr_output_linear(max(lin, vec3(0.0)), reg.paper_white_nits,
	                                          reg.peak_nits, reg.expand_gamut, reg.shoulder,
	                                          reg.src_primaries, reg.black_pq,
	                                          reg.to_panel, reg.scrgb), 1.0);
}
