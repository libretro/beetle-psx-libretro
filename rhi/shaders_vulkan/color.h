#ifndef COLOR_H
#define COLOR_H

/* Byte-domain truncation policy. The 0.001-code tolerance stabilizes exact
 * byte inputs after float arithmetic; values inside that margin intentionally
 * choose the upper code. Keep the scale and bias separate on every driver. */
highp vec3 psx_color8(highp vec3 color)
{
	precise highp vec3 color8 = max(color, vec3(0.0)) * 255.0 + vec3(0.001);
	return floor(color8);
}

/* Select RGB5 codes before choosing their storage encoding: VRAM expands
 * n as n << 3, whereas the display's RGB5 UNORM target represents n / 31.
 * Dithering is a signed offset in byte units, applied before RGB5 reduction. */
highp vec3 psx_color5(highp vec3 color, highp float offset)
{
	precise highp vec3 color8 = color * 255.0;
	precise highp vec3 biased = color8 + vec3(offset) + vec3(0.001);
	return clamp(floor(biased / 8.0), vec3(0.0), vec3(31.0));
}

highp float psx_dither_offset(highp ivec2 coord)
{
	/* The fixed PS1 matrix is:
	 * -4  0 -3  1
	 *  2 -2  3 -1
	 * -3  1 -4  0
	 *  3 -1  2 -2
	 * Its biased three-bit code interleaves XY parity and the low Y bit. */
	highp int parity = coord.x ^ coord.y;
	highp int code = ((parity & 1) << 2) | ((coord.y & 1) << 1) |
		((parity >> 1) & 1);
	return float(code - 4);
}

#endif
