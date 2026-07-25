#version 450
precision highp float;
precision highp int;

/* Pass 0 of the analog path, only when internal resolution is above 1x.
 *
 * Resolves the supersampled framebuffer down to native before anything is
 * modulated. This is the right order and not merely the cheap one: the console
 * emits a fixed-rate signal - 2560 base samples per active line, one sample per
 * scanline - and no amount of internal resolution changes that. Extra samples
 * buy a better-resolved *source* going into the encoder, which is exactly the
 * supersample-then-keep-native-output approach, not a wider signal.
 *
 * Done as its own pass because folding it into the encode would put a
 * scale_x * scale_y gather inside a 95-tap filter loop - 1520 texel reads per
 * output sample at 8x. Here it is scale^2 reads per native pixel, once.
 *
 * Box rather than anything cleverer: the samples being combined are point
 * samples of the same pixel's coverage, so an unweighted mean is what
 * supersampling means. The band-limiting that follows is the real filter. */

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant, std430) uniform Registers
{
	vec2 src_size;     /* supersampled framebuffer, texels */
	vec2 native_size;  /* native display rect */
} reg;

void main()
{
	highp vec2  scale = reg.src_size / reg.native_size;
	highp ivec2 n     = ivec2(max(floor(scale + 0.5), vec2(1.0)));
	highp vec2  base   = floor(gl_FragCoord.xy) * scale;

	highp vec3  acc = vec3(0.0);
	highp float cnt = 0.0;
	int x, y;

	for (y = 0; y < n.y; y++)
	{
		for (x = 0; x < n.x; x++)
		{
			highp vec2 t = clamp(base + vec2(float(x), float(y)) + 0.5,
			                     vec2(0.0), reg.src_size - 0.5);
			acc += textureLod(uSource, t / reg.src_size, 0.0).rgb;
			cnt += 1.0;
		}
	}

	FragColor = vec4(acc / max(cnt, 1.0), 1.0);
}
