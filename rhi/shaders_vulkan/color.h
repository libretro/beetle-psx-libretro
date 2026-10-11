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

highp float psx_dither_offset(sampler2D lut, ivec2 coord)
{
	/* The shared R8_UNORM LUT stores signed -4..3 as bytes 0..7. */
	return round(texelFetch(lut, coord & 3, 0).x * 255.0) - 4.0;
}

#endif
