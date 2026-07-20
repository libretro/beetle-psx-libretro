#version 450
precision highp float;
precision highp int;

#if defined(DITHER)
#include "dither.h"
#endif
#if defined(HDR)
#include "hdr.h"
#endif

layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(set = 0, binding = 1) uniform sampler2D uLOD;
layout(location = 0) in vec2 vUV;

layout(push_constant, std430) uniform Registers
{
	vec2 offset;
	vec2 range;
	vec2 uv_min;
	vec2 uv_max;
	float max_bias;
#if defined(HDR)
	/* Appended for the -DHDR variant; SDR keeps its 36-byte layout. */
	float paper_white_nits;
	int   expand_gamut;
	int   shoulder;
#endif
} registers;

void main()
{
	vec2 lod_uv = clamp(vUV, registers.uv_min, registers.uv_max);
	float b = textureLod(uLOD, lod_uv, 0.0).x;
	mediump vec3 rgb = textureLod(uTexture, lod_uv, registers.max_bias * b).rgb;
#if defined(HDR)
	/* The trilinear resolve interpolates the 8-bit source, producing
	 * sub-8-bit precision that 10-bit output preserves - so no debanding is
	 * needed (or wanted) here; just encode. */
	FragColor = vec4(encode_hdr10(rgb, registers.paper_white_nits, registers.expand_gamut, registers.shoulder), 1.0);
#else
	#if defined(DITHER)
		rgb = apply_dither(rgb, ivec2(gl_FragCoord.xy));
	#endif
	FragColor = vec4(rgb, 1.0);
#endif
}

