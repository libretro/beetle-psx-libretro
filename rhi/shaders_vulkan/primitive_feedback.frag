#version 450
precision highp float;
precision highp int;

#define FEEDBACK
#include "primitive_shading.h"

#ifdef MSAA
layout(set = 0, binding = 3, input_attachment_index = 0) uniform highp subpassInputMS uFeedbackFramebuffer;
#else
layout(set = 0, binding = 3, input_attachment_index = 0) uniform highp subpassInput uFeedbackFramebuffer;
#endif

/* Mask-tested draws and ordinary low-color blends share the program, but
 * only the former reject a destination whose mask bit is set. */
layout(constant_id = 7) const int MASK_TEST = 1;

void main()
{
	bool enhanced_texel;
	float blend_amt;
	vec4 source;
	primitive_source(source, enhanced_texel, blend_amt);

#ifdef MSAA
	vec4 destination = subpassLoad(uFeedbackFramebuffer, gl_SampleID);
#else
	vec4 destination = subpassLoad(uFeedbackFramebuffer);
#endif
	if (MASK_TEST != 0 && destination.a > 0.5)
		discard;

	vec3 shaded = source.rgb;
	vec3 blend_source = shaded;
#ifdef TEXTURED
	/* HDR feedback contract: opaque texels use the
	 * saturated source, while additive/subtractive blends may use a hot
	 * source. Keep both for fractional replacement-texture alpha too. */
	if (FRAMEBUFFER_FLOAT16 != 0)
	{
		shaded = clamp(shaded, 0.0, 1.0);
		blend_source = HDR_HOT_SOURCE != 0 &&
			(BLEND_MODE == 0 || BLEND_MODE == 2) ?
			max(blend_source, vec3(0.0)) : shaded;
	}
#endif
	vec3 background = destination.rgb;
	bool rgb5 = primitive_native_color() &&
		(!enhanced_texel || primitive_strict_color());
	if (rgb5)
	{
		/* Both operands are PS1 channel codes. Blend those integers directly:
		 * half and quarter operations truncate before the RGB5 store. */
		uvec3 foreground5 = uvec3(psx_color5(shaded, 0.0));
		uvec3 background5 = uvec3(psx_color5(background, 0.0));
		uvec3 result5 = foreground5;
		if (blend_amt > 0.5)
		{
			if (BLEND_MODE == 0) result5 = foreground5 + background5;
			if (BLEND_MODE == 1) result5 = (foreground5 + background5) >> 1u;
			if (BLEND_MODE == 2) result5 = background5 - min(background5, foreground5);
			if (BLEND_MODE == 3) result5 = background5 + (foreground5 >> 2u);
		}
		FragColor = vec4(vec3(min(result5, uvec3(31u))) * (8.0 / 255.0), source.a);
		return;
	}
	vec3 blended;
	if (BLEND_MODE == 0)
		blended = blend_source + background;
	if (BLEND_MODE == 1)
		blended = 0.5 * (clamp(shaded, 0.0, 1.0) + background);
	if (BLEND_MODE == 2)
		blended = max(min(background, vec3(1.0)) - blend_source, vec3(0.0));
	if (BLEND_MODE == 3)
		blended = clamp(shaded, 0.0, 1.0) * 0.25 + background;
	FragColor = vec4(mix(shaded, blended, blend_amt), source.a);
	if (primitive_native_color())
		FragColor.rgb = min(FragColor.rgb, vec3(248.0 / 255.0));
	if (FRAMEBUFFER_FLOAT16 == 0)
	{
		/* Higher-color blends use nearest-byte UNORM storage, unlike RGB5
		 * operations which truncate. Fixed-function half-code ties may
		 * choose either neighbour; neither is an integer-color error. */
		FragColor.rgb = nearest_color8(FragColor.rgb);
	}
	else
		FragColor.rgb = truncate_color8(FragColor.rgb);
}
