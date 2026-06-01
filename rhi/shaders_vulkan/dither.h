#ifndef DITHER_H_
#define DITHER_H_

layout(set = 0, binding = 2) uniform mediump sampler2D uDitherLUT;
layout(set = 0, binding = 3, std140) uniform DitherInfo
{
	float range;
	float inv_range;
	float dither_scale;
	int dither_shift;
} dither_info;

mediump vec3 apply_dither(mediump vec3 color, ivec2 coord)
{
	ivec2 wrapped_coord = (coord >> dither_info.dither_shift) & 3; // Dither LUT is 4x4.
	mediump float dither = texelFetch(uDitherLUT, wrapped_coord, 0).x * dither_info.dither_scale;
	mediump vec3 scaled_color = (color + dither) * dither_info.range;
	// Dither LUT is UNORM, round down here is equivalent to +/- dither and round-to-nearest.
	mediump vec3 rounded_color = floor(scaled_color);
	return rounded_color * dither_info.inv_range;
}

#endif
