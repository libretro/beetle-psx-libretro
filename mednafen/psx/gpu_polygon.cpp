#include <math.h>
#include <algorithm>
#include "libretro_cbs.h"

#define COORD_FBS 12
#define COORD_MF_INT(n) ((n) << COORD_FBS)
#define COORD_POST_PADDING 12

struct i_group
{
   uint32_t u, v;
   uint32_t r, g, b;
};

struct i_deltas
{
   uint32_t du_dx, dv_dx;
   uint32_t dr_dx, dg_dx, db_dx;

   uint32_t du_dy, dv_dy;
   uint32_t dr_dy, dg_dy, db_dy;
};

static INLINE int64_t MakePolyXFP(uint32_t x)
{
   return ((uint64_t)x << 32) + ((UINT64_C(1) << 32) - (1 << 11));
}

static INLINE int64_t MakePolyXFPStep(int32_t dx, int32_t dy)
{
   int64_t ret;
   int64_t dx_ex = (int64_t)dx << 32;

   if(dx_ex < 0)
      dx_ex -= dy - 1;

   if(dx_ex > 0)
      dx_ex += dy - 1;

   ret = dx_ex / dy;

   return(ret);
}

static INLINE int32_t GetPolyXFP_Int(int64_t xfp)
{
   return(xfp >> 32);
}

#define CALCIS(x,y) (((B.x - A.x) * (C.y - B.y)) - ((C.x - B.x) * (B.y - A.y)))
template<bool goraud, bool textured>
static INLINE bool CalcIDeltas(i_deltas &idl, const tri_vertex &A, const tri_vertex &B, const tri_vertex &C)
{
 int32 denom = CALCIS(x, y);

 if(!denom)
  return(false);

 if(goraud)
 {
  idl.dr_dx = (uint32)(CALCIS(r, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
  idl.dr_dy = (uint32)(CALCIS(x, r) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;

  idl.dg_dx = (uint32)(CALCIS(g, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
  idl.dg_dy = (uint32)(CALCIS(x, g) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;

  idl.db_dx = (uint32)(CALCIS(b, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
  idl.db_dy = (uint32)(CALCIS(x, b) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
 }

 if(textured)
 {
  idl.du_dx = (uint32)(CALCIS(u, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
  idl.du_dy = (uint32)(CALCIS(x, u) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;

  idl.dv_dx = (uint32)(CALCIS(v, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
  idl.dv_dy = (uint32)(CALCIS(x, v) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING;
 }

 return(true);
}
#undef CALCIS

template<bool goraud, bool textured>
static INLINE void AddIDeltas_DX(i_group &ig, const i_deltas &idl, uint32_t count = 1)
{
   if(textured)
   {
      ig.u += idl.du_dx * count;
      ig.v += idl.dv_dx * count;
   }

   if(goraud)
   {
      ig.r += idl.dr_dx * count;
      ig.g += idl.dg_dx * count;
      ig.b += idl.db_dx * count;
   }
}

template<bool goraud, bool textured>
static INLINE void AddIDeltas_DY(i_group &ig, const i_deltas &idl, uint32_t count = 1)
{
   if(textured)
   {
      ig.u += idl.du_dy * count;
      ig.v += idl.dv_dy * count;
   }

   if(goraud)
   {
      ig.r += idl.dr_dy * count;
      ig.g += idl.dg_dy * count;
      ig.b += idl.db_dy * count;
   }
}

template<bool goraud, bool textured, int BlendMode, bool TexMult, uint32 TexMode_TA, bool MaskEval_TA>
static INLINE void DrawSpan(PS_GPU *gpu, int y, const int32 x_start, const int32 x_bound, i_group ig, const i_deltas &idl)
{
   if(LineSkipTest(gpu, y >> gpu->upscale_shift))
      return;

   int32 clipx0 = gpu->ClipX0 << gpu->upscale_shift;
   int32 clipx1 = gpu->ClipX1 << gpu->upscale_shift;


  int32 x_ig_adjust = x_start;
  int32 w = x_bound - x_start;
  //int32 x = x_start;
  int32 x = sign_x_to_s32(11 + gpu->upscale_shift, x_start);

  bool dither      = DitherEnabled(gpu);

  if(x < clipx0)
  {
   int32 delta = clipx0 - x;
   x_ig_adjust += delta;
   x += delta;
   w -= delta;
  }

  if((x + w) > (clipx1 + 1))
   w = clipx1 + 1 - x;

  if(w <= 0)
   return;

  //printf("%d %d %d %d\n", x, w, ClipX0, ClipX1);

  AddIDeltas_DX<goraud, textured>(ig, idl, x_ig_adjust);
  AddIDeltas_DY<goraud, textured>(ig, idl, y);

  // Only compute timings for one every `upscale_shift` lines so that
  // we don't end up "slower" than 1x
  if ((y & ((1UL << gpu->upscale_shift) - 1)) == 0) {
     if(goraud || textured)
        gpu->DrawTimeAvail -= (w * 2) >> gpu->upscale_shift;
     else if((BlendMode >= 0) || MaskEval_TA)
        gpu->DrawTimeAvail -= (w + ((w + 1) >> 1)) >> gpu->upscale_shift;
     else
        gpu->DrawTimeAvail -= w >> gpu->upscale_shift;
  }

  do
  {
   const uint32 r = ig.r >> (COORD_FBS + COORD_POST_PADDING);
   const uint32 g = ig.g >> (COORD_FBS + COORD_POST_PADDING);
   const uint32 b = ig.b >> (COORD_FBS + COORD_POST_PADDING);
   uint32 dither_x = (x >> gpu->dither_upscale_shift) & 3;
   uint32 dither_y = (y >> gpu->dither_upscale_shift) & 3;

   //assert(x >= ClipX0 && x <= ClipX1);

   if(textured)
   {
	   uint16 fbw = GetTexel<TexMode_TA>(gpu, ig.u >> (COORD_FBS + COORD_POST_PADDING), ig.v >> (COORD_FBS + COORD_POST_PADDING));

    if(fbw)
    {
     if(TexMult)
     {

      if(!DitherEnabled(gpu))
      {
       dither_x = 3;
       dither_y = 2;
      }

      uint8_t *dither_offset = gpu->DitherLUT[dither_y][dither_x];
      fbw = ModTexel(dither_offset, fbw, r, g, b);
     }
     PlotPixel<BlendMode, MaskEval_TA, true>(gpu, x, y, fbw);
    }
   }
   else
   {
    uint16 pix = 0x8000;

    if(goraud && DitherEnabled(gpu))
    {
     pix |= gpu->DitherLUT[dither_y][dither_x][r] << 0;
     pix |= gpu->DitherLUT[dither_y][dither_x][g] << 5;
     pix |= gpu->DitherLUT[dither_y][dither_x][b] << 10;
    }
    else
    {
     pix |= (r >> 3) << 0;
     pix |= (g >> 3) << 5;
     pix |= (b >> 3) << 10;
    }

    PlotPixel<BlendMode, MaskEval_TA, false>(gpu, x, y, pix);
   }

   x++;
   AddIDeltas_DX<goraud, textured>(ig, idl);
  } while(MDFN_LIKELY(--w > 0));
}

template<bool goraud, bool textured, int BlendMode, bool TexMult, uint32_t TexMode_TA, bool MaskEval_TA>
static INLINE void DrawTriangle(PS_GPU *gpu, tri_vertex *vertices)
{
   i_deltas idl;
   unsigned core_vertex;

   int32 clipy0 = gpu->ClipY0 << gpu->upscale_shift;
   int32 clipy1 = gpu->ClipY1 << gpu->upscale_shift;

   //
   // Calculate the "core" vertex based on the unsorted input vertices, and sort vertices by Y.
   //
   {
      unsigned cvtemp = 0;

      if(vertices[1].x <= vertices[0].x)
         {
            if(vertices[2].x <= vertices[1].x)
               cvtemp = (1 << 2);
            else
               cvtemp = (1 << 1);
         }
      else if(vertices[2].x < vertices[0].x)
         cvtemp = (1 << 2);
      else
         cvtemp = (1 << 0);

      if(vertices[2].y < vertices[1].y)
         {
            std::swap(vertices[2], vertices[1]);
            cvtemp = ((cvtemp >> 1) & 0x2) | ((cvtemp << 1) & 0x4) | (cvtemp & 0x1);
         }

      if(vertices[1].y < vertices[0].y)
         {
            std::swap(vertices[1], vertices[0]);
            cvtemp = ((cvtemp >> 1) & 0x1) | ((cvtemp << 1) & 0x2) | (cvtemp & 0x4);
         }

      if(vertices[2].y < vertices[1].y)
         {
            std::swap(vertices[2], vertices[1]);
            cvtemp = ((cvtemp >> 1) & 0x2) | ((cvtemp << 1) & 0x4) | (cvtemp & 0x1);
         }

      core_vertex = cvtemp >> 1;
   }

   //
   // 0-height, abort out.
   //
   if(vertices[0].y == vertices[2].y)
      return;


   if(!CalcIDeltas<goraud, textured>(idl, vertices[0], vertices[1], vertices[2]))
      return;


 // [0] should be top vertex, [2] should be bottom vertex, [1] should be off to the side vertex.
 //
 //
 int64 base_coord;
 int64 base_step;

 int64 bound_coord_us;
 int64 bound_coord_ls;

 bool right_facing;
 i_group ig;

 if(textured)
 {
  ig.u = (COORD_MF_INT(vertices[core_vertex].u) + (1 << (COORD_FBS - 1 - gpu->upscale_shift))) << COORD_POST_PADDING;
  ig.v = (COORD_MF_INT(vertices[core_vertex].v) + (1 << (COORD_FBS - 1 - gpu->upscale_shift))) << COORD_POST_PADDING;

  if (gpu->upscale_shift > 0)
     {
        // Bias the texture coordinates so that it rounds to the
        // correct value when the game is mapping a 2D sprite using
        // triangles. Otherwise this could cause a small "shift" in
        // the texture coordinates when upscaling.

		 if(gpu->off_u)
			ig.u += (COORD_MF_INT(1) - (1 << (COORD_FBS - gpu->upscale_shift))) << COORD_POST_PADDING;
		 if (gpu->off_v)
			 ig.v += (COORD_MF_INT(1) - (1 << (COORD_FBS - gpu->upscale_shift))) << COORD_POST_PADDING;
     }
 }

 ig.r = (COORD_MF_INT(vertices[core_vertex].r) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING;
 ig.g = (COORD_MF_INT(vertices[core_vertex].g) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING;
 ig.b = (COORD_MF_INT(vertices[core_vertex].b) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING;

 AddIDeltas_DX<goraud, textured>(ig, idl, -vertices[core_vertex].x);
 AddIDeltas_DY<goraud, textured>(ig, idl, -vertices[core_vertex].y);

 base_coord = MakePolyXFP(vertices[0].x);
 base_step = MakePolyXFPStep((vertices[2].x - vertices[0].x), (vertices[2].y - vertices[0].y));

   //
   //
   //

if(vertices[1].y == vertices[0].y)
 {
  bound_coord_us = 0;
  right_facing = (bool)(vertices[1].x > vertices[0].x);
 }
 else
 {
  bound_coord_us = MakePolyXFPStep((vertices[1].x - vertices[0].x), (vertices[1].y - vertices[0].y));
  right_facing = (bool)(bound_coord_us > base_step);
 }

 if(vertices[2].y == vertices[1].y)
  bound_coord_ls = 0;
 else
  bound_coord_ls = MakePolyXFPStep((vertices[2].x - vertices[1].x), (vertices[2].y - vertices[1].y));

 //
 // Left side draw order
 //
 // core_vertex == 0
 //	Left(base): vertices[0] -> (?vertices[1]?) -> vertices[2]
 //
 // core_vertex == 1:
 // 	Left(base): vertices[1] -> vertices[2], vertices[1] -> vertices[0]
 //
 // core_vertex == 2:
 //	Left(base): vertices[2] -> (?vertices[1]?) -> vertices[0]
 //printf("%d %d\n", core_vertex, right_facing);
 struct tripart
 {
  uint64 x_coord[2];
  uint64 x_step[2];

  int32 y_coord;
  int32 y_bound;

  bool dec_mode;
 } tripart[2];

#if 0
 switch(core_vertex)
 {
  case 0:
	tripart[0].dec_mode = tripart[1].dec_mode = false;

	tripart[0].y_coord = vertices[0].y;
	tripart[0].y_bound = vertices[1].y;
	if(vertices[0].y != vertices[1].y)
	{
	 tripart[0].x_coord[0] = MakePolyXFP(vertices[0].x);
	 tripart[0].x_step[0] =

	 tripart[0].x_coord[1] = MakePolyXFP(vertices[0].x);
	 tripart[0].x_step[1] =
	}
	break;

  case 1:
	break;

  case 2:
	break;
 }
#endif

 unsigned vo = 0;
 unsigned vp = 0;

 if(core_vertex)
  vo = 1;

 if(core_vertex == 2)
  vp = 3;

 {
    struct tripart* tp = &tripart[vo];

  tp->y_coord = vertices[0 ^ vo].y;
  tp->y_bound = vertices[1 ^ vo].y;
  tp->x_coord[right_facing] = MakePolyXFP(vertices[0 ^ vo].x);
  tp->x_step[right_facing] = bound_coord_us;
  tp->x_coord[!right_facing] = base_coord + ((vertices[vo].y - vertices[0].y) * base_step);
  tp->x_step[!right_facing] = base_step;
  tp->dec_mode = vo;
 }

 {
    struct tripart* tp = &tripart[vo ^ 1];

  tp->y_coord = vertices[1 ^ vp].y;
  tp->y_bound = vertices[2 ^ vp].y;
  tp->x_coord[right_facing] = MakePolyXFP(vertices[1 ^ vp].x);
  tp->x_step[right_facing] = bound_coord_ls;
  tp->x_coord[!right_facing] = base_coord + ((vertices[1 ^ vp].y - vertices[0].y) * base_step); //base_coord + ((vertices[1].y - vertices[0].y) * base_step);
  tp->x_step[!right_facing] = base_step;
  tp->dec_mode = vp;
 }

 for(unsigned i = 0; i < 2; i++) //2; i++)
 {
  int32 yi = tripart[i].y_coord;
  int32 yb = tripart[i].y_bound;

  uint64 lc = tripart[i].x_coord[0];
  uint64 ls = tripart[i].x_step[0];

  uint64 rc = tripart[i].x_coord[1];
  uint64 rs = tripart[i].x_step[1];

  if(tripart[i].dec_mode)
  {
   while(MDFN_LIKELY(yi > yb))
   {
    yi--;
    lc -= ls;
    rc -= rs;
    //
    //
    //
    int32 y = sign_x_to_s32(11 + gpu->upscale_shift, yi);

    if(y < clipy0)
     break;

    if(y > clipy1)
    {
     gpu->DrawTimeAvail -= 2;
     continue;
    }

    DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(gpu, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, idl);
   }
  }
  else
  {
   while(MDFN_LIKELY(yi < yb))
   {
      int32 y = sign_x_to_s32(11 + gpu->upscale_shift, yi);

    if(y > clipy1)
     break;

    if(y < clipy0)
    {
     gpu->DrawTimeAvail -= 2;
     goto skipit;
    }

    DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(gpu, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, idl);
    //
    //
    //
    skipit: ;
    yi++;
    lc += ls;
    rc += rs;
   }
  }
 }

#if 0
 printf("[GPU] Vertices: %d:%d(r=%d, g=%d, b=%d) -> %d:%d(r=%d, g=%d, b=%d) -> %d:%d(r=%d, g=%d, b=%d)\n\n\n", vertices[0].x, vertices[0].y,
	vertices[0].r, vertices[0].g, vertices[0].b,
	vertices[1].x, vertices[1].y,
	vertices[1].r, vertices[1].g, vertices[1].b,
	vertices[2].x, vertices[2].y,
	vertices[2].r, vertices[2].g, vertices[2].b);
#endif
}


// Determine whether to offset UVs to account for difference in interpolation between PS1 and modern GPUs
void Calc_UVOffsets(PS_GPU *gpu, tri_vertex *vertices, unsigned count)
{
	// iCB: Just borrowing this from \parallel-psx\renderer\renderer.cpp
	uint16 off_u = 0;
	uint16 off_v = 0;

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
	if (count == 4)
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
			if (neg_dudx && zero_dudy)
				off_u++;
			else if (neg_dudy && zero_dudx)
				off_u++;
			if (neg_dvdx && zero_dvdy)
				off_v++;
			else if (neg_dvdy && zero_dvdx)
				off_v++;
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


template<int numvertices, bool goraud, bool textured, int BlendMode, bool TexMult, uint32_t TexMode_TA, bool MaskEval_TA, bool pgxp>
static void Command_DrawPolygon(PS_GPU *gpu, const uint32_t *cb)
{
   tri_vertex vertices[3];
   const uint32_t* baseCB = cb;
   const unsigned cb0     = cb[0];
   uint32_t clut          = 0;
   unsigned sv            = 0;
   bool invalidW          = false;
   //uint32_t tpage       = 0;

   vertices[0].x          = 0;
   vertices[0].y          = 0;
   vertices[0].u          = 0;
   vertices[0].v          = 0;
   vertices[0].r          = 0;
   vertices[0].g          = 0;
   vertices[0].b          = 0;
   vertices[0].precise[0] = 0.0f;
   vertices[0].precise[1] = 0.0f;
   vertices[0].precise[2] = 0.0f;

   vertices[1].x          = 0;
   vertices[1].y          = 0;
   vertices[1].u          = 0;
   vertices[1].v          = 0;
   vertices[1].r          = 0;
   vertices[1].g          = 0;
   vertices[1].b          = 0;
   vertices[1].precise[0] = 0.0f;
   vertices[1].precise[1] = 0.0f;
   vertices[1].precise[2] = 0.0f;

   vertices[2].x          = 0;
   vertices[2].y          = 0;
   vertices[2].u          = 0;
   vertices[2].v          = 0;
   vertices[2].r          = 0;
   vertices[2].g          = 0;
   vertices[2].b          = 0;
   vertices[2].precise[0] = 0.0f;
   vertices[2].precise[1] = 0.0f;
   vertices[2].precise[2] = 0.0f;

   // Base timing is approximate, and could be improved.
   if(numvertices == 4 && gpu->InCmd == INCMD_QUAD)
      gpu->DrawTimeAvail -= (28 + 18);
   else
      gpu->DrawTimeAvail -= (64 + 18);

   if(goraud && textured)
      gpu->DrawTimeAvail -= 150 * 3;
   else if(goraud)
      gpu->DrawTimeAvail -= 96 * 3;
   else if(textured)
      gpu->DrawTimeAvail -= 60 * 3;

   // if entire previous quad was rejected reset flags
   if (gpu->killQuadPart == 3)
	   gpu->killQuadPart = 0;

   if(numvertices == 4)
   {
      if(gpu->InCmd == INCMD_QUAD)
      {
         memcpy(&vertices[0], &gpu->InQuad_F3Vertices[1], 2 * sizeof(tri_vertex));
         clut = gpu->InQuad_clut;
       invalidW = gpu->InQuad_invalidW;
         sv = 2;
      }
   }
   //else
   // memset(vertices, 0, sizeof(vertices));

   for(unsigned v = sv; v < 3; v++)
   {
      if(v == 0 || goraud)
      {
         uint32_t raw_color = (*cb & 0xFFFFFF);

         vertices[v].r = raw_color & 0xFF;
         vertices[v].g = (raw_color >> 8) & 0xFF;
         vertices[v].b = (raw_color >> 16) & 0xFF;

         cb++;
      }
      else
      {
         vertices[v].r = vertices[0].r;
         vertices[v].g = vertices[0].g;
         vertices[v].b = vertices[0].b;
      }

      int32 x = sign_x_to_s32(11, ((int16_t)(*cb & 0xFFFF)));
      int32 y = sign_x_to_s32(11, ((int16_t)(*cb >> 16)));

      vertices[v].x = (x + gpu->OffsX) << gpu->upscale_shift;
      vertices[v].y = (y + gpu->OffsY) << gpu->upscale_shift;

      if (pgxp) {
   OGLVertex vert;
   PGXP_GetVertex(cb - baseCB, cb, &vert, 0, 0);

   vertices[v].precise[0] = ((vert.x + (float)gpu->OffsX) * UPSCALE(gpu));
   vertices[v].precise[1] = ((vert.y + (float)gpu->OffsY) * UPSCALE(gpu));
   vertices[v].precise[2] = vert.w;

   if (!vert.valid_w)
     invalidW = true;

      } else {
   vertices[v].precise[0] = (float)x + gpu->OffsX;
   vertices[v].precise[1] = (float)y + gpu->OffsY;

   invalidW = true;
      }

      cb++;

      if(textured)
      {
         vertices[v].u = (*cb & 0xFF);
         vertices[v].v = (*cb >> 8) & 0xFF;

         if(v == 0)
         {
            clut = ((*cb >> 16) & 0xFFFF) << 4;
            Update_CLUT_Cache<TexMode_TA>(gpu, (*cb >> 16) & 0xFFFF);
         }

         cb++;
      }
   }

   // iCB: If any vertices lack w components then set all to 1
   if (invalidW)
      for (unsigned v = 0; v < 3; v++)
         vertices[v].precise[2] = 1.f;

   // Calculated UV offsets (needed for hardware renderers and software with scaling)
   // Do one time updates for primitive
   if (textured && (gpu->InCmd != INCMD_QUAD))
      Calc_UVOffsets(gpu, vertices, numvertices);

   if(numvertices == 4)
   {
      if(gpu->InCmd == INCMD_QUAD)
      {
         gpu->InCmd = INCMD_NONE;

         // default first vertex of quad to 1 if any of the vertices are 1 (even if the first triangle was okay)
         if (invalidW)
            gpu->InQuad_F3Vertices[0].precise[2] = 1.f;
      }
      else
      {
         gpu->InCmd = INCMD_QUAD;
         gpu->InCmd_CC = cb0 >> 24;
         memcpy(&gpu->InQuad_F3Vertices[0], &vertices[0], sizeof(tri_vertex) * 3);
         gpu->InQuad_clut = clut;
         gpu->InQuad_invalidW = invalidW;
      }
   }

   if(abs(vertices[2].y - vertices[0].y) >= (512 << gpu->upscale_shift) ||
      abs(vertices[2].y - vertices[1].y) >= (512 << gpu->upscale_shift) ||
      abs(vertices[1].y - vertices[0].y) >= (512 << gpu->upscale_shift))
     {
       //PSX_WARNING("[GPU] Triangle height too large: %d", (vertices[2].y - vertices[0].y));
		 if (numvertices == 4)
			 gpu->killQuadPart |= (gpu->InCmd == INCMD_QUAD) ? 1 : 2;
     
		 // hardware renderer still needs to render first triangle
		 if ((rsx_intf_is_type() == RSX_SOFTWARE) || (gpu->killQuadPart != 2))
			 return;
     }

   if(abs(vertices[2].x - vertices[0].x) >= (1024 << gpu->upscale_shift) ||
      abs(vertices[2].x - vertices[1].x) >= (1024 << gpu->upscale_shift) ||
      abs(vertices[1].x - vertices[0].x) >= (1024 << gpu->upscale_shift))
     {
       //PSX_WARNING("[GPU] Triangle width too large: %d %d %d", abs(vertices[2].x - vertices[0].x), abs(vertices[2].x - vertices[1].x), abs(vertices[1].x - vertices[0].x));
		 if (numvertices == 4)
			 gpu->killQuadPart |= (gpu->InCmd == INCMD_QUAD) ? 1 : 2;
		 
		 // hardware renderer still needs to render first triangle
		 if ((rsx_intf_is_type() == RSX_SOFTWARE) || (gpu->killQuadPart != 2))
			return;
     }

   uint16_t clut_x = (clut & (0x3f << 4));
   uint16_t clut_y = (clut >> 10) & 0x1ff;

   tri_vertex lineVertices[3];	// Line Render: store second triangle vertices (software renderer modifies originals)
	bool lineFound = false;		// Used to loop drawing code to draw second triangle (avoids second inline call)
	do
	{

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
		enum blending_modes blend_mode = BLEND_MODE_AVERAGE;

		if (textured)
		{
			if (TexMult)
				blend_mode = BLEND_MODE_SUBTRACT;
			else
				blend_mode = BLEND_MODE_ADD;
		}

		// Line Renderer: Detect triangles that would resolve as lines at x1 scale and create second triangle to make quad
		if ((lineRenderMode != 0) && (!lineFound) && (numvertices == 3) && (textured))
		{
			if(lineRenderMode == 1)
				lineFound = Hack_FindLine(gpu, vertices, lineVertices);		// Default enabled
			else if (lineRenderMode == 2)
				lineFound = Hack_ForceLine(gpu, vertices, lineVertices);	// Aggressive mode enabled (causes more artifacts)
			else
				lineFound = false;
		}
		else
			lineFound = false;

		if (rsx_intf_is_type() == RSX_OPENGL || rsx_intf_is_type() == RSX_VULKAN)
		{
			Reset_UVLimits(gpu);

			if ((numvertices == 4) && (!gpu->killQuadPart))
			{
				if (gpu->InCmd == INCMD_NONE)
				{
					// We have 4 quad vertices, we can push that at once
					tri_vertex *first = &gpu->InQuad_F3Vertices[0];

					Extend_UVLimits(gpu, first, 1);
					Extend_UVLimits(gpu, vertices, 3);
					Finalise_UVLimits(gpu);

					rsx_intf_push_quad(first->precise[0],
						first->precise[1],
						first->precise[2],
						vertices[0].precise[0],
						vertices[0].precise[1],
						vertices[0].precise[2],
						vertices[1].precise[0],
						vertices[1].precise[1],
						vertices[1].precise[2],
						vertices[2].precise[0],
						vertices[2].precise[1],
						vertices[2].precise[2],
						((uint32_t)first->r) |
						((uint32_t)first->g << 8) |
						((uint32_t)first->b << 16),
						((uint32_t)vertices[0].r) |
						((uint32_t)vertices[0].g << 8) |
						((uint32_t)vertices[0].b << 16),
						((uint32_t)vertices[1].r) |
						((uint32_t)vertices[1].g << 8) |
						((uint32_t)vertices[1].b << 16),
						((uint32_t)vertices[2].r) |
						((uint32_t)vertices[2].g << 8) |
						((uint32_t)vertices[2].b << 16),
						first->u + gpu->off_u, first->v + gpu->off_v,
						vertices[0].u + gpu->off_u, vertices[0].v + gpu->off_v,
						vertices[1].u + gpu->off_u, vertices[1].v + gpu->off_v,
						vertices[2].u + gpu->off_u, vertices[2].v + gpu->off_v,
						gpu->min_u, gpu->min_v,
						gpu->max_u, gpu->max_v,
						gpu->TexPageX, gpu->TexPageY,
						clut_x, clut_y,
						blend_mode,
						2 - TexMode_TA,
						DitherEnabled(gpu),
						BlendMode,
						MaskEval_TA,
						gpu->MaskSetOR);
				}
			}
			else
			{
				tri_vertex *verts;

				// Only need to render first triangle that we skipped
				if (gpu->killQuadPart == 2)
					verts = &gpu->InQuad_F3Vertices[0];
				else
					verts = &vertices[0];

				Extend_UVLimits(gpu, verts, 3);
				Finalise_UVLimits(gpu);

				// Push a single triangle
				rsx_intf_push_triangle(verts[0].precise[0],
					verts[0].precise[1],
					verts[0].precise[2],
					verts[1].precise[0],
					verts[1].precise[1],
					verts[1].precise[2],
					verts[2].precise[0],
					verts[2].precise[1],
					verts[2].precise[2],
					((uint32_t)verts[0].r) |
					((uint32_t)verts[0].g << 8) |
					((uint32_t)verts[0].b << 16),
					((uint32_t)verts[1].r) |
					((uint32_t)verts[1].g << 8) |
					((uint32_t)verts[1].b << 16),
					((uint32_t)verts[2].r) |
					((uint32_t)verts[2].g << 8) |
					((uint32_t)verts[2].b << 16),
					verts[0].u, verts[0].v,
					verts[1].u, verts[1].v,
					verts[2].u, verts[2].v,
					gpu->min_u, gpu->min_v,
					gpu->max_u, gpu->max_v,
					gpu->TexPageX, gpu->TexPageY,
					clut_x, clut_y,
					blend_mode,
					2 - TexMode_TA,
					DitherEnabled(gpu),
					BlendMode,
					MaskEval_TA,
					gpu->MaskSetOR);

				if (gpu->killQuadPart == 2)
				{
					gpu->killQuadPart = 0;
					return;
				}

				gpu->killQuadPart = 0;
			}
		}
#endif

      if (rsx_intf_is_type() == RSX_SOFTWARE)
      {
         if (pgxp)
         {
            for (uint32 i = 0; i < 3; ++i)
            {
               vertices[i].x = vertices[i].precise[0];
               vertices[i].y = vertices[i].precise[1];
            }
         }
      }

		if (rsx_intf_has_software_renderer())
			DrawTriangle<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(gpu, vertices);

		// Line Render: Overwrite vertices with those of the second triangle
		if ((lineFound) && (numvertices == 3) && (textured))
			memcpy(&vertices[0], &lineVertices[0], 3 * sizeof(tri_vertex));

	} while (lineFound);
}

#undef COORD_POST_PADDING
#undef COORD_FBS
#undef COORD_MF_INT
