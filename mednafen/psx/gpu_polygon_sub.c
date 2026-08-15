/*
 * gpu_polygon_sub.c - polygon-helper free functions used by the
 * software rasteriser's polygon decoder.
 *
 * Switched to a minimal include set: gpu.h gives us PS_GPU and
 * tri_vertex (both now C-includable as of 7d257de), rhi_intf.h
 * gives us RHI_VULKAN and rhi_intf_is_type, and
 * beetle_psx_globals.h gives us psx_gpu_upscale_shift_hw.
 */

#include "gpu.h"
#include "../../rhi/rhi_intf.h"
#include "../../beetle_psx_globals.h"

#include <retro_miscellaneous.h>

/* Determine whether to offset UVs to account for difference in interpolation between PS1 and modern GPUs */
void Calc_UVOffsets_Adjust_Verts(PS_GPU *gpu, tri_vertex *vertices, unsigned count)
{
	/* iCB: Just borrowing this from \parallel-psx\renderer\renderer.cpp */
	uint16_t off_u = 0;
	uint16_t off_v = 0;
	bool may_be_2d = false;
	if (gpu->InCmd == INCMD_QUAD)
	{
		off_u = gpu->off_u;
		off_v = gpu->off_v;
		may_be_2d = gpu->may_be_2d;
	}

	/* For X/Y flipped 2D sprites, PSX games rely on a very specific rasterization behavior.
	 * If U or V is decreasing in X or Y, and we use the provided U/V as is, we will sample the wrong texel as interpolation
	 * covers an entire pixel, while PSX samples its interpolation essentially in the top-left corner and splats that interpolant across the entire pixel.
	 * While we could emulate this reasonably well in native resolution by shifting our vertex coords by 0.5,
	 * this breaks in upscaling scenarios, because we have several samples per native sample and we need NN rules to hit the same UV every time.
	 * One approach here is to use interpolate at offset or similar tricks to generalize the PSX interpolation patterns,
	 * but the problem is that vertices sharing an edge will no longer see the same UV (due to different plane derivatives),
	 * we end up sampling outside the intended boundary and artifacts are inevitable, so the only case where we can apply this fixup is for "sprites"
	 * or similar which should not share edges, which leads to this unfortunate code below.
	 */
	{
		/* It might be faster to do more direct checking here, but the code below handles primitives in any order
		 * and orientation, and is far more SIMD-friendly if needed.
		 *
		 * All inputs (x, y, u, v) are int32_t, so every derivative below is
		 * integer-valued.  It used to be computed in float: at upscale_shift
		 * == 0 the coords are small and the float math is exact, but with the
		 * software renderer upscaling (upscale_shift > 0) the coords are
		 * shifted up and the mul-add chains overflow 2^24, at which point the
		 * < 0 / == 0 tests round - and round differently under FMA contraction
		 * or x87-vs-SSE codegen, so off_u/off_v/may_be_2d (and the Wild Arms 2
		 * vertex fixup) could diverge between netplay peers.  Compute the
		 * derivatives exactly in int64 instead; the results feed only sign and
		 * zero tests, so the old `* inv_area` scaling was a no-op for the
		 * decisions (it cannot change a sign or a zero) and is dropped. */
		int64_t abx = (int64_t)vertices[1].x - vertices[0].x;
		int64_t aby = (int64_t)vertices[1].y - vertices[0].y;
		int64_t bcx = (int64_t)vertices[2].x - vertices[1].x;
		int64_t bcy = (int64_t)vertices[2].y - vertices[1].y;
		int64_t cax = (int64_t)vertices[0].x - vertices[2].x;
		int64_t cay = (int64_t)vertices[0].y - vertices[2].y;

		/* Compute static derivatives, just assume W is uniform across the primitive
		 * and that the plane equation remains the same across the quad. */
		int64_t dudx = -aby * vertices[2].u - bcy * vertices[0].u - cay * vertices[1].u;
		int64_t dvdx = -aby * vertices[2].v - bcy * vertices[0].v - cay * vertices[1].v;
		int64_t dudy = +abx * vertices[2].u + bcx * vertices[0].u + cax * vertices[1].u;
		int64_t dvdy = +abx * vertices[2].v + bcx * vertices[0].v + cax * vertices[1].v;
		int64_t area = bcx * cay - bcy * cax;

		/* iCB: Detect and reject any triangles with 0 size texture area */
		int64_t texArea = ((int64_t)vertices[1].u - vertices[0].u) * (vertices[2].v - vertices[0].v) - ((int64_t)vertices[2].u - vertices[0].u) * (vertices[1].v - vertices[0].v);

		/* Leverage PGXP to further avoid 3D polygons that just happen to align this way after projection */
		bool is3D = ((vertices[0].precise[2] != vertices[1].precise[2]) || (vertices[1].precise[2] != vertices[2].precise[2]));

		/* Shouldn't matter as degenerate primitives will be culled anyways. */
		if ((area != 0) && (texArea != 0) && !is3D)
		{
			/* Sign of (deriv / inv_area) equals (sign of deriv) XOR (sign
			 * of area); zero-ness is unchanged since inv_area is non-zero. */
			bool neg_area  = area < 0;
			bool neg_dudx  = (dudx < 0) != neg_area;
			bool neg_dudy  = (dudy < 0) != neg_area;
			bool neg_dvdx  = (dvdx < 0) != neg_area;
			bool neg_dvdy  = (dvdy < 0) != neg_area;
			bool zero_dudx = dudx == 0;
			bool zero_dudy = dudy == 0;
			bool zero_dvdx = dvdx == 0;
			bool zero_dvdy = dvdy == 0;

			/* Dumb heuristic to check if a polygon may be 2D */
			may_be_2d = may_be_2d || zero_dudy || zero_dudx || zero_dvdy || zero_dvdx;

			/* If we have negative dU or dV in any direction, increment the U or V to work properly with nearest-neighbor in this impl.
			 * If we don't have 1:1 pixel correspondence, this creates a slight "shift" in the sprite, but we guarantee that we don't sample garbage at least.
			 * Overall, this is kinda hacky because there can be legitimate, rare cases where 3D meshes hit this scenario, and a single texel offset can pop in, but
			 * this is way better than having borked 2D overall.
			 * TODO: Try to figure out if this can be generalized.
			 *
			 * TODO: If perf becomes an issue, we can probably SIMD the 8 comparisons above,
			 * create an 8-bit code, and use a LUT to get the offsets.
			 * Case 1: U is decreasing in X, but no change in Y.
			 * Case 2: U is decreasing in Y, but no change in X.
			 * Case 3: V is decreasing in X, but no change in Y.
			 * Case 4: V is decreasing in Y, but no change in X. */
			if (rhi_intf_is_type() != RHI_VULKAN || psx_gpu_upscale_shift_hw)
			{
				if (neg_dudx && zero_dudy)
					off_u = 1;
				else if (neg_dudy && zero_dudx)
					off_u = 1;
				if (neg_dvdx && zero_dvdy)
					off_v = 1;
				else if (neg_dvdy && zero_dvdx)
					off_v = 1;
			}

			/* HACK fix Wild Arms 2 overworld forest sprite
			 * TODO generalize this perhaps? */
			{
				const int64_t one = (int64_t)1 << gpu->upscale_shift;
				if (zero_dvdx &&
					(aby == one || bcy == one || cay == one) &&
					(aby == 0 || bcy == 0 || cay == 0) &&
					(aby == -one || bcy == -one || cay == -one)
				)
				{
					if (neg_dvdy)
					{
						if (aby == -one)
							vertices[0].v = vertices[1].v - 1;
						else if (bcy == -one)
							vertices[1].v = vertices[2].v - 1;
						else if (cay == -one)
							vertices[2].v = vertices[0].v - 1;

						if (aby == one)
							vertices[1].v = vertices[0].v - 1;
						else if (bcy == one)
							vertices[2].v = vertices[1].v - 1;
						else if (cay == one)
							vertices[0].v = vertices[2].v - 1;
					}
				}
			}
		}
	}

	gpu->off_u = off_u;
	gpu->off_v = off_v;
	gpu->may_be_2d = may_be_2d;
}

/* Reset min/max UVs for primitive */
void Reset_UVLimits(PS_GPU *gpu)
{
	gpu->min_u = UINT16_MAX;
	gpu->min_v = UINT16_MAX;
	gpu->max_u = 0;
	gpu->max_v = 0;
}

/* Determine min and max UVs sampled for a given primitive */
void Extend_UVLimits(PS_GPU *gpu, tri_vertex *vertices, unsigned count)
{
	uint8_t twx = gpu->SUCV.TWX_AND;
	uint8_t twy = gpu->SUCV.TWY_AND;

	uint16_t min_u = gpu->min_u;
	uint16_t min_v = gpu->min_v;
	uint16_t max_u = gpu->max_u;
	uint16_t max_v = gpu->max_v;

	if ((twx == (uint8_t)0xffu) && (twy == (uint8_t)0xffu))
	{
		unsigned int i;
		/* If we're not using texture window, we're likely accessing a small subset of the texture. */
		for (i = 0; i < count; i++)
		{
			uint16_t u = (uint16_t)vertices[i].u;
			uint16_t v = (uint16_t)vertices[i].v;
			if (u < min_u) min_u = u;
			if (v < min_v) min_v = v;
			if (u > max_u) max_u = u;
			if (v > max_v) max_v = v;
		}
	}
	else
	{
		/* texture window so don't clamp texture */
		min_u = 0;
		min_v = 0;
		max_u = UINT16_MAX;
		max_v = UINT16_MAX;
	}

	gpu->min_u = min_u;
	gpu->min_v = min_v;
	gpu->max_u = max_u;
	gpu->max_v = max_v;
}

/* Apply offsets to UV limits before returning */
void Finalise_UVLimits(PS_GPU *gpu)
{
	uint8_t twx = gpu->SUCV.TWX_AND;
	uint8_t twy = gpu->SUCV.TWY_AND;

	uint16_t min_u = gpu->min_u;
	uint16_t min_v = gpu->min_v;
	uint16_t max_u = gpu->max_u;
	uint16_t max_v = gpu->max_v;

	uint16_t off_u = gpu->off_u;
	uint16_t off_v = gpu->off_v;

	if ((twx == (uint8_t)0xffu) && (twy == (uint8_t)0xffu))
	{
		/* offset output UV Limits */
		min_u += off_u;
		min_v += off_v;
		max_u += off_u;
		max_v += off_v;

		/* In nearest neighbor, we'll get *very* close to this UV, but not close enough to actually sample it.
		 * If du/dx or dv/dx are negative, we probably need to invert this though ... */
		if ((rhi_intf_is_type() != RHI_VULKAN || psx_gpu_upscale_shift_hw) && gpu->may_be_2d)
		{
			if (max_u > min_u)
				max_u--;
			if (max_v > min_v)
				max_v--;
		}

		/* If there's no wrapping, we can prewrap and avoid fallback. */
		if ((max_u & 0xff00) == (min_u & 0xff00))
			max_u &= 0xff;
		if ((max_v & 0xff00) == (min_v & 0xff00))
			max_v &= 0xff;
	}
	else
	{
		/* texture window so don't clamp texture */
		min_u = 0;
		min_v = 0;
		max_u = UINT16_MAX;
		max_v = UINT16_MAX;
	}

	gpu->min_u = min_u;
	gpu->min_v = min_v;
	gpu->max_u = max_u;
	gpu->max_v = max_v;
}


/* 0 = disabled
 * 1 = enabled (default mode)
 * 2 = enabled (aggressive mode) */

/* Hack to deal with PS1 games rendering axis aligned lines using 1 pixel wide triangles with UVs that describe a line
 * Suitable for games like Soul Blade, Doom and Hexen */
bool Hack_FindLine(PS_GPU *gpu, tri_vertex* vertices, tri_vertex* outVertices)
{
	int32_t pxWidth = 1 << gpu->upscale_shift;	/* width of a single pixel */
	uint8_t cornerIdx, shortIdx, longIdx;

	/* reject 3D elements */
	if ((vertices[0].precise[2] != vertices[1].precise[2]) ||
		(vertices[1].precise[2] != vertices[2].precise[2]))
		return false;

	/* find short side of triangle / end of line with 2 vertices (guess which vertex is the right angle) */
	if ((vertices[0].u == vertices[1].u) && (vertices[0].v == vertices[1].v))
		cornerIdx = 0;
	else if ((vertices[1].u == vertices[2].u) && (vertices[1].v == vertices[2].v))
		cornerIdx = 1;
	else if ((vertices[2].u == vertices[0].u) && (vertices[2].v == vertices[0].v))
		cornerIdx = 2;
	else
		return false;

	/* assign other indices to remaining vertices */
	shortIdx = (cornerIdx + 1) % 3;
	longIdx = (shortIdx + 1) % 3;

	/* determine line orientation and check width */
	if ((vertices[cornerIdx].x == vertices[shortIdx].x) && (abs(vertices[cornerIdx].y - vertices[shortIdx].y) == pxWidth))
	{
		/* line is horizontal
		 * determine which is truly the corner by checking against the long side, while making sure it is axis aligned */
		if (vertices[shortIdx].y == vertices[longIdx].y)
		{
			uint8_t tempIdx = shortIdx;
			shortIdx = cornerIdx;
			cornerIdx = tempIdx;
		}
		else if (vertices[cornerIdx].y != vertices[longIdx].y)
			return false;

		/* flip corner index to other side of quad */
		outVertices[cornerIdx] = vertices[longIdx];
		outVertices[cornerIdx].y = vertices[shortIdx].y;
		outVertices[cornerIdx].precise[1] = vertices[shortIdx].precise[1];
	}
	else if ((vertices[cornerIdx].y == vertices[shortIdx].y) && (abs(vertices[cornerIdx].x - vertices[shortIdx].x) == pxWidth))
	{
		/* line is vertical
		 * determine which is truly the corner by checking against the long side, while making sure it is axis aligned */
		if (vertices[shortIdx].x == vertices[longIdx].x)
		{
			uint8_t tempIdx = shortIdx;
			shortIdx = cornerIdx;
			cornerIdx = tempIdx;
		}
		else if (vertices[cornerIdx].x != vertices[longIdx].x)
			return false;

		/* flip corner index to other side of quad */
		outVertices[cornerIdx] = vertices[longIdx];
		outVertices[cornerIdx].x = vertices[shortIdx].x;
		outVertices[cornerIdx].precise[0] = vertices[shortIdx].precise[0];
	}
	else
		return false;

	outVertices[shortIdx] = vertices[shortIdx];
	outVertices[longIdx] = vertices[longIdx];

	return true;
}

/* Hack to deal with PS1 games rendering axis aligned lines using 1 pixel wide triangles and force UVs to describe a line
 * Required for games like Dark Forces and Duke Nukem */
bool Hack_ForceLine(PS_GPU *gpu, tri_vertex* vertices, tri_vertex* outVertices)
{
	int32_t pxWidth = 1 << gpu->upscale_shift;	/* width of a single pixel */
	uint8_t cornerIdx, shortIdx, longIdx;
	uint8_t A, B, C;

	/* reject 3D elements */
	if ((vertices[0].precise[2] != vertices[1].precise[2]) ||
		(vertices[1].precise[2] != vertices[2].precise[2]))
		return false;

	/* find vertical AB */
	if (vertices[0].x == vertices[1].x)
		A = 0;
	else if (vertices[1].x == vertices[2].x)
		A = 1;
	else if (vertices[2].x == vertices[0].x)
		A = 2;
	else
		return false;

	/* assign other indices to remaining vertices */
	B = (A + 1) % 3;
	C = (B + 1) % 3;

	/* find horizontal AC or BC */
	if (vertices[A].y == vertices[C].y)
		cornerIdx = A;
	else if (vertices[B].y == vertices[C].y)
		cornerIdx = B;
	else
		return false;

	/* determine lengths of sides */
	if (abs(vertices[A].y - vertices[B].y) == pxWidth)
	{
		/* is Horizontal */
		shortIdx = (cornerIdx == A) ? B : A;
		longIdx = C;

		/* flip corner index to other side of quad */
		outVertices[cornerIdx] = vertices[longIdx];
		outVertices[cornerIdx].y = vertices[shortIdx].y;
		outVertices[cornerIdx].precise[1] = vertices[shortIdx].precise[1];
	}
	else if (abs(vertices[A].x - vertices[C].x) == pxWidth)
	{
		/* is Vertical */
		shortIdx = C;
		longIdx = (cornerIdx == A) ? B : A;

		/* flip corner index to other side of quad */
		outVertices[cornerIdx] = vertices[longIdx];
		outVertices[cornerIdx].x = vertices[shortIdx].x;
		outVertices[cornerIdx].precise[0] = vertices[shortIdx].precise[0];
	}
	else
		return false;

	/* force UVs into a line along the upper or left most edge of the triangle
	 * Otherwise the wrong UVs will be sampled on second triangle and by hardware renderers */
	vertices[shortIdx].u = vertices[cornerIdx].u;
	vertices[shortIdx].v = vertices[cornerIdx].v;

	/* copy other two vertices */
	outVertices[shortIdx] = vertices[shortIdx];
	outVertices[longIdx] = vertices[longIdx];

	return true;
}

/* Tunables for GPU_QuadPersp_Recover below.  QP_MIN_W rejects w
 * that a later 1/w would blow up on; QP_MIN_RATIO skips quads
 * whose affine error is sub-texel anyway; QP_MAX_RATIO rejects
 * implausibly extreme recoveries. */
#define QP_MIN_W     (1e-3)
#define QP_MIN_RATIO (1.01)
#define QP_MAX_RATIO (64.0)

/* GPU_QuadPersp_Recover - recover per-vertex perspective w for a
 * textured quad from its screen-space shape alone.
 *
 * A PS1 textured 4-point primitive is, in the overwhelmingly
 * common case, the projection of a planar rectangle in texture
 * space (the artist maps an axis-aligned texture rect onto a 3D
 * quad).  Four screen<->parameter point correspondences fully
 * determine the projective mapping (homography) from the unit
 * parameter square to the screen quad:
 *
 *   x(s,t) = (a*s + b*t + c) / (g*s + h*t + 1)
 *   y(s,t) = (d*s + e*t + f) / (g*s + h*t + 1)
 *
 * The denominator evaluated at the four corners IS the per-vertex
 * perspective w, up to a global scale that perspective-correct
 * interpolation is invariant to:
 *
 *   w(0,0) = 1        w(1,0) = 1 + g
 *   w(0,1) = 1 + h    w(1,1) = 1 + g + h
 *
 * g and h come from the standard square->quad closed form
 * (Heckbert, "Fundamentals of Texture Mapping and Image Warping",
 * sec. 2.2; re-derived independently and validated against
 * synthetic pinhole projections in the unit harness):
 *
 *   S  = P00 - P10 - P01 + P11
 *   D1 = P10 - P11
 *   D2 = P01 - P11
 *   g  = cross(S, D2) / cross(D1, D2)
 *   h  = cross(D1, S) / cross(D1, D2)
 *
 * PS1 quads arrive in strip order A,B,C,D covering the parameter
 * corners A=(0,0), B=(1,0), C=(0,1), D=(1,1); the perimeter is
 * therefore A->B->D->C.
 *
 * The recovered w is a heuristic - the quad might not actually be
 * a projected planar rectangle - so eligibility is gated hard:
 *
 *   - texture-space parallelogram: |uA - uB - uC + uD| <= 2 and
 *     likewise for v.  If the UV corners don't form (close to) a
 *     parallelogram, the planar-rect premise is already false and
 *     the homography would "correct" toward garbage.
 *   - convex screen perimeter with a consistent winding.  PS1
 *     quads can legally be twisted (bowties) or non-convex, and
 *     games use that; a homography does not describe those.
 *   - all recovered w strictly positive.  A sign flip means the
 *     recovered plane crosses the eye plane - not a projection of
 *     anything visible.
 *   - bounded anisotropy: wmax/wmin in (QP_MIN_RATIO,
 *     QP_MAX_RATIO).  Below the floor the affine error is
 *     sub-texel and correction is wasted per-pixel work in the
 *     software rasteriser; above the cap the quad is more likely
 *     degenerate data than a legitimate near-horizon surface.
 *
 * Decision math is exact: the parallelogram, degeneracy, winding
 * and convexity tests are integer (int64), following the
 * gpu_polygon_sub.c convention established by the Wild Arms 2
 * derivative fix above - accept/reject cannot diverge across
 * platforms from FP contraction.  Only the accepted-path w values
 * involve floating point, mirroring the existing PGXP pct maths.
 *
 * Inputs are the four corner vertices in strip order (A from the
 * first half-command, B,C,D from the second).  Screen x/y are the
 * post-offset, post-upscale integer coordinates; both offset and
 * uniform scale cancel in the difference/ratio structure above,
 * so software (upscaled) and hardware (native+PGXP-space) callers
 * recover identical w.
 *
 * Returns 1 and fills w_out[4] (normalised so min(w) == 1.0, all
 * finite, all >= 1.0) on success; returns 0 leaving w_out
 * untouched when the quad is ineligible.
 */
int GPU_QuadPersp_Recover(const tri_vertex *A, const tri_vertex *BCD,
      float *w_out)
{
	const tri_vertex *B = &BCD[0];
	const tri_vertex *C = &BCD[1];
	const tri_vertex *D = &BCD[2];

	int64_t sx, sy;          /* S  = A - B - C + D */
	int64_t d1x, d1y;        /* D1 = B - D */
	int64_t d2x, d2y;        /* D2 = C - D */
	int64_t den;
	int64_t e0x, e0y, e1x, e1y, e2x, e2y, e3x, e3y;
	int64_t c0, c1, c2, c3;
	double g, h;
	double w0, w1, w2, w3;
	double wmin, wmax;

	/* Texture-space parallelogram gate (exact).  u/v are 0..255
	 * so plain int arithmetic cannot overflow, but keep the int64
	 * convention of this file. */
	{
		int64_t pu = (int64_t)A->u - B->u - C->u + D->u;
		int64_t pv = (int64_t)A->v - B->v - C->v + D->v;
		if (pu < -2 || pu > 2 || pv < -2 || pv > 2)
			return 0;
	}

	sx  = (int64_t)A->x - B->x - C->x + D->x;
	sy  = (int64_t)A->y - B->y - C->y + D->y;

	/* Exact affine fast-out: a screen parallelogram is what the
	 * affine rasteriser already renders correctly. */
	if (sx == 0 && sy == 0)
		return 0;

	d1x = (int64_t)B->x - D->x;
	d1y = (int64_t)B->y - D->y;
	d2x = (int64_t)C->x - D->x;
	d2y = (int64_t)C->y - D->y;

	den = d1x * d2y - d1y * d2x;
	if (den == 0)
		return 0;

	/* Convexity + consistent winding of the perimeter A->B->D->C
	 * (exact).  Consecutive edge cross products must all share a
	 * strict sign; any zero (collinear corner) also rejects. */
	e0x = (int64_t)B->x - A->x;  e0y = (int64_t)B->y - A->y;
	e1x = (int64_t)D->x - B->x;  e1y = (int64_t)D->y - B->y;
	e2x = (int64_t)C->x - D->x;  e2y = (int64_t)C->y - D->y;
	e3x = (int64_t)A->x - C->x;  e3y = (int64_t)A->y - C->y;
	c0  = e0x * e1y - e0y * e1x;
	c1  = e1x * e2y - e1y * e2x;
	c2  = e2x * e3y - e2y * e3x;
	c3  = e3x * e0y - e3y * e0x;
	if (c0 > 0)
	{
		if (c1 <= 0 || c2 <= 0 || c3 <= 0)
			return 0;
	}
	else if (c0 < 0)
	{
		if (c1 >= 0 || c2 >= 0 || c3 >= 0)
			return 0;
	}
	else
		return 0;

	g = (double)(sx * d2y - sy * d2x) / (double)den;
	h = (double)(d1x * sy - d1y * sx) / (double)den;

	w0 = 1.0;
	w1 = 1.0 + g;
	w2 = 1.0 + h;
	w3 = 1.0 + g + h;

	if (w1 <= QP_MIN_W || w2 <= QP_MIN_W || w3 <= QP_MIN_W)
		return 0;

	wmin = w0;
	wmax = w0;
	if (w1 < wmin) wmin = w1;
	if (w1 > wmax) wmax = w1;
	if (w2 < wmin) wmin = w2;
	if (w2 > wmax) wmax = w2;
	if (w3 < wmin) wmin = w3;
	if (w3 > wmax) wmax = w3;

	if (wmax < wmin * QP_MIN_RATIO)
		return 0; /* visually indistinguishable from affine */
	if (wmax > wmin * QP_MAX_RATIO)
		return 0; /* implausibly extreme - likely not a planar rect */

	w_out[0] = (float)(w0 / wmin);
	w_out[1] = (float)(w1 / wmin);
	w_out[2] = (float)(w2 / wmin);
	w_out[3] = (float)(w3 / wmin);
	return 1;
}
