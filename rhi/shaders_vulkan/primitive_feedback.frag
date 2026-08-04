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
#endif

/* Check-mask (dst alpha) test. This program originally existed only for
 * mask-tested draws, so the test was unconditional. The 16F HDR target also
 * routes non-masked subtractive prims through here - fixed-function
 * REVERSE_SUBTRACT cannot floor at zero on a float attachment - and those
 * must not check-mask. Set from SpecConstIndex_MaskTest. */
layout(constant_id = 7) const int MASK_TEST = 1;

void main()
{
#ifdef TEXTURED
	vec4 NNColor = sample_vram_atlas(clamp_coord(vUV));
	if (all(equal(NNColor, vec4(0.0))))
		discard;

	vec4 color = NNColor;

	vec3 shaded_hot = color.rgb * vColor.rgb * (255.0 / 128.0);
	vec3 shaded     = clamp(shaded_hot, 0.0, 1.0);
	vec3 add_src    = (HDR_HOT_SOURCE != 0) ? shaded_hot : shaded;
	float blend_amt = NNColor.a;
#else
	vec3 shaded = vColor.rgb;
#define add_src shaded
	const float blend_amt = 1.0;
#endif

#ifdef MSAA
	// Need to be render per-sample here.
	vec4 fbcolor = subpassLoad(uFeedbackFramebuffer, gl_SampleID);
#else
	vec4 fbcolor = subpassLoad(uFeedbackFramebuffer);
#endif

	if (MASK_TEST != 0 && fbcolor.a > 0.5)
		discard;

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
		blended = mix(shaded, max(fbcolor.rgb - add_src, vec3(0.0)), blend_amt);
	if (BLEND_MODE == BLEND_ADD_QUARTER)
		blended = mix(shaded, clamp(shaded, 0.0, 1.0) * 0.25 + fbcolor.rgb, blend_amt);

#ifdef TEXTURED
	FragColor = vec4(blended, NNColor.a + vColor.a);
#else
	FragColor = vec4(blended, vColor.a);
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
