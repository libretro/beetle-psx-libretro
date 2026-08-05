#version 450
precision highp float;
precision highp int;

/* The chroma trap's bypass branch, as a fragment shader.
 *
 * analog_notch.comp does two jobs: it runs the IIR trap on luma, and it does
 * everything that has to happen regardless of the trap - the PAL delay line,
 * the NTSC setup pedestal, and the YIQ/YUV -> RGB conversion. Its `enable == 0`
 * path is exactly the second job on its own, which is what S-Video takes:
 * luma never shared a wire there, so there is no carrier residue to trap and a
 * notch would only cost detail.
 *
 * That same path is what a GL context below the compute floor needs. The trap
 * is a compute shader - shared memory, a barrier, a cross-lane scan - so it
 * needs GL 4.3 or GLES 3.1, and this renderer's floor is 3.0. Rather than
 * disable the cable simulation on such a driver, the encoded tiers run without
 * the trap: composite and RF keep their band limits, their comb, their
 * demodulation and their beat, and pay for it with the hanging dots along
 * horizontal colour edges that the trap exists to remove. That is a visible
 * difference from the Vulkan path and from GL 4.3+, and it is a much smaller
 * one than losing the cable entirely.
 *
 * Kept here, beside the compute shader it mirrors, rather than written into
 * the GL backend: it is the same arithmetic, and a second copy of the delay
 * line and the colour matrices is a second copy to get wrong. The GL
 * translation is generated from this file. */

#include "analog.h"

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uDecoded;   /* (luma, C1, C2) */

layout(push_constant, std430) uniform Registers
{
	vec2  sig_size;
	float line_split;    /* 1 progressive, 2 interlaced */
	float black_scale;   /* luma pedestal: y -> y*scale + offset. 1,0 = no-op */
	float black_offset;
} reg;

highp vec3 fetch(int x, int line)
{
	int k = clamp(x, 0, int(reg.sig_size.x) - 1);
	return texelFetch(uDecoded, ivec2(k, line), 0).xyz;
}

/* Chroma as the decoder sees it after the delay line. Luma is untouched -
 * averaging it vertically would just cost resolution. Identical to
 * analog_notch.comp's fetch_chroma_filtered; see there for why PAL averages
 * and NTSC does not. */
highp vec3 fetch_chroma_filtered(int x, int line)
{
	highp vec3 cur = fetch(x, line);
#if defined(PAL)
	int h = int(reg.sig_size.y) - 1;
	int d = int(reg.line_split);   /* same-field neighbours */
	highp vec3 up = fetch(x, max(line - d, 0));
	highp vec3 dn = fetch(x, min(line + d, h));
	cur.yz = 0.25 * up.yz + 0.5 * cur.yz + 0.25 * dn.yz;
#endif
	return cur;
}

void main()
{
	int         x   = int(floor(gl_FragCoord.x));
	int         line = int(floor(gl_FragCoord.y));
	highp vec3  yc  = fetch_chroma_filtered(x, line);

	yc.x = yc.x * reg.black_scale + reg.black_offset;
	FragColor = vec4(an_yc_to_rgb(yc), 1.0);
}
