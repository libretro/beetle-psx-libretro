#version 450
precision highp float;
precision highp int;

#include "common.h"
#if defined(DITHER)
#include "dither.h"
#endif

layout(location = 0) in highp vec2 vUV;

#if defined(SCALED)
layout(set = 0, binding = 0) uniform sampler2D uTexture;
#elif defined(UNSCALED)
layout(set = 0, binding = 0) uniform usampler2D uTexture;
#endif
#if defined(HDR)
layout(location = 0) out highp vec4 FragColor;
#else
layout(location = 0) out mediump vec4 FragColor;
#endif

layout(push_constant, std430) uniform Registers
{
	vec2 offset;
	vec2 range;
#if defined(HDR)
	/* Only present in the -DHDR variants, so the SDR pipelines keep
	 * their exact 16-byte push-constant layout. Fed from the frontend's
	 * HDR params (see libretro.c psx_hdr_paper_white_nits / _expand_gamut). */
	float paper_white_nits;
	int   expand_gamut;
	int   shoulder;   /* highlight roll-off: 0 Reinhard, 1 ACES */
#endif
} registers;

#if defined(HDR)
#include "hdr.h"
#endif

#if defined(BPP24)
mediump vec3 sample_bpp24(ivec2 coord)
{
	int base_x = (coord.x * 3) >> 1;
	int shift = 8 * (coord.x & 1);
	coord.x = base_x;
	vec2 uv = (vec2(coord) + 0.5) / vec2(1024.0, 512.0) + registers.offset;
	uint value = (textureLod(uTexture, uv, 0.0).x & 0xffffu) | (textureLodOffset(uTexture, uv, 0.0, ivec2(1, 0)).x << 16u);
	value >>= uint(shift);

	// The byte extraction must stay highp. Assigning the expression
	// directly to a mediump vec3 makes glslang decorate the integer
	// constructor/shift/mask chain RelaxedPrecision, and mobile GPUs
	// which honor 16-bit relaxed integers then truncate `value` before
	// the per-channel shifts, zeroing the blue (>> 16) lane.
	highp uvec3 channels = (uvec3(value) >> uvec3(0u, 8u, 16u)) & 0xffu;

	mediump vec3 rgb = vec3(channels) / 255.0;
	return rgb;
}

#if defined(BPP24_YUV)
const mediump mat3 rgb_to_yuv = mat3(0.299, -0.14713, 0.615, 0.587, -0.28886, -0.51499, 0.114, 0.436, -0.10001);
const mediump mat3 yuv_to_rgb = mat3(1.0, 1.0, 1.0, 0.0, -0.39465, 2.03211, 1.13983, -0.58060, 0.0);

mediump float to_luma(mediump vec3 rgb)
{
	return (rgb_to_yuv * rgb).x;
}

mediump vec2 to_chroma(mediump vec3 rgb)
{
	return (rgb_to_yuv * rgb).yz;
}

mediump vec3 sample_bpp24_quad(ivec2 coord)
{
	return 0.25 * (sample_bpp24(coord) + sample_bpp24(coord + ivec2(1, 0)) + sample_bpp24(coord + ivec2(0, 1)) + sample_bpp24(coord + 1));
}

mediump vec3 to_rgb(vec3 yuv)
{
	return yuv_to_rgb * yuv;
}

mediump vec3 sample_bpp24_yuv(ivec2 coord)
{
	ivec2 base_coord = coord - 1;
	ivec2 coord_low = base_coord & ~1;
	ivec2 coord_high = coord_low + 2;

	// We assume chroma is sited at center, because we reconstruct the low-res chroma by averaging a 2x2 quad.
	mediump vec2 chroma_filter_coeff = vec2(base_coord & 1) * 0.5 + 0.25;

	// Avoid negative coordinates by clamping.
	coord_low = max(coord_low, ivec2(0));
	coord_high = max(coord_high, ivec2(0));

	// Get our estimated, downsampled chroma by taking the average of 4 samples and converting that.
	// We need 4 chroma samples, which we then linearly interpolate.
	mediump vec3 rgb = sample_bpp24(coord);
	mediump vec3 rgb00 = sample_bpp24_quad(coord_low);
	mediump vec3 rgb10 = sample_bpp24_quad(ivec2(coord_high.x, coord_low.y));
	mediump vec3 rgb01 = sample_bpp24_quad(ivec2(coord_low.x, coord_high.y));
	mediump vec3 rgb11 = sample_bpp24_quad(coord_high);

	mediump vec3 filtered0 = mix(rgb00, rgb10, chroma_filter_coeff.x);
	mediump vec3 filtered1 = mix(rgb01, rgb11, chroma_filter_coeff.x);
	mediump vec3 filtered = mix(filtered0, filtered1, chroma_filter_coeff.y);

	mediump float y = to_luma(rgb);
	mediump vec2 uv = to_chroma(filtered);
	return to_rgb(vec3(y, uv));
}
#endif
#endif

void main()
{
	mediump vec3 rgb;
#if defined(SCALED)
	rgb = textureLod(uTexture, vUV, 0.0).rgb;
	#if defined(DITHER)
		rgb = apply_dither(rgb, ivec2(gl_FragCoord.xy));
	#endif
	#if defined(HDR)
		rgb = hdr_deband(rgb, gl_FragCoord.xy);
	#endif
#elif defined(UNSCALED)
	#if defined(BPP24)
		ivec2 coord = ivec2((vUV - registers.offset) * vec2(1024.0, 512.0));
		#if defined(BPP24_YUV)
			rgb = sample_bpp24_yuv(coord);
		#else
			rgb = sample_bpp24(coord);
		#endif
		#if defined(HDR)
			#if defined(BPP24_YUV)
				/* chroma is already sub-8-bit (interpolated) and preserved
				 * at 10-bit; dither luma only so we don't re-roughen it */
				rgb = hdr_deband_luma(rgb, gl_FragCoord.xy);
			#else
				rgb = hdr_deband(rgb, gl_FragCoord.xy);
			#endif
		#endif
	#else
		uint value = textureLod(uTexture, vUV, 0.0).x;
		rgb = abgr1555(value).rgb;
		#if defined(DITHER)
			rgb = apply_dither(rgb, ivec2(gl_FragCoord.xy));
		#endif
	#endif
#endif

#if defined(HDR)
	/* The SDR rgb is gamma-encoded 0..1; encode_hdr10 handles the
	 * linearise / paper-white / gamut / PQ chain. */
	FragColor = vec4(encode_hdr10(rgb, registers.paper_white_nits, registers.expand_gamut, registers.shoulder), 1.0);
#else
	FragColor = vec4(rgb, 1.0);
#endif
}
