#version 450
precision highp float;
precision highp int;

#ifdef TEXTURED
#define FILTERS
#endif

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

const int OPAQUE = 0;
const int SEMI_TRANS = 1;
const int SEMI_TRANS_OPAQUE = 2;
layout(constant_id = 0) const int TRANSPARENCY_MODE = OPAQUE;

const int FILTER_NEAREST = 0;
const int FILTER_XBR = 1;
const int FILTER_SABR = 2;
const int FILTER_BILINEAR = 3;
const int FILTER_3POINT = 4;
const int FILTER_JINC2 = 5;
layout(constant_id = 1) const int FILTER_TYPE = FILTER_NEAREST;

/* Mirrors primitive_feedback.frag. The fixed-function additive path
 * (ONE/ONE ADD) carries fragment output through unclamped on the 16F HDR
 * target, so the overbright option is honoured by conditionally skipping the
 * source clamp below. BLEND_MODE is the fixed-function blend this draw uses;
 * only plain additive goes hot - AVG and ADD_QUARTER clamp the source like
 * hardware, and subtractive is routed through the feedback program on 16F.
 * SDR is unaffected either way: the rhi forces HDR_HOT_SOURCE to 0 there. */
const int BLEND_ADD = 0;
layout(constant_id = 2) const int BLEND_MODE = BLEND_ADD;
layout(constant_id = 6) const int HDR_HOT_SOURCE = 0;
/* PGXP precise colour: the vertex colour itself may exceed 1.0 (the GTE's
 * pre-saturation value), so the source clamp stands aside on every draw,
 * not just plain additive. The rhi forces this to 0 off the fp16 target,
 * which keeps the hot path SDR-safe the same way HDR_HOT_SOURCE is. */
layout(constant_id = 8) const int PRECISE_COLOR = 0;
#endif

/* PGXP linear-light depth cueing; rides the precise-colour vertex path.
 * Common scope: fog applies to untextured gouraud (the classic depth-cued
 * geometry) as much as to textured surfaces. The rhi forces this to 0 off
 * the fp16 target and when either option is off, so the pow() cost exists
 * only where the feature is live. */
layout(constant_id = 9) const int PGXP_FOG = 0;
layout(location = 6) in mediump vec4 vFog;

#include "pgxp_fog.h"

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

	// Even for opaque draw calls, this pixel is transparent.
	// Sample in NN space since we need to do an exact test against 0.0.
	// Doing it in a filtered domain is a bit awkward.
	// In this pass, only accept opaque pixels.
	if (TRANSPARENCY_MODE == SEMI_TRANS_OPAQUE)
		if (all(equal(NNColor, vec4(0.0))) || NNColor.a > 0.5)
			discard;

	// To avoid opaque pixels from bleeding into the semi-transparent parts,
	// sample nearest-neighbor only in semi-transparent parts of the image.
	vec4 color = NNColor;

	// texture filtering
	if (FILTER_TYPE == FILTER_XBR)
		color = sample_vram_xbr(opacity);
	if (FILTER_TYPE == FILTER_BILINEAR)
		color = sample_vram_bilinear(opacity);
	if (FILTER_TYPE == FILTER_SABR)
		color = sample_vram_sabr(opacity);
	if (FILTER_TYPE == FILTER_JINC2)
		color = sample_vram_jinc2(opacity);
	if (FILTER_TYPE == FILTER_3POINT)
		color = sample_vram_3point(opacity);

	if (TRANSPARENCY_MODE == OPAQUE || TRANSPARENCY_MODE == SEMI_TRANS)
		if (color.a == 0.0 && all(equal(vec4(NNColor), vec4(0.0))))
			discard;

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

	vec3 shaded_hot = color.rgb * ((PGXP_FOG != 0) ? pgxp_fog_mix(vColor.rgb, vFog) : vColor.rgb) * (255.0 / 128.0);
	vec3 shaded = clamp(shaded_hot, 0.0, 1.0);
	/* The semi-trans-opaque pass and every other blend mode stay clamped;
	 * over-white there comes only from stacking, matching the option text. */
	if (HDR_HOT_SOURCE != 0 && TRANSPARENCY_MODE == SEMI_TRANS && BLEND_MODE == BLEND_ADD)
		shaded = shaded_hot;
	if (PRECISE_COLOR != 0)
		/* Hot above white, still floored at zero: a negative source is
		 * anti-light and unrepresentable on hardware. */
		shaded = max(shaded_hot, vec3(0.0));
	FragColor = vec4(shaded, NNColor.a + vColor.a);
#else
	FragColor = vec4((PGXP_FOG != 0) ? pgxp_fog_mix(vColor.rgb, vFog) : vColor.rgb, vColor.a);
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
