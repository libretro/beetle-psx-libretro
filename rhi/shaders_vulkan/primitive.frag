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
	bool raw_texture = false;
#ifdef TEXTURED
	vec4 NNColor;

	bool fastpath = (vParam.z & 0x100) != 0;
	bool hd_enabled = !fastpath && (vParam.z & 0x200) == 0;

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

#ifdef CEILING
	/* HDR ceiling pass: same coverage as the subtractive draw that follows
	 * (same discards, same depth test); MIN-blended so the destination is
	 * clamped to white exactly where the hardware's saturated value would be
	 * subtracted from. */
	FragColor = vec4(1.0);
	return;
#endif

	/* 0x2000 carries the GP0 raw-texture bit. Do not infer this from a
	 * neutral vertex colour: 0x808080 is also valid modulated input. */
	raw_texture = (uint(vParam.z) & 0x2000u) != 0u;
	bool fixed_feedback = (uint(vParam.z) & 0x800u) != 0u;
	if (fixed_feedback)
	{
		/* The sampled texture or palette holds GPU-rendered VRAM data.
		 * Reproduce the PlayStation GPU's fixed-point modulation so
		 * repeated framebuffer feedback decays at hardware rate: the
		 * float path below, plus the -0.49/255 store bias, truncates at
		 * 8-bit granularity and fades 2-4x slower than the console.
		 * ModTexel truncates the 5-bit texel times the 8-bit shading
		 * colour; DitherLUT adds the 4x4 offset, divides by eight with
		 * truncation, and clamps to a 5-bit channel. The result is
		 * emitted without the store bias so rgba8/10-bit storage
		 * round-trips it exactly. */
		const int dither_pattern[16] = int[](
			-4,  0, -3,  1,
			 2, -2,  3, -1,
			-3,  1, -4,  0,
			 3, -1,  2, -2);
		vec3 fshade = clamp((PGXP_FOG != 0) ? pgxp_fog_mix(vColor.rgb, vFog) : vColor.rgb, 0.0, 1.0);
		vec3 texel5 = floor(color.rgb * 31.0 + vec3(0.5));
		vec3 shade8 = floor(fshade * 255.0 + vec3(0.001));
		vec3 modulated = floor(texel5 * shade8 / 16.0);
		ivec2 dc = primitive_dither_coord();
		float md = primitive_dither_enabled()
			? float(dither_pattern[dc.y * 4 + dc.x]) : 0.0;
		vec3 q5 = clamp(floor((modulated + md) / 8.0), vec3(0.0), vec3(31.0));
		FragColor = vec4(primitive_native_color() ? q5 * (8.0 / 255.0) : q5 / 31.0,
			NNColor.a + vColor.a);
		return;
	}
	vec3 shaded_hot = raw_texture ? color.rgb :
		color.rgb * ((PGXP_FOG != 0) ? pgxp_fog_mix(vColor.rgb, vFog) : vColor.rgb) * (255.0 / 128.0);
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
#ifdef CEILING
	/* HDR ceiling pass: same coverage as the subtractive draw that follows
	 * (same discards, same depth test); MIN-blended so the destination is
	 * clamped to white exactly where the hardware's saturated value would be
	 * subtracted from. */
	FragColor = vec4(1.0);
	return;
#endif
	FragColor = vec4((PGXP_FOG != 0) ? pgxp_fog_mix(vColor.rgb, vFog) : vColor.rgb, vColor.a);
#endif

	if (primitive_native_color())
	{
		/* Every PS1 write stores RGB5. Modulated sources may use GP0 DTD;
		 * raw sources skip modulation and DTD, but a scaled framebuffer or
		 * replacement texture can still supply a noncanonical raw colour. */
		if (!raw_texture)
			FragColor.rgb = quantize_native_rgb5(FragColor.rgb,
				primitive_dither_enabled());
#ifdef TEXTURED
		else
			FragColor.rgb = quantize_native_rgb5(FragColor.rgb, false);
#endif
	}
	else if (!raw_texture)
	{
		/* Preserve the higher-colour truncation bias, but do not let the
		 * 16F target retain a negative value for transparent black. */
		FragColor.rgb = max(FragColor.rgb - 0.49 / 255.0, vec3(0.0));
	}
}
