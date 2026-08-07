/* PGXP linear-light depth cueing (see primitive.frag's declaration of the
 * option constants). Shared by the fixed-function and feedback fragment
 * programs: the feedback path is where 16F subtractive blending routes, so
 * leaving it out renders the PRE-cue vertex colour with no fog at all --
 * which is precisely how it shipped, and how Silent Hill made it visible.
 *
 * highp throughout: the mix runs through pow(x, 2.2), and dark fog colours
 * sit exactly where fp16 dies -- pow(2/255, 2.2) ~= 2.3e-5 is below the
 * fp16 normal range (6.1e-5) and flushes to zero on hardware that does not
 * keep subnormals, crushing dark fog to black. */
highp vec3 pgxp_fog_mix(highp vec3 shading, highp vec4 fog_cue)
{
	if (fog_cue.a <= 0.0)
		return shading;
	highp vec3 pre = pow(max(shading, vec3(0.0)), vec3(2.2));
	highp vec3 far = pow(max(fog_cue.rgb, vec3(0.0)), vec3(2.2));
	return pow(mix(pre, far, clamp(fog_cue.a, 0.0, 1.0)), vec3(1.0 / 2.2));
}
