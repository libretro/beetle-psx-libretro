#version 450
/* Zero-floor pass for fixed-function subtractive blending on the 16F HDR
 * target ("HDR True Multi-Pass Blending" off). Drawn as a full-framebuffer
 * quad with blend equation MAX after each batch of non-masked subtractive
 * primitives: max(dst, 0) restores the hardware floor the UNORM write stage
 * used to provide, and is a no-op on every pixel the batch left
 * non-negative. Sequential per-primitive clamping and one clamp at the end
 * of a contiguous subtractive run are algebraically identical (once the
 * running value would clamp, every further subtraction keeps both forms at
 * zero), so this is exact, not an approximation. Alpha carries the mask bit
 * and is >= 0 by construction, so MAX against zero preserves it. */
precision highp float;

layout(location = 0) out vec4 FragColor;

void main()
{
	FragColor = vec4(0.0);
}
