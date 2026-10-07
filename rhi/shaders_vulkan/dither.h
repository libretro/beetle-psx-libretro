#ifndef DITHER_H_
#define DITHER_H_

#include "color.h"

layout(set = 0, binding = 3, std140) uniform DitherInfo
{
	int dither_shift;
} dither_info;

highp vec3 apply_dither(highp vec3 color, ivec2 coord)
{
	ivec2 wrapped_coord = (coord >> dither_info.dither_shift) & 3; // Dither pattern repeats every four pixels.
	float offset = psx_dither_offset(wrapped_coord);
	return psx_color5(color, offset) / 31.0;
}

#endif
