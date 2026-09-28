#ifndef PRIMITIVE_H
#define PRIMITIVE_H

/* RGB5 write thresholds are one 8-bit code apart. Keep the interpolated
 * colour out of RelaxedPrecision so mobile FP16 arithmetic cannot move a
 * value across an authoritative truncation boundary. */
layout(location = 0) in highp vec4 vColor;
layout(location = 2) flat in mediump ivec3 vParam;
#if !defined(UNSCALED)
layout(constant_id = 3) const int SCALE = 1;
#endif
#ifdef TEXTURED
     #include "vram.h"
#endif
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D uDitherLUT;

/* Per-primitive state carried in BufferVertex::params. Native colour depth is
 * separate from the GP0 DTD bit: DTD selects the offset matrix, while every
 * ordinary 16-bpp write is reduced to RGB5 even when DTD is clear. */
const uint PARAM_NATIVE_COLOR = 0x0400u;
const uint PARAM_DITHER_NATIVE_RESOLUTION = 0x4000u;
const uint PARAM_DITHER = 0x8000u;

bool primitive_native_color()
{
	return (uint(vParam.z) & PARAM_NATIVE_COLOR) != 0u;
}

bool primitive_dither_enabled()
{
	return (uint(vParam.z) & PARAM_DITHER) != 0u;
}

ivec2 primitive_dither_coord()
{
	ivec2 coord = ivec2(gl_FragCoord.xy);
#if !defined(UNSCALED)
	if ((uint(vParam.z) & PARAM_DITHER_NATIVE_RESOLUTION) != 0u)
		coord /= SCALE;
#endif
	return coord & 3;
}

highp vec3 quantize_native_rgb5(highp vec3 color, bool dither)
{
	highp vec3 color8 = clamp(color, vec3(0.0), vec3(1.0)) * 255.0;
	if (dither)
	{
		float offset = round(texelFetch(uDitherLUT,
			primitive_dither_coord(), 0).x * 255.0) - 4.0;
		color8 += vec3(offset);
	}

	/* Keep the scaled framebuffer in the same 8-bit expansion used by
	 * abgr1555(): a stored 5-bit value n is represented by n << 3, not n/31.
	 * The small epsilon stabilizes values that are already exact multiples of
	 * eight against floating-point noise without changing a real threshold. */
	return floor((clamp(color8, vec3(0.0), vec3(255.0)) + 0.001) / 8.0) *
		(8.0 / 255.0);
}

#endif
