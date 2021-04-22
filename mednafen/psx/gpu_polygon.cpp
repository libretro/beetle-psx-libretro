#include <math.h>
#include <algorithm>
#include "beetle_psx_globals.h"

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
template<bool gouraud, bool textured>
static INLINE bool CalcIDeltas(i_deltas &idl, const tri_vertex &A, const tri_vertex &B, const tri_vertex &C)
{
 int32 denom = CALCIS(x, y);

 if(!denom)
  return(false);

 if(gouraud)
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

template<bool gouraud, bool textured>
static INLINE void AddIDeltas_DX(i_group &ig, const i_deltas &idl, uint32_t count = 1)
{
   if(textured)
   {
      ig.u += idl.du_dx * count;
      ig.v += idl.dv_dx * count;
   }

   if(gouraud)
   {
      ig.r += idl.dr_dx * count;
      ig.g += idl.dg_dx * count;
      ig.b += idl.db_dx * count;
   }
}

template<bool gouraud, bool textured>
static INLINE void AddIDeltas_DY(i_group &ig, const i_deltas &idl, uint32_t count = 1)
{
   if(textured)
   {
      ig.u += idl.du_dy * count;
      ig.v += idl.dv_dy * count;
   }

   if(gouraud)
   {
      ig.r += idl.dr_dy * count;
      ig.g += idl.dg_dy * count;
      ig.b += idl.db_dy * count;
   }
}

template<bool gouraud, bool textured, int BlendMode, bool TexMult, uint32 TexMode_TA>
static INLINE void DrawSpan(PS_GPU *gpu, int y, const int32 x_start, const int32 x_bound, i_group ig, const i_deltas &idl, bool MaskEval_TA)
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

  AddIDeltas_DX<gouraud, textured>(ig, idl, x_ig_adjust);
  AddIDeltas_DY<gouraud, textured>(ig, idl, y);

  // Only compute timings for one every `upscale_shift` lines so that
  // we don't end up "slower" than 1x
  if ((y & ((1UL << gpu->upscale_shift) - 1)) == 0) {
     if(gouraud || textured)
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
     PlotPixel<BlendMode, true>(gpu, x, y, fbw, MaskEval_TA);
    }
   }
   else
   {
    uint16 pix = 0x8000;

    if(gouraud && DitherEnabled(gpu))
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

    PlotPixel<BlendMode, false>(gpu, x, y, pix, MaskEval_TA);
   }

   x++;
   AddIDeltas_DX<gouraud, textured>(ig, idl);
  } while(MDFN_LIKELY(--w > 0));
}

template<bool gouraud, bool textured, int BlendMode, bool TexMult, uint32_t TexMode_TA>
static INLINE void DrawTriangle(PS_GPU *gpu, tri_vertex *vertices, bool MaskEval_TA)
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


   if(!CalcIDeltas<gouraud, textured>(idl, vertices[0], vertices[1], vertices[2]))
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

 AddIDeltas_DX<gouraud, textured>(ig, idl, -vertices[core_vertex].x);
 AddIDeltas_DY<gouraud, textured>(ig, idl, -vertices[core_vertex].y);

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

    DrawSpan<gouraud, textured, BlendMode, TexMult, TexMode_TA>(gpu, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, idl, MaskEval_TA);
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

    DrawSpan<gouraud, textured, BlendMode, TexMult, TexMode_TA>(gpu, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, idl, MaskEval_TA);
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

void Calc_UVOffsets_Adjust_Verts(PS_GPU *gpu, tri_vertex *vertices, unsigned count);
void Reset_UVLimits(PS_GPU *gpu);
void Extend_UVLimits(PS_GPU *gpu, tri_vertex *vertices, unsigned count);
void Finalise_UVLimits(PS_GPU *gpu);
bool Hack_FindLine(PS_GPU *gpu, tri_vertex* vertices, tri_vertex* outVertices);
bool Hack_ForceLine(PS_GPU *gpu, tri_vertex* vertices, tri_vertex* outVertices);

extern int psx_pgxp_2d_tol;

template<int numvertices, bool gouraud, bool textured, int BlendMode, bool TexMult, uint32_t TexMode_TA>
static void Command_DrawPolygon(PS_GPU *gpu, const uint32_t *cb, bool pgxp, bool MaskEval_TA)
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

   if(gouraud && textured)
      gpu->DrawTimeAvail -= 150 * 3;
   else if(gouraud)
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

   for (unsigned v = sv; v < 3; v++)
   {
      if (v == 0 || gouraud)
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

      if (pgxp)
      {
         OGLVertex vert;
         PGXP_GetVertex(cb - baseCB, cb, &vert, 0, 0);

         vertices[v].precise[0] = ((vert.x + (float)gpu->OffsX) * UPSCALE(gpu));
         vertices[v].precise[1] = ((vert.y + (float)gpu->OffsY) * UPSCALE(gpu));
         vertices[v].precise[2] = vert.w;

         if (!vert.valid_w || vert.w <= 0.0)
            invalidW = true;
      }
      else
      {
         vertices[v].precise[0] = vertices[v].x;
         vertices[v].precise[1] = vertices[v].y;
         invalidW = true;
      }

      cb++;

      if (textured)
      {
         vertices[v].u = (*cb & 0xFF);
         vertices[v].v = (*cb >> 8) & 0xFF;

         if (v == 0)
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
      {
         // lacking w component tends to mean degenerate coordinates
         // set to non-pgxp value if difference is too great
         if (pgxp && psx_pgxp_2d_tol >= 0)
         {
            unsigned tol = (unsigned)psx_pgxp_2d_tol << gpu->upscale_shift;
            if (
               abs(vertices[v].precise[0] - vertices[v].x) > tol ||
               abs(vertices[v].precise[1] - vertices[v].y) > tol
            )
            {
               vertices[v].precise[0] = vertices[v].x;
               vertices[v].precise[1] = vertices[v].y;
            }
         }
         vertices[v].precise[2] = 1.f;
      }

   // Copy before Calc_UVOffsets which modifies vertices
   // Calc_UVOffsets likes to see unadjusted vertices
   if(numvertices == 4 && gpu->InCmd != INCMD_QUAD)
      memcpy(&gpu->InQuad_F3Vertices[1], &vertices[1], sizeof(tri_vertex) * 2);

   // Calculated UV offsets (needed for hardware renderers and software with scaling)
   // Do one time updates for primitive
   if (textured)
      Calc_UVOffsets_Adjust_Verts(gpu, vertices, numvertices);

   if(numvertices == 4)
   {
      if(gpu->InCmd == INCMD_QUAD)
      {
         gpu->InCmd = INCMD_NONE;

         if (invalidW)
         {
            if (pgxp && psx_pgxp_2d_tol >= 0)
            {
               unsigned tol = (unsigned)psx_pgxp_2d_tol << gpu->upscale_shift;
               if (
                  abs(gpu->InQuad_F3Vertices[0].precise[0] - gpu->InQuad_F3Vertices[0].x) > tol ||
                  abs(gpu->InQuad_F3Vertices[0].precise[1] - gpu->InQuad_F3Vertices[0].y) > tol
               )
               {
                  gpu->InQuad_F3Vertices[0].precise[0] = gpu->InQuad_F3Vertices[0].x;
                  gpu->InQuad_F3Vertices[0].precise[1] = gpu->InQuad_F3Vertices[0].y;
               }
            }
            // default first vertex of quad to 1 if any of the vertices are 1 (even if the first triangle was okay)
            gpu->InQuad_F3Vertices[0].precise[2] = 1.f;
         }
      }
      else
      {
         gpu->InCmd = INCMD_QUAD;
         gpu->InCmd_CC = cb0 >> 24;
         memcpy(&gpu->InQuad_F3Vertices[0], &vertices[0], sizeof(tri_vertex));
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

		enum blending_modes blend_mode = BLEND_MODE_AVERAGE;

		if (textured)
		{
			if (TexMult)
				blend_mode = BLEND_MODE_SUBTRACT;
			else
				blend_mode = BLEND_MODE_ADD;
		}

		// Line Renderer: Detect triangles that would resolve as lines at x1 scale and create second triangle to make quad
		if ((line_render_mode != 0) && (!lineFound) && (numvertices == 3) && (textured))
		{
			if(line_render_mode == 1)
				lineFound = Hack_FindLine(gpu, vertices, lineVertices);		// Default enabled
			else if (line_render_mode == 2)
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
						gpu->MaskSetOR,
                  false,
                  gpu->may_be_2d);
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
			DrawTriangle<gouraud, textured, BlendMode, TexMult, TexMode_TA>(gpu, vertices, MaskEval_TA);

		// Line Render: Overwrite vertices with those of the second triangle
		if ((lineFound) && (numvertices == 3) && (textured))
			memcpy(&vertices[0], &lineVertices[0], 3 * sizeof(tri_vertex));

	} while (lineFound);
}

#undef COORD_POST_PADDING
#undef COORD_FBS
#undef COORD_MF_INT
