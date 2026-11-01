/* GPU polygon UV and line helpers. */

#include "gpu.h"
#include "../../rhi/rhi_intf.h"
#include "../../beetle_psx_globals.h"

#include <retro_miscellaneous.h>

static bool IsAxisAlignedTriangle(const tri_vertex *vertices)
{
	return vertices[0].precise[2] == vertices[1].precise[2] &&
		vertices[1].precise[2] == vertices[2].precise[2] &&
		(vertices[0].x == vertices[1].x || vertices[0].x == vertices[2].x ||
		 vertices[1].x == vertices[2].x) &&
		(vertices[0].y == vertices[1].y || vertices[0].y == vertices[2].y ||
		 vertices[1].y == vertices[2].y);
}

/* Match decoder size culling, including retained vertices. */
static bool TriangleFits(const tri_vertex *vertices, unsigned shift)
{
	int32_t min_x = MIN(vertices[0].x, MIN(vertices[1].x, vertices[2].x));
	int32_t max_x = MAX(vertices[0].x, MAX(vertices[1].x, vertices[2].x));
	int32_t min_y = MIN(vertices[0].y, MIN(vertices[1].y, vertices[2].y));
	int32_t max_y = MAX(vertices[0].y, MAX(vertices[1].y, vertices[2].y));
	return (int64_t)max_x - min_x < ((int64_t)1024 << shift) &&
		(int64_t)max_y - min_y < ((int64_t)512 << shift);
}

/* Classify the complete strip; one aligned triangle does not imply a sprite. */
static bool IsAxisAlignedQuad(const tri_vertex *first,
      const tri_vertex *vertices, bool triangle_is_3d)
{
	const tri_vertex *b = &vertices[0];
	const tri_vertex *c = &vertices[1];
	const tri_vertex *d = &vertices[2];
	return !triangle_is_3d && first->precise[2] == b->precise[2] &&
		((first->x == c->x && b->x == d->x &&
		  first->y == b->y && c->y == d->y &&
		  first->x != b->x && first->y != c->y) ||
		 (first->x == b->x && c->x == d->x &&
		  first->y == c->y && b->y == d->y &&
		  first->x != c->x && first->y != b->y));
}

/* Calculate UV offsets and shared one-pixel vertex adjustments. */
void Calc_UVOffsets_Adjust_Verts(PS_GPU *gpu, tri_vertex *vertices, unsigned count)
{
	/* iCB: Just borrowing this from \parallel-psx\renderer\renderer.cpp */
	uint16_t off_u = 0;
	uint16_t off_v = 0;
	bool may_be_2d = false;
	enum rhi_renderer_type renderer = rhi_intf_is_type();
	bool hardware_command = count == 4 && gpu->InCmd == INCMD_QUAD &&
		(renderer == RHI_OPENGL || renderer == RHI_VULKAN);
	bool hardware_quad = false;
	if (gpu->InCmd == INCMD_QUAD)
	{
		off_u = gpu->off_u;
		off_v = gpu->off_v;
		may_be_2d = gpu->may_be_2d;
	}

	/* Hardware interpolation can cross a flipped sprite's texel boundary.
	 * Keep edge corrections for sprite-like mappings, not oblique mesh quads. */
	{
		/* Exact derivatives keep sign/zero tests deterministic when upscaled,
		 * including the shared one-pixel vertex adjustment. */
		int64_t abx = (int64_t)vertices[1].x - vertices[0].x;
		int64_t aby = (int64_t)vertices[1].y - vertices[0].y;
		int64_t bcx = (int64_t)vertices[2].x - vertices[1].x;
		int64_t bcy = (int64_t)vertices[2].y - vertices[1].y;
		int64_t cax = (int64_t)vertices[0].x - vertices[2].x;
		int64_t cay = (int64_t)vertices[0].y - vertices[2].y;

		/* Derivative numerators for the current triangle. */
		int64_t dudx = -aby * vertices[2].u - bcy * vertices[0].u - cay * vertices[1].u;
		int64_t dvdx = -aby * vertices[2].v - bcy * vertices[0].v - cay * vertices[1].v;
		int64_t dudy = +abx * vertices[2].u + bcx * vertices[0].u + cax * vertices[1].u;
		int64_t dvdy = +abx * vertices[2].v + bcx * vertices[0].v + cax * vertices[1].v;
		int64_t area = bcx * cay - bcy * cax;

		/* iCB: Detect and reject any triangles with 0 size texture area */
		int64_t texArea = ((int64_t)vertices[1].u - vertices[0].u) * (vertices[2].v - vertices[0].v) - ((int64_t)vertices[2].u - vertices[0].u) * (vertices[1].v - vertices[0].v);

		/* Leverage PGXP to further avoid 3D polygons that just happen to align this way after projection */
		bool is3D = ((vertices[0].precise[2] != vertices[1].precise[2]) || (vertices[1].precise[2] != vertices[2].precise[2]));

		bool first_valid = false;
		bool second_valid = false;
		if (hardware_command)
		{
			const tri_vertex *first = gpu->InQuad_F3Vertices;
			int64_t first_area = 0;
			first_valid = !(gpu->killQuadPart & 1) &&
				TriangleFits(first, gpu->upscale_shift);
			if (first_valid)
				first_area = ((int64_t)first[1].x - first[0].x) *
					((int64_t)first[2].y - first[0].y) - ((int64_t)first[2].x - first[0].x) *
					((int64_t)first[1].y - first[0].y);
			first_valid = first_valid && first_area != 0;
			second_valid = !(gpu->killQuadPart & 2) && area != 0 &&
				TriangleFits(vertices, gpu->upscale_shift);
			hardware_quad = first_valid && second_valid && texArea != 0;
			if (hardware_quad)
			{
				/* Recompute complete-quad corrections from retained vertices. */
				off_u = off_v = 0;
				may_be_2d = false;
			}
		}

		/* Shouldn't matter as degenerate primitives will be culled anyways. */
		if ((area != 0) && (texArea != 0) && !is3D)
		{
			/* Derivative sign is numerator sign XOR area sign; zero stays zero. */
			bool neg_area  = area < 0;
			bool neg_dudx  = (dudx < 0) != neg_area;
			bool neg_dudy  = (dudy < 0) != neg_area;
			bool neg_dvdx  = (dvdx < 0) != neg_area;
			bool neg_dvdy  = (dvdy < 0) != neg_area;
			bool zero_dudx = dudx == 0;
			bool zero_dudy = dudy == 0;
			bool zero_dvdx = dvdx == 0;
			bool zero_dvdy = dvdy == 0;

			/* Use complete-quad geometry when available, otherwise the legacy heuristic. */
			if (hardware_quad)
				may_be_2d = IsAxisAlignedQuad(&gpu->InQuad_F3Vertices[0], vertices, is3D) &&
					((zero_dudy && zero_dvdx) || (zero_dudx && zero_dvdy));
			else
				may_be_2d = may_be_2d || zero_dudy || zero_dudx || zero_dvdy || zero_dvdx;

			/* Correct flipped sprite-like UVs. Complete hardware quads must pass
			 * the geometry check; other primitives retain the legacy flip policy. */
			if ((!hardware_quad || may_be_2d) &&
				(renderer != RHI_VULKAN || psx_gpu_upscale_shift_hw))
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
		if (hardware_command && !hardware_quad && (first_valid || second_valid))
		{
			/* Preserve 2D fallbacks; collapsed UVs need the saved first mapping,
			 * which may already include one-pixel vertex adjustments. */
			bool sprite_geometry = first_valid && second_valid ?
				IsAxisAlignedQuad(&gpu->InQuad_F3Vertices[0], vertices, is3D) :
				IsAxisAlignedTriangle(first_valid ? gpu->InQuad_F3Vertices : vertices);
			if (!sprite_geometry)
			{
				off_u = off_v = 0;
				may_be_2d = false;
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
