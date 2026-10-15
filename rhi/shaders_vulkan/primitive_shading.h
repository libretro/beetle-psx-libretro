#ifndef PRIMITIVE_SHADING_H
#define PRIMITIVE_SHADING_H

/* Keep source sampling, modulation and DTD identical on fixed-function and
 * destination-read blend paths. The caller only chooses the blend/store. */
#ifdef TEXTURED
#define FILTERS
#endif

#include "common.h"
#include "primitive.h"

#ifdef TEXTURED
layout(set = 0, binding = 4) uniform sampler2D uHighResTexture;
/* Keep replacement support enabled by default; the host specializes it
 * away for batches without a bound replacement handle. */
layout(constant_id = 12) const int HIGH_RES_TEXTURE = 1;
layout(push_constant, std430) uniform Push
{
	ivec4 hd_texture_vram_rect; // The area of vram this hd texture covers
	ivec4 hd_texture_texel_rect; // The area of this hd texture's own texels that may currently be used
} push;
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

#endif

/* Sampling and modulation are shared; HDR source clamps retain each blend path's contract.
 * This is shared with primitive_feedback.frag. The fixed-function additive path
 * (ONE/ONE ADD) carries fragment output through unclamped on the 16F HDR
 * target, so the overbright option is honoured by conditionally skipping the
 * source clamp below. BLEND_MODE is the fixed-function blend this draw uses;
 * only plain additive goes hot - AVG and ADD_QUARTER clamp the source like
 * hardware, and subtractive is routed through the feedback program on 16F.
 * SDR is unaffected either way: the rhi forces HDR_HOT_SOURCE to 0 there. */
const int BLEND_ADD = 0;
layout(constant_id = 2) const int BLEND_MODE = BLEND_ADD;
#ifdef TEXTURED
layout(constant_id = 6) const int HDR_HOT_SOURCE = 0;
/* Fixed-function PGXP precise colour may exceed 1.0 (the GTE's
 * pre-saturation value), so that path's source clamp stands aside on every draw,
 * not just plain additive. The rhi forces this to 0 off the fp16 target,
 * which keeps the hot path SDR-safe the same way HDR_HOT_SOURCE is. */
layout(constant_id = 8) const int PRECISE_COLOR = 0;
#endif

/* gpu_polygon.c supplies the finished per-vertex depth-cued color. */

void primitive_source(out vec4 source, out bool enhanced_texel,
		out float blend_amt)
{
	primitive_line_coverage();
	float opacity = 1.0;
	bool raw_texture = false;
	/* Replacement and filtered texels can carry more than RGB5 precision.
	 * Expanded 16 bpp preserves their extra shades; Native intentionally
	 * reduces them along with ordinary PlayStation colour words. */
	enhanced_texel = false;
	bool hd_texel = false;
	blend_amt = 1.0;
	vec3 shade_color = vColor.rgb;
#ifdef TEXTURED
	vec4 NNColor;

	bool fastpath = HIGH_RES_TEXTURE != 0 && (vParam.z & 0x100) != 0;
	bool hd_enabled = HIGH_RES_TEXTURE != 0 && !fastpath && (vParam.z & 0x200) == 0;

	vec4 hdColor;
	if (fastpath) {
		NNColor = sample_hd_fast(vUV);
		enhanced_texel = true;
		hd_texel = true;
	} else if (hd_enabled && sample_hd_texture_nearest_hack(vUV, hdColor)) {
		NNColor = hdColor;
		enhanced_texel = true;
		hd_texel = true;
	} else {
		NNColor = sample_vram_atlas(FILTER_TYPE == FILTER_NEAREST ?
			native_texture_coord() : clamp_coord(vUV));
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
	if (FILTER_TYPE != FILTER_NEAREST)
	{
		enhanced_texel = true;
		hd_texel = false;
	}

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
			enhanced_texel = true;
			hd_texel = true;
		}
	}

	if (opacity < 0.5)
		discard;
	blend_amt = NNColor.a;

#ifdef CEILING
	/* HDR ceiling pass: same coverage as the subtractive draw that follows
	 * (same discards, same depth test); MIN-blended so the destination is
	 * clamped to white exactly where the hardware's saturated value would be
	 * subtracted from. */
	source = vec4(1.0);
	return;
#endif

	/* 0x2000 carries the GP0 raw-texture bit. Do not infer this from a
	 * neutral vertex colour: 0x808080 is also valid modulated input. */
	raw_texture = (uint(vParam.z) & 0x2000u) != 0u;
	if (!enhanced_texel)
		shade_color = primitive_color();
	bool framebuffer_feedback =
		(uint(vParam.z) & PARAM_FRAMEBUFFER_FEEDBACK) != 0u;
	if (framebuffer_feedback)
	{
		vec3 texel5 = framebuffer_feedback_texel5(color.rgb);
		if (PRECISE_COLOR != 0 && (uint(vParam.z) & 3u) != 0u)
		{
			/* Neutral indexed feedback preserves its RGB5 source while the
			 * precise-colour target remains wide. Shaded indexed textures are
			 * intentionally not marked as framebuffer feedback. */
			color.rgb = texel5 / 31.0;
		}
		else
		{
			/* Direct-colour feedback must quantize each GP0 modulation step,
			 * even on an FP16 target, so repeated fades decay at hardware rate.
			 * Standard-colour feedback retains the same established path. */
			vec3 fshade = clamp(shade_color, 0.0, 1.0);
			vec3 shade8 = psx_color8(fshade);
			vec3 modulated = floor(texel5 * shade8 / 16.0);
			float md = primitive_dither_offset();
			vec3 q5 = clamp(floor((modulated + md) / 8.0),
				vec3(0.0), vec3(31.0));
			/* Low-color modes retain q*8 for their display expansion.
			 * 32 bpp and HDR use full-range colors before blending, so texture
			 * white agrees with an untextured white background during fades. */
			source = vec4(primitive_native_color() ?
				q5 * (8.0 / 255.0) :
				(PRIMITIVE_COLOR_MODE != 3 ?
					psx_expand_color5(q5) : q5 / 31.0),
				NNColor.a + vColor.a);
			return;
		}
	}
	vec3 shaded_hot = raw_texture ? color.rgb :
		color.rgb * shade_color * (255.0 / 128.0);
#ifdef FEEDBACK
	/* HDR feedback chooses its ordinary and boosted sources while blending.
	 * Keep modulation shared without imposing the fixed-function clamp. */
	vec3 shaded = shaded_hot;
#else
	/* SDR dithering precedes saturation. Preserve the existing fixed-function
	 * HDR precise-colour policy. */
	vec3 shaded = FRAMEBUFFER_FLOAT16 == 0 ? shaded_hot : clamp(shaded_hot, 0.0, 1.0);
	if (HDR_HOT_SOURCE != 0 && TRANSPARENCY_MODE == SEMI_TRANS &&
		BLEND_MODE == BLEND_ADD)
		shaded = shaded_hot;
	if (PRECISE_COLOR != 0)
		/* Hot above white, still floored at zero: a negative source is
		 * anti-light and unrepresentable on hardware. */
		shaded = max(shaded_hot, vec3(0.0));
#endif
	source = vec4(shaded, NNColor.a + vColor.a);
#else
#ifdef CEILING
	/* HDR ceiling pass: same coverage as the subtractive draw that follows
	 * (same discards, same depth test); MIN-blended so the destination is
	 * clamped to white exactly where the hardware's saturated value would be
	 * subtracted from. */
	source = vec4(1.0);
	return;
#endif
	shade_color = primitive_color();
	source = vec4(shade_color, vColor.a);
#endif

	/* HD images arrive as ordinary normalized bytes. The low-color Vulkan
	 * target encodes RGB5 as q*8, so map enhanced 16-bpp white into that
	 * same range before mixing it with ordinary draws. Filtered VRAM already
	 * uses this encoding. Native intentionally reduces the original HD byte. */
	if (primitive_native_color() && !primitive_strict_color() && hd_texel)
		source.rgb *= 248.0 / 255.0;

	if (primitive_native_color() && (!enhanced_texel || primitive_strict_color()))
	{
		/* Every PS1 write stores RGB5. Modulated sources may use GP0 DTD;
		 * raw sources skip modulation and DTD, but a scaled framebuffer can
		 * still supply a noncanonical raw colour. Replacement textures and
		 * filtered texels are kept wide only in expanded 16 bpp. Native
		 * deliberately reduces enhanced sources as well. */
		if (!raw_texture)
			source.rgb = quantize_native_rgb5(source.rgb,
				primitive_dither_enabled());
#ifdef TEXTURED
		else
			source.rgb = quantize_native_rgb5(source.rgb, false);
#endif
	}
	else if (!raw_texture)
	{
		/* Higher-color dithering is a source operation, not a late RGB5
		 * display conversion. HDR retains its existing wide path. */
		if (FRAMEBUFFER_FLOAT16 == 0)
			source.rgb += vec3(primitive_dither_offset() / 255.0);
#ifdef FEEDBACK
		/* Wide feedback applies its existing export bias to the blended
		 * result, once. The fixed-function source retains its old bias. */
		if (FRAMEBUFFER_FLOAT16 == 0)
#endif
			source.rgb = truncate_color8(source.rgb);
	}
	/* Stabilize the byte source before fixed-function or explicit blending.
	 * Otherwise filtered raw samples can be rounded differently by those
	 * two routes, even when the replacement itself contains exact bytes. */
	if (FRAMEBUFFER_FLOAT16 == 0 && raw_texture && enhanced_texel &&
		!primitive_strict_color())
		source.rgb = nearest_color8(source.rgb);
	/* Clamp enhanced 16-bpp stores after their source dither as well. */
	if (primitive_native_color() && enhanced_texel && !primitive_strict_color())
		source.rgb = min(source.rgb, vec3(248.0 / 255.0));
	else if (FRAMEBUFFER_FLOAT16 == 0)
		source.rgb = min(source.rgb, vec3(1.0));
}

#endif
