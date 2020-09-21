#include "psx.h"

// Determine whether to offset UVs to account for difference in interpolation between PS1 and modern GPUs
void Calc_UVOffsets_Adjust_Verts(PS_GPU *gpu, tri_vertex *vertices, unsigned count)
{
	// iCB: Just borrowing this from \parallel-psx\renderer\renderer.cpp
	uint16 off_u = 0;
	uint16 off_v = 0;
	if (gpu->InCmd != INCMD_QUAD)
	{
		off_u = 0;
		off_v = 0;
	}
	else
	{
		off_u = gpu->off_u;
		off_v = gpu->off_v;
	}

	// For X/Y flipped 2D sprites, PSX games rely on a very specific rasterization behavior.
	// If U or V is decreasing in X or Y, and we use the provided U/V as is, we will sample the wrong texel as interpolation
	// covers an entire pixel, while PSX samples its interpolation essentially in the top-left corner and splats that interpolant across the entire pixel.
	// While we could emulate this reasonably well in native resolution by shifting our vertex coords by 0.5,
	// this breaks in upscaling scenarios, because we have several samples per native sample and we need NN rules to hit the same UV every time.
	// One approach here is to use interpolate at offset or similar tricks to generalize the PSX interpolation patterns,
	// but the problem is that vertices sharing an edge will no longer see the same UV (due to different plane derivatives),
	// we end up sampling outside the intended boundary and artifacts are inevitable, so the only case where we can apply this fixup is for "sprites"
	// or similar which should not share edges, which leads to this unfortunate code below.
	//
	// Only apply this workaround for quads.
	// if (count == 4)
	{
		// It might be faster to do more direct checking here, but the code below handles primitives in any order
		// and orientation, and is far more SIMD-friendly if needed.
		float abx = vertices[1].precise[0] - vertices[0].precise[0];
		float aby = vertices[1].precise[1] - vertices[0].precise[1];
		float bcx = vertices[2].precise[0] - vertices[1].precise[0];
		float bcy = vertices[2].precise[1] - vertices[1].precise[1];
		float cax = vertices[0].precise[0] - vertices[2].precise[0];
		float cay = vertices[0].precise[1] - vertices[2].precise[1];

		// Compute static derivatives, just assume W is uniform across the primitive
		// and that the plane equation remains the same across the quad.
		float dudx = -aby * float(vertices[2].u) - bcy * float(vertices[0].u) - cay * float(vertices[1].u);
		float dvdx = -aby * float(vertices[2].v) - bcy * float(vertices[0].v) - cay * float(vertices[1].v);
		float dudy = +abx * float(vertices[2].u) + bcx * float(vertices[0].u) + cax * float(vertices[1].u);
		float dvdy = +abx * float(vertices[2].v) + bcx * float(vertices[0].v) + cax * float(vertices[1].v);
		float area = bcx * cay - bcy * cax;

		// iCB: Detect and reject any triangles with 0 size texture area
		float texArea = (vertices[1].u - vertices[0].u) * (vertices[2].v - vertices[0].v) - (vertices[2].u - vertices[0].u) * (vertices[1].v - vertices[0].v);

		// Leverage PGXP to further avoid 3D polygons that just happen to align this way after projection
		bool is3D = ((vertices[0].precise[2] != vertices[1].precise[2]) || (vertices[1].precise[2] != vertices[2].precise[2]));

		// Shouldn't matter as degenerate primitives will be culled anyways.
		if ((area != 0.0f) && (texArea != 0.0f) && !is3D)
		{
			float inv_area = 1.0f / area;
			dudx *= inv_area;
			dudy *= inv_area;
			dvdx *= inv_area;
			dvdy *= inv_area;

			bool neg_dudx = dudx < 0.0f;
			bool neg_dudy = dudy < 0.0f;
			bool neg_dvdx = dvdx < 0.0f;
			bool neg_dvdy = dvdy < 0.0f;
			bool zero_dudx = dudx == 0.0f;
			bool zero_dudy = dudy == 0.0f;
			bool zero_dvdx = dvdx == 0.0f;
			bool zero_dvdy = dvdy == 0.0f;

			// If we have negative dU or dV in any direction, increment the U or V to work properly with nearest-neighbor in this impl.
			// If we don't have 1:1 pixel correspondence, this creates a slight "shift" in the sprite, but we guarantee that we don't sample garbage at least.
			// Overall, this is kinda hacky because there can be legitimate, rare cases where 3D meshes hit this scenario, and a single texel offset can pop in, but
			// this is way better than having borked 2D overall.
			// TODO: Try to figure out if this can be generalized.
			//
			// TODO: If perf becomes an issue, we can probably SIMD the 8 comparisons above,
			// create an 8-bit code, and use a LUT to get the offsets.
			// Case 1: U is decreasing in X, but no change in Y.
			// Case 2: U is decreasing in Y, but no change in X.
			// Case 3: V is decreasing in X, but no change in Y.
			// Case 4: V is decreasing in Y, but no change in X.
			if (gpu->InCmd != INCMD_QUAD)
			{
				if (neg_dudx && zero_dudy)
					off_u++;
				else if (neg_dudy && zero_dudx)
					off_u++;
				if (neg_dvdx && zero_dvdy)
					off_v++;
				else if (neg_dvdy && zero_dvdx)
					off_v++;
			}

			// HACK fix Wild Arms 2 overworld forest sprite
			// TODO generalize this perhaps?
			const float one = float(1 << gpu->upscale_shift);
			if (zero_dvdx &&
				(aby == one || bcy == one || cay == one) &&
				(aby == 0.0 || bcy == 0.0 || cay == 0.0) &&
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

	gpu->off_u = off_u;
	gpu->off_v = off_v;
}

// Reset min/max UVs for primitive
void Reset_UVLimits(PS_GPU *gpu)
{
	gpu->min_u = UINT16_MAX;
	gpu->min_v = UINT16_MAX;
	gpu->max_u = 0;
	gpu->max_v = 0;
}

// Determine min and max UVs sampled for a given primitive
void Extend_UVLimits(PS_GPU *gpu, tri_vertex *vertices, unsigned count)
{
	uint8 twx = gpu->SUCV.TWX_AND;
	uint8 twy = gpu->SUCV.TWY_AND;

	uint16 min_u = gpu->min_u;
	uint16 min_v = gpu->min_v;
	uint16 max_u = gpu->max_u;
	uint16 max_v = gpu->max_v;

	if ((twx == (uint8)0xffu) && (twy == (uint8)0xffu))
	{
		// If we're not using texture window, we're likely accessing a small subset of the texture.
		for (unsigned int i = 0; i < count; i++)
		{
			min_u = std::min(min_u, uint16_t(vertices[i].u));
			min_v = std::min(min_v, uint16_t(vertices[i].v));
			max_u = std::max(max_u, uint16_t(vertices[i].u));
			max_v = std::max(max_v, uint16_t(vertices[i].v));
		}
	}
	else
	{
		// texture window so don't clamp texture
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

// Apply offsets to UV limits before returning
void Finalise_UVLimits(PS_GPU *gpu)
{
	uint8 twx = gpu->SUCV.TWX_AND;
	uint8 twy = gpu->SUCV.TWY_AND;

	uint16 min_u = gpu->min_u;
	uint16 min_v = gpu->min_v;
	uint16 max_u = gpu->max_u;
	uint16 max_v = gpu->max_v;

	uint16 off_u = gpu->off_u;
	uint16 off_v = gpu->off_v;

	if ((twx == (uint8)0xffu) && (twy == (uint8)0xffu))
	{
		// offset output UV Limits
		min_u += off_u;
		min_v += off_v;
		max_u += off_u;
		max_v += off_v;

		// In nearest neighbor, we'll get *very* close to this UV, but not close enough to actually sample it.
		// If du/dx or dv/dx are negative, we probably need to invert this though ...
		if (max_u > min_u)
			max_u--;
		if (max_v > min_v)
			max_v--;

		// If there's no wrapping, we can prewrap and avoid fallback.
		if ((max_u & 0xff00) == (min_u & 0xff00))
			max_u &= 0xff;
		if ((max_v & 0xff00) == (min_v & 0xff00))
			max_v &= 0xff;
	}
	else
	{
		// texture window so don't clamp texture
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


// 0 = disabled
// 1 = enabled (default mode)
// 2 = enabled (aggressive mode)

// Hack to deal with PS1 games rendering axis aligned lines using 1 pixel wide triangles with UVs that describe a line
// Suitable for games like Soul Blade, Doom and Hexen
bool Hack_FindLine(PS_GPU *gpu, tri_vertex* vertices, tri_vertex* outVertices)
{
	int32 pxWidth = 1 << gpu->upscale_shift;	// width of a single pixel
	uint8 cornerIdx, shortIdx, longIdx;

	// reject 3D elements
	if ((vertices[0].precise[2] != vertices[1].precise[2]) ||
		(vertices[1].precise[2] != vertices[2].precise[2]))
		return false;

	// find short side of triangle / end of line with 2 vertices (guess which vertex is the right angle)
	if ((vertices[0].u == vertices[1].u) && (vertices[0].v == vertices[1].v))
		cornerIdx = 0;
	else if ((vertices[1].u == vertices[2].u) && (vertices[1].v == vertices[2].v))
		cornerIdx = 1;
	else if ((vertices[2].u == vertices[0].u) && (vertices[2].v == vertices[0].v))
		cornerIdx = 2;
	else
		return false;

	// assign other indices to remaining vertices
	shortIdx = (cornerIdx + 1) % 3;
	longIdx = (shortIdx + 1) % 3;

	// determine line orientation and check width
	if ((vertices[cornerIdx].x == vertices[shortIdx].x) && (abs(vertices[cornerIdx].y - vertices[shortIdx].y) == pxWidth))
	{
		// line is horizontal
		// determine which is truly the corner by checking against the long side, while making sure it is axis aligned
		if (vertices[shortIdx].y == vertices[longIdx].y)
		{
			uint8 tempIdx = shortIdx;
			shortIdx = cornerIdx;
			cornerIdx = tempIdx;
		}
		else if (vertices[cornerIdx].y != vertices[longIdx].y)
			return false;

		// flip corner index to other side of quad
		outVertices[cornerIdx] = vertices[longIdx];
		outVertices[cornerIdx].y = vertices[shortIdx].y;
		outVertices[cornerIdx].precise[1] = vertices[shortIdx].precise[1];
	}
	else if ((vertices[cornerIdx].y == vertices[shortIdx].y) && (abs(vertices[cornerIdx].x - vertices[shortIdx].x) == pxWidth))
	{
		// line is vertical
		// determine which is truly the corner by checking against the long side, while making sure it is axis aligned
		if (vertices[shortIdx].x == vertices[longIdx].x)
		{
			uint8 tempIdx = shortIdx;
			shortIdx = cornerIdx;
			cornerIdx = tempIdx;
		}
		else if (vertices[cornerIdx].x != vertices[longIdx].x)
			return false;

		// flip corner index to other side of quad
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

// Hack to deal with PS1 games rendering axis aligned lines using 1 pixel wide triangles and force UVs to describe a line
// Required for games like Dark Forces and Duke Nukem
bool Hack_ForceLine(PS_GPU *gpu, tri_vertex* vertices, tri_vertex* outVertices)
{
	int32 pxWidth = 1 << gpu->upscale_shift;	// width of a single pixel
	uint8 cornerIdx, shortIdx, longIdx;

	// reject 3D elements
	if ((vertices[0].precise[2] != vertices[1].precise[2]) ||
		(vertices[1].precise[2] != vertices[2].precise[2]))
		return false;

	// find vertical AB
	uint8 A, B, C;
	if (vertices[0].x == vertices[1].x)
		A = 0;
	else if (vertices[1].x == vertices[2].x)
		A = 1;
	else if (vertices[2].x == vertices[0].x)
		A = 2;
	else
		return false;

	// assign other indices to remaining vertices
	B = (A + 1) % 3;
	C = (B + 1) % 3;

	// find horizontal AC or BC
	if (vertices[A].y == vertices[C].y)
		cornerIdx = A;
	else if (vertices[B].y == vertices[C].y)
		cornerIdx = B;
	else
		return false;

	// determine lengths of sides
	if (abs(vertices[A].y - vertices[B].y) == pxWidth)
	{
		// is Horizontal
		shortIdx = (cornerIdx == A) ? B : A;
		longIdx = C;

		// flip corner index to other side of quad
		outVertices[cornerIdx] = vertices[longIdx];
		outVertices[cornerIdx].y = vertices[shortIdx].y;
		outVertices[cornerIdx].precise[1] = vertices[shortIdx].precise[1];
	}
	else if (abs(vertices[A].x - vertices[C].x) == pxWidth)
	{
		// is Vertical
		shortIdx = C;
		longIdx = (cornerIdx == A) ? B : A;

		// flip corner index to other side of quad
		outVertices[cornerIdx] = vertices[longIdx];
		outVertices[cornerIdx].x = vertices[shortIdx].x;
		outVertices[cornerIdx].precise[0] = vertices[shortIdx].precise[0];
	}
	else
		return false;

	// force UVs into a line along the upper or left most edge of the triangle
	// Otherwise the wrong UVs will be sampled on second triangle and by hardware renderers
	vertices[shortIdx].u = vertices[cornerIdx].u;
	vertices[shortIdx].v = vertices[cornerIdx].v;

	// copy other two vertices
	outVertices[shortIdx] = vertices[shortIdx];
	outVertices[longIdx] = vertices[longIdx];

	return true;
}
