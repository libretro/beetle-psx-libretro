/*
===========================================================================

ACES 2.0 output transform, stage one: Hellwig2022 JMh.

Ported from the Academy reference CTL.  CPU only - the shader backends
consume hue tables built from this, not this code.

===========================================================================
*/

#ifndef HDR_ACES2_H__
#define HDR_ACES2_H__

#include <boolean.h>

typedef struct {
	double	RGB_to_CAM16_c[9];
	double	CAM16_c_to_RGB[9];
	double	cone_to_Aab[9];
	double	Aab_to_cone[9];
	double	F_L_n;
	double	cz;
	double	inv_cz;
	double	A_w_J;
	double	inv_A_w_J;
} aces2JMhParams_t;

/* prims is red xy, green xy, blue xy, white xy - the CTL's Chromaticities */
void	ACES2_SetSurround( int mode );   /* 0 dark, 1 dim (default), 2 average */
void	ACES2_InitJMhParams( const double prims[8], aces2JMhParams_t *p );
/* linear RGB in one set of primaries to another, through XYZ: a 3x3 the
 * CTL way (row vector times matrix), which is also the column-major
 * order a GL mat3 upload takes.  False when the chromaticities are
 * degenerate. */
bool	ACES2_PrimariesMatrix( const double from[8], const double to[8], double out[9] );

void	ACES2_RGBToJMh( const double RGB[3], const aces2JMhParams_t *p, double JMh[3] );
void	ACES2_JMhToRGB( const double JMh[3], const aces2JMhParams_t *p, double RGB[3] );

/* The achromatic axis on its own.
 *
 * Y here is on the reference's luminance scale, where diffuse white is
 * 100 and not 1 - the parameters are built with XYZ_w scaled by
 * ref_luminance.  Passing a 0..1 value gives a plausible-looking answer
 * that is wrong by about 89% in J, which is exactly what it did to me
 * the first time. */
double	ACES2_JToY( double J, const aces2JMhParams_t *p );
double	ACES2_YToJ( double Y, const aces2JMhParams_t *p );

#endif /* !HDR_ACES2_H__ */

/*
===========================================================================

The display gamut cusp table.

For each hue, the cusp is the most colourful point the display's own
primaries can reach - the edge of the RGB cube, seen in JMh.  The gamut
compression stage needs it per pixel, and finding it involves a search,
so it is found once per hue here and read back by interpolation later.

TotalTableSize is the reference's 360 entries plus two that wrap the
ends, so interpolation across 0 and 360 degrees needs no special case.

===========================================================================
*/

#define ACES2_TABLE_SIZE		360
#define ACES2_TABLE_TOTAL		( ACES2_TABLE_SIZE + 2 )
#define ACES2_BASE_INDEX		1

typedef struct {
	double	J[ACES2_TABLE_TOTAL];
	double	M[ACES2_TABLE_TOTAL];
	double	h[ACES2_TABLE_TOTAL];
	/* Entry hues.  Uniform apart from six entries moved onto the cube
	 * corner hues, where the cusp has a kink that a uniform table
	 * interpolates straight across.
	 *
	 * The shaders never see these: ACES2_PackHueTables resamples the
	 * table onto an even grid, so their lookup stays one index and one
	 * fetch and they inherit the corner accuracy without needing the
	 * entry hues at all. */
	double	hueOfEntry[ACES2_TABLE_SIZE];
} aces2CuspTable_t;

void	ACES2_BuildCuspTable( const aces2JMhParams_t *limit, double peakLuminance,
							  aces2CuspTable_t *table );
void	ACES2_CuspForHue( const aces2CuspTable_t *table, double hue, double JM[2] );

/*
===========================================================================

The ACES 2.0 tone scale.

Forward takes scene-referred linear and returns luminance in cd/m2.
Inverse takes that luminance DIVIDED by the 100 reference, not the
luminance itself - the reference's own comment says cd/m2 but every
caller divides first, and taking it at its word makes the round trip
fail by four orders of magnitude.

===========================================================================
*/

typedef struct {
	double	n, n_r, g, t_1, c_t, s_2, u_2, m_2;
	double	forward_limit;
	double	inverse_limit;
} aces2TSParams_t;

void	ACES2_InitTSParams( double peakLuminance, aces2TSParams_t *p );
double	ACES2_ToneScaleFwd( double x, const aces2TSParams_t *p );
double	ACES2_ToneScaleInv( double Yn, const aces2TSParams_t *p );

/*
===========================================================================

Chroma compression.

Between the tone scale and the gamut compression.  The tone scale moves
J and leaves M alone, which on its own makes tone-mapped colour look
more saturated than it was; this pulls M back by an amount that depends
on how far up the lightness range the pixel landed, and on how much
reach the rendering space has at that hue.

The reach table is the second of the two hue tables - the outer limit of
M at maximum lightness in the reach primaries, found per hue by search.

===========================================================================
*/

typedef struct {
	double	reachM[ACES2_TABLE_TOTAL];
	double	limit_J_max;
	double	model_gamma_inv;
	double	sat;
	double	sat_thr;
	double	compr;
	double	chroma_compress_scale;
} aces2ChromaParams_t;

void	ACES2_InitChromaParams( const aces2JMhParams_t *input,
								const aces2JMhParams_t *reach,
								const aces2TSParams_t *ts,
								double peakLuminance,
								aces2ChromaParams_t *p );

void	ACES2_ChromaCompressFwd( const double JMh[3], double tonemappedJ,
								 const aces2ChromaParams_t *p, double out[3] );
void	ACES2_ChromaCompressInv( const double JMh[3], double J,
								 const aces2ChromaParams_t *p, double out[3] );

/*
===========================================================================

Gamut compression - the geometry.

This is the part my original scoping got wrong.  I said the gamut
boundary is found iteratively and that ARB, having no loops, would need
something exotic.  It is closed form: two analytic hull intersections
blended with a smooth minimum.  The only searching is in building the
tables, which happens once on the CPU.

The upper hull exponent is passed in rather than looked up, and stays
that way now that the table exists: ACES2_BuildUpperHullGammaTable
supplies it per hue, and keeping the geometry free of the lookup is
what let it be tested on its own with a fixed exponent.

===========================================================================
*/

typedef struct {
	double	limit_J_max;
	double	mid_J;
	double	focus_dist;
	double	lower_hull_gamma_inv;
	double	model_gamma_inv;
} aces2GamutParams_t;

void	ACES2_InitGamutParams( const aces2JMhParams_t *input, const aces2TSParams_t *ts,
							   double peakLuminance, aces2GamutParams_t *p );

/* JMcusp is the {J, M} from the cusp table; gammaTopInv is the upper
 * hull exponent for this hue; reachM is the reach table value.
 *
 * Jx is separate from JMh[0] on purpose, and it is not a detail to fold
 * away: the focus gain is a function of it, and the inverse does not
 * know the source J.  So the inverse runs this twice - once with Jx set
 * to the compressed J to estimate the source, then again with that
 * estimate - which is only expressible if Jx is an argument.  Collapsing
 * the two is what made my first round-trip test fail by half. */
void	ACES2_GamutCompress( const double JMh[3], double Jx,
							 const aces2GamutParams_t *p,
							 const double JMcusp[2], double gammaTopInv,
							 double reachM, bool invert, double out[3] );

/* The reference's two-pass inverse, wrapped. */
void	ACES2_GamutCompressInv( const double JMh[3], const aces2GamutParams_t *p,
								const double JMcusp[2], double gammaTopInv,
								double reachM, double out[3] );

/*
===========================================================================

The upper hull exponent table - the third and last of the hue tables.

The closed-form boundary estimate needs an exponent describing how the
gamut's upper hull bends between the cusp and maximum lightness.  There
is no formula for it: the reference searches, per hue, for the smallest
exponent whose estimated boundary stays outside the real gamut at five
sample lightnesses.  Outside, not on - the estimate is allowed to
overshoot and then be pulled in, but never to cut the gamut short.

===========================================================================
*/

typedef struct {
	double	gammaInv[ACES2_TABLE_TOTAL];
} aces2GammaTable_t;

void	ACES2_BuildUpperHullGammaTable( const aces2CuspTable_t *cusp,
										const aces2GamutParams_t *gp,
										const aces2JMhParams_t *limit,
										double peakLuminance,
										aces2GammaTable_t *table );
double	ACES2_GammaInvForHue( const aces2GammaTable_t *table, double hue );

/*
===========================================================================

The whole forward transform, assembled.

AP0 linear in, display RGB out.  This is the thing the shader stages
have to reproduce, and having it in one call is what makes them
checkable rather than merely plausible.

===========================================================================
*/

typedef struct {
	aces2JMhParams_t	input;
	aces2JMhParams_t	reach;
	aces2JMhParams_t	limit;
	aces2TSParams_t		ts;
	aces2ChromaParams_t	chroma;
	aces2GamutParams_t	gamut;
	aces2CuspTable_t	cusp;
	aces2GammaTable_t	gamma;
	double				peakLuminance;
} aces2Params_t;

void	ACES2_Init( const double limitPrims[8], double peakLuminance, aces2Params_t *p );
void	ACES2_OutputTransformFwd( const double aces[3], const aces2Params_t *p, double RGB[3] );

/*
===========================================================================

Packing the hue tables for the shaders.

All three tables are functions of hue on the same 360 entry grid, so
they travel as one RGBA16 texture: cusp J, cusp M, reach M, and the
upper hull exponent, one texel per degree.  One fetch gives a shader
everything it needs for a hue.

Each channel is scaled into 0..1 by its own factor, returned in scales,
because the four have quite different ranges - J to about 100, reach M
past 190, the exponent under 1.  The shader multiplies back.
===========================================================================
*/

/* The texture is resampled finer than the table it comes from.  The
 * table's entries sit on the cube corners so its own interpolation is
 * accurate, but a shader reading it would have to find the bracketing
 * pair, and the entries are not evenly spaced.  Resampling onto an even
 * grid keeps the shader lookup to one arithmetic index and one fetch
 * with hardware interpolation, and 2048 entries put the residual kink
 * error at 0.25% of the M range - against 1.4% at one entry per degree,
 * and 0.04% for the table itself. */
#define ACES2_LUT_WIDTH		2048

void	ACES2_PackHueTables( const aces2Params_t *p, unsigned short *rgba16,
							 double scales[4] );

/* The assembled inverse: display RGB back to AP0 linear.
 *
 * Not exact, and cannot be.  The gamut compression's inverse estimates
 * the source lightness rather than knowing it, and the AP0 to AP1 clamp
 * at the front is not injective - anything outside AP1 was folded onto
 * its boundary on the way in and no inverse can unfold it.  Within the
 * range where both are well behaved it round trips to a few parts in a
 * million; outside it, it returns the nearest thing that would have
 * produced the same output. */
void	ACES2_OutputTransformInv( const double RGB[3], const aces2Params_t *p, double aces[3] );

/* A GUI drawn onto a surface in the world is authored in display space:
 * its 0.5 grey is meant to read as 0.5.  The engine treats that texture
 * as albedo, so it goes through the output transform and does not land
 * where the artist put it - 0.10 arrives at 0.064.
 *
 * The transform being invertible is what fixes that.  Pre-correcting the
 * texel through the inverse means the forward transform returns it
 * exactly.  Doing the full inverse per texel would cost more than the
 * transform itself, so this bakes the neutral axis into a small table:
 * given the value the artist wants on screen, the scene value that
 * produces it.  Exact on greys, and close enough on colour that text
 * stays the colour it was authored. */
#define ACES2_GUI_LUT_SIZE	256
void	ACES2_BuildGuiInverse( const aces2Params_t *p, float table[ACES2_GUI_LUT_SIZE] );
