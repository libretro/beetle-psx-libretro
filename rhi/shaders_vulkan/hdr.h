/* ---- Shared HDR10 (PQ Rec.2020) output math -------------------------------
 * Included by the Vulkan display shaders (quad.frag, mipmap_resolve.frag)
 * under -DHDR. Every entry point is parameterised - paper white and the gamut
 * mode are passed in - so each shader supplies them from its own push
 * constant rather than this header reaching into a fixed `registers` layout.
 *
 * The colour math matches the prboom "Color Format = HDR" path and
 * RetroArch's own HDR composition, so an HDR frame lands at the same
 * brightness and saturation as the SDR one:
 *   - display transfer is a pure pow(2.4) (RetroArch linearises SDR with 2.4
 *     in its Vulkan/D3D HDR shaders; the sRGB piecewise curve is wrong here
 *     and lifts blacks),
 *   - ordinary content is scaled to paper white,
 *   - Rec.709 -> target primaries keyed to the frontend "Colour Boost" (same
 *     matrices RetroArch applies, so switching SDR<->HDR10 does not shift
 *     saturation),
 *   - SMPTE ST.2084 (PQ) encode over 0..10000 nits.
 * Runs in highp; precision qualifiers are ignored on the desktop Vulkan
 * target but keep the intent explicit. */
#ifndef HDR_H
#define HDR_H

const highp float PQ_M1     = 2610.0 / 16384.0;
const highp float PQ_M2     = (2523.0 / 4096.0) * 128.0;
const highp float PQ_C1     = 3424.0 / 4096.0;
const highp float PQ_C2     = (2413.0 / 4096.0) * 32.0;
const highp float PQ_C3     = (2392.0 / 4096.0) * 32.0;
const highp float PQ_MAXNITS = 10000.0;

highp vec3 pq_encode(highp vec3 nits)
{
	highp vec3 y  = clamp(nits / PQ_MAXNITS, vec3(0.0), vec3(1.0));
	highp vec3 ym = pow(y, vec3(PQ_M1));
	return pow((PQ_C1 + PQ_C2 * ym) / (1.0 + PQ_C3 * ym), vec3(PQ_M2));
}

/* Inverse of sdr_to_linear. Needed wherever something has to be averaged as
 * light and then handed back to a signal-domain consumer. */
highp vec3 linear_to_sdr(highp vec3 c, int sdr_eotf)
{
	c = max(c, vec3(0.0));
	if (sdr_eotf == 1)
		return pow(c, vec3(1.0 / 2.2));
	if (sdr_eotf == 2)
		return mix(1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055,
		           c * 12.92,
		           lessThanEqual(c, vec3(0.0031308)));
	return pow(c, vec3(1.0 / 2.4));
}

/* Source primaries, applied to linear light before the target rotation below.
 *
 * The framebuffer is R'G'B', but which chromaticities those coordinates *mean*
 * is a property of the display the content was authored on, and PSX-era content
 * was not authored on Rec.709. This is orthogonal to the cable: primaries are a
 * property of the authoring monitor, not the wire, so RGB output wants the same
 * treatment as composite.
 *
 *   1  SMPTE-C  (BT.601 525) - what NTSC-era studio monitors used
 *   2  EBU      (BT.601 625) - PAL. Differs from Rec.709 in green alone and by
 *                              only 4%, which is why PAL barely moves.
 *   3  NTSC 1953             - the original, aspirational FCC primaries.
 *                              Enormously wider: its green lands at -0.40 in
 *                              Rec.709, i.e. 40% outside the gamut, so it is
 *                              only actually representable in a wide container.
 *                              Reportedly retained in Japan, though the better
 *                              documented NTSC-J difference is black setup, so
 *                              treat this as flavour rather than accuracy.
 *
 * Every one of these maps some primary outside Rec.709, so on an SDR output the
 * result clips; under HDR10 the Rec.2020 container holds it. Default is 0, no
 * rotation, which leaves the 24-bit path and the HDR path agreeing. */
highp vec3 src_primaries_to_709(highp vec3 c, int src_primaries)
{
	if (src_primaries == 1)          /* SMPTE-C */
		return vec3(
			 0.939542064 * c.r +  0.050181357 * c.g +  0.010276579 * c.b,
			 0.017772223 * c.r +  0.965792862 * c.g +  0.016434914 * c.b,
			-0.001621600 * c.r + -0.004369750 * c.g +  1.005991350 * c.b);
	else if (src_primaries == 2)     /* EBU */
		return vec3(
			 1.044043209 * c.r + -0.044043209 * c.g,
			                       c.g,
			                       0.011793378 * c.g +  0.988206622 * c.b);
	else if (src_primaries == 3)     /* NTSC 1953, Bradford-adapted C -> D65 */
		return vec3(
			 1.486156846 * c.r + -0.403554906 * c.g + -0.082601940 * c.b,
			-0.025101109 * c.r +  0.954024686 * c.g +  0.071076423 * c.b,
			-0.027224002 * c.r + -0.044095233 * c.g +  1.071319235 * c.b);
	return c;
}

/* Gamut rotation, applied to linear light. Cases mirror prboom / RetroArch:
 * 0 Accurate (709->2020), 1 Expanded, 2 Wide (709->P3), 3 Super (no rotation). */
highp vec3 rec709_to_target(highp vec3 c, int expand_gamut)
{
	if (expand_gamut == 1)          /* Expanded */
		return vec3(
			 0.6274040 * c.r +  0.3292820 * c.g +  0.0433136 * c.b,
			 0.0457456 * c.r +  0.9417770 * c.g +  0.0124772 * c.b,
			-0.0012106 * c.r +  0.0176041 * c.g +  0.9836070 * c.b);
	else if (expand_gamut == 2)     /* Wide (DCI-P3) */
		return vec3(
			 0.8215873 * c.r +  0.1763479 * c.g +  0.0020641 * c.b,
			 0.0328261 * c.r +  0.9695096 * c.g + -0.0023367 * c.b,
			 0.0188038 * c.r +  0.0725063 * c.g +  0.9086907 * c.b);
	else if (expand_gamut == 3)     /* Super (stay Rec.709) */
		return c;
	/* Accurate: proper Rec.709 -> Rec.2020 */
	return vec3(
		0.6274040 * c.r + 0.3292820 * c.g + 0.0433136 * c.b,
		0.0690970 * c.r + 0.9195400 * c.g + 0.0113612 * c.b,
		0.0163916 * c.r + 0.0880132 * c.g + 0.8955950 * c.b);
}

/* Reference SDR transfer, applied to the whole signal (see encode_hdr10).
 *   0  BT.1886 pure 2.4 - matches RetroArch's own SDR->HDR composition and a
 *      TV-like reference display. Historical behaviour, still the default.
 *   1  pure 2.2 - closer to a PC monitor tracking sRGB's nominal gamma.
 *   2  sRGB piecewise - what Windows assumes when it composites SDR content
 *      into an HDR desktop. Lifts the toe relative to 2.4.
 * This is a viewing-reference choice, not a correctness one: it decides
 * whether 30-bit HDR lands at the same brightness the 24-bit path did on the
 * same display. Against a 2.2 monitor, 2.4 is 12.9% down in linear light at
 * code 0.5 and 24.2% down at 0.25, which reads as "HDR looks dimmer and more
 * contrasty". All three agree exactly at 0.0 and 1.0, so paper white and the
 * roll-off knee threshold do not move, and all extend monotonically past 1.0
 * so the additive overshoot decodes with the same curve as everything else. */
highp vec3 sdr_to_linear(highp vec3 c, int sdr_eotf)
{
	if (sdr_eotf == 1)
		return pow(c, vec3(2.2));
	if (sdr_eotf == 2)
		return mix(pow((c + vec3(0.055)) / 1.055, vec3(2.4)),
		           c / 12.92,
		           lessThanEqual(c, vec3(0.04045)));
	return pow(c, vec3(2.4));
}

highp float filmic_shoulder(highp float x)
{
	/* Punchier alternative to Reinhard's o/(o+1) as the highlight-shoulder
	 * magnitude: maps the normalised overshoot [0,inf) to [0,1), rising faster
	 * and reaching the ceiling sooner (0.63 vs 0.50 at x=1, 0.86 vs 0.67 at
	 * x=2), which is the behaviour the "filmic" roll-off option documents.
	 *
	 * Both shoulders have to satisfy S'(0) == 1: that is what keeps the encode
	 * C1-continuous where the overshoot region joins reference white, because
	 * the slope just above white is S'(0) * d(lin)/dc against d(lin)/dc just
	 * below it. The Narkowicz ACES fit previously used here has S'(0) = b/e =
	 * 0.214 and so dropped the slope 4.7x exactly at white. Normalising it
	 * (scaling the input by e/b) restores the origin slope but sends the peak
	 * derivative to ~8.4, trading the step for a worse spike a few percent
	 * above white. 1 - exp(-x) has unit slope at the origin and a maximum
	 * derivative of 1, so it is smooth on both counts, and is cheaper than the
	 * rational fit it replaces. */
	return 1.0 - exp(-x);
}

/* Source primaries for a signal-domain consumer: decode, rotate, re-encode.
 *
 * The HDR path gets this for free inside encode_hdr10, which is already
 * holding linear light. An SDR output has to round-trip for it, which is two
 * extra pow() per pixel - so the identity case returns immediately, and a
 * default configuration pays nothing.
 *
 * Every one of the rotations maps some primary outside Rec.709, and an SDR
 * output clips those. That is a real loss and the reason NTSC 1953 in
 * particular only makes sense on a wide-gamut display - but clipping the
 * out-of-gamut corners is still closer to the intended picture than
 * interpreting SMPTE-C coordinates as Rec.709 ones, which is what happens
 * otherwise. */
highp vec3 sdr_apply_src_primaries(highp vec3 rgb, int sdr_eotf, int src_primaries)
{
	if (src_primaries == 0)
		return rgb;
	return linear_to_sdr(
		src_primaries_to_709(sdr_to_linear(max(rgb, vec3(0.0)), sdr_eotf), src_primaries),
		sdr_eotf);
}

/* Entry point for callers that already hold linear light, so it is not decoded
 * and re-encoded on the way in. `scene_linear` is normalised with 1.0 at
 * reference white; values above that are the additive overshoot the roll-off
 * compresses. */
highp vec3 encode_hdr10_linear(highp vec3 scene_linear, highp float paper_white_nits,
                               int expand_gamut, int shoulder, int src_primaries)
{
	/* STEP 3: one transfer function across the whole range, then compress
	 * whatever lands above paper white.
	 *
	 * The signal is PSX-native and gamma-encoded; sdr_to_linear is the
	 * display transfer (defaulting to the pure 2.4 RetroArch uses in its own
	 * Vulkan/D3D HDR shaders). Applying it to the whole value - including the >1.0 left in the 16F
	 * framebuffer by additive blends - is what keeps the encode continuous.
	 * Decoding only [0,1] and treating the overshoot as if it were already
	 * linear light mixes two domains in one sum, and the slope then steps by
	 * headroom/(2.4*paper_white) - 1.67x at 200/1000 nits - exactly at
	 * reference white, which contours any gradient that crosses it and makes
	 * the deband grain visibly coarsen at the same threshold.
	 *
	 * Content in [0,1] is untouched by this: the transfer maps 1.0 to 1.0 for
	 * every sdr_eotf, so sdr_to_linear(c)*paper_white <= paper white, so the roll-off below never engages and the standard range still
	 * maps bit-for-bit onto the SDR result.
	 *
	 * The knee is driven by the peak (brightest) overshoot channel and the
	 * overshoot is scaled proportionally, rather than kneeing each channel on
	 * its own: a per-channel roll-off compresses the brightest channel hardest,
	 * which desaturates a hot coloured highlight toward white (a saturated
	 * additive red would wash out as it brightens). Scaling by the shared
	 * factor keeps the overshoot's chromaticity, so a hot additive red stays
	 * red. Dim channels are left alone, as before. */
	const highp float peak_nits = 1000.0;   /* additive highlight ceiling */
	highp float headroom = max(peak_nits - paper_white_nits, 0.0);
	highp vec3  lin      = src_primaries_to_709(max(scene_linear, vec3(0.0)), src_primaries)
	                     * paper_white_nits;
	/* Linear-light overshoot. Forced to zero when there is no headroom
	 * (paper white at or above the ceiling), which clamps to paper white
	 * instead of letting the knee pass the value through unscaled. */
	highp vec3  over     = (headroom > 0.0)
	                          ? max(lin - vec3(paper_white_nits), vec3(0.0))
	                          : vec3(0.0);
	highp float omax     = max(over.r, max(over.g, over.b));
	highp float o        = (omax > 0.0) ? (omax / headroom) : 0.0;
	/* Per-unit-overshoot factor S(o)/o, shared across channels. shoulder
	 * selects S: 0 = Reinhard o/(o+1) (S/o = 1/(o+1)); 1 = filmic. Both have
	 * S'(0) == 1, so the peak channel leaves white at the same slope it
	 * arrived with and asymptotes to the ceiling.
	 *
	 * (1-exp(-o))/o cancels catastrophically as o -> 0 - fp32 loses 40% of the
	 * value by o = 1e-7, and that is precisely the neighbourhood of reference
	 * white - so the small-o branch uses the series 1 - o/2 + o^2/6, which is
	 * good to ~1e-7 at the crossover and exact at the origin. */
	highp float knee     = 0.0;
	if (o > 0.0)
	{
		if (shoulder == 1)
			knee = (o < 1e-2)
			          ? (1.0 - o * (0.5 - o * (1.0 / 6.0)))
			          : (filmic_shoulder(o) / o);
		else
			knee = 1.0 / (o + 1.0);
	}
	lin = min(lin, vec3(paper_white_nits)) + over * knee;
	lin = rec709_to_target(lin, expand_gamut);
	return pq_encode(lin);
}

highp vec3 encode_hdr10(highp vec3 rgb, highp float paper_white_nits, int expand_gamut,
                        int shoulder, int sdr_eotf, int src_primaries)
{
	return encode_hdr10_linear(sdr_to_linear(max(rgb, vec3(0.0)), sdr_eotf),
	                           paper_white_nits, expand_gamut, shoulder, src_primaries);
}

/* ---- Debanding dither -----------------------------------------------------
 * For the genuinely-8-bit paths whose gradients are already quantised and
 * would band at 10-bit. ~1 8-bit-LSB of triangular-PDF noise, spatially
 * distributed with interleaved gradient noise (blue-noise-like, no texture),
 * applied in gamma space. TPDF decorrelates the noise from the signal, so it
 * reads as a faint even grain. Not for interpolated content (mipmap resolve,
 * YUV chroma) - that already carries sub-8-bit precision 10-bit preserves. */
highp float hdr_ign(highp vec2 p, highp vec2 k)  /* IGN along direction k -> [0,1) */
{
	return fract(52.9829189 * fract(dot(p, k)));
}

highp float hdr_tri(highp float u)  /* uniform [0,1) -> triangular (-1,1) */
{
	return u < 0.5 ? sqrt(2.0 * u) - 1.0 : 1.0 - sqrt(2.0 - 2.0 * u);
}

highp vec3 hdr_deband(highp vec3 rgb, highp vec2 fragcoord)
{
	/* Per-channel IGN *direction* (not a shared field offset) gives three
	 * decorrelated fields, so the grain carries no chroma tint (pairwise
	 * channel correlation ~0). Each is remapped to a triangular PDF. */
	highp vec3 t = vec3(
		hdr_tri(hdr_ign(fragcoord, vec2( 0.06711056,  0.00583715))),
		hdr_tri(hdr_ign(fragcoord, vec2( 0.00583715,  0.06711056))),
		hdr_tri(hdr_ign(fragcoord, vec2( 0.06711056, -0.00583715))));
	return rgb + t * (1.0 / 255.0);
}

/* Luma-only variant for the YUV FMV path. Its chroma is reconstructed at
 * sub-8-bit precision (2x2 average + bilinear) that 10-bit preserves, so it
 * must NOT be dithered; only the per-pixel 8-bit luma steps. An equal offset
 * to R,G,B is chroma-neutral through the BT.601 matrix (U,V rows sum ~0, Y
 * row 1), so one triangular sample on all channels dithers luma alone. */
highp vec3 hdr_deband_luma(highp vec3 rgb, highp vec2 fragcoord)
{
	highp float t = hdr_tri(hdr_ign(fragcoord, vec2(0.06711056, 0.00583715)));
	return rgb + vec3(t) * (1.0 / 255.0);
}

#endif /* HDR_H */
