#include <math.h>

#define COORD_FBS 12
#define COORD_MF_INT(n) ((n) << COORD_FBS)
#define COORD_POST_PADDING	12

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

/*
 Store and do most math with interpolant coordinates and deltas as unsigned to avoid violating strict overflow(due to biasing),
 but when actually grabbing the coordinates, treat them as signed(with signed right shift) so we can do saturation properly.
*/
static INLINE int32_t COORD_GET_INT(int32_t n)
{
   return(n >> COORD_FBS);
}

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

static INLINE bool CalcIDeltas(i_deltas &idl, const tri_vertex &A, const tri_vertex &B, const tri_vertex &C)
{
   const unsigned sa = 32;
   int64_t num = ((int64_t)COORD_MF_INT(1)) << sa;
   int64_t denom = CALCIS(x, y);
   int64_t one_div;

   if(!denom)
      return(false);

   one_div = num / denom;

   idl.dr_dx = ((one_div * CALCIS(r, y)) + 0x00000000) >> sa;
   idl.dr_dy = ((one_div * CALCIS(x, r)) + 0x00000000) >> sa;

   idl.dg_dx = ((one_div * CALCIS(g, y)) + 0x00000000) >> sa;
   idl.dg_dy = ((one_div * CALCIS(x, g)) + 0x00000000) >> sa;

   idl.db_dx = ((one_div * CALCIS(b, y)) + 0x00000000) >> sa;
   idl.db_dy = ((one_div * CALCIS(x, b)) + 0x00000000) >> sa;

   idl.du_dx = ((one_div * CALCIS(u, y)) + 0x00000000) >> sa;
   idl.du_dy = ((one_div * CALCIS(x, u)) + 0x00000000) >> sa;

   idl.dv_dx = ((one_div * CALCIS(v, y)) + 0x00000000) >> sa;
   idl.dv_dy = ((one_div * CALCIS(x, v)) + 0x00000000) >> sa;

   // idl.du_dx = ((int64_t)CALCIS(u, y) << COORD_FBS) / denom;
   // idl.du_dy = ((int64_t)CALCIS(x, u) << COORD_FBS) / denom;

   // idl.dv_dx = ((int64_t)CALCIS(v, y) << COORD_FBS) / denom;
   // idl.dv_dy = ((int64_t)CALCIS(x, v) << COORD_FBS) / denom;

   //printf("Denom=%lld - CIS_UY=%d, CIS_XU=%d, CIS_VY=%d, CIS_XV=%d\n", denom, CALCIS(u, y), CALCIS(x, u), CALCIS(v, y), CALCIS(x, v));
   //printf("  du_dx=0x%08x, du_dy=0x%08x --- dv_dx=0x%08x, dv_dy=0x%08x\n", idl.du_dx, idl.du_dy, idl.dv_dx, idl.dv_dy);

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

template<bool goraud, bool textured, int BlendMode, bool TexMult, uint32_t TexMode_TA, bool MaskEval_TA>
INLINE void PS_GPU::DrawSpan(int y, uint32_t clut_offset, const int32_t x_start, const int32_t x_bound, i_group ig, const i_deltas &idl)
{
   int32_t xs = x_start, xb = x_bound;
   int32 clipx0 = ClipX0 << upscale_shift;
   int32 clipx1 = ClipX1 << upscale_shift;

   if(LineSkipTest(this, y >> upscale_shift))
      return;

   if(xs < xb)	// (xs != xb)
   {
      if(xs < clipx0)
         xs = clipx0;

      if(xb > (clipx1 + 1))
         xb = clipx1 + 1;

      if(xs < xb && ((y & (upscale() - 1)) == 0))
      {
         DrawTimeAvail -= (xb - xs) >> upscale_shift;

         if(goraud || textured)
         {
            DrawTimeAvail -= (xb - xs) >> upscale_shift;
         }
         else if((BlendMode >= 0) || MaskEval_TA)
         {
            DrawTimeAvail -= (((((xb  >> upscale_shift) + 1) & ~1) - ((xs  >> upscale_shift) & ~1)) >> 1);
         }
      }

      if(textured)
      {
         ig.u += (xs * idl.du_dx) + (y * idl.du_dy);
         ig.v += (xs * idl.dv_dx) + (y * idl.dv_dy);
      }

      if(goraud)
      {
         ig.r += (xs * idl.dr_dx) + (y * idl.dr_dy);
         ig.g += (xs * idl.dg_dx) + (y * idl.dg_dy);
         ig.b += (xs * idl.db_dx) + (y * idl.db_dy);
      }


      for(int32_t x = xs; MDFN_LIKELY(x < xb); x++)
      {
         uint32_t r, g, b;

         if(goraud)
         {
            r = RGB8SAT[COORD_GET_INT(ig.r)];
            g = RGB8SAT[COORD_GET_INT(ig.g)];
            b = RGB8SAT[COORD_GET_INT(ig.b)];
         }
         else
         {
            r = COORD_GET_INT(ig.r);
            g = COORD_GET_INT(ig.g);
            b = COORD_GET_INT(ig.b);
         }

	 bool dither = DitherEnabled();
         int32_t dither_x = x >> dither_upscale_shift;
         int32_t dither_y = y >> dither_upscale_shift;

         if(textured)
         {
            uint16_t fbw = GetTexel<TexMode_TA>(clut_offset, COORD_GET_INT(ig.u), COORD_GET_INT(ig.v));

            if(fbw)
            {
	      if(TexMult) {

                  fbw = ModTexel(fbw, r, g, b, (dither) ? (dither_x & 3) : 3, (dither) ? (dither_y & 3) : 2);
	      }
	      PlotPixel<BlendMode, MaskEval_TA, true>(x, y, fbw);
            }
         }
         else
         {
            uint16_t pix = 0;

            if(goraud && dither)
            {
               uint8_t *dither_offset = DitherLUT[dither_y & 3][dither_x & 3];
               pix = 0x8000 | (dither_offset[r] << 0) | (dither_offset[g] << 5) | 
                  (dither_offset[b] << 10);
            }
            else
               pix = 0x8000 | ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10);

            PlotPixel<BlendMode, MaskEval_TA, false>(x, y, pix);
         }

         AddIDeltas_DX<goraud, textured>(ig, idl);
         //AddStep<goraud, textured>(perp_coord, perp_step);
      }
   }
}

template<bool goraud, bool textured, int BlendMode, bool TexMult, uint32_t TexMode_TA, bool MaskEval_TA>
void PS_GPU::DrawTriangle(tri_vertex *vertices, uint32_t clut)
{
   i_deltas idl;

   //
   // Sort vertices by y.
   //
   if(vertices[2].y < vertices[1].y)
      vertex_swap(tri_vertex, vertices[1], vertices[2]);

   if(vertices[1].y < vertices[0].y)
      vertex_swap(tri_vertex, vertices[0], vertices[1]);

   if(vertices[2].y < vertices[1].y)
      vertex_swap(tri_vertex, vertices[1], vertices[2]);

   if(vertices[0].y == vertices[2].y)
      return;

   if((vertices[2].y - vertices[0].y) >= (512 << upscale_shift))
   {
      //PSX_WARNING("[GPU] Triangle height too large: %d", (vertices[2].y - vertices[0].y));
      return;
   }

   if(abs(vertices[2].x - vertices[0].x) >= (1024 << upscale_shift) ||
      abs(vertices[2].x - vertices[1].x) >= (1024 << upscale_shift) ||
      abs(vertices[1].x - vertices[0].x) >= (1024 << upscale_shift))
   {
      //PSX_WARNING("[GPU] Triangle width too large: %d %d %d", abs(vertices[2].x - vertices[0].x), abs(vertices[2].x - vertices[1].x), abs(vertices[1].x - vertices[0].x));
      return;
   }

   int32 clipy0 = ClipY0 << upscale_shift;
   int32 clipy1 = ClipY1 << upscale_shift;

   if(!CalcIDeltas(idl, vertices[0], vertices[1], vertices[2]))
      return;

   // [0] should be top vertex, [2] should be bottom vertex, [1] should be off to the side vertex.
   //
   //
   int32_t y_start = vertices[0].y;
   int32_t y_middle = vertices[1].y;
   int32_t y_bound = vertices[2].y;

   int64_t base_coord;
   int64_t base_step;

   int64_t bound_coord_ul;
   int64_t bound_coord_us;

   int64_t bound_coord_ll;
   int64_t bound_coord_ls;

   bool right_facing;
   //bool bottom_up;
   i_group ig;

   //
   // Find vertex with lowest X coordinate, and use as the base for calculating interpolants from.
   //
   {
      unsigned iggvi = 0;

      //
      // <=, not <
      //
      if(vertices[1].x <= vertices[iggvi].x)
         iggvi = 1;

      if(vertices[2].x <= vertices[iggvi].x)
         iggvi = 2;

      ig.u = COORD_MF_INT(vertices[iggvi].u) + (1 << (COORD_FBS - 1));
      ig.v = COORD_MF_INT(vertices[iggvi].v) + (1 << (COORD_FBS - 1));

      if (upscale_shift > 0) {
	// Bias the texture coordinates so that it rounds to the
	// correct value when the game is mapping a 2D sprite using
	// triangles. Otherwise this could cause a small "shift" in
	// the texture coordinates when upscaling

	if (idl.du_dy == 0 && (int32_t)idl.du_dx > 0) {
	  ig.u -= (1 << (COORD_FBS - 1 - upscale_shift));
	}
	if (idl.dv_dx == 0 && (int32_t)idl.dv_dy > 0) {
	  ig.v -= (1 << (COORD_FBS - 1 - upscale_shift));
	}
      }

      ig.r = COORD_MF_INT(vertices[iggvi].r);
      ig.g = COORD_MF_INT(vertices[iggvi].g);
      ig.b = COORD_MF_INT(vertices[iggvi].b);

      AddIDeltas_DX<goraud, textured>(ig, idl, -vertices[iggvi].x);
      AddIDeltas_DY<goraud, textured>(ig, idl, -vertices[iggvi].y);
   }

   base_coord = MakePolyXFP(vertices[0].x);
   base_step = MakePolyXFPStep((vertices[2].x - vertices[0].x), (vertices[2].y - vertices[0].y));

   bound_coord_ul = MakePolyXFP(vertices[0].x);
   bound_coord_ll = MakePolyXFP(vertices[1].x);

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

   if(y_start < clipy0)
   {
      int32_t count = clipy0 - y_start;

      y_start = clipy0;
      base_coord += base_step * count;
      bound_coord_ul += bound_coord_us * count;

      if(y_middle < clipy0)
      {
         int32_t count_ls = clipy0 - y_middle;

         y_middle = clipy0;
         bound_coord_ll += bound_coord_ls * count_ls;
      }
   }

   if(y_bound > (clipy1 + 1))
   {
      y_bound = clipy1 + 1;

      if(y_middle > y_bound)
         y_middle = y_bound;
   }

   if(right_facing)
   {
      for(int32_t y = y_start; y < y_middle; y++)
      {
         DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(y, clut, GetPolyXFP_Int(base_coord), GetPolyXFP_Int(bound_coord_ul), ig, idl);
         base_coord += base_step;
         bound_coord_ul += bound_coord_us;
      }

      for(int32_t y = y_middle; y < y_bound; y++)
      {
         DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(y, clut, GetPolyXFP_Int(base_coord), GetPolyXFP_Int(bound_coord_ll), ig, idl);
         base_coord += base_step;
         bound_coord_ll += bound_coord_ls;
      }
   }
   else
   {
      for(int32_t y = y_start; y < y_middle; y++)
      {
         DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(y, clut, GetPolyXFP_Int(bound_coord_ul), GetPolyXFP_Int(base_coord), ig, idl);
         base_coord += base_step;
         bound_coord_ul += bound_coord_us;
      }

      for(int32_t y = y_middle; y < y_bound; y++)
      {
         DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(y, clut, GetPolyXFP_Int(bound_coord_ll), GetPolyXFP_Int(base_coord), ig, idl);
         base_coord += base_step;
         bound_coord_ll += bound_coord_ls;
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

template<int numvertices, bool goraud, bool textured, int BlendMode, bool TexMult, uint32_t TexMode_TA, bool MaskEval_TA>
INLINE void PS_GPU::Command_DrawPolygon(const uint32_t *cb)
{
   const unsigned cb0 = cb[0];
   tri_vertex vertices[3] = {{0}};
   uint32_t clut = 0;
   unsigned sv = 0;
   //uint32_t tpage = 0;

   // Base timing is approximate, and could be improved.
   if(numvertices == 4 && InCmd == INCMD_QUAD)
      DrawTimeAvail -= (28 + 18);
   else
      DrawTimeAvail -= (64 + 18);

   if(goraud && textured)
      DrawTimeAvail -= 150 * 3;
   else if(goraud)
      DrawTimeAvail -= 96 * 3;
   else if(textured)
      DrawTimeAvail -= 60 * 3;

   if(numvertices == 4)
   {
      if(InCmd == INCMD_QUAD)
      {
         memcpy(&vertices[0], &InQuad_F3Vertices[1], 2 * sizeof(tri_vertex));
         clut = InQuad_clut;
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

      // Attempt to retrieve subpixel coordinates if available
      const subpixel_vertex *pv = GetSubpixelVertex(x, y);

      if (pv != NULL) {
	vertices[v].x = (int32)roundf((pv->x + (float)OffsX) * upscale());
	vertices[v].y = (int32)roundf((pv->y + (float)OffsY) * upscale());
      } else {
	vertices[v].x = (x + OffsX) << upscale_shift;
	vertices[v].y = (y + OffsY) << upscale_shift;
      }

      cb++;

      if(textured)
      {
         vertices[v].u = (*cb & 0xFF);
         vertices[v].v = (*cb >> 8) & 0xFF;

         if(v == 0)
         {
            clut = ((*cb >> 16) & 0xFFFF) << 4;
         }

         cb++;
      }
   }

   if(numvertices == 4)
   {
      if(InCmd == INCMD_QUAD)
      {
         InCmd = INCMD_NONE;
      }
      else
      {
         InCmd = INCMD_QUAD;
         InCmd_CC = cb0 >> 24;
         memcpy(&InQuad_F3Vertices[0], &vertices[0], sizeof(tri_vertex) * 3);
         InQuad_clut = clut;
      }
   }

   uint16_t clut_x = (clut & (0x3f << 4));
   uint16_t clut_y = (clut >> 10) & 0x1ff;

   enum blending_modes blend_mode = BLEND_MODE_AVERAGE;

   if (textured)
   {
      if (TexMult)
         blend_mode = BLEND_MODE_SUBTRACT;
      else
         blend_mode = BLEND_MODE_ADD;
   }

   rsx_intf_push_triangle(vertices[0].x, vertices[0].y,
         vertices[1].x, vertices[1].y,
         vertices[2].x, vertices[2].y,
         ((uint32_t)vertices[0].r) | ((uint32_t)vertices[0].g << 8) | ((uint32_t)vertices[0].b << 16),
         ((uint32_t)vertices[1].r) | ((uint32_t)vertices[1].g << 8) | ((uint32_t)vertices[1].b << 16),
			  ((uint32_t)vertices[2].r) | ((uint32_t)vertices[2].g << 8) | ((uint32_t)vertices[2].b << 16),
         vertices[0].u, vertices[0].v,
         vertices[1].u, vertices[1].v,
         vertices[2].u, vertices[2].v,
         this->TexPageX, this->TexPageY,
         clut_x, clut_y,
         blend_mode,
         2 - TexMode_TA,
         DitherEnabled(),
         BlendMode);

   DrawTriangle<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(vertices, clut);
}

#undef COORD_POST_PADDING
#undef COORD_FBS
#undef COORD_MF_INT
