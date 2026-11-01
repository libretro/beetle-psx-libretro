/* ---- Display transform (tone curves, ACES 2.0, expansion) ---------------
 * The "Display Transform" core option, applied by hdr_grade_linear when
 * dt.mode.x is non-zero; 0 keeps the soft knee in hdr.h. Shared with the
 * GL backend through gen_analog_gl.py. The constants come from
 * rhi/hdr_transform.c, which also holds CPU twins of every curve.
 *
 * Input is scene-linear Rec.709 with 1.0 at paper white. Every curve
 * except ACES 2.0 (full) returns the same units; ACES 2.0 (full) returns
 * its limiting primaries at 1.0 = 100 nits, which dt_grade converts.
 *
 * Vulkan builds (glslang predefines VULKAN) bind at set 0, bindings 8-12. */
#ifndef HDR_TRANSFORM_H
#define HDR_TRANSFORM_H

#ifdef VULKAN
layout(set = 0, binding = 8, std140) uniform DisplayTransform
#else
layout(std140) uniform DisplayTransform
#endif
{
	mat3 inRgbToCam;    /* ACES 2.0 input (AP0) RGB -> CAM16 cones */
	mat3 inConeToAab;
	mat3 limAabToCone;
	mat3 limCamToRgb;   /* ACES 2.0 cones -> limiting primaries */
	mat3 aces2ToOut;    /* limiting primaries -> Rec.2020 container */
	vec4 mode;          /* curve, H = peak/paper, hue preservation, expansion */
	vec4 sh;            /* shoulder A, its slope, GT normalisation, AgX punchy */
	vec4 gt;            /* GT toe m, S0, 1 - S1, C2 */
	vec4 film;          /* Filmic Log rescale, Filmic Log norm, expansion knee, ACES 2.0 exposure */
	vec4 inScalars;     /* F_L_n, cz, inv_cz, A_w_J */
	vec4 limScalars;
	vec4 ts;            /* m_2, s_2, g, t_1 */
	vec4 cc;            /* limit_J_max, model_gamma_inv, sat, sat_thr */
	vec4 cc2;           /* compr, chroma compress scale, mid_J, focus_dist */
	vec4 gm;            /* lower hull gamma inv, forward limit, cube ready, unused */
	vec4 lutScale;      /* cusp J, cusp M, reach M, upper hull gamma inv */
} dt;

#ifdef VULKAN
layout(set = 0, binding = 9) uniform sampler2D uDtAces2Lut;
layout(set = 0, binding = 10) uniform sampler2D uDtFilmLut;
layout(set = 0, binding = 11) uniform sampler2D uDtFilmDesat;
layout(set = 0, binding = 12) uniform sampler3D uDtAces2Cube;
#else
uniform highp sampler2D uDtAces2Lut;
uniform highp sampler2D uDtFilmLut;
uniform highp sampler2D uDtFilmDesat;
uniform highp sampler3D uDtAces2Cube;
#endif

/* Curves without headroom of their own are used up to paper white and
 * carried on to H by a shoulder joined at their slope there. */
highp float dt_shoulder(highp float v, highp float base)
{
	highp float e;
	if (v <= 1.0)
		return base;
	e = v - 1.0;
	return 1.0 + e * dt.sh.x * dt.sh.y / (e + dt.sh.x);
}

/* ---- ACES 2.0 output transform (Hellwig2022 JMh) ---- */
highp float dt_cone_fwd(highp float v)
{
	highp float f = pow(abs(v), 0.42);
	highp float r = f / (27.13 + f);
	return v < 0.0 ? -r : r;
}

highp float dt_cone_inv(highp float v)
{
	highp float a = min(abs(v), 0.99);
	highp float r = pow((27.13 * a) / (1.0 - a), 1.0 / 0.42);
	return v < 0.0 ? -r : r;
}

highp vec3 dt_rgb_to_jmh(highp vec3 rgb, highp mat3 to_cam, highp mat3 cone_to_aab, highp vec4 sc)
{
	highp vec3 m   = to_cam * rgb;
	highp vec3 aab = cone_to_aab * vec3(dt_cone_fwd(m.x), dt_cone_fwd(m.y), dt_cone_fwd(m.z));
	highp float h;
	if (aab.x <= 0.0)
		return vec3(0.0);
	h = degrees(atan(aab.z, aab.y));
	if (h < 0.0)
		h += 360.0;
	return vec3(100.0 * pow(aab.x, sc.y), length(aab.yz), h);
}

highp vec3 dt_jmh_to_rgb(highp vec3 jmh, highp mat3 aab_to_cone, highp mat3 to_rgb, highp vec4 sc)
{
	highp float hr  = radians(jmh.z);
	highp vec3  aab = vec3(pow(jmh.x * 0.01, sc.z), jmh.y * cos(hr), jmh.y * sin(hr));
	highp vec3  a   = aab_to_cone * aab;
	return to_rgb * vec3(dt_cone_inv(a.x), dt_cone_inv(a.y), dt_cone_inv(a.z));
}

highp float dt_j_to_y(highp float J, highp vec4 sc)
{
	return dt_cone_inv(sc.w * pow(abs(J) * 0.01, sc.z)) / sc.x;
}

highp float dt_y_to_j(highp float Y, highp vec4 sc)
{
	highp float J = 100.0 * pow(dt_cone_fwd(abs(Y) * sc.x) / sc.w, sc.y);
	return Y < 0.0 ? -J : J;
}

highp float dt_ts_fwd(highp float x)
{
	highp float f = dt.ts.x * pow(max(x, 0.0) / (x + dt.ts.y), dt.ts.z);
	return max(f * f / (f + dt.ts.w), 0.0) * 100.0;
}

highp float dt_cc_norm(highp float h)
{
	highp float hr = radians(h);
	highp float a = cos(hr), b = sin(hr);
	highp float c2 = a * a - b * b, s2 = 2.0 * a * b;
	highp float c3 = 4.0 * a * a * a - 3.0 * a, s3 = 3.0 * b - 4.0 * b * b * b;
	return (11.34072 * a + 16.46899 * c2 + 7.88380 * c3 + 14.66441 * b
	        - 6.37224 * s2 + 9.19364 * s3 + 77.12896) * dt.cc2.y;
}

highp float dt_toe_fwd(highp float x, highp float limit, highp float k1in, highp float k2in)
{
	highp float k2, k1, k3, mb, mc;
	if (x > limit)
		return x;
	k2 = max(k2in, 0.001);
	k1 = sqrt(k1in * k1in + k2 * k2);
	k3 = (limit + k1) / (limit + k2);
	mb = k3 * x - k1;
	mc = k2 * k3 * x;
	return 0.5 * (mb + sqrt(mb * mb + 4.0 * mc));
}

highp vec4 dt_lut_at(highp float h)
{
	highp float u = (mod(h, 360.0) + 0.5) / 360.0;
	return texture(uDtAces2Lut, vec2(u, 0.5)) * dt.lutScale;
}

highp vec3 dt_chroma_compress(highp vec3 jmh, highp float tmJ, highp vec4 lut)
{
	highp float nJ, snJ, Mn, limit, M;
	if (jmh.y == 0.0)
		return vec3(tmJ, 0.0, jmh.z);
	nJ    = tmJ / dt.cc.x;
	snJ   = max(0.0, 1.0 - nJ);
	Mn    = dt_cc_norm(jmh.z);
	limit = pow(nJ, dt.cc.y) * lut.z / Mn;
	M     = jmh.y * pow(tmJ / jmh.x, dt.cc.y) / Mn;
	M     = limit - dt_toe_fwd(limit - M, limit - 0.001, snJ * dt.cc.z, sqrt(nJ * nJ + dt.cc.w));
	M     = dt_toe_fwd(M, limit, nJ * dt.cc2.x, snJ);
	return vec3(tmJ, M * Mn, jmh.z);
}

highp float dt_focus_gain(highp float J, highp float athr)
{
	highp float gain = dt.cc.x * dt.cc2.w;
	if (J > athr)
	{
		highp float adj = log(max(dt.cc.x - athr, 1e-6) / max(dt.cc.x - J, 0.0001)) / log(10.0);
		gain *= adj * adj + 1.0;
	}
	return gain;
}

highp float dt_solve_j(highp float J, highp float M, highp float fJ, highp float sg)
{
	highp float Ms = M / sg, a = Ms / fJ, b, c;
	if (J < fJ)
	{
		b = 1.0 - Ms;
		c = -J;
		return -2.0 * c / (b + sqrt(b * b - 4.0 * a * c));
	}
	b = -(1.0 + Ms + dt.cc.x * a);
	c = dt.cc.x * Ms + J;
	return -2.0 * c / (b - sqrt(b * b - 4.0 * a * c));
}

highp float dt_vec_slope(highp float iJ, highp float fJ, highp float sg)
{
	highp float d = (iJ < fJ) ? iJ : (dt.cc.x - iJ);
	return d * (iJ - fJ) / (fJ * sg);
}

highp float dt_smin(highp float a, highp float b, highp float ref)
{
	highp float s  = 0.12 * ref;
	highp float hh = max(s - abs(a - b), 0.0) / s;
	return min(a, b) - hh * hh * hh * s / 6.0;
}

highp float dt_bound_m(highp float iJ, highp float slope, highp float ig,
                       highp float Jm, highp float Mm, highp float Jr)
{
	return Jr * pow(iJ / Jr, ig) * Mm / (Jm - slope * Mm);
}

highp vec3 dt_gamut_compress(highp vec3 jmh, highp vec4 lut)
{
	highp float blend, fJ, athr, sg, iJ, slope, iJc, lower, upper, gb, rb, prop, thr, M;
	if (jmh.x <= 0.0)
		return vec3(0.0, 0.0, jmh.z);
	if (jmh.y < 0.0 || jmh.x > dt.cc.x)
		return vec3(jmh.x, 0.0, jmh.z);
	blend = min(1.3 - lut.x / dt.cc.x, 1.0);
	fJ    = lut.x + (dt.cc2.z - lut.x) * blend;
	athr  = lut.x + (dt.cc.x - lut.x) * 0.3;
	sg    = dt_focus_gain(jmh.x, athr);
	iJ    = dt_solve_j(jmh.x, jmh.y, fJ, sg);
	slope = dt_vec_slope(iJ, fJ, sg);
	iJc   = dt_solve_j(lut.x, lut.y, fJ, sg);
	lower = dt_bound_m(iJ, slope, dt.gm.x, lut.x, lut.y, iJc);
	upper = dt_bound_m(dt.cc.x - iJ, -slope, lut.w, dt.cc.x - lut.x, lut.y, dt.cc.x - iJc);
	gb    = dt_smin(lower, upper, lut.y);
	if (gb <= 0.0)
		return vec3(jmh.x, 0.0, jmh.z);
	rb    = dt_bound_m(iJ, slope, dt.cc.y, dt.cc.x, lut.z, dt.cc.x);
	prop  = max(gb / rb, 0.75);
	thr   = prop * gb;
	M     = jmh.y;
	if (!(M <= thr || prop >= 1.0))
	{
		highp float mo = M - thr, go = gb - thr, ro = rb - thr;
		highp float scale = ro / ((ro / go) - 1.0);
		highp float nd = mo / scale;
		M = thr + scale * nd / (1.0 + nd);
	}
	return vec3(iJ + M * slope, M, jmh.z);
}

/* Rec.709 (D65) linear -> ACES2065-1 (AP0, D60), Bradford adapted: the
 * transform's input space. Column-major. */
const highp mat3 DT_709_TO_AP0 = mat3(
	0.4397010, 0.0897923, 0.0175440,
	0.3829780, 0.8134230, 0.1115440,
	0.1773350, 0.0967616, 0.8707040);

highp vec3 dt_clamp_ap0_to_ap1(highp vec3 aces, highp float upper)
{
	const highp mat3 to_ap1 = mat3( 1.4514393161, -0.0765537733,  0.0083161484,
	                               -0.2365107469,  1.1762296998, -0.0060324498,
	                               -0.2149285693, -0.0996759265,  0.9977163014);
	const highp mat3 to_ap0 = mat3( 0.6954522414,  0.0447945634, -0.0055258826,
	                                0.1406786965,  0.8596711185,  0.0040252103,
	                                0.1638690622,  0.0955343182,  1.0015006723);
	return to_ap0 * clamp(to_ap1 * aces, 0.0, upper);
}

/* Scene Rec.709 (1.0 = paper white) -> limiting primaries (1.0 = 100 nits). */
highp vec3 dt_aces2(highp vec3 v)
{
	highp vec3  aces = dt_clamp_ap0_to_ap1(DT_709_TO_AP0 * v * dt.film.w, dt.gm.y);
	highp vec3  jmh  = dt_rgb_to_jmh(aces, dt.inRgbToCam, dt.inConeToAab, dt.inScalars);
	highp float tmJ  = dt_y_to_j(dt_ts_fwd(dt_j_to_y(jmh.x, dt.inScalars) * 0.01), dt.inScalars);
	highp vec3  tm   = dt_chroma_compress(jmh, tmJ, dt_lut_at(jmh.z));
	highp vec3  cg   = dt_gamut_compress(tm, dt_lut_at(tm.z));
	return dt_jmh_to_rgb(cg, dt.limAabToCone, dt.limCamToRgb, dt.limScalars);
}

/* ---- the curves on the whole colour ---- */
highp vec3 dt_aces_fit_rrt(highp vec3 v)
{
	highp vec3 a = v * (v + 0.0245786) - 0.000090537;
	highp vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
	return a / b;
}

highp vec3 dt_curve_rgb(highp vec3 c)
{
	int m = int(dt.mode.x + 0.5);
	if (m == 22)        /* AgX */
	{
		const highp mat3 mi = mat3(0.842479062253094, 0.0784335999999992, 0.0792237451477643,
		                           0.0423282422610123, 0.878468636469772, 0.0791661274605434,
		                           0.0423756549057051, 0.0784336, 0.879142973793104);
		const highp mat3 mo = mat3(1.19687900512017, -0.0980208811401368, -0.0990297440797205,
		                           -0.0528968517574562, 1.15190312990417, -0.0989611768448433,
		                           -0.0529716355144438, -0.0980434501171241, 1.15107367264116);
		highp vec3 v = mi * max(c, vec3(0.0));
		highp vec3 v2, v4;
		v  = clamp((log2(max(v, vec3(1e-10))) + 12.47393) / 16.500009, 0.0, 1.0);
		v2 = v * v;
		v4 = v2 * v2;
		v  = 15.5 * v4 * v2 - 40.14 * v4 * v + 31.96 * v4
		   - 6.868 * v2 * v + 0.4298 * v2 + 0.1191 * v - 0.00232;
		v  = pow(max(mo * v, vec3(0.0)), vec3(2.2)) * 1.4491792;
		if (dt.sh.w > 0.5)  /* Punchy: contrast 1.35, saturation 1.4 */
		{
			highp vec3  u = pow(v, vec3(1.35));
			highp float n = dot(u, vec3(0.2126, 0.7152, 0.0722));
			v = max(vec3(n) + (u - vec3(n)) * 1.4, vec3(0.0));
		}
		return v;
	}
	if (m == 21)        /* Khronos PBR Neutral */
	{
		highp float x   = min(c.r, min(c.g, c.b));
		highp float off = x < 0.08 ? x - 6.25 * x * x : 0.04;
		highp vec3  v   = c - off;
		highp float peak = max(v.r, max(v.g, v.b));
		highp float d, np, g;
		if (peak < 0.76)
			return v * 1.1506276;
		d  = 1.0 - 0.76;
		np = 1.0 - d * d / (peak + d - 0.76);
		v *= np / peak;
		g  = 1.0 - 1.0 / (0.15 * (peak - np) + 1.0);
		return mix(v, vec3(np), g) * 1.1506276;
	}
	if (m == 18)        /* Filmic Log (Sobotka): desaturation cube, contrast curve */
	{
		highp vec3  t = clamp((log2(max(c, vec3(1e-10))) + 12.4739312) * 0.04, 0.0, 1.0);
		highp vec3  d = t * 32.0;
		highp float bz = floor(d.z), fz = d.z - bz;
		highp vec2  uv = vec2((bz * 33.0 + min(d.x, 31.999) + 0.5) / 1089.0, (d.y + 0.5) / 33.0);
		highp vec3  s0 = texture(uDtFilmDesat, uv).rgb;
		highp vec3  s1 = texture(uDtFilmDesat, uv + vec2(33.0 / 1089.0, 0.0)).rgb;
		highp vec3  o;
		t   = clamp(mix(s0, s1, fz) * dt.film.x, 0.0, 1.0);
		o.r = texture(uDtFilmLut, vec2(t.r, 0.5)).r;
		o.g = texture(uDtFilmLut, vec2(t.g, 0.5)).r;
		o.b = texture(uDtFilmLut, vec2(t.b, 0.5)).r;
		return pow(max(o, vec3(0.0)), vec3(2.2)) * dt.film.y;
	}
	if (m == 20)        /* ACES Fitted (Hill) */
	{
		const highp mat3 mi = mat3(0.59719, 0.07600, 0.02840,
		                           0.35458, 0.90834, 0.13383,
		                           0.04823, 0.01566, 0.83777);
		const highp mat3 mo = mat3( 1.60475, -0.10208, -0.00327,
		                           -0.53108,  1.10813, -0.07276,
		                           -0.07367, -0.00605,  1.07602);
		return max(mo * dt_aces_fit_rrt(mi * c), vec3(0.0)) * 1.6152077;
	}
	{                   /* 19: Reinhard-Jodie */
		highp float l  = dot(c, vec3(0.2126, 0.7152, 0.0722));
		highp vec3  tv = c / (1.0 + c);
		return mix(c / (1.0 + l), tv, tv) * 2.0;
	}
}

/* ---- the per-channel curves ---- */
highp float dt_curve(highp float v)
{
	int m = int(dt.mode.x + 0.5);
	highp float x = max(v, 0.0);
	if (m == 1)         /* ACES (Narkowicz) */
		return dt_shoulder(v, v * (2.51 * v + 0.03) / (v * (2.43 * v + 0.59) + 0.14) * 1.2440945);
	if (m == 2)         /* Hejl and Burgess-Dawson */
	{
		highp float h = max(v - 0.004, 0.0);
		return dt_shoulder(v, pow((h * (6.2 * h + 0.5)) / (h * (6.2 * h + 1.7) + 0.06), 2.2) * 1.4629683);
	}
	if (m == 3)         /* GT (Uchimura) */
	{
		highp float t  = clamp(v / dt.gt.x, 0.0, 1.0);
		highp float w0 = 1.0 - (t * t * (3.0 - 2.0 * t));
		highp float w2 = step(dt.gt.y, v);
		highp float w1 = 1.0 - w0 - w2;
		highp float T  = dt.gt.x * pow(x / dt.gt.x, 1.33);
		highp float S  = 1.0 - dt.gt.z * exp(-dt.gt.w * (v - dt.gt.y));
		return (T * w0 + v * w1 + S * w2) * dt.sh.z;
	}
	if (m == 4)         /* Hable (Uncharted 2) */
		return dt_shoulder(v, (((v * (0.15 * v + 0.05) + 0.004) / (v * (0.15 * v + 0.50) + 0.06))
		                       - 0.0666667) * 4.5319149);
	if (m == 5)         /* Lottes */
		return dt_shoulder(v, (pow(x, 1.6) / (pow(x, 1.5632) * 0.1674199 + 1.0730397)) * 1.2404596);
	if (m == 6)         /* Drago */
		return dt_shoulder(v, log(1.0 + x) / log(2.0 + 8.0 * pow(x / 8.0, 0.2344653))
		                      * (1.0 / 0.9542425) * 2.6616800);
	if (m == 7)         /* Unreal */
		return dt_shoulder(v, pow(v / (v + 0.155) * 1.019, 2.2) * 1.3173380);
	if (m == 8)         /* Filmic ALU (Hejl 2015) */
	{
		highp float va = 1.425 * x + 0.05;
		highp float f  = ((x * va + 0.004) / ((x * va + 0.055) + 0.0491)) - 0.0821;
		return dt_shoulder(v, pow(max(f, 0.0), 2.2) * 1.4132626);
	}
	if (m == 9)         /* Reinhard (plain) */
		return dt_shoulder(v, (x / (1.0 + x)) * 2.0);
	if (m == 10)        /* Reinhard (extended, white 4) */
		return dt_shoulder(v, (x * (1.0 + x / 16.0) / (1.0 + x)) * 1.8823529);
	if (m == 11)        /* Exponential */
		return dt_shoulder(v, (1.0 - exp(-x)) * 1.5819767);
	if (m == 12)        /* Hable 2017 piecewise */
	{
		highp float f;
		if (x < 0.25)
			f = 1.1313708 * pow(x, 1.25);
		else if (x < 0.70)
			f = 0.20 + (x - 0.25);
		else
			f = 0.65 + 0.35 * (x - 0.70) / ((x - 0.70) + 0.35);
		return dt_shoulder(v, f * 1.2322275);
	}
	if (m == 13)        /* ACES 2.0 tone scale alone */
	{
		highp float f = 1.0471038 * pow(x / (x + 0.9198583), 1.15);
		return dt_shoulder(v, (f * f / (f + 0.04)) * 2.1854781);
	}
	if (m == 14)        /* Tumblin-Rushmeier */
		return dt_shoulder(v, pow(x, 0.6075));
	if (m == 15)        /* Ward */
		return dt_shoulder(v, x);
	if (m == 16)        /* Schlick */
		return dt_shoulder(v, 2.3 * x / (2.3 * x - x + 1.0));
	/* 17: Reinhard-Devlin */
	return dt_shoulder(v, (x / (x + 0.3010864)) * 1.3010864);
}

/* HDR Expansion: lifts the top of the range into the headroom. */
highp vec3 dt_expand_curve(highp vec3 v, highp float E, highp float K)
{
	highp vec3 t  = clamp((v - K) / max(1.0 - K, 1e-4), 0.0, 1.0);
	highp vec3 hi = K + (1.0 - K) * t + (E - 1.0) * t * t + max(v - 1.0, 0.0);
	return mix(v, hi, step(vec3(K), v));
}

highp vec3 dt_expand(highp vec3 c)
{
	highp float E = dt.mode.y, p;
	if (dt.mode.w < 0.5 || E <= 1.0001)
		return c;
	if (dt.mode.w < 1.5)
		return dt_expand_curve(c, E, dt.film.z);
	p = max(c.r, max(c.g, c.b));
	if (p <= 1e-5)
		return c;
	return c * (dt_expand_curve(vec3(p), E, dt.film.z).x / p);
}

/* dt_aces2() baked: the whole transform, solved in double precision on the
 * CPU, as a 65^3 cube over the PQ code of the input light. Closer to the
 * reference than the per-pixel solve, and one filtered fetch. */
highp vec3 dt_aces2_cube(highp vec3 v)
{
	const highp float n = 65.0;
	highp vec3 y = clamp(v * dt.film.w * 0.01, 0.0, 1.0);
	highp vec3 p = pow(y, vec3(0.1593017578125));
	highp vec3 u = pow((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), vec3(78.84375));
	return textureLod(uDtAces2Cube, u * ((n - 1.0) / n) + 0.5 / n, 0.0).rgb;
}

/* Scene Rec.709 (1.0 = paper white) -> Rec.709 in paper-white units, or,
 * for ACES 2.0 (full), Rec.2020 nits; `absolute` tells the caller which. */
highp vec3 dt_apply(highp vec3 v, highp float paper_white_nits, out bool absolute)
{
	highp float pre;
	int m = int(dt.mode.x + 0.5);
	v = dt_expand(max(v, vec3(0.0)));
	absolute = (m == 23);
	if (absolute)
		return dt.aces2ToOut * (dt.gm.z > 0.5 ? dt_aces2_cube(v) : dt_aces2(v)) * 100.0;
	pre = max(v.r, max(v.g, v.b));
	if (m >= 18)
		v = dt_curve_rgb(v);
	else
		v = vec3(dt_curve(v.r), dt_curve(v.g), dt_curve(v.b));
	if (dt.mode.z > 0.5)   /* Highlight Hue Preservation */
	{
		highp float post = max(v.r, max(v.g, v.b));
		highp float g    = 1.0 - 1.0 / (0.15 * max(pre - post, 0.0) + 1.0);
		v = mix(v, vec3(post), g);
	}
	return v;
}

#endif /* HDR_TRANSFORM_H */
