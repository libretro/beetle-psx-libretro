/* PGXP depth cueing (see primitive.frag's declaration of the option
 * constants). Shared by the fixed-function and feedback fragment
 * programs: the feedback path is where 16F subtractive blending routes, so
 * leaving it out renders the PRE-cue vertex colour with no fog at all --
 * which is precisely how it shipped, and how Silent Hill made it visible.
 *
 * The mix is the console's own gamma-domain lerp, in float. It must NOT be
 * done in linearised light: the GTE's cue is authored against the byte-domain
 * ramp, and games fade to a BLACK far colour (Silent Hill: FC = 0, t -> 1),
 * where a linear-space mix turns the console's (1 - t) scale into
 * (1 - t)^(1/2.2) -- two to three-and-a-half times too bright across the
 * whole fade, which reads as the fog simply missing, and any vertex that
 * falls back to the architectural baked bytes sits beside it at console
 * brightness, which reads as a checkerboard. Keeping the byte-domain shape
 * in float keeps the authored look while still removing the 5-bit
 * requantisation banding from the fade, which is the whole point of
 * recovering the pre-cue colour.
 *
 * highp: dark fade tails sit where fp16 flushes to zero on hardware that
 * does not keep subnormals, crushing the end of the ramp to black. */
highp vec3 pgxp_fog_mix(highp vec3 shading, highp vec4 fog_cue)
{
	if (fog_cue.a <= 0.0)
		return shading;
	return mix(max(shading, vec3(0.0)), max(fog_cue.rgb, vec3(0.0)),
			clamp(fog_cue.a, 0.0, 1.0));
}
