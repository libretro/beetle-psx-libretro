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

highp vec3 encode_hdr10(highp vec3 rgb, highp float paper_white_nits, int expand_gamut)
{
	/* STEP 3: additive highlights above paper white. Ordinary content in
	 * [0,1] maps to [0, paper white] exactly as SDR does. Additive blends
	 * (B+F) leave >1.0 values in the 16F framebuffer; instead of clamping
	 * them, soft-knee the overshoot so a muzzle flash / lamp / explosion
	 * glows above paper white but rolls off toward a fixed peak rather than
	 * blowing out (pow(2.4) alone would send 2.0 to ~5x paper white). The
	 * knee is Reinhard: over/(over+1) maps [0,inf) -> [0,1), scaled by the
	 * headroom between paper white and the highlight ceiling.
	 *
	 * The knee is driven by the peak (brightest) overshoot channel and the
	 * overshoot is scaled proportionally, rather than kneeing each channel
	 * on its own: a per-channel Reinhard compresses the brightest channel
	 * hardest, which desaturates a hot coloured highlight toward white (a
	 * saturated additive red would wash out as it brightens). Scaling by the
	 * shared factor keeps the overshoot's chromaticity, so a hot additive
	 * red stays red. The peak channel rolls off identically to before, and
	 * neutral (grey) highlights and all [0,1] content are unchanged. */
	const highp float peak_nits = 1000.0;   /* additive highlight ceiling */
	highp vec3  c        = max(rgb, vec3(0.0));
	highp vec3  base     = pow(min(c, vec3(1.0)), vec3(2.4)) * paper_white_nits;
	highp vec3  over     = max(c - vec3(1.0), vec3(0.0));
	highp float o        = max(over.r, max(over.g, over.b));
	highp float headroom = max(peak_nits - paper_white_nits, 0.0);
	highp vec3  glow     = headroom * over / (o + 1.0);
	highp vec3  lin      = base + glow;
	lin = rec709_to_target(lin, expand_gamut);
	return pq_encode(lin);
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
