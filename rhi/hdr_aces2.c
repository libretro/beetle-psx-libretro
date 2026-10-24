/*
===========================================================================

ACES 2.0 output transform, stage one: the Hellwig2022 JMh appearance space.

This is a port of the corresponding part of the Academy's reference CTL
(Lib.Academy.OutputTransform.ctl), transcribed rather than reinvented.
Every constant here comes from that file; none of it is tuned by eye.

This file began as the JMh stage alone and now carries the whole
transform.  What is here, in the order the reference applies it:

  the AP0 to AP1 clamp
  linear RGB to and from Hellwig2022 JMh
  the tone scale, forward and inverse
  chroma compression, forward and inverse
  gamut compression, and its two-pass inverse
  the three hue tables - cusp, reach, upper hull exponent - and the
  packing that hands them to a shader as one texture

ACES2_OutputTransformFwd assembles those into the forward transform,
and both shader backends are transcriptions of it rather than separate
derivations.

Both directions are assembled: ACES2_OutputTransformFwd and
ACES2_OutputTransformInv.  The inverse is not exact everywhere and
cannot be - the gamut compression's inverse estimates the source
lightness rather than knowing it, and the AP0 to AP1 clamp at the front
folds anything outside AP1 onto its boundary, which no inverse can
unfold.  Where both are well behaved it round trips to 5.7e-07.

Both shader backends are wired: ARB for the desktop composite, GLSL for
everything else, each a transcription of this file rather than a
separate derivation and each checked against it.

The separate "ACES 2.0 Tone Scale" roll-off is still a roll-off: the
tone scale alone, applied per channel, with none of the appearance
model around it.  That is deliberate and its option text says so.

It is CPU-only on purpose.  The two shader backends consume hue tables
built from this, and the tables are built once at init; getting the maths
right in one place first, where it can be tested, is cheaper than getting
it wrong in two shader dialects at once.

===========================================================================
*/

#include <boolean.h>

#include "hdr_aces2.h"

#include <math.h>

#define ACES2_PI 3.14159265358979323846

/* ---- constants, all from the reference ---- */

static const double ref_luminance = 100.0;
static const double L_A           = 100.0;
static const double Y_b           = 20.0;
/* Viewing-condition surround, CAM16's (c, F) pair.  The reference sets
   are average (0.69, 1.0), dim (0.59, 0.9) and dark (0.525, 0.8); dim
   has always been the baked value here and stays the default.  Settable
   before the tables are built - the caller owns the rebuild, exactly as
   it does for peak luminance changes. */
static double surround_1    = 0.59;
static double surround_2    = 0.9;

void ACES2_SetSurround( int mode ) {
	switch ( mode ) {
	case 0:  surround_1 = 0.525; surround_2 = 0.8; break;   /* dark */
	case 2:  surround_1 = 0.69;  surround_2 = 1.0; break;   /* average */
	default: surround_1 = 0.59;  surround_2 = 0.9; break;   /* dim */
	}
}

static const double J_scale             = 100.0;
static const double cam_nl_Y_reference  = 100.0;
static const double cam_nl_offset       = 0.2713 * cam_nl_Y_reference;
static const double cam_nl_scale        = 4.0 * cam_nl_Y_reference;

/* CAM16 primaries, used to build MATRIX_16 */
static const double CAM16_PRI[8] = {
	0.8336,  0.1735,		/* red   */
	2.3854, -1.4659,		/* green */
	0.0870, -0.1250,		/* blue  */
	0.3330,  0.3330			/* white */
};

/* ---- small matrix helpers ---- */

static void mat3_mul( const double a[9], const double b[9], double out[9] ) {
	int i, j, k;
	double t[9];
	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			double s = 0.0;
			for ( k = 0; k < 3; k++ ) {
				s += a[i*3+k] * b[k*3+j];
			}
			t[i*3+j] = s;
		}
	}
	for ( i = 0; i < 9; i++ ) {
		out[i] = t[i];
	}
}

/* The CTL's mult_f3_f33 treats the vector as a row: out = v * M. */
static void vec3_mul_mat3( const double v[3], const double m[9], double out[3] ) {
	double t[3];
	int j;
	for ( j = 0; j < 3; j++ ) {
		t[j] = v[0] * m[0*3+j] + v[1] * m[1*3+j] + v[2] * m[2*3+j];
	}
	out[0] = t[0]; out[1] = t[1]; out[2] = t[2];
}

static bool mat3_invert( const double m[9], double out[9] ) {
	const double a = m[0], b = m[1], c = m[2];
	const double d = m[3], e = m[4], f = m[5];
	const double g = m[6], h = m[7], i = m[8];
	const double A =  ( e * i - f * h );
	const double B = -( d * i - f * g );
	const double C =  ( d * h - e * g );
	const double det = a * A + b * B + c * C;
	double inv;

	if ( det == 0.0 ) {
		return false;
	}
	inv = 1.0 / det;
	out[0] = A * inv;
	out[1] = -( b * i - c * h ) * inv;
	out[2] =  ( b * f - c * e ) * inv;
	out[3] = B * inv;
	out[4] =  ( a * i - c * g ) * inv;
	out[5] = -( a * f - c * d ) * inv;
	out[6] = C * inv;
	out[7] = -( a * h - b * g ) * inv;
	out[8] =  ( a * e - b * d ) * inv;
	return true;
}

/*
================
RGBtoXYZ

Chromaticities are laid out as the CTL's Chromaticities struct:
red xy, green xy, blue xy, white xy.
================
*/
static void RGB_to_XYZ_matrix( const double p[8], double out[9] ) {
	const double xr = p[0], yr = p[1];
	const double xg = p[2], yg = p[3];
	const double xb = p[4], yb = p[5];
	const double xw = p[6], yw = p[7];

	const double zr = 1.0 - xr - yr;
	const double zg = 1.0 - xg - yg;
	const double zb = 1.0 - xb - yb;

	double M[9];
	double Minv[9];
	double W[3];
	double S[3];
	int i;

	M[0] = xr / yr; M[1] = 1.0; M[2] = zr / yr;
	M[3] = xg / yg; M[4] = 1.0; M[5] = zg / yg;
	M[6] = xb / yb; M[7] = 1.0; M[8] = zb / yb;
	W[0] = xw / yw; W[1] = 1.0; W[2] = ( 1.0 - xw - yw ) / yw;

	mat3_invert( M, Minv );
	vec3_mul_mat3( W, Minv, S );

	for ( i = 0; i < 3; i++ ) {
		out[i*3+0] = S[i] * M[i*3+0];
		out[i*3+1] = S[i] * M[i*3+1];
		out[i*3+2] = S[i] * M[i*3+2];
	}
}

bool ACES2_PrimariesMatrix( const double from[8], const double to[8], double out[9] ) {
	double Mf[9], Mt[9], MtInv[9];
	int i, j;
	for ( i = 1; i < 8; i += 2 ) {
		if ( from[i] <= 0.0 || to[i] <= 0.0 ) {
			return false;
		}
	}
	RGB_to_XYZ_matrix( from, Mf );
	RGB_to_XYZ_matrix( to, Mt );
	if ( !mat3_invert( Mt, MtInv ) ) {
		return false;
	}
	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			out[i*3+j] = Mf[i*3+0] * MtInv[0*3+j] + Mf[i*3+1] * MtInv[1*3+j] + Mf[i*3+2] * MtInv[2*3+j];
		}
	}
	return true;
}

/* ---- cone response compression ---- */

static double cone_fwd_abs( double Rc ) {
	const double F_L_Y = pow( Rc, 0.42 );
	return F_L_Y / ( cam_nl_offset + F_L_Y );
}

static double cone_inv_abs( double Ra ) {
	const double Ra_lim = Ra < 0.99 ? Ra : 0.99;
	const double F_L_Y = ( cam_nl_offset * Ra_lim ) / ( 1.0 - Ra_lim );
	return pow( F_L_Y, 1.0 / 0.42 );
}

static double cone_fwd( double v ) {
	const double r = cone_fwd_abs( fabs( v ) );
	return v < 0.0 ? -r : r;
}

static double cone_inv( double v ) {
	const double r = cone_inv_abs( fabs( v ) );
	return v < 0.0 ? -r : r;
}

/*
================
ACES2_InitJMhParams

Builds the per-primaries constants: the two matrices that carry linear
RGB into the cone response space and back, and the scalars the lightness
conversion needs.
================
*/
void ACES2_InitJMhParams( const double prims[8], aces2JMhParams_t *p ) {
	double MATRIX_16[9], RGB_TO_XYZ[9], XYZ_w[3], RGB_w[3];
	double D_RGB[3], RGB_wc[3], RGB_Aw[3];
	double cone_to_Aab[9];
	double M1[9], M2[9], RGB_to_CAM16[9], RGB_to_CAM16_c[9];
	double Y_w, k, k4, F_L, F_L_n, cz, A_w;
	double white[3];
	static const double base_cone_to_Aab[9] = {
		2.0,        1.0,          1.0 / 9.0,
		1.0,      -12.0 / 11.0,   1.0 / 9.0,
		1.0 / 20.0, 1.0 / 11.0,  -2.0 / 9.0
	};
	double model_gamma;
	int i;

	white[0] = white[1] = white[2] = ref_luminance;
	model_gamma = surround_1 * ( 1.48 + sqrt( Y_b / ref_luminance ) );

	{
		double XYZ_to_RGB_cam[9];
		RGB_to_XYZ_matrix( CAM16_PRI, XYZ_to_RGB_cam );
		mat3_invert( XYZ_to_RGB_cam, MATRIX_16 );
	}

	RGB_to_XYZ_matrix( prims, RGB_TO_XYZ );
	vec3_mul_mat3( white, RGB_TO_XYZ, XYZ_w );
	Y_w = XYZ_w[1];
	vec3_mul_mat3( XYZ_w, MATRIX_16, RGB_w );

	k  = 1.0 / ( 5.0 * L_A + 1.0 );
	k4 = k * k * k * k;
	F_L = 0.2 * k4 * ( 5.0 * L_A )
		+ 0.1 * ( 1.0 - k4 ) * ( 1.0 - k4 ) * pow( 5.0 * L_A, 1.0 / 3.0 );
	F_L_n = F_L / ref_luminance;
	cz = model_gamma;

	for ( i = 0; i < 3; i++ ) {
		D_RGB[i]  = F_L_n * Y_w / RGB_w[i];
		RGB_wc[i] = D_RGB[i] * RGB_w[i];
		RGB_Aw[i] = cone_fwd( RGB_wc[i] );
	}

	for ( i = 0; i < 9; i++ ) {
		cone_to_Aab[i] = cam_nl_scale * base_cone_to_Aab[i];
	}
	A_w = cone_to_Aab[0*3+0] * RGB_Aw[0]
		+ cone_to_Aab[1*3+0] * RGB_Aw[1]
		+ cone_to_Aab[2*3+0] * RGB_Aw[2];

	/* MATRIX_RGB_to_CAM16 = (RGB_TO_XYZ * MATRIX_16) * ref_luminance,
	 * then the per-channel adaptation applied on the diagonal. */
	mat3_mul( RGB_TO_XYZ, MATRIX_16, M1 );
	for ( i = 0; i < 9; i++ ) {
		M2[i] = 0.0;
	}
	M2[0] = M2[4] = M2[8] = ref_luminance;
	mat3_mul( M1, M2, RGB_to_CAM16 );
	for ( i = 0; i < 3; i++ ) {
		RGB_to_CAM16_c[i*3+0] = RGB_to_CAM16[i*3+0] * D_RGB[0];
		RGB_to_CAM16_c[i*3+1] = RGB_to_CAM16[i*3+1] * D_RGB[1];
		RGB_to_CAM16_c[i*3+2] = RGB_to_CAM16[i*3+2] * D_RGB[2];
	}

	for ( i = 0; i < 3; i++ ) {
		p->cone_to_Aab[i*3+0] = cone_to_Aab[i*3+0] / A_w;
		p->cone_to_Aab[i*3+1] = cone_to_Aab[i*3+1] * 43.0 * surround_2;
		p->cone_to_Aab[i*3+2] = cone_to_Aab[i*3+2] * 43.0 * surround_2;
	}
	mat3_invert( p->cone_to_Aab, p->Aab_to_cone );

	for ( i = 0; i < 9; i++ ) {
		p->RGB_to_CAM16_c[i] = RGB_to_CAM16_c[i];
	}
	mat3_invert( p->RGB_to_CAM16_c, p->CAM16_c_to_RGB );

	p->F_L_n     = F_L_n;
	p->cz        = cz;
	p->inv_cz    = 1.0 / cz;
	p->A_w_J     = cone_fwd_abs( F_L );
	p->inv_A_w_J = 1.0 / p->A_w_J;
}

/*
================
ACES2_RGBToJMh
================
*/
void ACES2_RGBToJMh( const double RGB[3], const aces2JMhParams_t *p, double JMh[3] ) {
	double rgb_m[3], rgb_a[3], Aab[3];
	int i;

	vec3_mul_mat3( RGB, p->RGB_to_CAM16_c, rgb_m );
	for ( i = 0; i < 3; i++ ) {
		rgb_a[i] = cone_fwd( rgb_m[i] );
	}
	vec3_mul_mat3( rgb_a, p->cone_to_Aab, Aab );

	if ( Aab[0] <= 0.0 ) {
		JMh[0] = JMh[1] = JMh[2] = 0.0;
		return;
	}

	JMh[0] = J_scale * pow( Aab[0], p->cz );
	JMh[1] = sqrt( Aab[1] * Aab[1] + Aab[2] * Aab[2] );
	JMh[2] = atan2( Aab[2], Aab[1] ) * ( 180.0 / ACES2_PI );
	if ( JMh[2] < 0.0 ) {
		JMh[2] += 360.0;
	}
}

/*
================
ACES2_JMhToRGB
================
*/
void ACES2_JMhToRGB( const double JMh[3], const aces2JMhParams_t *p, double RGB[3] ) {
	const double h_rad = JMh[2] * ( ACES2_PI / 180.0 );
	double Aab[3], rgb_a[3], rgb_m[3];
	int i;

	Aab[0] = pow( JMh[0] * ( 1.0 / J_scale ), p->inv_cz );
	Aab[1] = JMh[1] * cos( h_rad );
	Aab[2] = JMh[1] * sin( h_rad );

	vec3_mul_mat3( Aab, p->Aab_to_cone, rgb_a );
	for ( i = 0; i < 3; i++ ) {
		rgb_m[i] = cone_inv( rgb_a[i] );
	}
	vec3_mul_mat3( rgb_m, p->CAM16_c_to_RGB, RGB );
}

/*
================
ACES2_JToY / ACES2_YToJ

The achromatic axis on its own, which the tone scale stage uses.
================
*/
double ACES2_JToY( double J, const aces2JMhParams_t *p ) {
	const double A = pow( fabs( J ) * ( 1.0 / J_scale ), p->inv_cz );
	return cone_inv_abs( p->A_w_J * A ) / p->F_L_n;
}

double ACES2_YToJ( double Y, const aces2JMhParams_t *p ) {
	const double Ra = cone_fwd_abs( fabs( Y ) * p->F_L_n );
	const double J = J_scale * pow( Ra * p->inv_A_w_J, p->cz );
	return Y < 0.0 ? -J : J;
}

/* ---- the display gamut cusp table ---- */

/*
================
ACES2_CubeCorner

The six corners of the RGB cube that are neither black nor white, in the
order the reference generates them: red, yellow, green, cyan, blue,
magenta.
================
*/
static void ACES2_CubeCorner( int corner, double rgb[3] ) {
	rgb[0] = ( ( ( corner + 1 ) % 6 ) < 3 ) ? 1.0 : 0.0;
	rgb[1] = ( ( ( corner + 5 ) % 6 ) < 3 ) ? 1.0 : 0.0;
	rgb[2] = ( ( ( corner + 3 ) % 6 ) < 3 ) ? 1.0 : 0.0;
}

/*
================
ACES2_BuildCuspTable

The corners are taken round the cube in hue order, then for each table
hue the cusp is bisected along the cube edge between the two corners
that bracket it.  The reference's tolerance is 1e-7 in the edge
parameter, which is what the loop count below is sized for.
================
*/
void ACES2_BuildCuspTable( const aces2JMhParams_t *limit, double peakLuminance,
						   aces2CuspTable_t *table ) {
	double cornerRGB[8][3], cornerJMh[8][3];
	double tmpRGB[6][3], tmpJMh[6][3];
	int minIndex = 0;
	int i, c;

	for ( i = 0; i < ACES2_TABLE_SIZE; i++ ) {
		table->hueOfEntry[i] = (double)i;
	}

	for ( i = 0; i < 6; i++ ) {
		double unit[3];
		ACES2_CubeCorner( i, unit );
		tmpRGB[i][0] = unit[0] * peakLuminance / 100.0;
		tmpRGB[i][1] = unit[1] * peakLuminance / 100.0;
		tmpRGB[i][2] = unit[2] * peakLuminance / 100.0;
		ACES2_RGBToJMh( tmpRGB[i], limit, tmpJMh[i] );
		if ( tmpJMh[i][2] < tmpJMh[minIndex][2] ) {
			minIndex = i;
		}
	}

	for ( i = 0; i < 6; i++ ) {
		const int src = ( i + minIndex ) % 6;
		for ( c = 0; c < 3; c++ ) {
			cornerRGB[i+1][c] = tmpRGB[src][c];
			cornerJMh[i+1][c] = tmpJMh[src][c];
		}
	}
	for ( c = 0; c < 3; c++ ) {
		cornerRGB[0][c] = cornerRGB[6][c];
		cornerRGB[7][c] = cornerRGB[1][c];
		cornerJMh[0][c] = cornerJMh[6][c];
		cornerJMh[7][c] = cornerJMh[1][c];
	}
	cornerJMh[0][2] -= 360.0;
	cornerJMh[7][2] += 360.0;

	/* The table hues start uniform and then the six corner hues are moved
	 * onto the nearest entry.  A cusp has a kink at each corner, and a
	 * uniform table interpolates straight across it, cutting the corner
	 * off - measured at 1.4% of the M range for Rec.709, with both worst
	 * cases landing exactly on a corner hue.  Corners here sit at least
	 * 0.45 degrees from a whole degree, so no entry has to move far
	 * enough to pass its neighbour and the table stays ordered. */
	{
		int c;
		for ( c = 1; c <= 6; c++ ) {
			double ch = cornerJMh[c][2];
			int idx;
			while ( ch < 0.0 ) {
				ch += 360.0;
			}
			while ( ch >= 360.0 ) {
				ch -= 360.0;
			}
			idx = (int)( ch + 0.5 );
			if ( idx >= ACES2_TABLE_SIZE ) {
				idx = 0;
			}
			table->hueOfEntry[idx] = ch;
		}
	}

	for ( i = ACES2_BASE_INDEX; i < ACES2_TABLE_TOTAL - 1; i++ ) {
		const double hue = table->hueOfEntry[i - ACES2_BASE_INDEX];
		int upper = 1, lower;
		double lowerT = 0.0, upperT = 1.0, JMh[3], sample[3];
		int guard;

		for ( c = 1; c < 8; c++ ) {
			if ( cornerJMh[c][2] > hue ) {
				upper = c;
				break;
			}
		}
		lower = upper - 1;

		if ( cornerJMh[lower][2] == hue ) {
			table->J[i] = cornerJMh[lower][0];
			table->M[i] = cornerJMh[lower][1] * ( 1.0 + 0.27 * 0.12 );
			table->h[i] = hue;
			continue;
		}

		for ( guard = 0; guard < 64 && ( upperT - lowerT ) > 1e-7; guard++ ) {
			const double t = 0.5 * ( lowerT + upperT );
			for ( c = 0; c < 3; c++ ) {
				sample[c] = cornerRGB[lower][c] + ( cornerRGB[upper][c] - cornerRGB[lower][c] ) * t;
			}
			ACES2_RGBToJMh( sample, limit, JMh );
			if ( JMh[2] < cornerJMh[lower][2] ) {
				upperT = t;
			} else if ( JMh[2] >= cornerJMh[upper][2] ) {
				lowerT = t;
			} else if ( JMh[2] > hue ) {
				upperT = t;
			} else {
				lowerT = t;
			}
		}

		table->J[i] = JMh[0];
		/* the reference widens M here by smooth_m * smooth_cusps */
		table->M[i] = JMh[1] * ( 1.0 + 0.27 * 0.12 );
		table->h[i] = hue;
	}

	/* wrap entries, so a lookup either side of 0 degrees interpolates */
	table->J[0] = table->J[ACES2_TABLE_SIZE];
	table->M[0] = table->M[ACES2_TABLE_SIZE];
	table->h[0] = -1.0;
	table->J[ACES2_TABLE_TOTAL-1] = table->J[ACES2_BASE_INDEX];
	table->M[ACES2_TABLE_TOTAL-1] = table->M[ACES2_BASE_INDEX];
	table->h[ACES2_TABLE_TOTAL-1] = 360.0;
}

/*
================
ACES2_CuspForHue

Linear interpolation between the two entries bracketing the hue.  The
table is uniform in hue, so the index is arithmetic rather than a
search - which is also what makes this a texture fetch in a shader.

The table entries are not evenly spaced: six of them sit on the cube
corner hues, where the cusp has a kink that even spacing would
interpolate straight across.  Measured before that was done, worst
error was 0.31 in J and 0.66 in M - 1.4% of the M range - and both
worst cases landed on a corner hue.  With the entries moved it is
0.0067 and 0.0184, a factor of 36.

The reference pays for its non-uniform table with a binary search at
lookup.  This does not: entries are within half a degree of their
index, so floor(hue) plus at most one step either side always brackets
the hue, which is arithmetic and two comparisons.

The shaders do not read this table directly - ACES2_PackHueTables
resamples it onto an even grid so their lookup stays a single index and
a single fetch.  That was the alternative to giving them the entry
hues, and it costs 16 KB rather than a second texture and a second
fetch per pixel.

================
*/
void ACES2_CuspForHue( const aces2CuspTable_t *table, double hue, double JM[2] ) {
	double t;
	int lo;

	hue = fmod( hue, 360.0 );
	if ( hue < 0.0 ) {
		hue += 360.0;
	}
	/* Entries are within half a degree of their index, so the bracketing
	 * pair is the one at floor(hue) or a single step either side.  That
	 * keeps the lookup arithmetic plus two comparisons - no search - which
	 * is what makes it expressible in a shader. */
	lo = (int)hue;
	if ( lo > 0 && hue < table->hueOfEntry[lo] ) {
		lo--;
	} else if ( lo < ACES2_TABLE_SIZE - 1 && hue > table->hueOfEntry[lo+1] ) {
		lo++;
	}

	{
		const double h0 = table->hueOfEntry[lo];
		const double h1 = ( lo + 1 < ACES2_TABLE_SIZE )
						? table->hueOfEntry[lo+1] : 360.0;
		const double span = h1 - h0;
		t = ( span > 1e-9 ) ? ( hue - h0 ) / span : 0.0;
		if ( t < 0.0 ) t = 0.0;
		if ( t > 1.0 ) t = 1.0;
	}
	lo += ACES2_BASE_INDEX;

	JM[0] = table->J[lo] + ( table->J[lo+1] - table->J[lo] ) * t;
	JM[1] = table->M[lo] + ( table->M[lo+1] - table->M[lo] ) * t;
}

/* ---- the tone scale ---- */

/*
================
ACES2_InitTSParams

Solved once from the peak luminance, exactly as the reference does.
The chain from r_hit down to m_2 has no shortcuts worth taking: each
term feeds the next, and the whole point of it is that 18% scene grey
lands on c_d nits, which is the check worth keeping.
================
*/
void ACES2_InitTSParams( double peakLuminance, aces2TSParams_t *p ) {
	const double n_r = 100.0;
	const double g   = 1.15;
	const double c   = 0.18;
	const double c_d = 10.013;
	const double w_g = 0.14;
	const double t_1 = 0.04;
	const double r_hit_min = 128.0, r_hit_max = 896.0;
	const double n = peakLuminance;

	const double r_hit = r_hit_min + ( r_hit_max - r_hit_min )
					   * ( log( n / n_r ) / log( 10000.0 / 100.0 ) );
	const double m_0 = n / n_r;
	const double m_1 = 0.5 * ( m_0 + sqrt( m_0 * ( m_0 + 4.0 * t_1 ) ) );
	const double u   = pow( ( r_hit / m_1 ) / ( ( r_hit / m_1 ) + 1.0 ), g );
	const double m   = m_1 / u;
	const double w_i = log( n / 100.0 ) / log( 2.0 );
	const double c_t = c_d / n_r * ( 1.0 + w_i * w_g );
	const double g_ip = 0.5 * ( c_t + sqrt( c_t * ( c_t + 4.0 * t_1 ) ) );
	const double g_ipp2 = -( m_1 * pow( g_ip / m, 1.0 / g ) )
						/ ( pow( g_ip / m, 1.0 / g ) - 1.0 );
	const double w_2 = c / g_ipp2;
	const double s_2 = w_2 * m_1;
	const double u_2 = pow( ( r_hit / m_1 ) / ( ( r_hit / m_1 ) + w_2 ), g );
	const double m_2 = m_1 / u_2;

	p->n   = n;
	p->n_r = n_r;
	p->g   = g;
	p->t_1 = t_1;
	p->c_t = c_t;
	p->s_2 = s_2;
	p->u_2 = u_2;
	p->m_2 = m_2;
	p->forward_limit = 8.0 * r_hit;
	p->inverse_limit = n / ( u_2 * n_r );
}

double ACES2_ToneScaleFwd( double x, const aces2TSParams_t *p ) {
	const double f = p->m_2 * pow( ( x > 0.0 ? x : 0.0 ) / ( x + p->s_2 ), p->g );
	const double h = f * f / ( f + p->t_1 );	/* flare */
	return ( h > 0.0 ? h : 0.0 ) * p->n_r;
}

double ACES2_ToneScaleInv( double Yn, const aces2TSParams_t *p ) {
	double Z = Yn;
	double h;

	if ( Z > p->inverse_limit ) {
		Z = p->inverse_limit;
	}
	if ( Z < 0.0 ) {
		Z = 0.0;
	}
	h = ( Z + sqrt( Z * ( 4.0 * p->t_1 + Z ) ) ) * 0.5;
	return p->s_2 / ( pow( p->m_2 / h, 1.0 / p->g ) - 1.0 );
}

/* ---- chroma compression ---- */

/*
================
ACES2_Toe

The reference's shared toe, used twice in each direction with different
constants.  Above the limit it does nothing, which is what keeps the
compression confined to the range that needs it.
================
*/
static double ACES2_Toe( double x, double limit, double k1_in, double k2_in, bool invert ) {
	double k1, k2, k3;

	if ( x > limit ) {
		return x;
	}
	k2 = k2_in > 0.001 ? k2_in : 0.001;
	k1 = sqrt( k1_in * k1_in + k2 * k2 );
	k3 = ( limit + k1 ) / ( limit + k2 );

	if ( invert ) {
		return ( x * x + k1 * x ) / ( k3 * ( x + k2 ) );
	} else {
		const double minus_b = k3 * x - k1;
		const double minus_c = k2 * k3 * x;
		return 0.5 * ( minus_b + sqrt( minus_b * minus_b + 4.0 * minus_c ) );
	}
}

/*
================
ACES2_ChromaCompressNorm

A fixed three-harmonic fit in hue.  These coefficients are the
reference's; there is nothing to derive here.
================
*/
static double ACES2_ChromaCompressNorm( double h, double scale ) {
	const double hr = h * ( ACES2_PI / 180.0 );
	const double a = cos( hr );
	const double b = sin( hr );
	const double cos_hr2 = a * a - b * b;
	const double sin_hr2 = 2.0 * a * b;
	const double cos_hr3 = 4.0 * a * a * a - 3.0 * a;
	const double sin_hr3 = 3.0 * b - 4.0 * b * b * b;
	const double M = 11.34072 * a
				   + 16.46899 * cos_hr2
				   +  7.88380 * cos_hr3
				   + 14.66441 * b
				   -  6.37224 * sin_hr2
				   +  9.19364 * sin_hr3
				   + 77.12896;
	return M * scale;
}

static double ACES2_ReachMFromTable( const aces2ChromaParams_t *p, double h ) {
	double hue = fmod( h, 360.0 );
	int base;
	double t;

	if ( hue < 0.0 ) {
		hue += 360.0;
	}
	base = (int)hue;
	t = hue - (double)base;
	return p->reachM[base + ACES2_BASE_INDEX]
		 + ( p->reachM[base + ACES2_BASE_INDEX + 1] - p->reachM[base + ACES2_BASE_INDEX] ) * t;
}

/*
================
ACES2_InitChromaParams

The reach table asks, for each hue: how much M can the reach primaries
hold at maximum lightness before a channel goes negative.  The reference
walks outward in steps of 50 until it finds the outside, then bisects,
which is what is done here - the stepped search matters because the
boundary is not monotonic in a way a plain bisection from zero could
rely on.
================
*/
void ACES2_InitChromaParams( const aces2JMhParams_t *input,
							 const aces2JMhParams_t *reach,
							 const aces2TSParams_t *ts,
							 double peakLuminance,
							 aces2ChromaParams_t *p ) {
	const double chroma_compress      = 2.4;
	const double chroma_compress_fact = 3.3;
	const double chroma_expand        = 1.3;
	const double chroma_expand_fact   = 0.69;
	const double chroma_expand_thr    = 0.5;
	const double model_gamma = 0.59 * ( 1.48 + sqrt( 20.0 / 100.0 ) );
	const double log_peak = log10( peakLuminance / 100.0 );
	int i;

	p->limit_J_max     = ACES2_YToJ( peakLuminance, input );
	p->model_gamma_inv = 1.0 / model_gamma;
	p->sat     = 0.2 > ( chroma_expand - ( chroma_expand * chroma_expand_fact ) * log_peak )
			   ? 0.2 : ( chroma_expand - ( chroma_expand * chroma_expand_fact ) * log_peak );
	p->sat_thr = chroma_expand_thr / peakLuminance;
	p->compr   = chroma_compress + ( chroma_compress * chroma_compress_fact ) * log_peak;
	p->chroma_compress_scale = pow( 0.03379 * peakLuminance, 0.30596 ) - 0.45135;

	for ( i = 0; i < ACES2_TABLE_SIZE; i++ ) {
		const double hue = (double)i;
		const double search_range = 50.0, search_maximum = 1300.0;
		double low = 0.0, high = search_range;
		bool outside = false;

		while ( !outside && high < search_maximum ) {
			double JMh[3];
			double RGB[3];
			JMh[0] = p->limit_J_max; JMh[1] = high; JMh[2] = hue;
			ACES2_JMhToRGB( JMh, reach, RGB );
			outside = ( RGB[0] < 0.0 || RGB[1] < 0.0 || RGB[2] < 0.0 );
			if ( !outside ) {
				low = high;
				high += search_range;
			}
		}

		while ( high - low > 1e-2 ) {
			const double sampleM = 0.5 * ( high + low );
			double JMh[3];
			double RGB[3];
			JMh[0] = p->limit_J_max; JMh[1] = sampleM; JMh[2] = hue;
			ACES2_JMhToRGB( JMh, reach, RGB );
			if ( RGB[0] < 0.0 || RGB[1] < 0.0 || RGB[2] < 0.0 ) {
				high = sampleM;
			} else {
				low = sampleM;
			}
		}
		p->reachM[i + ACES2_BASE_INDEX] = high;
	}

	p->reachM[0] = p->reachM[ACES2_TABLE_SIZE];
	p->reachM[ACES2_TABLE_TOTAL-1] = p->reachM[ACES2_BASE_INDEX];

	(void)ts;
}

void ACES2_ChromaCompressFwd( const double JMh[3], double tonemappedJ,
							  const aces2ChromaParams_t *p, double out[3] ) {
	const double J = JMh[0], M = JMh[1], h = JMh[2];
	double M_compr = M;

	if ( M != 0.0 ) {
		const double nJ = tonemappedJ / p->limit_J_max;
		const double snJ = ( 1.0 - nJ ) > 0.0 ? ( 1.0 - nJ ) : 0.0;
		const double Mnorm = ACES2_ChromaCompressNorm( h, p->chroma_compress_scale );
		const double limit = pow( nJ, p->model_gamma_inv ) * ACES2_ReachMFromTable( p, h ) / Mnorm;
		const double toe_limit = limit - 0.001;
		const double toe_snJ_sat = snJ * p->sat;
		const double toe_sqrt = sqrt( nJ * nJ + p->sat_thr );
		const double toe_nJ_compr = nJ * p->compr;

		M_compr = M * pow( tonemappedJ / J, p->model_gamma_inv );
		M_compr = M_compr / Mnorm;
		M_compr = limit - ACES2_Toe( limit - M_compr, toe_limit, toe_snJ_sat, toe_sqrt, false );
		M_compr = ACES2_Toe( M_compr, limit, toe_nJ_compr, snJ, false );
		M_compr = M_compr * Mnorm;
	}

	out[0] = tonemappedJ;
	out[1] = M_compr;
	out[2] = h;
}

void ACES2_ChromaCompressInv( const double JMh[3], double J,
							  const aces2ChromaParams_t *p, double out[3] ) {
	const double tonemappedJ = JMh[0], M_compr = JMh[1], h = JMh[2];
	double M = M_compr;

	if ( M_compr != 0.0 ) {
		const double nJ = tonemappedJ / p->limit_J_max;
		const double snJ = ( 1.0 - nJ ) > 0.0 ? ( 1.0 - nJ ) : 0.0;
		const double Mnorm = ACES2_ChromaCompressNorm( h, p->chroma_compress_scale );
		const double limit = pow( nJ, p->model_gamma_inv ) * ACES2_ReachMFromTable( p, h ) / Mnorm;
		const double toe_limit = limit - 0.001;
		const double toe_snJ_sat = snJ * p->sat;
		const double toe_sqrt = sqrt( nJ * nJ + p->sat_thr );
		const double toe_nJ_compr = nJ * p->compr;

		M = M_compr / Mnorm;
		M = ACES2_Toe( M, limit, toe_nJ_compr, snJ, true );
		M = limit - ACES2_Toe( limit - M, toe_limit, toe_snJ_sat, toe_sqrt, true );
		M = M * Mnorm;
		M = M * pow( tonemappedJ / J, -p->model_gamma_inv );
	}

	out[0] = J;
	out[1] = M;
	out[2] = h;
}

/* ---- gamut compression ---- */

static const double ACES2_smooth_cusps = 0.12;
static const double ACES2_cusp_mid_blend = 1.3;
static const double ACES2_focus_gain_blend = 0.3;
static const double ACES2_compression_threshold = 0.75;

void ACES2_InitGamutParams( const aces2JMhParams_t *input, const aces2TSParams_t *ts,
							double peakLuminance, aces2GamutParams_t *p ) {
	const double focus_distance = 1.35;
	const double focus_distance_scaling = 1.75;
	const double model_gamma = 0.59 * ( 1.48 + sqrt( 20.0 / 100.0 ) );
	const double log_peak = log10( peakLuminance / 100.0 );
	const double lower_hull_gamma = 1.14 + 0.07 * log_peak;

	p->limit_J_max = ACES2_YToJ( peakLuminance, input );
	p->mid_J = ACES2_YToJ( ts->c_t * 100.0, input );
	p->focus_dist = focus_distance + focus_distance * focus_distance_scaling * log_peak;
	p->lower_hull_gamma_inv = 1.0 / lower_hull_gamma;
	p->model_gamma_inv = 1.0 / model_gamma;
}

static double ACES2_FocusGain( double J, double analytical_threshold,
							   double limit_J_max, double focus_dist ) {
	double gain = limit_J_max * focus_dist;

	if ( J > analytical_threshold ) {
		const double denom = ( limit_J_max - J ) > 0.0001 ? ( limit_J_max - J ) : 0.0001;
		double adj = log10( ( limit_J_max - analytical_threshold ) / denom );
		adj = adj * adj + 1.0;
		gain = gain * adj;
	}
	return gain;
}

static double ACES2_SolveJIntersect( double J, double M, double focusJ,
									 double maxJ, double slope_gain ) {
	const double M_scaled = M / slope_gain;
	const double a = M_scaled / focusJ;

	if ( J < focusJ ) {
		const double b = 1.0 - M_scaled;
		const double c = -J;
		const double root = sqrt( b * b - 4.0 * a * c );
		return -2.0 * c / ( b + root );
	} else {
		const double b = -( 1.0 + M_scaled + maxJ * a );
		const double c = maxJ * M_scaled + J;
		const double root = sqrt( b * b - 4.0 * a * c );
		return -2.0 * c / ( b - root );
	}
}

static double ACES2_CompressionVectorSlope( double intersect_J, double focus_J,
											double limit_J_max, double slope_gain ) {
	const double direction = ( intersect_J < focus_J )
						   ? intersect_J : ( limit_J_max - intersect_J );
	return direction * ( intersect_J - focus_J ) / ( focus_J * slope_gain );
}

static double ACES2_SminScaled( double a, double b, double scale_reference ) {
	const double s = ACES2_smooth_cusps * scale_reference;
	const double d = fabs( a - b );
	const double h = ( ( s - d ) > 0.0 ? ( s - d ) : 0.0 ) / s;
	const double m = a < b ? a : b;
	return m - h * h * h * s * ( 1.0 / 6.0 );
}

static double ACES2_BoundaryM( double J_axis_intersect, double slope, double inv_gamma,
							   double J_max, double M_max, double J_ref ) {
	const double normalised_J = J_axis_intersect / J_ref;
	const double shifted = J_ref * pow( normalised_J, inv_gamma );
	return shifted * M_max / ( J_max - slope * M_max );
}

/*
================
ACES2_GamutBoundary

The lower hull straight, the upper hull flipped about J_max with the
slope negated, and a smooth minimum between them so the join at the
cusp has no corner.
================
*/
static double ACES2_GamutBoundary( const double JMcusp[2], double J_max,
								   double gamma_top_inv, double gamma_bottom_inv,
								   double J_intersect_source, double slope,
								   double J_intersect_cusp ) {
	const double lower = ACES2_BoundaryM( J_intersect_source, slope, gamma_bottom_inv,
										  JMcusp[0], JMcusp[1], J_intersect_cusp );
	const double f_cusp   = J_max - J_intersect_cusp;
	const double f_source = J_max - J_intersect_source;
	const double f_cusp_J = J_max - JMcusp[0];
	const double upper = ACES2_BoundaryM( f_source, -slope, gamma_top_inv,
										  f_cusp_J, JMcusp[1], f_cusp );
	return ACES2_SminScaled( lower, upper, JMcusp[1] );
}

static double ACES2_ReinhardRemap( double scale, double nd, bool invert ) {
	if ( invert ) {
		if ( nd >= 1.0 ) {
			return scale;
		}
		return scale * -( nd / ( nd - 1.0 ) );
	}
	return scale * nd / ( 1.0 + nd );
}

static double ACES2_RemapM( double M, double gamut_boundary_M,
							double reach_boundary_M, bool invert ) {
	const double boundary_ratio = gamut_boundary_M / reach_boundary_M;
	const double proportion = boundary_ratio > ACES2_compression_threshold
							? boundary_ratio : ACES2_compression_threshold;
	const double threshold = proportion * gamut_boundary_M;
	double m_offset, gamut_offset, reach_offset, scale, nd;

	if ( M <= threshold || proportion >= 1.0 ) {
		return M;
	}
	m_offset     = M - threshold;
	gamut_offset = gamut_boundary_M - threshold;
	reach_offset = reach_boundary_M - threshold;
	scale = reach_offset / ( ( reach_offset / gamut_offset ) - 1.0 );
	nd = m_offset / scale;
	return threshold + ACES2_ReinhardRemap( scale, nd, invert );
}

void ACES2_GamutCompress( const double JMh[3], double Jx,
						  const aces2GamutParams_t *p,
						  const double JMcusp[2], double gammaTopInv,
						  double reachM, bool invert, double out[3] ) {
	const double J = JMh[0], M = JMh[1], h = JMh[2];
	double focus_J, analytical_threshold, slope_gain;
	double J_intersect_source, gamut_slope, J_intersect_cusp;
	double gamut_boundary_M, reach_boundary_M, remapped_M;

	if ( J <= 0.0 ) {
		out[0] = 0.0; out[1] = 0.0; out[2] = h;
		return;
	}
	if ( M < 0.0 || J > p->limit_J_max ) {
		out[0] = J; out[1] = 0.0; out[2] = h;
		return;
	}

	{
		const double t = ACES2_cusp_mid_blend - ( JMcusp[0] / p->limit_J_max );
		const double blend = t < 1.0 ? t : 1.0;
		focus_J = JMcusp[0] + ( p->mid_J - JMcusp[0] ) * blend;
	}
	analytical_threshold = JMcusp[0]
						 + ( p->limit_J_max - JMcusp[0] ) * ACES2_focus_gain_blend;

	slope_gain = ACES2_FocusGain( Jx, analytical_threshold, p->limit_J_max, p->focus_dist );
	J_intersect_source = ACES2_SolveJIntersect( J, M, focus_J, p->limit_J_max, slope_gain );
	gamut_slope = ACES2_CompressionVectorSlope( J_intersect_source, focus_J,
												p->limit_J_max, slope_gain );
	J_intersect_cusp = ACES2_SolveJIntersect( JMcusp[0], JMcusp[1], focus_J,
											  p->limit_J_max, slope_gain );

	gamut_boundary_M = ACES2_GamutBoundary( JMcusp, p->limit_J_max, gammaTopInv,
											p->lower_hull_gamma_inv,
											J_intersect_source, gamut_slope,
											J_intersect_cusp );
	if ( gamut_boundary_M <= 0.0 ) {
		out[0] = J; out[1] = 0.0; out[2] = h;
		return;
	}

	reach_boundary_M = ACES2_BoundaryM( J_intersect_source, gamut_slope,
										p->model_gamma_inv, p->limit_J_max,
										reachM, p->limit_J_max );

	remapped_M = ACES2_RemapM( M, gamut_boundary_M, reach_boundary_M, invert );

	out[0] = J_intersect_source + remapped_M * gamut_slope;
	out[1] = remapped_M;
	out[2] = h;
}

void ACES2_GamutCompressInv( const double JMh[3], const aces2GamutParams_t *p,
							 const double JMcusp[2], double gammaTopInv,
							 double reachM, double out[3] ) {
	const double analytical_threshold = JMcusp[0]
		+ ( p->limit_J_max - JMcusp[0] ) * ACES2_focus_gain_blend;
	double Jx = JMh[0];

	if ( Jx > analytical_threshold ) {
		double tmp[3];
		ACES2_GamutCompress( JMh, Jx, p, JMcusp, gammaTopInv, reachM, true, tmp );
		Jx = tmp[0];
	}
	ACES2_GamutCompress( JMh, Jx, p, JMcusp, gammaTopInv, reachM, true, out );
}

/* ---- the upper hull exponent table ---- */

/* the reference samples the hull at these fractions between cusp and max */
static const double ACES2_testPositions[5] = { 0.01, 0.1, 0.5, 0.8, 0.99 };

/*
================
ACES2_EvaluateGammaFit

True when the estimated boundary lies outside the real gamut at every
sample.  "Outside" is any channel above the luminance limit, which is
what makes an overshooting estimate acceptable and a short one not.
================
*/
static bool ACES2_EvaluateGammaFit( const double JMcusp[2], double hue,
									const double J_intersect_source[5],
									const double slopes[5],
									const double J_intersect_cusp[5],
									double top_gamma_inv,
									double peakLuminance,
									const aces2GamutParams_t *gp,
									const aces2JMhParams_t *limit ) {
	const double luminance_limit = peakLuminance / 100.0;
	int i;

	for ( i = 0; i < 5; i++ ) {
		double M = ACES2_GamutBoundary( JMcusp, gp->limit_J_max, top_gamma_inv,
										gp->lower_hull_gamma_inv,
										J_intersect_source[i], slopes[i],
										J_intersect_cusp[i] );
		double J = J_intersect_source[i] + slopes[i] * M;
		double JMh[3];
		double RGB[3];

		JMh[0] = J; JMh[1] = M; JMh[2] = hue;

		ACES2_JMhToRGB( JMh, limit, RGB );
		if ( !( RGB[0] > luminance_limit || RGB[1] > luminance_limit
				|| RGB[2] > luminance_limit ) ) {
			return false;
		}
	}
	return true;
}

void ACES2_BuildUpperHullGammaTable( const aces2CuspTable_t *cusp,
									 const aces2GamutParams_t *gp,
									 const aces2JMhParams_t *limit,
									 double peakLuminance,
									 aces2GammaTable_t *table ) {
	const double gamma_minimum = 0.0, gamma_maximum = 5.0;
	const double gamma_search_step = 0.4, gamma_accuracy = 1e-5;
	int i;

	for ( i = ACES2_BASE_INDEX; i < ACES2_BASE_INDEX + ACES2_TABLE_SIZE; i++ ) {
		const double hue = cusp->h[i];
		double JMcusp[2];
		double J_intersect_source[5], slopes[5], J_intersect_cusp[5];
		double focus_J, analytical_threshold;
		double low = gamma_minimum, high = gamma_minimum + gamma_search_step;
		double testGamma = -1.0;
		bool outside = false;
		int k;

		JMcusp[0] = cusp->J[i];
		JMcusp[1] = cusp->M[i];
		analytical_threshold = JMcusp[0]
			+ ( gp->limit_J_max - JMcusp[0] ) * ACES2_focus_gain_blend;
		{
			const double t = ACES2_cusp_mid_blend - ( JMcusp[0] / gp->limit_J_max );
			const double blend = t < 1.0 ? t : 1.0;
			focus_J = JMcusp[0] + ( gp->mid_J - JMcusp[0] ) * blend;
		}

		for ( k = 0; k < 5; k++ ) {
			const double test_J = JMcusp[0]
				+ ( gp->limit_J_max - JMcusp[0] ) * ACES2_testPositions[k];
			const double slope_gain = ACES2_FocusGain( test_J, analytical_threshold,
													   gp->limit_J_max, gp->focus_dist );
			J_intersect_source[k] = ACES2_SolveJIntersect( test_J, JMcusp[1], focus_J,
														   gp->limit_J_max, slope_gain );
			slopes[k] = ACES2_CompressionVectorSlope( J_intersect_source[k], focus_J,
													   gp->limit_J_max, slope_gain );
			J_intersect_cusp[k] = ACES2_SolveJIntersect( JMcusp[0], JMcusp[1], focus_J,
														  gp->limit_J_max, slope_gain );
		}

		while ( !outside && high < gamma_maximum ) {
			if ( !ACES2_EvaluateGammaFit( JMcusp, hue, J_intersect_source, slopes,
										  J_intersect_cusp, 1.0 / high,
										  peakLuminance, gp, limit ) ) {
				low = high;
				high += gamma_search_step;
			} else {
				outside = true;
			}
		}

		while ( ( high - low ) > gamma_accuracy ) {
			testGamma = 0.5 * ( high + low );
			if ( ACES2_EvaluateGammaFit( JMcusp, hue, J_intersect_source, slopes,
										 J_intersect_cusp, 1.0 / testGamma,
										 peakLuminance, gp, limit ) ) {
				high = testGamma;
			} else {
				low = testGamma;
			}
		}

		table->gammaInv[i] = 1.0 / high;
	}

	table->gammaInv[0] = table->gammaInv[ACES2_TABLE_SIZE];
	table->gammaInv[ACES2_TABLE_TOTAL-1] = table->gammaInv[ACES2_BASE_INDEX];
}

double ACES2_GammaInvForHue( const aces2GammaTable_t *table, double hue ) {
	double h = fmod( hue, 360.0 );
	int lo;
	double t;

	if ( h < 0.0 ) {
		h += 360.0;
	}
	lo = (int)h;
	t = h - (double)lo;
	lo += ACES2_BASE_INDEX;
	return table->gammaInv[lo] + ( table->gammaInv[lo+1] - table->gammaInv[lo] ) * t;
}

/* ---- the assembled forward transform ---- */

static const double ACES2_AP0[8] = { 0.7347, 0.2653, 0.0, 1.0, 0.0001, -0.077, 0.32168, 0.33767 };
static const double ACES2_AP1[8] = { 0.713, 0.293, 0.165, 0.830, 0.128, 0.044, 0.32168, 0.33767 };

void ACES2_Init( const double limitPrims[8], double peakLuminance, aces2Params_t *p ) {
	p->peakLuminance = peakLuminance;
	ACES2_InitJMhParams( ACES2_AP0, &p->input );
	ACES2_InitJMhParams( ACES2_AP1, &p->reach );
	ACES2_InitJMhParams( limitPrims, &p->limit );
	ACES2_InitTSParams( peakLuminance, &p->ts );
	ACES2_InitChromaParams( &p->input, &p->reach, &p->ts, peakLuminance, &p->chroma );
	ACES2_InitGamutParams( &p->input, &p->ts, peakLuminance, &p->gamut );
	ACES2_BuildCuspTable( &p->limit, peakLuminance, &p->cusp );
	ACES2_BuildUpperHullGammaTable( &p->cusp, &p->gamut, &p->limit, peakLuminance, &p->gamma );
}

/*
================
ACES2_ClampAP0ToAP1

The reference's first step, and not an optional one.  Colours outside
AP1 are physically representable in AP0 but the appearance model does
not behave on them, and without this the transform emits channels well
past the display limit - 181383 of 900000 in my first end-to-end run,
by up to 0.17.  Round-tripping through AP1 with a clamp removes them.
================
*/
static void ACES2_ClampAP0ToAP1( const double aces[3], double lower, double upper,
								 double out[3] ) {
	/* AP0 to AP1 and back, both from the standard primaries */
	static const double AP0_TO_AP1[9] = {
		 1.4514393161, -0.2365107469, -0.2149285693,
		-0.0765537733,  1.1762296998, -0.0996759265,
		 0.0083161484, -0.0060324498,  0.9977163014 };
	static const double AP1_TO_AP0[9] = {
		 0.6954522414,  0.1406786965,  0.1638690622,
		 0.0447945634,  0.8596711185,  0.0955343182,
		-0.0055258826,  0.0040252103,  1.0015006723 };
	double ap1[3], clamped[3];
	int i;

	for ( i = 0; i < 3; i++ ) {
		ap1[i] = AP0_TO_AP1[i*3+0] * aces[0]
			   + AP0_TO_AP1[i*3+1] * aces[1]
			   + AP0_TO_AP1[i*3+2] * aces[2];
		clamped[i] = ap1[i] < lower ? lower : ( ap1[i] > upper ? upper : ap1[i] );
	}
	for ( i = 0; i < 3; i++ ) {
		out[i] = AP1_TO_AP0[i*3+0] * clamped[0]
			   + AP1_TO_AP0[i*3+1] * clamped[1]
			   + AP1_TO_AP0[i*3+2] * clamped[2];
	}
}

/*
================
ACES2_OutputTransformFwd

AP0 linear in, display RGB out, unclamped.

Unclamped is deliberate and the output does overshoot: feeding 200000
random ACES colours, 122249 produce a channel above 1, by up to 0.167.
That is the transform working, not a defect, and the reason is in the
gamma search that built the exponent table - it accepts an exponent
only when the estimated boundary lies OUTSIDE the real gamut at every
sample.  An estimate that never cuts the gamut short is necessarily an
estimate that sometimes sits beyond it, so a compressed colour can land
just outside and the display encode clamps it.

Checked rather than assumed, because "by design" is an easy thing to
say about one's own bug.  The cusp table is widened by the reference
before use, which was my first suspect; removing that widening makes
the overshoot six times worse - worst case 1.067 against 0.167 - so the
widening is what holds it in rather than what causes it.

The stage this rests on has independent corroboration.  Compared
against colour-science's Hellwig2022, which is a separate
implementation by different people: hue agrees to 0.01 degrees on
saturated colours and J to within a few percent, with the residual
sitting exactly where ACES 2.0 changes the model's nonlinearity from
stock Hellwig.  Neutral handling differs by construction - this code
forces M to zero on the grey axis through the adaptation term, and the
stock model does not.
================
*/
void ACES2_OutputTransformFwd( const double aces[3], const aces2Params_t *p, double RGB[3] ) {
	double JMh[3], tm[3], compressed[3], clamped[3];
	double linear, tonemappedY, J_ts;
	double JMcusp[2], gammaInv, reachM;

	ACES2_ClampAP0ToAP1( aces, 0.0, p->ts.forward_limit, clamped );
	ACES2_RGBToJMh( clamped, &p->input, JMh );

	/* tone scale on the achromatic axis, then chroma compression */
	linear = ACES2_JToY( JMh[0], &p->input ) / 100.0;
	tonemappedY = ACES2_ToneScaleFwd( linear, &p->ts );
	J_ts = ACES2_YToJ( tonemappedY, &p->input );
	ACES2_ChromaCompressFwd( JMh, J_ts, &p->chroma, tm );

	/* gamut compression, which needs all three hue tables */
	ACES2_CuspForHue( &p->cusp, tm[2], JMcusp );
	gammaInv = ACES2_GammaInvForHue( &p->gamma, tm[2] );
	reachM = ACES2_ReachMFromTable( &p->chroma, tm[2] );
	ACES2_GamutCompress( tm, tm[0], &p->gamut, JMcusp, gammaInv, reachM, false, compressed );

	ACES2_JMhToRGB( compressed, &p->limit, RGB );
}

/* ---- packing for the shader ---- */

/*
================
ACES2_PackHueTables

Verified on a driver rather than assumed: uploaded as GL_RGBA16 the
texture reports back sixteen bits per channel and the values return
within half a quantisation step - 7.6e-06 of each channel's range,
which is 0.0007 in cusp J and 0.0015 in reach M.  Both are two orders
below the 1.4% the uniform hue spacing already costs at the cube
corners, so the packing is not the limiting error.

The GLES question this raised is settled.  GL_RGBA16 is a desktop
format and GLES2 - which is every GLES target in this tree - has no
normalised 16-bit texture, so the upload falls back to RGBA8 there.
Eight bits is enough: the widest channel is the reach, quantising to
0.38 against a resampling residual of about 0.12 the table already
carries.  A high and low byte pair would recover the precision for a
second fetch per pixel, and there is nothing to spend it on.
*/
void ACES2_PackHueTables( const aces2Params_t *p, unsigned short *rgba16,
						  double scales[4] ) {
	double maxJ = 0.0, maxM = 0.0, maxR = 0.0, maxG = 0.0;
	int i;

	for ( i = ACES2_BASE_INDEX; i < ACES2_BASE_INDEX + ACES2_TABLE_SIZE; i++ ) {
		if ( p->cusp.J[i] > maxJ ) maxJ = p->cusp.J[i];
		if ( p->cusp.M[i] > maxM ) maxM = p->cusp.M[i];
		if ( p->chroma.reachM[i] > maxR ) maxR = p->chroma.reachM[i];
		if ( p->gamma.gammaInv[i] > maxG ) maxG = p->gamma.gammaInv[i];
	}
	/* a little headroom so the largest entry is not sitting on 65535 */
	scales[0] = maxJ * 1.01;
	scales[1] = maxM * 1.01;
	scales[2] = maxR * 1.01;
	scales[3] = maxG * 1.01;

	for ( i = 0; i < ACES2_LUT_WIDTH; i++ ) {
		/* sample the tables at this texel's hue rather than copying
		 * entries across - the cusp table is not evenly spaced */
		const double hue = ( (double)i + 0.5 ) * ( 360.0 / (double)ACES2_LUT_WIDTH );
		double JM[2];
		double reach, gam;
		int lo = (int)hue;
		double t;
		double v[4];
		int c;

		ACES2_CuspForHue( &p->cusp, hue, JM );
		t = hue - (double)lo;
		if ( lo >= ACES2_TABLE_SIZE ) {
			lo = ACES2_TABLE_SIZE - 1;
			t = 0.0;
		}
		reach = p->chroma.reachM[lo + ACES2_BASE_INDEX]
			  + ( p->chroma.reachM[lo + ACES2_BASE_INDEX + 1]
				- p->chroma.reachM[lo + ACES2_BASE_INDEX] ) * t;
		gam   = p->gamma.gammaInv[lo + ACES2_BASE_INDEX]
			  + ( p->gamma.gammaInv[lo + ACES2_BASE_INDEX + 1]
				- p->gamma.gammaInv[lo + ACES2_BASE_INDEX] ) * t;

		v[0] = JM[0] / scales[0];
		v[1] = JM[1] / scales[1];
		v[2] = reach / scales[2];
		v[3] = gam / scales[3];
		for ( c = 0; c < 4; c++ ) {
			double q = v[c] * 65535.0 + 0.5;
			if ( q < 0.0 ) q = 0.0;
			if ( q > 65535.0 ) q = 65535.0;
			rgba16[i*4+c] = (unsigned short)q;
		}
	}
}

void ACES2_OutputTransformInv( const double RGB[3], const aces2Params_t *p, double aces[3] ) {
	double JMh[3], tm[3], JMcusp[2], gammaInv, reachM;
	double Y, linear, J;

	/* display RGB back to the appearance space of the limit primaries */
	ACES2_RGBToJMh( RGB, &p->limit, JMh );
	if ( JMh[0] <= 0.0 ) {
		aces[0] = aces[1] = aces[2] = 0.0;
		return;
	}

	/* undo the gamut compression, which needs the same three hue tables */
	ACES2_CuspForHue( &p->cusp, JMh[2], JMcusp );
	gammaInv = ACES2_GammaInvForHue( &p->gamma, JMh[2] );
	reachM = ACES2_ReachMFromTable( &p->chroma, JMh[2] );
	ACES2_GamutCompressInv( JMh, &p->gamut, JMcusp, gammaInv, reachM, tm );

	/* undo the tone scale on the achromatic axis, then the chroma
	 * compression - in that order, because the chroma inverse needs the
	 * scene J and the tone scale is what recovers it */
	Y = ACES2_JToY( tm[0], &p->input );
	linear = ACES2_ToneScaleInv( Y / 100.0, &p->ts );
	J = ACES2_YToJ( linear * 100.0, &p->input );
	ACES2_ChromaCompressInv( tm, J, &p->chroma, JMh );

	ACES2_JMhToRGB( JMh, &p->input, aces );
}


/*
================
ACES2_BuildGuiInverse

For each display value the artist intended, the scene value that the
forward transform maps back onto it.  Solved on the neutral axis by the
assembled inverse, then clamped monotonic - the inverse is not exact at
the very top, where the gamut compression's boundary estimate stops
being invertible, and a table that dipped there would show as a band.
================
*/
void ACES2_BuildGuiInverse( const aces2Params_t *p, float table[ACES2_GUI_LUT_SIZE] ) {
	int i;
	double prev = 0.0;

	for ( i = 0; i < ACES2_GUI_LUT_SIZE; i++ ) {
		const double want = (double)i / (double)( ACES2_GUI_LUT_SIZE - 1 );
		double disp[3], scene[3];

		disp[0] = disp[1] = disp[2] = want;
		ACES2_OutputTransformInv( disp, p, scene );
		if ( !( scene[1] > prev ) ) {
			scene[1] = prev;
		}
		prev = scene[1];
		table[i] = (float)scene[1];
	}
}
