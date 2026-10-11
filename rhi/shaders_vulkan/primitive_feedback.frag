#version 450
precision highp float;
precision highp int;

#include "common.h"
#include "primitive.h"

#ifdef MSAA
layout(set = 0, binding = 3, input_attachment_index = 0) uniform highp subpassInputMS uFeedbackFramebuffer;
#else
layout(set = 0, binding = 3, input_attachment_index = 0) uniform highp subpassInput uFeedbackFramebuffer;
#endif

const int BLEND_ADD = 0;
const int BLEND_AVG = 1;
const int BLEND_SUB = 2;
const int BLEND_ADD_QUARTER = 3;
layout(constant_id = 2) const int BLEND_MODE = BLEND_ADD;
#ifdef TEXTURED
/* HDR only. 0: clamp the ~2x modulated source to reference white before the
 * additive/subtractive blend (over-white then comes only from stacking).
 * 1: leave the source hot for punchier single-layer additive/subtractive glow.
 * SDR is unaffected either way - the UNORM write clamps. Set from a core
 * option via SpecConstIndex_HotSource. */
layout(constant_id = 6) const int HDR_HOT_SOURCE = 0;
/* Mirrors primitive.frag. Framebuffer feedback needs its RGB5 texel
 * reconstructed under HDR, but still uses this wide shading path. */
layout(constant_id = 8) const int PRECISE_COLOR = 0;
#endif

/* Check-mask (dst alpha) test. This program originally existed only for
 * mask-tested draws, so the test was unconditional. The 16F HDR target also
 * routes non-masked subtractive prims through here - fixed-function
 * REVERSE_SUBTRACT cannot floor at zero on a float attachment - and those
 * must not check-mask. Set from SpecConstIndex_MaskTest. */
layout(constant_id = 7) const int MASK_TEST = 1;
/* Mirrors primitive.frag: the vertex colour carries the PRE-cue value when
 * the fog sidecar hits, so every program that shades with vColor must run
 * the same mix or it un-fogs the primitive. */
layout(constant_id = 9) const int PGXP_FOG = 0;
layout(location = 6) in highp vec4 vFog;
#include "pgxp_fog.h"


void main()
{
	bool raw_texture = false;
	vec3 shade_color = primitive_color();
	vec3 shaded;
	vec3 add_src;
	float blend_amt;
#ifdef TEXTURED
	vec4 NNColor = sample_vram_atlas(native_texture_coord());
	if (all(equal(NNColor, vec4(0.0))))
		discard;

	vec4 color = NNColor;

	/* Raw texture colour bypasses vertex modulation. The final store bias
	 * remains below because this program emits a derived blended result. */
	raw_texture = (uint(vParam.z) & 0x2000u) != 0u;
	if ((uint(vParam.z) & PARAM_FRAMEBUFFER_FEEDBACK) != 0u &&
		PRECISE_COLOR != 0)
		color.rgb = framebuffer_feedback_texel5(color.rgb) / 31.0;
	vec3 shaded_hot = raw_texture ? color.rgb :
		color.rgb * ((PGXP_FOG != 0) ? pgxp_fog_mix(shade_color, vFog) : shade_color) * (255.0 / 128.0);
	shaded     = clamp(shaded_hot, 0.0, 1.0);
	add_src    = (HDR_HOT_SOURCE != 0) ? max(shaded_hot, vec3(0.0)) : shaded;
	blend_amt = NNColor.a;
#else
	shaded = (PGXP_FOG != 0) ? pgxp_fog_mix(shade_color, vFog) : shade_color;
	add_src = shaded;
	blend_amt = 1.0;
#endif
	/* Raw texture sources can come from noncanonical filtered or rendered
	 * feedback. Reduce them to the PS1's RGB5 input before blending, without
	 * applying GP0 dithering to raw texels. Flat shaders retain their path. */
#ifdef TEXTURED
	if (primitive_native_color() && raw_texture)
	{
		shaded = quantize_native_rgb5(shaded, false);
		add_src = shaded;
	}
#endif
	if (primitive_native_color() && !raw_texture)
	{
		/* The PS1 reduces the source to RGB5 before semitransparency. */
		shaded = quantize_native_rgb5(shaded, primitive_dither_enabled());
		add_src = shaded;
	}

#ifdef MSAA
	// Need to be render per-sample here.
	vec4 fbcolor = subpassLoad(uFeedbackFramebuffer, gl_SampleID);
#else
	vec4 fbcolor = subpassLoad(uFeedbackFramebuffer);
#endif

	if (MASK_TEST != 0 && fbcolor.a > 0.5)
		discard;
	/* The derived framebuffer can hold either native RGB5 expansion or an
	 * older n/31 encoding. Blend the same destination word the PS1 reads. */
	if (primitive_native_color())
		fbcolor.rgb = quantize_native_rgb5(fbcolor.rgb, false);

	vec3 blended;
	if (BLEND_MODE == BLEND_ADD)
		blended = mix(shaded, add_src + fbcolor.rgb, blend_amt);
	if (BLEND_MODE == BLEND_AVG)
		blended = mix(shaded, 0.5 * (clamp(shaded, 0.0, 1.0) + fbcolor.rgb), blend_amt);
	if (BLEND_MODE == BLEND_SUB)
		/* Hardware floors B - F at zero per channel. The UNORM attachment
		 * used to provide that implicitly at the write stage; the 16F HDR
		 * target does not, and a negative residue both diverges from
		 * hardware and dims every later additive draw over the same pixels
		 * (dark halos around subtractive effects). No-op on UNORM. */
		/* ...and subtracts from a SATURATED destination: stacked additive layers
		 * may exceed white on the 16F target, hardware clamped each one at white. */
		blended = mix(shaded, max(min(fbcolor.rgb, vec3(1.0)) - add_src, vec3(0.0)), blend_amt);
	if (BLEND_MODE == BLEND_ADD_QUARTER)
		blended = mix(shaded, clamp(shaded, 0.0, 1.0) * 0.25 + fbcolor.rgb, blend_amt);

#ifdef TEXTURED
	FragColor = vec4(blended, NNColor.a + vColor.a);
#else
	FragColor = vec4(blended, vColor.a);
#endif

	if (primitive_native_color())
	{
		/* Average and quarter-add can leave half/quarter RGB5 steps. Store the
		 * same integer result as the hardware after programmable blending. */
		FragColor.rgb = quantize_native_rgb5(FragColor.rgb, false);
	}
	else
	{
		FragColor.rgb = truncate_color8(FragColor.rgb);
	}
}
