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

/* Expand a five-bit code to an explicit byte. Do not leave the normalized
 * q/31 fraction for implementation-dependent UNORM conversion. */
highp vec3 psx_expand_color5(highp vec3 color5)
{
   return floor(color5 * (255.0 / 31.0) + vec3(0.5)) / 255.0;
}

/* Select native RGB5 codes; VRAM expands each code as n << 3. Dithering
 * is a signed offset in byte units, applied before RGB5 reduction. */
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

/* SDR scaled storage uses q*8 byte codes for ordinary RGB5 draws. Native
 * keeps that digital expansion; 16 bpp expands the same q to full range.
 * Called after core resolves, before color management or analog simulation. */
highp vec3 psx_display_color(highp vec3 color, int mode)
{
	if (mode == 2)
		return color;
	/* Expanded 16 bpp with enhanced draws/resolves: ordinary stores are
	 * canonical q*8; preserve intermediate shades during display expansion. */
	if (mode == 3)
		return floor(clamp(color * (255.0 / 248.0), 0.0, 1.0) *
			255.0 + vec3(0.5)) / 255.0;
	highp vec3 q5 = psx_color5(color, 0.0);
	highp vec3 display8 = mode == 0 ? q5 * 8.0 :
		floor(q5 * (255.0 / 31.0) + vec3(0.5));
	return display8 / 255.0;
}

#endif
