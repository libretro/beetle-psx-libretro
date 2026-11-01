#version 450
precision highp float;
precision highp int;

/* HDR bloom stages, ported from boom3's hdr_bright / hdr_blur / hdr_down /
 * hdr_down_karis / hdr_up. One source, one stage per define:
 *
 *   BRIGHT  the bright pass: scene-linear light (1.0 = paper white) into the
 *           first band at quarter size, soft-thresholded and firefly
 *           compressed so a single hot texel cannot flicker the halo.
 *           p0 = threshold, knee, firefly knee; p1.xy = source texel.
 *   BLUR    one direction of the two-band Gaussian; p1.xy = texel * dir.
 *   DOWN    the convolution pyramid's 13-tap downsample (KARIS: the first
 *           level, weighted 1/(1+max) so hot texels cannot dominate);
 *           p1.xy = source texel.
 *   UP      the pyramid's 3x3 tent upsample, added into the level above;
 *           p0.x = radius, p1.xy = source texel.
 *
 * Runs through analog.vert, whose vUV covers the target. */

layout(location = 0) in highp vec2 vUV;
layout(location = 0) out highp vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D uSource;

layout(push_constant, std430) uniform Registers
{
	vec4 p0;
	vec4 p1;
} reg;

highp vec3 tap(highp vec2 o)
{
	return textureLod(uSource, vUV + reg.p1.xy * o, 0.0).rgb;
}

#if defined(KARIS)
highp float kw(highp vec3 c)
{
	return 1.0 / (1.0 + max(c.r, max(c.g, c.b)));
}
#endif

void main()
{
#if defined(BRIGHT)
	highp vec3 lin = 0.25 * (tap(vec2(-1.0, -1.0)) + tap(vec2(1.0, -1.0)) +
	                         tap(vec2(-1.0, 1.0)) + tap(vec2(1.0, 1.0)));
	lin = max(lin, vec3(0.0));
	highp float lum = dot(lin, vec3(0.2126, 0.7152, 0.0722));
	highp float sk = clamp(lum - reg.p0.x + reg.p0.y, 0.0, 2.0 * reg.p0.y);
	sk = sk * sk / (4.0 * reg.p0.y);
	highp vec3 b = lin * (max(sk, lum - reg.p0.x) / max(lum, 1e-5)) / (1.0 - reg.p0.x);
	highp float l = dot(b, vec3(0.2126, 0.7152, 0.0722));
	FragColor = vec4(b / (1.0 + l * reg.p0.z), 1.0);
#elif defined(BLUR)
	highp vec3 c = tap(vec2(0.0)) * 0.24458;
	c += tap(vec2( 1.36268)) * 0.31802;
	c += tap(vec2(-1.36268)) * 0.31802;
	c += tap(vec2( 3.21159)) * 0.05717;
	c += tap(vec2(-3.21159)) * 0.05717;
	c += tap(vec2( 5.11234)) * 0.00251;
	c += tap(vec2(-5.11234)) * 0.00251;
	FragColor = vec4(c, 1.0);
#elif defined(DOWN)
	highp vec3 a = tap(vec2(-2.0,  2.0));
	highp vec3 b = tap(vec2( 0.0,  2.0));
	highp vec3 c = tap(vec2( 2.0,  2.0));
	highp vec3 d = tap(vec2(-2.0,  0.0));
	highp vec3 e = tap(vec2( 0.0,  0.0));
	highp vec3 f = tap(vec2( 2.0,  0.0));
	highp vec3 g = tap(vec2(-2.0, -2.0));
	highp vec3 h = tap(vec2( 0.0, -2.0));
	highp vec3 i = tap(vec2( 2.0, -2.0));
	highp vec3 j = tap(vec2(-1.0,  1.0));
	highp vec3 k = tap(vec2( 1.0,  1.0));
	highp vec3 l = tap(vec2(-1.0, -1.0));
	highp vec3 m = tap(vec2( 1.0, -1.0));
#if defined(KARIS)
	highp vec3 acc = vec3(0.0);
	highp float ws = 0.0;
	highp float w;
	w = kw(a) * 0.03125; acc += a * w; ws += w;
	w = kw(b) * 0.06250; acc += b * w; ws += w;
	w = kw(c) * 0.03125; acc += c * w; ws += w;
	w = kw(d) * 0.06250; acc += d * w; ws += w;
	w = kw(e) * 0.12500; acc += e * w; ws += w;
	w = kw(f) * 0.06250; acc += f * w; ws += w;
	w = kw(g) * 0.03125; acc += g * w; ws += w;
	w = kw(h) * 0.06250; acc += h * w; ws += w;
	w = kw(i) * 0.03125; acc += i * w; ws += w;
	w = kw(j) * 0.12500; acc += j * w; ws += w;
	w = kw(k) * 0.12500; acc += k * w; ws += w;
	w = kw(l) * 0.12500; acc += l * w; ws += w;
	w = kw(m) * 0.12500; acc += m * w; ws += w;
	FragColor = vec4(acc / ws, 1.0);
#else
	highp vec3 o = e * 0.125;
	o += (a + c + g + i) * 0.03125;
	o += (b + d + f + h) * 0.0625;
	o += (j + k + l + m) * 0.125;
	FragColor = vec4(o, 1.0);
#endif
#elif defined(UP)
	highp float r = reg.p0.x;
	highp vec3 s = tap(vec2(-r, -r));
	s += tap(vec2(0.0, -r)) * 2.0;
	s += tap(vec2( r, -r));
	s += tap(vec2(-r, 0.0)) * 2.0;
	s += tap(vec2(0.0)) * 4.0;
	s += tap(vec2( r, 0.0)) * 2.0;
	s += tap(vec2(-r,  r));
	s += tap(vec2(0.0,  r)) * 2.0;
	s += tap(vec2( r,  r));
	FragColor = vec4(s * 0.0625, 1.0);
#endif
}
