#ifndef PRIMITIVE_H
#define PRIMITIVE_H

#include "color.h"

/* RGB5 write thresholds are eight byte codes apart. Keep the interpolated
 * colour out of RelaxedPrecision so mobile FP16 arithmetic cannot move a
 * value across an authoritative truncation boundary. */
layout(location = 0) in highp vec4 vColor;
layout(location = 2) flat in mediump ivec3 vParam;
layout(location = 7) flat in highp uvec3 vColorOrigin;
layout(location = 8) flat in highp uvec3 vColorDX;
layout(location = 9) flat in highp uvec3 vColorDY;
layout(location = 13) flat in highp uvec2 vLine;
#if !defined(UNSCALED)
layout(constant_id = 3) const int SCALE = 1;
#endif
#ifdef TEXTURED
     #include "vram.h"
#endif
layout(location = 0) out vec4 FragColor;
layout(constant_id = 10) const int FRAMEBUFFER_FLOAT16 = 0;

highp vec3 truncate_color8(highp vec3 color)
{
	if (FRAMEBUFFER_FLOAT16 != 0)
		return max(color - 0.49 / 255.0, vec3(0.0));

	/* A bias near half an UNORM code relies on implementation-dependent
	 * float-to-fixed rounding. An exact GP0 colour can lose a bit that a
	 * later indexed texture read interprets as a different palette index.
	 * Truncate explicitly, stabilizing exact 8-bit inputs against float
	 * interpolation noise, and write exactly on its UNORM code value.
	 * Keep multiply and add separate so drivers agree at the tolerance
	 * boundaries instead of choosing different levels through contraction. */
	return psx_color8(color) / 255.0;
}

/* Per-primitive state carried in BufferVertex::params. Native colour depth is
 * separate from the GP0 DTD bit: DTD selects the offset matrix, while every
 * ordinary 16-bpp write is reduced to RGB5 even when DTD is clear. */
const uint PARAM_FIXED_COLOR = 0x0004u;
const uint PARAM_NATIVE_LINE = 0x0010u;
const uint PARAM_NATIVE_COLOR = 0x0400u;
#ifdef TEXTURED
const uint PARAM_FRAMEBUFFER_FEEDBACK = 0x0800u;
#endif
const uint PARAM_DITHER_NATIVE_RESOLUTION = 0x4000u;
const uint PARAM_DITHER = 0x8000u;

#ifdef TEXTURED
/* Scaled Vulkan VRAM can hold an RGB5 texel as either n << 3 (native-colour
 * storage) or n / 31 (the older fixed-feedback output). Decode through the
 * 8-bit expansion so both representations return the exact same n. */
highp vec3 framebuffer_feedback_texel5(highp vec3 color)
{
	/* Reconstruction keeps the existing RGB5-code tolerance, which also
	 * recovers native-colour codes rounded down by float16 storage. */
	return clamp(floor(color * (255.0 / 8.0) + vec3(0.001)),
		vec3(0.0), vec3(31.0));
}
#endif

bool primitive_native_color()
{
	return (uint(vParam.z) & PARAM_NATIVE_COLOR) != 0u;
}

void primitive_line_coverage()
{
	if ((uint(vParam.z) & PARAM_NATIVE_LINE) == 0u)
		return;
	ivec2 first = ivec2(int(vLine.x << 16u) >> 16, int(vLine.x) >> 16);
	ivec2 delta = ivec2(int(vLine.y << 16u) >> 16, int(vLine.y) >> 16);
	ivec2 coord = ivec2(gl_FragCoord.xy);
	int steps = max(abs(delta.x), abs(delta.y));
	bool horizontal = abs(delta.x) > abs(delta.y);
	int major = horizontal ? coord.x - first.x : coord.y - first.y;
	int direction = horizontal ? delta.x : delta.y;
	int k = direction < 0 ? -major : major;
	if (k < 0 || k > steps)
		discard;
	int minor = horizontal ? first.y : first.x;
	if (steps != 0)
	{
		int d = horizontal ? delta.y : delta.x;
		// Match GL: DrawLine biases X, and descending Y, by -1024 / 2^32.
		// Legal lines have fewer than 1024 steps, so ceil-step error cannot
		// cancel the bias. X ties down; Y ties down only when descending.
		int n = 2 * d * k + steps - ((!horizontal || d < 0) ? 1 : 0);
		int divisor = 2 * steps;
		minor += (n - (n < 0 ? divisor - 1 : 0)) / divisor;
	}
	if ((horizontal ? coord.y : coord.x) != minor)
		discard;
}

highp vec3 primitive_color()
{
	if ((uint(vParam.z) & PARAM_FIXED_COLOR) == 0u)
		return vColor.rgb;

	/* Native integer pixel coordinates; CPU slopes are truncated to 12
	 * fractional bits and the origin already includes the half-code bias.
	 * Wrap and extract the byte before texture modulation or RGB5 reduction. */
	uvec2 coord = uvec2(gl_FragCoord.xy);
	uvec3 color = vColorOrigin + coord.x * vColorDX + coord.y * vColorDY;
	return vec3((color >> 12u) & 255u) / 255.0;
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

highp float primitive_dither_offset()
{
	return primitive_dither_enabled() ?
		psx_dither_offset(primitive_dither_coord()) : 0.0;
}

highp vec3 quantize_native_rgb5(highp vec3 color, bool dither)
{
	float offset = dither ? primitive_dither_offset() : 0.0;
	return psx_color5(color, offset) * (8.0 / 255.0);
}

#endif
