#include <math.h>

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
static INLINE void DrawSpan(PS_GPU *gpu, int y, uint32_t clut_offset, const int32_t x_start, const int32_t x_bound, i_group ig, const i_deltas &idl)
{
   int32_t xs = x_start, xb = x_bound;
   int32 clipx0 = gpu->ClipX0 << gpu->upscale_shift;
   int32 clipx1 = gpu->ClipX1 << gpu->upscale_shift;

   if(LineSkipTest(gpu, y >> gpu->upscale_shift))
      return;

   if(xs < xb) // (xs != xb)
   {
      if(xs < clipx0)
         xs = clipx0;

      if(xb > (clipx1 + 1))
         xb = clipx1 + 1;

      if(xs < xb && ((y & (UPSCALE(gpu) - 1)) == 0))
      {
         gpu->DrawTimeAvail -= (xb - xs) >> gpu->upscale_shift;

         if(goraud || textured)
         {
            gpu->DrawTimeAvail -= (xb - xs) >> gpu->upscale_shift;
         }
         else if((BlendMode >= 0) || MaskEval_TA)
         {
            gpu->DrawTimeAvail -= (((((xb  >> gpu->upscale_shift) + 1) & ~1) - ((xs  >> gpu->upscale_shift) & ~1)) >> 1);
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
            r = gpu->RGB8SAT[COORD_GET_INT(ig.r)];
            g = gpu->RGB8SAT[COORD_GET_INT(ig.g)];
            b = gpu->RGB8SAT[COORD_GET_INT(ig.b)];
         }
         else
         {
            r = COORD_GET_INT(ig.r);
            g = COORD_GET_INT(ig.g);
            b = COORD_GET_INT(ig.b);
         }

         bool dither      = DitherEnabled(gpu);
         int32_t dither_x = x >> gpu->dither_upscale_shift;
         int32_t dither_y = y >> gpu->dither_upscale_shift;

         if(textured)
         {
            uint16_t fbw = GetTexel<TexMode_TA>(gpu, clut_offset, COORD_GET_INT(ig.u), COORD_GET_INT(ig.v));

            if(fbw)
            {
               if(TexMult)
               {
                  uint8_t *dither_offset = gpu->DitherLUT[(dither) ? (dither_y & 3) : 2][(dither) ? (dither_x & 3) : 3];
                  fbw = ModTexel(dither_offset, fbw, r, g, b);
               }
               PlotPixel<BlendMode, MaskEval_TA, true>(gpu, x, y, fbw);
            }
         }
         else
         {
            uint16_t pix = 0;

            if(goraud && dither)
            {
               uint8_t *dither_offset = gpu->DitherLUT[dither_y & 3][dither_x & 3];
               pix = 0x8000 | (dither_offset[r] << 0) | (dither_offset[g] << 5) | 
                  (dither_offset[b] << 10);
            }
            else
               pix = 0x8000 | ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10);

            PlotPixel<BlendMode, MaskEval_TA, false>(gpu, x, y, pix);
         }

         AddIDeltas_DX<goraud, textured>(ig, idl);
         //AddStep<goraud, textured>(perp_coord, perp_step);
      }
   }
}

template<bool goraud, bool textured, int BlendMode, bool TexMult, uint32_t TexMode_TA, bool MaskEval_TA>
static INLINE void DrawTriangle(PS_GPU *gpu, tri_vertex *vertices, uint32_t clut)
{
   i_deltas idl;

   int32 clipy0 = gpu->ClipY0 << gpu->upscale_shift;
   int32 clipy1 = gpu->ClipY1 << gpu->upscale_shift;

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

      if (gpu->upscale_shift > 0)
      {
         // Bias the texture coordinates so that it rounds to the
         // correct value when the game is mapping a 2D sprite using
         // triangles. Otherwise this could cause a small "shift" in
         // the texture coordinates when upscaling

         if (idl.du_dy == 0 && (int32_t)idl.du_dx > 0)
            ig.u -= (1 << (COORD_FBS - 1 - gpu->upscale_shift));
         if (idl.dv_dx == 0 && (int32_t)idl.dv_dy > 0)
            ig.v -= (1 << (COORD_FBS - 1 - gpu->upscale_shift));
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
         DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(gpu, y, clut, GetPolyXFP_Int(base_coord), GetPolyXFP_Int(bound_coord_ul), ig, idl);
         base_coord += base_step;
         bound_coord_ul += bound_coord_us;
      }

      for(int32_t y = y_middle; y < y_bound; y++)
      {
         DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(gpu, y, clut, GetPolyXFP_Int(base_coord), GetPolyXFP_Int(bound_coord_ll), ig, idl);
         base_coord += base_step;
         bound_coord_ll += bound_coord_ls;
      }
   }
   else
   {
      for(int32_t y = y_start; y < y_middle; y++)
      {
         DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(gpu, y, clut, GetPolyXFP_Int(bound_coord_ul), GetPolyXFP_Int(base_coord), ig, idl);
         base_coord += base_step;
         bound_coord_ul += bound_coord_us;
      }

      for(int32_t y = y_middle; y < y_bound; y++)
      {
         DrawSpan<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(gpu, y, clut, GetPolyXFP_Int(bound_coord_ll), GetPolyXFP_Int(base_coord), ig, idl);
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
         }

         cb++;
      }
   }

   // iCB: If any vertices lack w components then set all to 1
   if (invalidW)
      for (unsigned v = 0; v < 3; v++)
         vertices[v].precise[2] = 1.f;


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
       return;
     }

   if(abs(vertices[2].x - vertices[0].x) >= (1024 << gpu->upscale_shift) ||
      abs(vertices[2].x - vertices[1].x) >= (1024 << gpu->upscale_shift) ||
      abs(vertices[1].x - vertices[0].x) >= (1024 << gpu->upscale_shift))
     {
       //PSX_WARNING("[GPU] Triangle width too large: %d %d %d", abs(vertices[2].x - vertices[0].x), abs(vertices[2].x - vertices[1].x), abs(vertices[1].x - vertices[0].x));
       return;
     }

   uint16_t clut_x = (clut & (0x3f << 4));
   uint16_t clut_y = (clut >> 10) & 0x1ff;

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   enum blending_modes blend_mode = BLEND_MODE_AVERAGE;

   if (textured)
   {
      if (TexMult)
         blend_mode = BLEND_MODE_SUBTRACT;
      else
         blend_mode = BLEND_MODE_ADD;
   }

   if (rsx_intf_is_type() == RSX_OPENGL || rsx_intf_is_type() == RSX_VULKAN)
   {
      if (numvertices == 4)
      {
         if (gpu->InCmd == INCMD_NONE)
         {
            // We have 4 quad vertices, we can push that at once
            tri_vertex *first = &gpu->InQuad_F3Vertices[0];

            rsx_intf_push_quad(  first->precise[0],
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
                  first->u, first->v,
                  vertices[0].u, vertices[0].v,
                  vertices[1].u, vertices[1].v,
                  vertices[2].u, vertices[2].v,
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
         // Push a single triangle
         rsx_intf_push_triangle( vertices[0].precise[0],
               vertices[0].precise[1],
               vertices[0].precise[2],
               vertices[1].precise[0],
               vertices[1].precise[1],
               vertices[1].precise[2],
               vertices[2].precise[0],
               vertices[2].precise[1],
               vertices[2].precise[2],
               ((uint32_t)vertices[0].r) |
               ((uint32_t)vertices[0].g << 8) |
               ((uint32_t)vertices[0].b << 16),
               ((uint32_t)vertices[1].r) |
               ((uint32_t)vertices[1].g << 8) |
               ((uint32_t)vertices[1].b << 16),
               ((uint32_t)vertices[2].r) |
               ((uint32_t)vertices[2].g << 8) |
               ((uint32_t)vertices[2].b << 16),
               vertices[0].u, vertices[0].v,
               vertices[1].u, vertices[1].v,
               vertices[2].u, vertices[2].v,
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
#endif

   if (rsx_intf_has_software_renderer())
      DrawTriangle<goraud, textured, BlendMode, TexMult, TexMode_TA, MaskEval_TA>(gpu, vertices, clut);
}

#undef COORD_POST_PADDING
#undef COORD_FBS
#undef COORD_MF_INT
