#version 450
precision highp float;
precision highp int;

/* Pass 1 of the analog path: framebuffer -> modulated signal.
 *
 * Output is one base-clock sample per fragment, at width = visible_pixels *
 * div. The gather folds the CRTC's zero-order hold into the filter, so there
 * is no intermediate upscaled texture: base sample k belongs to native pixel
 * k / div, and the luma / chroma low-passes are evaluated directly over that.
 *
 * Emits vec2: .x = luma-ish channel, .y = modulated chroma. Composite is the
 * sum of the two and the decode has to separate them again; S-Video keeps them
 * apart, which is exactly the difference between the two cables. RGB does not
 * come through here at all - it bypasses the whole path. */

#include "analog.h"

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant, std430) uniform Registers
{
	vec2  src_size;      /* native framebuffer size, texels */
	float div;           /* base clocks per pixel: 10,8,7,5,4 */
	float x1;            /* GP1(06h).X1, video clocks from HSYNC */
	float inv_ratio;     /* 1/15 NTSC, 1/12 PAL */
	float line_adv;      /* 0.5 NTSC, 0.75 PAL */
	float field_adv;     /* from the frame/field counter */
} reg;

/* Native pixel under base-clock sample k, clamped at the line edges so the
 * filter skirts do not wrap into the opposite side of the screen. */
highp vec3 fetch_base(highp float k, highp float row)
{
	highp float px = clamp(floor(k / reg.div), 0.0, reg.src_size.x - 1.0);
	return textureLod(uSource, vec2((px + 0.5) / reg.src_size.x, row), 0.0).rgb;
}

void main()
{
	highp float n    = floor(gl_FragCoord.x);
	highp float row  = vUV.y;
	highp float line = floor(gl_FragCoord.y);

	/* Luma and chroma are band-limited separately and to very different
	 * widths - ~4.2 MHz against ~1.3 MHz - which is most of why composite
	 * looks the way it does. Two gathers rather than one because the tap
	 * counts differ by 3x and running luma at the chroma length would throw
	 * away the detail the luma filter is meant to keep. */
	highp float y = 0.0;
	for (int t = -AN_LUMA_N; t <= AN_LUMA_N; t++)
	{
		highp float w = AN_LUMA[t < 0 ? -t : t];
		y += w * an_rgb_to_yc(fetch_base(n + float(t), row)).x;
	}

	highp vec2 iq = vec2(0.0);
	for (int t = -AN_CHROMA_N; t <= AN_CHROMA_N; t++)
	{
		highp float w = AN_CHROMA[t < 0 ? -t : t];
		iq += w * an_rgb_to_yc(fetch_base(n + float(t), row)).yz;
	}

	highp float ph = an_phase(n, line, reg.x1, reg.inv_ratio,
	                          reg.line_adv, reg.field_adv);
	highp vec2  cs = an_carrier(ph);
	highp float vs = an_v_sign(line);

	FragColor = vec4(y, an_modulate(iq, cs, vs), 0.0, 1.0);
}
