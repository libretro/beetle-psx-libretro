#version 450
precision highp float;
precision highp int;

#include "common.h"
#include "primitive.h"

layout(set = 0, binding = 4) uniform sampler2D uHighResTexture;
layout(push_constant, std430) uniform Push
{
	ivec4 hd_texture_vram_rect; // The area of vram this hd texture covers
	ivec4 hd_texture_texel_rect; // The area of this hd texture's own texels that may currently be used
} push;
#ifdef TEXTURED
#include "hdtextures.h"
#endif

void main()
{
	float opacity = 1.0;
#ifdef TEXTURED
	vec4 NNColor;

	bool fastpath = (vParam.z & 0x100) != 0;
	bool hd_enabled = !fastpath && (vParam.z & 0x200) == 0;
	bool cache_hit = (vParam.z & 0x400) != 0;

	vec4 hdColor;
	if (fastpath) {
		NNColor = sample_hd_fast(vUV);
	} else if (hd_enabled && sample_hd_texture_nearest_hack(vUV, hdColor)) {
		NNColor = hdColor;
	} else {
		NNColor = sample_vram_atlas(clamp_coord(vUV));
	}

#if defined(OPAQUE) && defined(SEMI_TRANS) && defined(SEMI_TRANS_OPAQUE) 
#error "Invalid defines."
#endif

	// Even for opaque draw calls, this pixel is transparent.
	// Sample in NN space since we need to do an exact test against 0.0.
	// Doing it in a filtered domain is a bit awkward.
#ifdef SEMI_TRANS_OPAQUE
	// In this pass, only accept opaque pixels.
	if (all(equal(NNColor, vec4(0.0))) || NNColor.a > 0.5)
		discard;
#endif

#ifdef SEMI_TRANS
	// To avoid opaque pixels from bleeding into the semi-transparent parts,
	// sample nearest-neighbor only in semi-transparent parts of the image.
	vec4 color = NNColor;
#else

	vec4 color = NNColor;
#endif

	// texture filtering
#if defined(FILTER_XBR)
	color = sample_vram_xbr(opacity);
#elif defined(FILTER_BILINEAR)
	color = sample_vram_bilinear(opacity);
#elif defined(FILTER_SABR)
	color = sample_vram_sabr(opacity);
#elif defined(FILTER_JINC2)
	color = sample_vram_jinc2(opacity);
#elif defined(FILTER_3POINT)
	color = sample_vram_3point(opacity);
#endif

#if defined(OPAQUE) || defined(SEMI_TRANS)
	if (color.a == 0.0 && all(equal(vec4(NNColor), vec4(0.0))))
		discard;
#endif

	// hd texture filtering
	if (hd_enabled) {
		bool valid = true;
		vec4 hd_color = sample_hd_texture_trilinear(vUV, valid);
		if (valid) {
			color = hd_color;
			opacity = hd_color.a;
		}
	}

	if (opacity < 0.5)
		discard;

	vec3 shaded = color.rgb * vColor.rgb * (255.0 / 128.0);
	FragColor = vec4(shaded, NNColor.a + vColor.a);
#else
	FragColor = vColor;
#endif

	// Get round down behavior instead of round-to-nearest.
	// This is required for various "fade" out effects.
	// However, don't accidentially round down if we are already rounded to avoid
	// unintended feedback effects.
	FragColor.rgb -= 0.49 / 255.0;

#if 0
#if defined(TEXTURED)
	if ((vParam.z & 0x100) != 0)
		FragColor.rgb += textureLod(uDitherLUT, gl_FragCoord.xy * 0.25, 0.0).xxx - 4.0 / 255.0;
#endif
	FragColor.rgb = quantize_bgr555(FragColor.rgb);
#endif
}
