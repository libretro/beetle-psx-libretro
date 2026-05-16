#include <math.h>
#include "beetle_psx_globals.h"
#include "gpu_subdiv.h"

#define COORD_FBS 12
#define COORD_MF_INT(n) ((n) << COORD_FBS)
#define COORD_POST_PADDING 12

struct i_group
{
   uint32_t u, v;
   uint32_t r, g, b;
   /* Perspective-correct texture interpolants.  Used only when the
    * `pct` runtime flag is set on the call to DrawTriangle /
    * DrawSpan (textured + PGXP-on + texture-correction-option-on
    * + all w > 0).  Carries (1/w), (u/w), (v/w) at the current
    * pixel.  Per-pixel U,V are then `(u_over_w / inv_w)` and
    * `(v_over_w / inv_w)`, which produces the same perspective-
    * correct mapping the HW renderers get for free via
    * `gl_Position * w` in primitive.vert.
    *
    * When `pct` is false these fields are simply not touched. */
   float pct_inv_w;
   float pct_u_over_w;
   float pct_v_over_w;
};
typedef struct i_group i_group;

struct i_deltas
{
   uint32_t du_dx, dv_dx;
   uint32_t dr_dx, dg_dx, db_dx;

   uint32_t du_dy, dv_dy;
   uint32_t dr_dy, dg_dy, db_dy;

   /* Per-pixel deltas for the perspective-correct interpolants in
    * `i_group`.  Computed by CalcIDeltas_Persp when the primitive
    * takes the pct path; otherwise left uninitialised since
    * DrawSpan / DrawTriangle won't read them. */
   float d_inv_w_dx,    d_inv_w_dy;
   float d_u_over_w_dx, d_u_over_w_dy;
   float d_v_over_w_dx, d_v_over_w_dy;
   /* Per-primitive UV bounds (the convex hull of the three vertex
    * UVs).  We clamp the recovered U/V to these at every pixel:
    * the rasteriser walks integer pixel positions, but the precise
    * triangle has float edges, so pixels at the very boundary land
    * a fraction outside the precise triangle and extrapolate the
    * perspective interpolants.  Linear extrapolation of u/w then
    * dividing by an inv_w that drifts toward zero produces UV
    * overshoots of many texels - which the &0xFF wrap then turns
    * into completely wrong texels and visible speckles.  Clamping
    * to [min,max] of the input vertex UVs - which is what the HW
    * fragment shader does via clamp_coord(vUV) against vTexLimits -
    * keeps these edge pixels at the closest valid texel. */
   float min_u, max_u, min_v, max_v;
};
typedef struct i_deltas i_deltas;

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

/*
 * CalcIDeltas - compute per-pixel deltas (du/dx, du/dy, dr/dx,
 * etc.) for the upcoming triangle, given the three vertices.
 *
 * Macro parameters:
 *   SUFFIX        - mangled-name suffix
 *   GOURAUD_LIT   - 0/1 literal: when 1, vertex colours r/g/b
 *                   interpolate across the triangle and the
 *                   corresponding deltas are stored.  When 0 the
 *                   colour is flat across the triangle and the
 *                   delta computations are dropped.
 *   TEXTURED_LIT  - 0/1 literal: when 1, vertex texture coords
 *                   u/v interpolate across the triangle.  Same
 *                   elision when 0.
 *
 * CALCIS computes the cross-product-style "interpolant slope"
 * given two attribute axes.  Returns false if the triangle is
 * degenerate (zero-area), in which case the caller skips the
 * draw.
 *
 * COORD_FBS is the fixed-point shift for screen coordinates;
 * COORD_POST_PADDING is an additional left-shift so the
 * fixed-point result has enough headroom for the per-pixel
 * accumulation in the inner loop.
 */
#define CALCIS(x,y) (((B->x - A->x) * (C->y - B->y)) - ((C->x - B->x) * (B->y - A->y)))
#define DEFINE_CalcIDeltas(SUFFIX, GOURAUD_LIT, TEXTURED_LIT) \
static INLINE bool CalcIDeltas_##SUFFIX(i_deltas *idl, const tri_vertex *A, const tri_vertex *B, const tri_vertex *C) \
{ \
   int32_t denom = CALCIS(x, y); \
   if (!denom) \
      return false; \
   if (GOURAUD_LIT) \
   { \
      idl->dr_dx = (uint32_t)(CALCIS(r, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
      idl->dr_dy = (uint32_t)(CALCIS(x, r) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
      idl->dg_dx = (uint32_t)(CALCIS(g, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
      idl->dg_dy = (uint32_t)(CALCIS(x, g) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
      idl->db_dx = (uint32_t)(CALCIS(b, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
      idl->db_dy = (uint32_t)(CALCIS(x, b) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
   } \
   if (TEXTURED_LIT) \
   { \
      idl->du_dx = (uint32_t)(CALCIS(u, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
      idl->du_dy = (uint32_t)(CALCIS(x, u) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
      idl->dv_dx = (uint32_t)(CALCIS(v, y) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
      idl->dv_dy = (uint32_t)(CALCIS(x, v) * (1 << COORD_FBS) / denom) << COORD_POST_PADDING; \
   } \
   return true; \
}

DEFINE_CalcIDeltas(g0_t0, 0, 0)
DEFINE_CalcIDeltas(g0_t1, 0, 1)
DEFINE_CalcIDeltas(g1_t0, 1, 0)
DEFINE_CalcIDeltas(g1_t1, 1, 1)
#undef CALCIS

/*
 * CalcIDeltas_Persp - compute per-pixel deltas for the
 * perspective-correct interpolants (1/w, u/w, v/w) carried in
 * `i_group`.
 *
 * Mirrors CalcIDeltas above but in float and using the precise
 * vertex coords (precise[0]/precise[1], in upscaled-pixel space)
 * for the denominator.  The PGXP-supplied w (precise[2]) is the
 * pre-projection depth; (u/w), (v/w) and (1/w) interpolate
 * linearly in screen space, and per-pixel division of the first
 * two by the third gives the perspective-correct (U,V) that the
 * HW renderers obtain implicitly via `gl_Position * w` in
 * primitive.vert.
 *
 * Returns false on degenerate triangles (zero precise area); the
 * caller falls back to the affine path in that case.  Callers are
 * expected to have already screened for `!invalidW` and `w > 0`.
 */
static INLINE bool CalcIDeltas_Persp(i_deltas *idl,
   const tri_vertex *A, const tri_vertex *B, const tri_vertex *C)
{
   float ax = A->precise[0], ay = A->precise[1];
   float bx = B->precise[0], by = B->precise[1];
   float cx = C->precise[0], cy = C->precise[1];

   float inv_wA = 1.0f / A->precise[2];
   float inv_wB = 1.0f / B->precise[2];
   float inv_wC = 1.0f / C->precise[2];

   float uA = (float)A->u * inv_wA;
   float uB = (float)B->u * inv_wB;
   float uC = (float)C->u * inv_wC;

   float vA = (float)A->v * inv_wA;
   float vB = (float)B->v * inv_wB;
   float vC = (float)C->v * inv_wC;

   /* CALCIS-style cross product, evaluated in float for the
    * denominator and per-attribute slopes.  Reject near-degenerate
    * triangles (precise area essentially zero): the divide would
    * produce huge per-pixel deltas that the inner-loop bounds
    * clamp would have to swallow.  Cheaper to fall back to affine
    * here. */
   float denom = (bx - ax) * (cy - by) - (cx - bx) * (by - ay);
   float inv_denom;
   if (denom > -1e-6f && denom < 1e-6f)
      return false;
   inv_denom = 1.0f / denom;

   /* d(attr)/dx = cross(attr,y) / denom
    * d(attr)/dy = cross(x,attr) / denom */
   idl->d_inv_w_dx    = ((inv_wB - inv_wA) * (cy - by) - (inv_wC - inv_wB) * (by - ay)) * inv_denom;
   idl->d_inv_w_dy    = ((bx - ax) * (inv_wC - inv_wB) - (cx - bx) * (inv_wB - inv_wA)) * inv_denom;
   idl->d_u_over_w_dx = ((uB - uA) * (cy - by) - (uC - uB) * (by - ay)) * inv_denom;
   idl->d_u_over_w_dy = ((bx - ax) * (uC - uB) - (cx - bx) * (uB - uA)) * inv_denom;
   idl->d_v_over_w_dx = ((vB - vA) * (cy - by) - (vC - vB) * (by - ay)) * inv_denom;
   idl->d_v_over_w_dy = ((bx - ax) * (vC - vB) - (cx - bx) * (vB - vA)) * inv_denom;

   /* Convex hull of the input vertex UVs - used at every pixel to
    * clamp the recovered U/V.  Avoids overshoot at edge pixels
    * that rasterise just outside the precise triangle. */
   {
      float u0 = (float)A->u, u1 = (float)B->u, u2 = (float)C->u;
      float v0 = (float)A->v, v1 = (float)B->v, v2 = (float)C->v;
      float min_u = u0, max_u = u0;
      float min_v = v0, max_v = v0;
      if (u1 < min_u) min_u = u1; else if (u1 > max_u) max_u = u1;
      if (u2 < min_u) min_u = u2; else if (u2 > max_u) max_u = u2;
      if (v1 < min_v) min_v = v1; else if (v1 > max_v) max_v = v1;
      if (v2 < min_v) min_v = v2; else if (v2 > max_v) max_v = v2;
      idl->min_u = min_u;
      idl->max_u = max_u;
      idl->min_v = min_v;
      idl->max_v = max_v;
   }
   return true;
}

/*
 * AddIDeltas_DX / AddIDeltas_DY - step the interpolant group
 * `ig` by `count` pixels in screen-X or screen-Y, using the
 * per-pixel deltas from CalcIDeltas above.
 *
 * GOURAUD_LIT / TEXTURED_LIT elide the entire delta-add when the
 * corresponding vertex attribute isn't carried by this primitive.
 *
 * `count` is 1 for the per-pixel inner-loop case; larger values
 * are used by DrawTriangle to skip across clipped regions in a
 * single multiply rather than iterating.  C macros do not support
 * default arguments so callers must always pass `count` explicitly.
 */
#define DEFINE_AddIDeltas_DX(SUFFIX, GOURAUD_LIT, TEXTURED_LIT) \
static INLINE void AddIDeltas_DX_##SUFFIX(i_group *ig, const i_deltas *idl, uint32_t count) \
{ \
   if (TEXTURED_LIT) \
   { \
      ig->u += idl->du_dx * count; \
      ig->v += idl->dv_dx * count; \
   } \
   if (GOURAUD_LIT) \
   { \
      ig->r += idl->dr_dx * count; \
      ig->g += idl->dg_dx * count; \
      ig->b += idl->db_dx * count; \
   } \
}

DEFINE_AddIDeltas_DX(g0_t0, 0, 0)
DEFINE_AddIDeltas_DX(g0_t1, 0, 1)
DEFINE_AddIDeltas_DX(g1_t0, 1, 0)
DEFINE_AddIDeltas_DX(g1_t1, 1, 1)

#define DEFINE_AddIDeltas_DY(SUFFIX, GOURAUD_LIT, TEXTURED_LIT) \
static INLINE void AddIDeltas_DY_##SUFFIX(i_group *ig, const i_deltas *idl, uint32_t count) \
{ \
   if (TEXTURED_LIT) \
   { \
      ig->u += idl->du_dy * count; \
      ig->v += idl->dv_dy * count; \
   } \
   if (GOURAUD_LIT) \
   { \
      ig->r += idl->dr_dy * count; \
      ig->g += idl->dg_dy * count; \
      ig->b += idl->db_dy * count; \
   } \
}

DEFINE_AddIDeltas_DY(g0_t0, 0, 0)
DEFINE_AddIDeltas_DY(g0_t1, 0, 1)
DEFINE_AddIDeltas_DY(g1_t0, 1, 0)
DEFINE_AddIDeltas_DY(g1_t1, 1, 1)

/*
 * DrawSpan - rasterise one horizontal span of a triangle.
 *
 * Called per scanline by DrawTriangle once the top/bottom edges
 * have been walked to the appropriate y.  `ig` carries the
 * interpolant values at x_start; `idl` carries the per-pixel
 * deltas for stepping across the span.
 *
 * Macro parameters:
 *   SUFFIX        - mangled-name suffix
 *   GOURAUD_LIT   - 0/1: per-vertex colour interpolation enabled
 *   TEXTURED_LIT  - 0/1: texture sampling enabled
 *   BM_VAL        - integer literal blend mode (-1 / 0..3)
 *   BM_TAG        - matching blend tag (BMopaque / BMavg / BMadd /
 *                   BMsub / BMaddq) used in the PlotPixel_<...>
 *                   mangled call
 *   TM_LIT        - 0/1: when 1, texel colour is modulated by the
 *                   shaded vertex colour (PS1 "texture color
 *                   modulation"); when 0, the texel goes to the
 *                   framebuffer unmodified
 *   MO_LIT        - 4 / 8 / 15bpp texel format selector
 *                   (0 / 1 / 2)
 *   ME_LIT        - 0/1: gate writes on destination mask bit
 *
 * Calls PlotPixel (upscale-aware) per fragment; PlotPixel itself
 * is specialised on (BlendMode, MaskEval_TA, textured), and is
 * inlined here so the inner loop is fully fused.
 */
#define DEFINE_DrawSpan(SUFFIX, GOURAUD_LIT, TEXTURED_LIT, BM_VAL, BM_TAG, TM_LIT, MO_LIT, ME_LIT) \
static INLINE void DrawSpan_##SUFFIX(PS_GPU *gpu, int y, const int32_t x_start, const int32_t x_bound, i_group ig, const i_deltas *idl, const bool pct) \
{ \
   int32_t clipx0; \
   int32_t clipx1; \
   int32_t x_ig_adjust; \
   int32_t w; \
   int32_t x; \
   if (LineSkipTest(gpu, y >> gpu->upscale_shift)) \
      return; \
   clipx0 = gpu->ClipX0 << gpu->upscale_shift; \
   clipx1 = gpu->ClipX1 << gpu->upscale_shift; \
   x_ig_adjust = x_start; \
   w = x_bound - x_start; \
   /*int32 x = x_start;*/ \
   x = sign_x_to_s32(11 + gpu->upscale_shift, x_start); \
   /* The 'bool dither = DitherEnabled(gpu)' from the original was \
    * never read - all uses fall through to direct DitherEnabled() \
    * calls.  Dropped to silence -Wunused-but-set-variable. */ \
   if (x < clipx0) \
   { \
      int32_t delta = clipx0 - x; \
      x_ig_adjust += delta; \
      x += delta; \
      w -= delta; \
   } \
   if ((x + w) > (clipx1 + 1)) \
      w = clipx1 + 1 - x; \
   if (w <= 0) \
      return; \
   /*printf("%d %d %d %d\n", x, w, ClipX0, ClipX1);*/ \
   AddIDeltas_DX_g##GOURAUD_LIT##_t##TEXTURED_LIT(&ig, idl, x_ig_adjust); \
   AddIDeltas_DY_g##GOURAUD_LIT##_t##TEXTURED_LIT(&ig, idl, y); \
   /* Step the perspective-correct interpolants to the clipped start \
    * pixel.  Affine UV is stepped above by AddIDeltas_DX/DY; for the \
    * pct path we step the float (1/w, u/w, v/w) the same way.  When \
    * the pct flag is off, idl's pct fields are not initialised, so \
    * the loads are conditional on `pct`. */ \
   if (TEXTURED_LIT && pct) \
   { \
      ig.pct_inv_w    += idl->d_inv_w_dx    * (float)x_ig_adjust + idl->d_inv_w_dy    * (float)y; \
      ig.pct_u_over_w += idl->d_u_over_w_dx * (float)x_ig_adjust + idl->d_u_over_w_dy * (float)y; \
      ig.pct_v_over_w += idl->d_v_over_w_dx * (float)x_ig_adjust + idl->d_v_over_w_dy * (float)y; \
   } \
   /* Only compute timings for one every `upscale_shift` lines so that we */ \
   /* don't end up "slower" than 1x.  Also skip the charge for native rows */ \
   /* that LineSkipTest would have rejected when dfe-skip is in effect; */ \
   /* psx_gpu_rasterize_both_fields draws those rows anyway but we still */ \
   /* want the rasteriser budget consumption to match real-PSX behaviour, */ \
   /* otherwise complex 480i scenes drop polygons when DrawTimeAvail */ \
   /* hits zero halfway through the frame. */ \
   if ((y & ((1UL << gpu->upscale_shift) - 1)) == 0 \
         && !DfeWouldSkip(gpu, y >> gpu->upscale_shift)) { \
      if ((GOURAUD_LIT) || (TEXTURED_LIT)) \
         gpu->DrawTimeAvail -= (w * 2) >> gpu->upscale_shift; \
      else if (((BM_VAL) >= 0) || (ME_LIT)) \
         gpu->DrawTimeAvail -= (w + ((w + 1) >> 1)) >> gpu->upscale_shift; \
      else \
         gpu->DrawTimeAvail -= w >> gpu->upscale_shift; \
   } \
   do \
   { \
      const uint32_t r = ig.r >> (COORD_FBS + COORD_POST_PADDING); \
      const uint32_t g = ig.g >> (COORD_FBS + COORD_POST_PADDING); \
      const uint32_t b = ig.b >> (COORD_FBS + COORD_POST_PADDING); \
      uint32_t dither_x = (x >> gpu->dither_upscale_shift) & 3; \
      uint32_t dither_y = (y >> gpu->dither_upscale_shift) & 3; \
      /*assert(x >= ClipX0 && x <= ClipX1);*/ \
      if (TEXTURED_LIT) \
      { \
         int32_t tex_u, tex_v; \
         uint16_t fbw; \
         /* Perspective-correct sampling.  Three guards: \
          * (1) inv_w > eps - very small inv_w corresponds to a \
          *     vertex at huge w (effectively at infinity), where \
          *     1/inv_w explodes; fall through to the affine path. \
          * (2) clamp recovered U/V to the convex hull of the three \
          *     vertex UVs.  Critical: the rasteriser walks integer \
          *     pixel positions, but the precise triangle has float \
          *     edges, so pixels at the very boundary land a \
          *     fraction outside the precise triangle and linearly \
          *     extrapolate u/w, v/w, 1/w.  Dividing the \
          *     extrapolated u/w by an inv_w that drifts toward \
          *     zero produces UV values many texels outside the \
          *     vertex range, which the &0xFF wrap below would turn \
          *     into completely wrong texels (visible speckles).  \
          *     The HW fragment shader does the same clamp via \
          *     clamp_coord(vUV) against vTexLimits. \
          * (3) AND with 0xFF on the int result, matching the PSX \
          *     UV byte domain and the affine path's natural uint32 \
          *     wrap; this also turns small negatives produced by \
          *     the rounding bias near vertex U/V=0 into the \
          *     correct 0 (clamp(.,0,max) + +0.5 bias = 0..max+0.5, \
          *     int cast = 0..max, AND = same; for negative inputs \
          *     pre-clamp, the clamp brings them to 0 anyway). \
          * The +bias matches the affine seed's `+(1 << (COORD_FBS \
          * - 1 - upscale_shift))`: half a native texel added \
          * before the integer truncation, i.e. round-to-nearest \
          * sampling.  The HW backends get this implicitly via \
          * primitive.frag's `int(vUV)` after the GPU's pixel- \
          * center interpolation. */ \
         if (pct && ig.pct_inv_w > 1e-6f) \
         { \
            float inv  = 1.0f / ig.pct_inv_w; \
            float bias = 0.5f / (float)(1 << gpu->upscale_shift); \
            float fu   = ig.pct_u_over_w * inv + bias; \
            float fv   = ig.pct_v_over_w * inv + bias; \
            if (fu < idl->min_u) fu = idl->min_u; \
            else if (fu > idl->max_u) fu = idl->max_u; \
            if (fv < idl->min_v) fv = idl->min_v; \
            else if (fv > idl->max_v) fv = idl->max_v; \
            tex_u = ((int32_t)fu) & 0xFF; \
            tex_v = ((int32_t)fv) & 0xFF; \
         } \
         else \
         { \
            tex_u = ig.u >> (COORD_FBS + COORD_POST_PADDING); \
            tex_v = ig.v >> (COORD_FBS + COORD_POST_PADDING); \
         } \
         fbw = GetTexel_TM##MO_LIT(gpu, tex_u, tex_v); \
         if (fbw) \
         { \
            if (TM_LIT) \
            { \
               uint8_t *dither_offset; \
               if (!DitherEnabled(gpu)) \
               { \
                  dither_x = 3; \
                  dither_y = 2; \
               } \
               dither_offset = gpu->DitherLUT[dither_y][dither_x]; \
               fbw = ModTexel(dither_offset, fbw, r, g, b); \
            } \
            PlotPixel_##BM_TAG##_ME##ME_LIT##_T1(gpu, x, y, fbw); \
         } \
      } \
      else \
      { \
         uint16_t pix = 0x8000; \
         if ((GOURAUD_LIT) && DitherEnabled(gpu)) \
         { \
            pix |= gpu->DitherLUT[dither_y][dither_x][r] << 0; \
            pix |= gpu->DitherLUT[dither_y][dither_x][g] << 5; \
            pix |= gpu->DitherLUT[dither_y][dither_x][b] << 10; \
         } \
         else \
         { \
            pix |= (r >> 3) << 0; \
            pix |= (g >> 3) << 5; \
            pix |= (b >> 3) << 10; \
         } \
         PlotPixel_##BM_TAG##_ME##ME_LIT##_T0(gpu, x, y, pix); \
      } \
      x++; \
      AddIDeltas_DX_g##GOURAUD_LIT##_t##TEXTURED_LIT(&ig, idl, 1); \
      if (TEXTURED_LIT && pct) \
      { \
         ig.pct_inv_w    += idl->d_inv_w_dx; \
         ig.pct_u_over_w += idl->d_u_over_w_dx; \
         ig.pct_v_over_w += idl->d_v_over_w_dx; \
      } \
   } while (MDFN_LIKELY(--w > 0)); \
}

/* DRAWSPAN_T0_BMGROUP and DRAWSPAN_T1_BMGROUP emit the 10
 * (BlendMode * MaskEval) specs for given outer parameters.
 *   T0 (non-textured): TM/MO collapse to 0; only Gouraud varies
 *      across the 2 outer-group calls.
 *   T1 (textured)    : full TM (2) * MO (3) cross product. */

#define DRAWSPAN_T0_BMGROUP(G) \
   DEFINE_DrawSpan(g##G##_t0_BMopaque_TM0_MO0_ME0, G, 0, -1, BMopaque, 0, 0, 0) \
   DEFINE_DrawSpan(g##G##_t0_BMopaque_TM0_MO0_ME1, G, 0, -1, BMopaque, 0, 0, 1) \
   DEFINE_DrawSpan(g##G##_t0_BMavg_TM0_MO0_ME0,    G, 0,  0, BMavg,    0, 0, 0) \
   DEFINE_DrawSpan(g##G##_t0_BMavg_TM0_MO0_ME1,    G, 0,  0, BMavg,    0, 0, 1) \
   DEFINE_DrawSpan(g##G##_t0_BMadd_TM0_MO0_ME0,    G, 0,  1, BMadd,    0, 0, 0) \
   DEFINE_DrawSpan(g##G##_t0_BMadd_TM0_MO0_ME1,    G, 0,  1, BMadd,    0, 0, 1) \
   DEFINE_DrawSpan(g##G##_t0_BMsub_TM0_MO0_ME0,    G, 0,  2, BMsub,    0, 0, 0) \
   DEFINE_DrawSpan(g##G##_t0_BMsub_TM0_MO0_ME1,    G, 0,  2, BMsub,    0, 0, 1) \
   DEFINE_DrawSpan(g##G##_t0_BMaddq_TM0_MO0_ME0,   G, 0,  3, BMaddq,   0, 0, 0) \
   DEFINE_DrawSpan(g##G##_t0_BMaddq_TM0_MO0_ME1,   G, 0,  3, BMaddq,   0, 0, 1)

#define DRAWSPAN_T1_BMGROUP(G, TM, MO) \
   DEFINE_DrawSpan(g##G##_t1_BMopaque_TM##TM##_MO##MO##_ME0, G, 1, -1, BMopaque, TM, MO, 0) \
   DEFINE_DrawSpan(g##G##_t1_BMopaque_TM##TM##_MO##MO##_ME1, G, 1, -1, BMopaque, TM, MO, 1) \
   DEFINE_DrawSpan(g##G##_t1_BMavg_TM##TM##_MO##MO##_ME0,    G, 1,  0, BMavg,    TM, MO, 0) \
   DEFINE_DrawSpan(g##G##_t1_BMavg_TM##TM##_MO##MO##_ME1,    G, 1,  0, BMavg,    TM, MO, 1) \
   DEFINE_DrawSpan(g##G##_t1_BMadd_TM##TM##_MO##MO##_ME0,    G, 1,  1, BMadd,    TM, MO, 0) \
   DEFINE_DrawSpan(g##G##_t1_BMadd_TM##TM##_MO##MO##_ME1,    G, 1,  1, BMadd,    TM, MO, 1) \
   DEFINE_DrawSpan(g##G##_t1_BMsub_TM##TM##_MO##MO##_ME0,    G, 1,  2, BMsub,    TM, MO, 0) \
   DEFINE_DrawSpan(g##G##_t1_BMsub_TM##TM##_MO##MO##_ME1,    G, 1,  2, BMsub,    TM, MO, 1) \
   DEFINE_DrawSpan(g##G##_t1_BMaddq_TM##TM##_MO##MO##_ME0,   G, 1,  3, BMaddq,   TM, MO, 0) \
   DEFINE_DrawSpan(g##G##_t1_BMaddq_TM##TM##_MO##MO##_ME1,   G, 1,  3, BMaddq,   TM, MO, 1)

DRAWSPAN_T0_BMGROUP(0)
DRAWSPAN_T0_BMGROUP(1)

DRAWSPAN_T1_BMGROUP(0, 0, 0)
DRAWSPAN_T1_BMGROUP(0, 0, 1)
DRAWSPAN_T1_BMGROUP(0, 0, 2)
DRAWSPAN_T1_BMGROUP(0, 1, 0)
DRAWSPAN_T1_BMGROUP(0, 1, 1)
DRAWSPAN_T1_BMGROUP(0, 1, 2)
DRAWSPAN_T1_BMGROUP(1, 0, 0)
DRAWSPAN_T1_BMGROUP(1, 0, 1)
DRAWSPAN_T1_BMGROUP(1, 0, 2)
DRAWSPAN_T1_BMGROUP(1, 1, 0)
DRAWSPAN_T1_BMGROUP(1, 1, 1)
DRAWSPAN_T1_BMGROUP(1, 1, 2)

/*
 * DrawTriangle - rasterise one triangle.
 *
 * Walks the top and bottom edges to compute scanline x ranges,
 * then calls DrawSpan once per scanline.  Macro parameters
 * pass through verbatim from Command_DrawPolygon below; see
 * DrawSpan above for their semantics.
 *
 * The "core_vertex" selection picks which of the three vertices
 * is the apex (highest or lowest y) and which two form the base.
 * This affects subpixel-correct edge walking on PS1 hardware
 * where the rasterisation order of the three triangle vertices
 * is observable in some games' rendering.
 */
#define DEFINE_DrawTriangle(SUFFIX, GOURAUD_LIT, TEXTURED_LIT, BM_VAL, BM_TAG, TM_LIT, MO_LIT, ME_LIT) \
static INLINE void DrawTriangle_##SUFFIX(PS_GPU *gpu, tri_vertex *vertices, const bool pct, const i_deltas *override_idl) \
{ \
   i_deltas idl; \
   unsigned core_vertex; \
   int32_t clipy0 = gpu->ClipY0 << gpu->upscale_shift; \
   int32_t clipy1 = gpu->ClipY1 << gpu->upscale_shift; \
   int64_t base_coord; \
   int64_t base_step; \
   int64_t bound_coord_us; \
   int64_t bound_coord_ls; \
   bool right_facing; \
   bool pct_local = pct; \
   i_group ig; \
   unsigned vo = 0; \
   unsigned vp = 0; \
   unsigned i; \
   struct tripart \
   { \
      uint64_t x_coord[2]; \
      uint64_t x_step[2]; \
      int32_t y_coord; \
      int32_t y_bound; \
      bool dec_mode; \
   } tripart[2]; \
   /* Calculate the "core" vertex based on the unsorted input vertices, and sort vertices by Y. */ \
   { \
      unsigned cvtemp = 0; \
      if (vertices[1].x <= vertices[0].x) \
      { \
         if (vertices[2].x <= vertices[1].x) \
            cvtemp = (1 << 2); \
         else \
            cvtemp = (1 << 1); \
      } \
      else if (vertices[2].x < vertices[0].x) \
         cvtemp = (1 << 2); \
      else \
         cvtemp = (1 << 0); \
      if (vertices[2].y < vertices[1].y) \
      { \
         vertex_swap(tri_vertex, vertices[2], vertices[1]); \
         cvtemp = ((cvtemp >> 1) & 0x2) | ((cvtemp << 1) & 0x4) | (cvtemp & 0x1); \
      } \
      if (vertices[1].y < vertices[0].y) \
      { \
         vertex_swap(tri_vertex, vertices[1], vertices[0]); \
         cvtemp = ((cvtemp >> 1) & 0x1) | ((cvtemp << 1) & 0x2) | (cvtemp & 0x4); \
      } \
      if (vertices[2].y < vertices[1].y) \
      { \
         vertex_swap(tri_vertex, vertices[2], vertices[1]); \
         cvtemp = ((cvtemp >> 1) & 0x2) | ((cvtemp << 1) & 0x4) | (cvtemp & 0x1); \
      } \
      core_vertex = cvtemp >> 1; \
   } \
   /* 0-height, abort out. */ \
   if (vertices[0].y == vertices[2].y) \
      return; \
   /* When override_idl is supplied (subdivision children, sharing \
    * the parent's color/UV gradients to avoid per-triangle fixed- \
    * point rounding seams), use it directly.  Otherwise compute \
    * deltas from this triangle's own vertices. */ \
   if (override_idl) \
      idl = *override_idl; \
   else if (!CalcIDeltas_g##GOURAUD_LIT##_t##TEXTURED_LIT(&idl, &vertices[0], &vertices[1], &vertices[2])) \
      return; \
   /* Perspective-correct UV deltas - only when the caller said this \
    * primitive is eligible (textured + PGXP-on + texture-correction \
    * + all w > 0).  CalcIDeltas_Persp uses the precise float verts, \
    * which can degenerate even when the integer ones don't (e.g. all \
    * three projected to the same float coord); fall back to affine \
    * in that case. */ \
   if (TEXTURED_LIT && pct_local) \
   { \
      if (!CalcIDeltas_Persp(&idl, &vertices[0], &vertices[1], &vertices[2])) \
         pct_local = false; \
   } \
   /* [0] should be top vertex, [2] should be bottom vertex, [1] should be off to the side vertex. */ \
   if (TEXTURED_LIT) \
   { \
      ig.u = (COORD_MF_INT(vertices[core_vertex].u) + (1 << (COORD_FBS - 1 - gpu->upscale_shift))) << COORD_POST_PADDING; \
      ig.v = (COORD_MF_INT(vertices[core_vertex].v) + (1 << (COORD_FBS - 1 - gpu->upscale_shift))) << COORD_POST_PADDING; \
      if (gpu->upscale_shift > 0) \
      { \
         /* Bias the texture coordinates so that it rounds to the */ \
         /* correct value when the game is mapping a 2D sprite using */ \
         /* triangles.  Otherwise this could cause a small "shift" in */ \
         /* the texture coordinates when upscaling. */ \
         if (gpu->off_u) \
            ig.u += (COORD_MF_INT(1) - (1 << (COORD_FBS - gpu->upscale_shift))) << COORD_POST_PADDING; \
         if (gpu->off_v) \
            ig.v += (COORD_MF_INT(1) - (1 << (COORD_FBS - gpu->upscale_shift))) << COORD_POST_PADDING; \
      } \
      if (pct_local) \
      { \
         /* Seed the perspective interpolants at the core vertex (origin \
          * for the per-pixel DX/DY stepping that DrawSpan does), then \
          * subtract the core vertex's screen-space offset so that \
          * adding (x_ig_adjust, y) inside DrawSpan resolves to the \
          * value at that pixel.  Use the *float* precise[0]/precise[1] \
          * coords for the anchor, not the integer vertices[].x/y - the \
          * (u_corevertex * inv_w_corevertex) seed value is the function \
          * value at the precise float position, and any sub-pixel drift \
          * between precise[0]/precise[1] and the integer vertices.x/y \
          * (which was clobbered to (int)precise[0] earlier) would \
          * otherwise propagate into a UV error of delta * fract(precise) \
          * per pixel.  CalcIDeltas_Persp uses the same precise floats for \
          * the denominator, so this keeps the screen-space math fully \
          * consistent. */ \
         float inv_w = 1.0f / vertices[core_vertex].precise[2]; \
         ig.pct_inv_w    = inv_w; \
         ig.pct_u_over_w = (float)vertices[core_vertex].u * inv_w; \
         ig.pct_v_over_w = (float)vertices[core_vertex].v * inv_w; \
         ig.pct_inv_w    -= idl.d_inv_w_dx    * vertices[core_vertex].precise[0] \
                          + idl.d_inv_w_dy    * vertices[core_vertex].precise[1]; \
         ig.pct_u_over_w -= idl.d_u_over_w_dx * vertices[core_vertex].precise[0] \
                          + idl.d_u_over_w_dy * vertices[core_vertex].precise[1]; \
         ig.pct_v_over_w -= idl.d_v_over_w_dx * vertices[core_vertex].precise[0] \
                          + idl.d_v_over_w_dy * vertices[core_vertex].precise[1]; \
      } \
   } \
   ig.r = (COORD_MF_INT(vertices[core_vertex].r) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING; \
   ig.g = (COORD_MF_INT(vertices[core_vertex].g) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING; \
   ig.b = (COORD_MF_INT(vertices[core_vertex].b) + (1 << (COORD_FBS - 1))) << COORD_POST_PADDING; \
   AddIDeltas_DX_g##GOURAUD_LIT##_t##TEXTURED_LIT(&ig, &idl, -vertices[core_vertex].x); \
   AddIDeltas_DY_g##GOURAUD_LIT##_t##TEXTURED_LIT(&ig, &idl, -vertices[core_vertex].y); \
   base_coord = MakePolyXFP(vertices[0].x); \
   base_step  = MakePolyXFPStep((vertices[2].x - vertices[0].x), (vertices[2].y - vertices[0].y)); \
   if (vertices[1].y == vertices[0].y) \
   { \
      bound_coord_us = 0; \
      right_facing = (bool)(vertices[1].x > vertices[0].x); \
   } \
   else \
   { \
      bound_coord_us = MakePolyXFPStep((vertices[1].x - vertices[0].x), (vertices[1].y - vertices[0].y)); \
      right_facing = (bool)(bound_coord_us > base_step); \
   } \
   if (vertices[2].y == vertices[1].y) \
      bound_coord_ls = 0; \
   else \
      bound_coord_ls = MakePolyXFPStep((vertices[2].x - vertices[1].x), (vertices[2].y - vertices[1].y)); \
   if (core_vertex) \
      vo = 1; \
   if (core_vertex == 2) \
      vp = 3; \
   { \
      struct tripart* tp = &tripart[vo]; \
      tp->y_coord = vertices[0 ^ vo].y; \
      tp->y_bound = vertices[1 ^ vo].y; \
      tp->x_coord[right_facing] = MakePolyXFP(vertices[0 ^ vo].x); \
      tp->x_step[right_facing] = bound_coord_us; \
      tp->x_coord[!right_facing] = base_coord + ((vertices[vo].y - vertices[0].y) * base_step); \
      tp->x_step[!right_facing] = base_step; \
      tp->dec_mode = vo; \
   } \
   { \
      struct tripart* tp = &tripart[vo ^ 1]; \
      tp->y_coord = vertices[1 ^ vp].y; \
      tp->y_bound = vertices[2 ^ vp].y; \
      tp->x_coord[right_facing] = MakePolyXFP(vertices[1 ^ vp].x); \
      tp->x_step[right_facing] = bound_coord_ls; \
      tp->x_coord[!right_facing] = base_coord + ((vertices[1 ^ vp].y - vertices[0].y) * base_step); \
      tp->x_step[!right_facing] = base_step; \
      tp->dec_mode = vp; \
   } \
   for (i = 0; i < 2; i++) \
   { \
      int32_t yi = tripart[i].y_coord; \
      int32_t yb = tripart[i].y_bound; \
      uint64_t lc = tripart[i].x_coord[0]; \
      uint64_t ls = tripart[i].x_step[0]; \
      uint64_t rc = tripart[i].x_coord[1]; \
      uint64_t rs = tripart[i].x_step[1]; \
      if (tripart[i].dec_mode) \
      { \
         while (MDFN_LIKELY(yi > yb)) \
         { \
            int32_t y; \
            yi--; \
            lc -= ls; \
            rc -= rs; \
            y = sign_x_to_s32(11 + gpu->upscale_shift, yi); \
            if (y < clipy0) \
               break; \
            if (y > clipy1) \
            { \
               gpu->DrawTimeAvail -= 2; \
               continue; \
            } \
            DrawSpan_g##GOURAUD_LIT##_t##TEXTURED_LIT##_##BM_TAG##_TM##TM_LIT##_MO##MO_LIT##_ME##ME_LIT(gpu, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, &idl, pct_local); \
         } \
      } \
      else \
      { \
         while (MDFN_LIKELY(yi < yb)) \
         { \
            int32_t y = sign_x_to_s32(11 + gpu->upscale_shift, yi); \
            if (y > clipy1) \
               break; \
            if (y < clipy0) \
            { \
               gpu->DrawTimeAvail -= 2; \
               goto skipit_##SUFFIX; \
            } \
            DrawSpan_g##GOURAUD_LIT##_t##TEXTURED_LIT##_##BM_TAG##_TM##TM_LIT##_MO##MO_LIT##_ME##ME_LIT(gpu, yi, GetPolyXFP_Int(lc), GetPolyXFP_Int(rc), ig, &idl, pct_local); \
            skipit_##SUFFIX: ; \
            yi++; \
            lc += ls; \
            rc += rs; \
         } \
      } \
   } \
}

#define DRAWTRI_T0_BMGROUP(G) \
   DEFINE_DrawTriangle(g##G##_t0_BMopaque_TM0_MO0_ME0, G, 0, -1, BMopaque, 0, 0, 0) \
   DEFINE_DrawTriangle(g##G##_t0_BMopaque_TM0_MO0_ME1, G, 0, -1, BMopaque, 0, 0, 1) \
   DEFINE_DrawTriangle(g##G##_t0_BMavg_TM0_MO0_ME0,    G, 0,  0, BMavg,    0, 0, 0) \
   DEFINE_DrawTriangle(g##G##_t0_BMavg_TM0_MO0_ME1,    G, 0,  0, BMavg,    0, 0, 1) \
   DEFINE_DrawTriangle(g##G##_t0_BMadd_TM0_MO0_ME0,    G, 0,  1, BMadd,    0, 0, 0) \
   DEFINE_DrawTriangle(g##G##_t0_BMadd_TM0_MO0_ME1,    G, 0,  1, BMadd,    0, 0, 1) \
   DEFINE_DrawTriangle(g##G##_t0_BMsub_TM0_MO0_ME0,    G, 0,  2, BMsub,    0, 0, 0) \
   DEFINE_DrawTriangle(g##G##_t0_BMsub_TM0_MO0_ME1,    G, 0,  2, BMsub,    0, 0, 1) \
   DEFINE_DrawTriangle(g##G##_t0_BMaddq_TM0_MO0_ME0,   G, 0,  3, BMaddq,   0, 0, 0) \
   DEFINE_DrawTriangle(g##G##_t0_BMaddq_TM0_MO0_ME1,   G, 0,  3, BMaddq,   0, 0, 1)

#define DRAWTRI_T1_BMGROUP(G, TM, MO) \
   DEFINE_DrawTriangle(g##G##_t1_BMopaque_TM##TM##_MO##MO##_ME0, G, 1, -1, BMopaque, TM, MO, 0) \
   DEFINE_DrawTriangle(g##G##_t1_BMopaque_TM##TM##_MO##MO##_ME1, G, 1, -1, BMopaque, TM, MO, 1) \
   DEFINE_DrawTriangle(g##G##_t1_BMavg_TM##TM##_MO##MO##_ME0,    G, 1,  0, BMavg,    TM, MO, 0) \
   DEFINE_DrawTriangle(g##G##_t1_BMavg_TM##TM##_MO##MO##_ME1,    G, 1,  0, BMavg,    TM, MO, 1) \
   DEFINE_DrawTriangle(g##G##_t1_BMadd_TM##TM##_MO##MO##_ME0,    G, 1,  1, BMadd,    TM, MO, 0) \
   DEFINE_DrawTriangle(g##G##_t1_BMadd_TM##TM##_MO##MO##_ME1,    G, 1,  1, BMadd,    TM, MO, 1) \
   DEFINE_DrawTriangle(g##G##_t1_BMsub_TM##TM##_MO##MO##_ME0,    G, 1,  2, BMsub,    TM, MO, 0) \
   DEFINE_DrawTriangle(g##G##_t1_BMsub_TM##TM##_MO##MO##_ME1,    G, 1,  2, BMsub,    TM, MO, 1) \
   DEFINE_DrawTriangle(g##G##_t1_BMaddq_TM##TM##_MO##MO##_ME0,   G, 1,  3, BMaddq,   TM, MO, 0) \
   DEFINE_DrawTriangle(g##G##_t1_BMaddq_TM##TM##_MO##MO##_ME1,   G, 1,  3, BMaddq,   TM, MO, 1)

DRAWTRI_T0_BMGROUP(0)
DRAWTRI_T0_BMGROUP(1)

DRAWTRI_T1_BMGROUP(0, 0, 0)
DRAWTRI_T1_BMGROUP(0, 0, 1)
DRAWTRI_T1_BMGROUP(0, 0, 2)
DRAWTRI_T1_BMGROUP(0, 1, 0)
DRAWTRI_T1_BMGROUP(0, 1, 1)
DRAWTRI_T1_BMGROUP(0, 1, 2)
DRAWTRI_T1_BMGROUP(1, 0, 0)
DRAWTRI_T1_BMGROUP(1, 0, 1)
DRAWTRI_T1_BMGROUP(1, 0, 2)
DRAWTRI_T1_BMGROUP(1, 1, 0)
DRAWTRI_T1_BMGROUP(1, 1, 1)
DRAWTRI_T1_BMGROUP(1, 1, 2)

/* Forward declarations for the helper functions defined in
 * gpu_polygon_sub.c.  Since gpu_polygon.c is now itself plain C
 * (textually included into gpu.c, also plain C as of stage 5),
 * these are simple C-linkage forward declarations. */
void Calc_UVOffsets_Adjust_Verts(PS_GPU *gpu, tri_vertex *vertices, unsigned count);
void Reset_UVLimits(PS_GPU *gpu);
void Extend_UVLimits(PS_GPU *gpu, tri_vertex *vertices, unsigned count);
void Finalise_UVLimits(PS_GPU *gpu);
bool Hack_FindLine(PS_GPU *gpu, tri_vertex* vertices, tri_vertex* outVertices);
bool Hack_ForceLine(PS_GPU *gpu, tri_vertex* vertices, tri_vertex* outVertices);

/* The C++ template wrapper for DrawTriangle that previously sat
 * here is gone in stage 4: Command_DrawPolygon below is now itself
 * macro-generated, so the per-spec call to DrawTriangle resolves
 * to a direct mangled-name reference at preprocessor time.  No
 * runtime switch on BlendMode / TexMode_TA / TexMult / MaskEval is
 * needed since every parameter is a literal in the macro context.
 */

extern int psx_pgxp_2d_tol;

/*
 * Command_DrawPolygon - top-level GP0 polygon command handler.
 *
 * Parses the GP0 command buffer for vertex/colour/uv data,
 * applies the drawing-offset, optionally consults PGXP for
 * subpixel-precision projection, then walks one or two triangles
 * (a triangle for numvertices==3, two tris for numvertices==4).
 *
 * Macro parameters:
 *   SUFFIX        - mangled-name suffix encoding all dimensions
 *   NV_LIT        - 3 or 4: numvertices
 *   GOURAUD_LIT   - 0/1: per-vertex colour
 *   TEXTURED_LIT  - 0/1: texture sampling
 *   BM_VAL        - integer literal blend mode (-1 / 0..3)
 *   BM_TAG        - matching blend tag (BMopaque / BMavg / BMadd /
 *                   BMsub / BMaddq) used in the DrawTriangle_<...>
 *                   mangled call
 *   TM_LIT        - 0/1: texel-colour modulation flag
 *   MO_LIT        - 0/1/2: 4 / 8 / 15bpp texture format
 *   ME_LIT        - 0/1: mask-bit gate
 *   PGXP_LIT      - 0/1: PGXP subpixel-precision path
 *
 * Reached from the GP0 dispatch via:
 *   Commands[0x20..0x3F].func[abr][slot] (POLY_HELPER family)
 *     -> G_Command_DrawPolygon (PGXP runtime gate, in gpu.cpp)
 *       -> Command_DrawPolygon (this body)
 *         -> DrawTriangle_<...> (per triangle, mangled-name call)
 *           -> DrawSpan_<...> (per scanline)
 *             -> PlotPixel_<...> (per pixel)
 *
 * Every layer is fully macro-specialised, so the entire chain
 * reduces to direct calls under -O2 since all parameters are
 * compile-time literals in the macro context.
 */

/* Forward declaration for the subdivision emit callback.  Used from
 * inside the DEFINE_Command_DrawPolygon macro body (when flushing
 * deferred polygons before drawing an ineligible one) and from
 * gpu_polygon_subdiv_flush() at the bottom of this file.  The actual
 * definition is at the end of this file, after all DrawTriangle
 * specs are visible. */
static void gpu_subdiv_emit_one(PS_GPU *gpu, tri_vertex *vertices,
      uint8_t flags, const tri_vertex *parent_vertices, void *tag);

#define DEFINE_Command_DrawPolygon(SUFFIX, NV_LIT, GOURAUD_LIT, TEXTURED_LIT, BM_VAL, BM_TAG, TM_LIT, MO_LIT, ME_LIT, PGXP_LIT) \
static void Command_DrawPolygon_##SUFFIX(PS_GPU *gpu, const uint32_t *cb) \
{ \
   tri_vertex     vertices[3]; \
   const uint32_t *baseCB = cb; \
   const unsigned cb0     = cb[0]; \
   uint32_t       clut    = 0; \
   unsigned       sv      = 0; \
   bool           invalidW = false; \
   unsigned       v; \
   uint16_t       clut_x, clut_y; \
   tri_vertex     lineVertices[3]; \
   bool           lineFound = false; \
   /*uint32_t tpage = 0;*/ \
   vertices[0].x          = 0; \
   vertices[0].y          = 0; \
   vertices[0].u          = 0; \
   vertices[0].v          = 0; \
   vertices[0].r          = 0; \
   vertices[0].g          = 0; \
   vertices[0].b          = 0; \
   vertices[0].precise[0] = 0.0f; \
   vertices[0].precise[1] = 0.0f; \
   vertices[0].precise[2] = 0.0f; \
   vertices[1].x          = 0; \
   vertices[1].y          = 0; \
   vertices[1].u          = 0; \
   vertices[1].v          = 0; \
   vertices[1].r          = 0; \
   vertices[1].g          = 0; \
   vertices[1].b          = 0; \
   vertices[1].precise[0] = 0.0f; \
   vertices[1].precise[1] = 0.0f; \
   vertices[1].precise[2] = 0.0f; \
   vertices[2].x          = 0; \
   vertices[2].y          = 0; \
   vertices[2].u          = 0; \
   vertices[2].v          = 0; \
   vertices[2].r          = 0; \
   vertices[2].g          = 0; \
   vertices[2].b          = 0; \
   vertices[2].precise[0] = 0.0f; \
   vertices[2].precise[1] = 0.0f; \
   vertices[2].precise[2] = 0.0f; \
   /* Base timing is approximate, and could be improved. */ \
   if (NV_LIT == 4 && gpu->InCmd == INCMD_QUAD) \
      gpu->DrawTimeAvail -= (28 + 18); \
   else \
      gpu->DrawTimeAvail -= (64 + 18); \
   if (GOURAUD_LIT && TEXTURED_LIT) \
      gpu->DrawTimeAvail -= 150 * 3; \
   else if (GOURAUD_LIT) \
      gpu->DrawTimeAvail -= 96 * 3; \
   else if (TEXTURED_LIT) \
      gpu->DrawTimeAvail -= 60 * 3; \
   /* if entire previous quad was rejected reset flags */ \
   if (gpu->killQuadPart == 3) \
      gpu->killQuadPart = 0; \
   if (NV_LIT == 4) \
   { \
      if (gpu->InCmd == INCMD_QUAD) \
      { \
         memcpy(&vertices[0], &gpu->InQuad_F3Vertices[1], 2 * sizeof(tri_vertex)); \
         clut     = gpu->InQuad_clut; \
         invalidW = gpu->InQuad_invalidW; \
         sv       = 2; \
      } \
   } \
   /* else memset(vertices, 0, sizeof(vertices)); */ \
   for (v = sv; v < 3; v++) \
   { \
      int32_t x, y; \
      if (v == 0 || GOURAUD_LIT) \
      { \
         uint32_t raw_color = (*cb & 0xFFFFFF); \
         vertices[v].r = raw_color & 0xFF; \
         vertices[v].g = (raw_color >> 8) & 0xFF; \
         vertices[v].b = (raw_color >> 16) & 0xFF; \
         cb++; \
      } \
      else \
      { \
         vertices[v].r = vertices[0].r; \
         vertices[v].g = vertices[0].g; \
         vertices[v].b = vertices[0].b; \
      } \
      x = sign_x_to_s32(11, ((int16_t)(*cb & 0xFFFF))); \
      y = sign_x_to_s32(11, ((int16_t)(*cb >> 16))); \
      vertices[v].x = (x + gpu->OffsX) << gpu->upscale_shift; \
      vertices[v].y = (y + gpu->OffsY) << gpu->upscale_shift; \
      if (PGXP_LIT) \
      { \
         OGLVertex vert; \
         PGXP_GetVertex(cb - baseCB, cb, &vert, 0, 0); \
         vertices[v].precise[0] = ((vert.x + (float)gpu->OffsX) * UPSCALE(gpu)); \
         vertices[v].precise[1] = ((vert.y + (float)gpu->OffsY) * UPSCALE(gpu)); \
         vertices[v].precise[2] = vert.w; \
         if (!vert.valid_w || vert.w <= 0.0) \
            invalidW = true; \
      } \
      else \
      { \
         vertices[v].precise[0] = vertices[v].x; \
         vertices[v].precise[1] = vertices[v].y; \
         invalidW = true; \
      } \
      cb++; \
      if (TEXTURED_LIT) \
      { \
         vertices[v].u = (*cb & 0xFF); \
         vertices[v].v = (*cb >> 8) & 0xFF; \
         if (v == 0) \
         { \
            clut = ((*cb >> 16) & 0xFFFF) << 4; \
            Update_CLUT_Cache_TM##MO_LIT(gpu, (*cb >> 16) & 0xFFFF); \
         } \
         cb++; \
      } \
   } \
   /* iCB: If any vertices lack w components then set all to 1 */ \
   if (invalidW) \
      for (v = 0; v < 3; v++) \
      { \
         /* lacking w component tends to mean degenerate coordinates */ \
         /* set to non-pgxp value if difference is too great */ \
         if (PGXP_LIT && psx_pgxp_2d_tol >= 0) \
         { \
            float tol = (float)((unsigned)psx_pgxp_2d_tol << gpu->upscale_shift); \
            if ( \
               fabsf(vertices[v].precise[0] - (float)vertices[v].x) > tol || \
               fabsf(vertices[v].precise[1] - (float)vertices[v].y) > tol \
            ) \
            { \
               vertices[v].precise[0] = vertices[v].x; \
               vertices[v].precise[1] = vertices[v].y; \
            } \
         } \
         vertices[v].precise[2] = 1.f; \
      } \
   /* Copy before Calc_UVOffsets which modifies vertices */ \
   /* Calc_UVOffsets likes to see unadjusted vertices */ \
   if (NV_LIT == 4 && gpu->InCmd != INCMD_QUAD) \
      memcpy(&gpu->InQuad_F3Vertices[1], &vertices[1], sizeof(tri_vertex) * 2); \
   /* Calculated UV offsets (needed for hardware renderers and software with scaling) */ \
   /* Do one time updates for primitive */ \
   if (TEXTURED_LIT) \
      Calc_UVOffsets_Adjust_Verts(gpu, vertices, NV_LIT); \
   if (NV_LIT == 4) \
   { \
      if (gpu->InCmd == INCMD_QUAD) \
      { \
         gpu->InCmd = INCMD_NONE; \
         if (invalidW) \
         { \
            if (PGXP_LIT && psx_pgxp_2d_tol >= 0) \
            { \
               float tol = (float)((unsigned)psx_pgxp_2d_tol << gpu->upscale_shift); \
               if ( \
                  fabsf(gpu->InQuad_F3Vertices[0].precise[0] - (float)gpu->InQuad_F3Vertices[0].x) > tol || \
                  fabsf(gpu->InQuad_F3Vertices[0].precise[1] - (float)gpu->InQuad_F3Vertices[0].y) > tol \
               ) \
               { \
                  gpu->InQuad_F3Vertices[0].precise[0] = gpu->InQuad_F3Vertices[0].x; \
                  gpu->InQuad_F3Vertices[0].precise[1] = gpu->InQuad_F3Vertices[0].y; \
               } \
            } \
            /* default first vertex of quad to 1 if any of the vertices are 1 (even if the first triangle was okay) */ \
            gpu->InQuad_F3Vertices[0].precise[2] = 1.f; \
         } \
      } \
      else \
      { \
         gpu->InCmd = INCMD_QUAD; \
         gpu->InCmd_CC = cb0 >> 24; \
         memcpy(&gpu->InQuad_F3Vertices[0], &vertices[0], sizeof(tri_vertex)); \
         gpu->InQuad_clut = clut; \
         gpu->InQuad_invalidW = invalidW; \
      } \
   } \
   if (abs(vertices[2].y - vertices[0].y) >= (512 << gpu->upscale_shift) || \
       abs(vertices[2].y - vertices[1].y) >= (512 << gpu->upscale_shift) || \
       abs(vertices[1].y - vertices[0].y) >= (512 << gpu->upscale_shift)) \
   { \
      if (NV_LIT == 4) \
         gpu->killQuadPart |= (gpu->InCmd == INCMD_QUAD) ? 1 : 2; \
      /* hardware renderer still needs to render first triangle */ \
      if ((rsx_intf_is_type() == RSX_SOFTWARE) || (gpu->killQuadPart != 2)) \
         return; \
   } \
   if (abs(vertices[2].x - vertices[0].x) >= (1024 << gpu->upscale_shift) || \
       abs(vertices[2].x - vertices[1].x) >= (1024 << gpu->upscale_shift) || \
       abs(vertices[1].x - vertices[0].x) >= (1024 << gpu->upscale_shift)) \
   { \
      if (NV_LIT == 4) \
         gpu->killQuadPart |= (gpu->InCmd == INCMD_QUAD) ? 1 : 2; \
      /* hardware renderer still needs to render first triangle */ \
      if ((rsx_intf_is_type() == RSX_SOFTWARE) || (gpu->killQuadPart != 2)) \
         return; \
   } \
   clut_x = (clut & (0x3f << 4)); \
   clut_y = (clut >> 10) & 0x1ff; \
   /* Line Render: store second triangle vertices (software renderer modifies originals) */ \
   /* Used to loop drawing code to draw second triangle (avoids second inline call) */ \
   do \
   { \
      enum blending_modes blend_mode = BLEND_MODE_AVERAGE; \
      if (TEXTURED_LIT) \
      { \
         if (TM_LIT) \
            blend_mode = BLEND_MODE_SUBTRACT; \
         else \
            blend_mode = BLEND_MODE_ADD; \
      } \
      /* Line Renderer: Detect triangles that would resolve as lines at x1 scale and create second triangle to make quad */ \
      if ((line_render_mode != 0) && (!lineFound) && (NV_LIT == 3) && (TEXTURED_LIT)) \
      { \
         if (line_render_mode == 1) \
            lineFound = Hack_FindLine(gpu, vertices, lineVertices);  /* Default enabled */ \
         else if (line_render_mode == 2) \
            lineFound = Hack_ForceLine(gpu, vertices, lineVertices); /* Aggressive mode enabled (causes more artifacts) */ \
         else \
            lineFound = false; \
      } \
      else \
         lineFound = false; \
      if (rsx_intf_is_type() == RSX_OPENGL || rsx_intf_is_type() == RSX_VULKAN) \
      { \
         /* hw_subdiv_buffered: set when the HW subdivision \
          * intercept successfully buffered this primitive into the \
          * S_pending ring.  When true, the original \
          * rsx_intf_push_quad / rsx_intf_push_triangle calls below \
          * are skipped (subdivision will rasterise the children \
          * itself at flush time).  The SW shadow path further \
          * down still runs normally with the ORIGINAL primitive \
          * so VRAM emulation observes the unsubdivided pixels -- \
          * subdivision is a display-side filter only. */ \
         bool hw_subdiv_buffered = false; \
         Reset_UVLimits(gpu); \
         /* Loop subdivision hook for the HW renderer.  Three \
          * compile-time gates dead-code-eliminate this block from \
          * specs where subdivision can never apply: \
          *   - TEXTURED_LIT  (textured specs never subdivide -- \
          *     UV interpolation across subdivided meshes not yet \
          *     implemented) \
          *   - PGXP_LIT      (subdivision requires PGXP precise[]) \
          *   - NV_LIT        (subdivision considers both NV=3 \
          *     triangles and NV=4 quads -- the latter as two \
          *     triangles in PSX's strip layout) \
          * Runtime gate is psx_gpu_subdivision_level > 0 plus the \
          * usual eligibility check (opaque, mask-eval off, valid \
          * PGXP for every vertex).  Quad-as-quad batching to \
          * rsx_intf_push_quad is preserved when subdivision is \
          * off or this primitive is ineligible. \
          * \
          * For HW the emit callback rasterises subdivided children \
          * via rsx_intf_push_triangle.  No parent-delta override \
          * gymnastics needed: HW renderers do barycentric \
          * interpolation in shader and have watertight rasterisation, \
          * so adjacent siblings sharing an edge are continuous in \
          * colour and gap-free in fill -- both of which the SW \
          * rasteriser fails to guarantee, which is why SW \
          * subdivision is currently disabled. */ \
         if (!(TEXTURED_LIT) && psx_gpu_subdivision_level > 0) \
         { \
            uint8_t _sd_flags = \
                  ((GOURAUD_LIT) ? TT_SUBDIV_F_GOURAUD : 0) \
                /* Encode PSX semi-transparency mode (BM_VAL 0..3) \
                 * as the 2-bit BLEND0/BLEND1 field plus the \
                 * BLEND_ENABLE bit.  BM_VAL == -1 means opaque -- \
                 * BLEND_ENABLE stays clear and the 2-bit field is \
                 * don't-care.  The HW emit callback decodes these \
                 * back into the rasteriser's -1/0..3 blend_mode \
                 * argument so subdivided shadows, additive effects, \
                 * etc. preserve their transparency. */ \
                | ((BM_VAL) >= 0 ? TT_SUBDIV_F_BLEND_ENABLE : 0) \
                | (((BM_VAL) >= 0 && ((BM_VAL) & 1)) ? TT_SUBDIV_F_BLEND0 : 0) \
                | (((BM_VAL) >= 0 && ((BM_VAL) & 2)) ? TT_SUBDIV_F_BLEND1 : 0) \
                | ((ME_LIT) ? TT_SUBDIV_F_MASKEVAL : 0) \
                | ((PGXP_LIT) && !invalidW ? TT_SUBDIV_F_PGXP_VALID : 0); \
            if ((NV_LIT == 4) && (!gpu->killQuadPart) && (gpu->InCmd == INCMD_NONE)) \
            { \
               /* Quad case: try to push two triangles to the \
                * subdivision buffer.  PSX quad layout: \
                *   first       vertices[0] \
                *   vertices[1] vertices[2] \
                * The renderer eventually rasterises as two triangles \
                * sharing the diagonal (first <-> vertices[2]) or \
                * (vertices[0] <-> vertices[1]).  We choose the same \
                * split parallel-psx uses internally: (first, v0, v1) \
                * and (v2, v1, v0). */ \
               tri_vertex *first = &gpu->InQuad_F3Vertices[0]; \
               tri_vertex tri_a[3], tri_b[3]; \
               tri_a[0] = *first;       tri_a[1] = vertices[0]; tri_a[2] = vertices[1]; \
               tri_b[0] = vertices[2];  tri_b[1] = vertices[1]; tri_b[2] = vertices[0]; \
               if (tt_subdiv_is_eligible(tri_a, _sd_flags) \
                && tt_subdiv_is_eligible(tri_b, _sd_flags)) \
               { \
                  /* Both halves eligible: buffer them as a 4-vertex \
                   * mesh.  Subdivision will see the shared edge \
                   * (v0, v1) and treat it as interior, producing \
                   * proper mesh-wide smoothing. */ \
                  if (tt_subdiv_would_overlap(tri_a) || tt_subdiv_would_overlap(tri_b)) \
                     tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
                  if (tt_subdiv_push(tri_a, _sd_flags) && tt_subdiv_push(tri_b, _sd_flags)) \
                     hw_subdiv_buffered = true; \
                  else \
                  { \
                     /* Push failed mid-way (buffer-full corner case): \
                      * flush and let the normal path render this quad \
                      * unsubdivided. */ \
                     tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
                  } \
               } \
               else \
               { \
                  /* Quad doesn't qualify as a whole; flush any \
                   * pending and fall through to the normal path. */ \
                  tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
               } \
            } \
            else if (!(NV_LIT == 4 && !gpu->killQuadPart)) \
            { \
               /* Single-triangle case (NV=3, or killQuadPart=2 \
                * second triangle).  Same logic as the quad case \
                * but with one triangle. */ \
               tri_vertex *verts = (gpu->killQuadPart == 2) \
                                   ? &gpu->InQuad_F3Vertices[0] : &vertices[0]; \
               if (tt_subdiv_is_eligible(verts, _sd_flags)) \
               { \
                  if (tt_subdiv_would_overlap(verts)) \
                     tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
                  if (tt_subdiv_push(verts, _sd_flags)) \
                  { \
                     hw_subdiv_buffered = true; \
                     /* Mirror the original path's killQuadPart \
                      * bookkeeping: the killQuadPart=2 case means \
                      * we're rendering the fix-up first triangle \
                      * of a previously bbox-culled quad, and the \
                      * original code returns from the macro after \
                      * doing so (the SW path already handled this \
                      * triangle on a previous call).  When \
                      * subdivision buffers it, we still need to \
                      * return to avoid re-running the SW shadow \
                      * path on a triangle it already drew. */ \
                     if (gpu->killQuadPart == 2) \
                     { \
                        gpu->killQuadPart = 0; \
                        return; \
                     } \
                  } \
                  else \
                     tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
               } \
               else \
               { \
                  /* Single-triangle runtime-ineligible (blended, \
                   * mask-eval, or invalid PGXP).  Add pin hints if \
                   * the polygon doesn't overlap the buffered region, \
                   * otherwise flush to preserve paint order. */ \
                  if (tt_subdiv_would_intersect_ineligible(verts)) \
                     tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
                  else \
                     tt_subdiv_add_pin_hints(verts); \
               } \
            } \
         } \
         else if (!(TEXTURED_LIT) && (PGXP_LIT)) \
         { \
            /* Polygon is untextured PGXP-tracked but ineligible at \
             * runtime (subdivision off, or some other runtime gate). \
             * No pin hints needed in the off case (buffer is empty); \
             * harmless when the buffer happens to have content from \
             * an earlier eligible state. */ \
         } \
         else if ((TEXTURED_LIT) && (PGXP_LIT)) \
         { \
            /* Textured PGXP-tracked polygon: it shares its world-3D \
             * vertices with neighbouring untextured polygons in many \
             * character meshes (face textured, body untextured, etc.). \
             * Record the three vertex PGXP positions as pin hints for \
             * the next subdivision flush, then let it draw directly. \
             * Previously we flushed the buffer here; that turned out \
             * to fragment character meshes into many tiny batches \
             * (texpage / blend mode changes between body parts \
             * trigger ineligible-polygon submissions interleaved \
             * with eligibles), defeating cross-batch adjacency and \
             * exposing every batch boundary as a visible seam in \
             * the smoothed silhouette.  Pin hints let the buffer \
             * accumulate the WHOLE untextured set of the character \
             * before flushing, and the next flush pins exactly the \
             * boundary vertices shared with these now-drawn textured \
             * neighbours.  True silhouettes (vertices with no \
             * textured neighbour) still get Loop boundary smoothing. \
             * Costs nothing when subdivision is disabled: the buffer \
             * is empty so pin hints serve no purpose, but the cost \
             * is 3 cheap hash inserts that get cleared at the next \
             * (no-op) flush. \
             * \
             * Overlap exception: if the textured polygon is drawn \
             * INSIDE the buffered region's bounding box (a paint- \
             * over draw like an eye on a face), we must still flush \
             * the buffer first.  Otherwise the subdivided body \
             * would paint over the textured eye at flush time, \
             * inverting paint order.  This is the same logic \
             * tt_subdiv_would_overlap() implements for eligible \
             * polys. */ \
            tri_vertex *hint_v; \
            if ((NV_LIT == 4) && (!gpu->killQuadPart) && (gpu->InCmd == INCMD_NONE)) \
            { \
               tri_vertex quad_a[3], quad_b[3]; \
               quad_a[0] = gpu->InQuad_F3Vertices[0]; \
               quad_a[1] = vertices[0]; \
               quad_a[2] = vertices[1]; \
               quad_b[0] = vertices[2]; \
               quad_b[1] = vertices[1]; \
               quad_b[2] = vertices[0]; \
               if (tt_subdiv_would_intersect_ineligible(quad_a) || tt_subdiv_would_intersect_ineligible(quad_b)) \
                  tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
               else \
               { \
                  tt_subdiv_add_pin_hints(quad_a); \
                  tt_subdiv_add_pin_hints(quad_b); \
               } \
            } \
            else \
            { \
               hint_v = (gpu->killQuadPart == 2) \
                        ? &gpu->InQuad_F3Vertices[0] : &vertices[0]; \
               if (tt_subdiv_would_intersect_ineligible(hint_v)) \
                  tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
               else \
                  tt_subdiv_add_pin_hints(hint_v); \
            } \
         } \
         else \
         { \
            /* Compile-time PGXP off or other rare path.  No reliable \
             * precise[] coords for pin hints; flush conservatively \
             * to preserve paint order. */ \
            tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
         } \
         if (!hw_subdiv_buffered) \
         { \
            if ((NV_LIT == 4) && (!gpu->killQuadPart)) \
            { \
               if (gpu->InCmd == INCMD_NONE) \
               { \
                  /* We have 4 quad vertices, we can push that at once */ \
                  tri_vertex *first = &gpu->InQuad_F3Vertices[0]; \
                  Extend_UVLimits(gpu, first, 1); \
                  Extend_UVLimits(gpu, vertices, 3); \
                  Finalise_UVLimits(gpu); \
                  rsx_intf_push_quad(first->precise[0], \
                     first->precise[1], \
                     first->precise[2], \
                     vertices[0].precise[0], \
                     vertices[0].precise[1], \
                     vertices[0].precise[2], \
                     vertices[1].precise[0], \
                     vertices[1].precise[1], \
                     vertices[1].precise[2], \
                     vertices[2].precise[0], \
                     vertices[2].precise[1], \
                     vertices[2].precise[2], \
                     ((uint32_t)first->r) | ((uint32_t)first->g << 8) | ((uint32_t)first->b << 16), \
                     ((uint32_t)vertices[0].r) | ((uint32_t)vertices[0].g << 8) | ((uint32_t)vertices[0].b << 16), \
                     ((uint32_t)vertices[1].r) | ((uint32_t)vertices[1].g << 8) | ((uint32_t)vertices[1].b << 16), \
                     ((uint32_t)vertices[2].r) | ((uint32_t)vertices[2].g << 8) | ((uint32_t)vertices[2].b << 16), \
                     first->u + gpu->off_u, first->v + gpu->off_v, \
                     vertices[0].u + gpu->off_u, vertices[0].v + gpu->off_v, \
                     vertices[1].u + gpu->off_u, vertices[1].v + gpu->off_v, \
                     vertices[2].u + gpu->off_u, vertices[2].v + gpu->off_v, \
                     gpu->min_u, gpu->min_v, \
                     gpu->max_u, gpu->max_v, \
                     gpu->TexPageX, gpu->TexPageY, \
                     clut_x, clut_y, \
                     blend_mode, \
                     2 - (MO_LIT), \
                     DitherEnabled(gpu), \
                     (BM_VAL), \
                     (ME_LIT), \
                     gpu->MaskSetOR != 0, \
                     false, \
                     gpu->may_be_2d); \
               } \
            } \
            else \
            { \
               tri_vertex *verts; \
               /* Only need to render first triangle that we skipped */ \
               if (gpu->killQuadPart == 2) \
                  verts = &gpu->InQuad_F3Vertices[0]; \
               else \
                  verts = &vertices[0]; \
               Extend_UVLimits(gpu, verts, 3); \
               Finalise_UVLimits(gpu); \
               /* Push a single triangle */ \
               rsx_intf_push_triangle(verts[0].precise[0], \
                  verts[0].precise[1], \
                  verts[0].precise[2], \
                  verts[1].precise[0], \
                  verts[1].precise[1], \
                  verts[1].precise[2], \
                  verts[2].precise[0], \
                  verts[2].precise[1], \
                  verts[2].precise[2], \
                  ((uint32_t)verts[0].r) | ((uint32_t)verts[0].g << 8) | ((uint32_t)verts[0].b << 16), \
                  ((uint32_t)verts[1].r) | ((uint32_t)verts[1].g << 8) | ((uint32_t)verts[1].b << 16), \
                  ((uint32_t)verts[2].r) | ((uint32_t)verts[2].g << 8) | ((uint32_t)verts[2].b << 16), \
                  verts[0].u, verts[0].v, \
                  verts[1].u, verts[1].v, \
                  verts[2].u, verts[2].v, \
                  gpu->min_u, gpu->min_v, \
                  gpu->max_u, gpu->max_v, \
                  gpu->TexPageX, gpu->TexPageY, \
                  clut_x, clut_y, \
                  blend_mode, \
                  2 - (MO_LIT), \
                  DitherEnabled(gpu), \
                  (BM_VAL), \
                  (ME_LIT), \
                  gpu->MaskSetOR != 0); \
               if (gpu->killQuadPart == 2) \
               { \
                  gpu->killQuadPart = 0; \
                  return; \
               } \
               gpu->killQuadPart = 0; \
            } \
         } \
      } \
      if (rsx_intf_is_type() == RSX_SOFTWARE) \
      { \
         if (PGXP_LIT) \
         { \
            uint32_t i; \
            for (i = 0; i < 3; ++i) \
            { \
               vertices[i].x = vertices[i].precise[0]; \
               vertices[i].y = vertices[i].precise[1]; \
            } \
         } \
      } \
      if (rsx_intf_has_software_renderer()) \
      { \
         /* Perspective-correct texturing for the SW rasteriser. \
          * Eligible when this primitive came in through the PGXP \
          * path with valid w (precise[2] still carries the real \
          * GTE-derived value; invalidW forces it to 1, which would \
          * collapse the perspective math to affine but more \
          * expensively), is textured, and the user enabled the \
          * "PGXP Perspective Correct Texturing" core option. \
          * With PGXP_LIT or TEXTURED_LIT 0 at compile time the \
          * whole expression folds to false. */ \
         bool pct = (PGXP_LIT) && (TEXTURED_LIT) \
                    && !invalidW \
                    && PGXP_texture_correction_enabled(); \
         /* Loop subdivision hook.  Three compile-time gates fold the \
          * whole block out in DCE on the specs where subdivision \
          * cannot apply: \
          *   - TEXTURED_LIT  (textured specs never subdivide) \
          *   - PGXP_LIT      (subdivision requires PGXP precise[]) \
          * For specs that survive both: the runtime gate is one \
          * load (psx_gpu_subdivision_level), branch-predicted taken \
          * when the option is off; eligibility, push, and flush are \
          * all bypassed via short-circuit evaluation.  When the \
          * option is on, the per-triangle cost is one eligibility \
          * test + one push (cheap memcpy into the pending ring); \
          * actual subdivision work happens at flush time. */ \
         { \
            uint8_t _sd_flags = \
                  ((GOURAUD_LIT) ? TT_SUBDIV_F_GOURAUD : 0) \
                /* Encode PSX semi-transparency mode in 2-bit \
                 * BLEND0/BLEND1 field + BLEND_ENABLE bit; see the \
                 * matching block in the quad-case branch above for \
                 * the rationale.  BM_VAL == -1 means opaque. */ \
                | ((BM_VAL) >= 0 ? TT_SUBDIV_F_BLEND_ENABLE : 0) \
                | (((BM_VAL) >= 0 && ((BM_VAL) & 1)) ? TT_SUBDIV_F_BLEND0 : 0) \
                | (((BM_VAL) >= 0 && ((BM_VAL) & 2)) ? TT_SUBDIV_F_BLEND1 : 0) \
                | ((ME_LIT) ? TT_SUBDIV_F_MASKEVAL : 0) \
                | ((PGXP_LIT) && !invalidW ? TT_SUBDIV_F_PGXP_VALID : 0); \
            if (!(TEXTURED_LIT) && (PGXP_LIT) \
                && psx_gpu_subdivision_level > 0 \
                && rsx_intf_is_type() == RSX_SOFTWARE \
                && tt_subdiv_is_eligible(vertices, _sd_flags)) \
            { \
               /* Overlap check: if this eligible triangle would be \
                * drawn inside the bounding box of already-buffered \
                * polygons, it's a paint-over draw (e.g. eyes/mouth \
                * on a face, decals on a wall) and must be drawn \
                * AFTER the buffered region.  Flush first, then push \
                * fresh.  Without this, the small overlay polygon \
                * gets merged into the larger mesh during \
                * subdivision and disappears visually (FF7 face \
                * features). */ \
               if (tt_subdiv_would_overlap(vertices)) \
                  tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
               if (tt_subdiv_push(vertices, _sd_flags)) \
               { \
                  /* Buffered; skip immediate rasterisation. */ \
                  if (lineFound) \
                     memcpy(&vertices[0], &lineVertices[0], 3 * sizeof(tri_vertex)); \
                  continue; \
               } \
               /* Push failed (buffer full): fall through and rasterise \
                * this one directly.  A flush would be cleaner but \
                * deeply complicates re-entrancy here. */ \
            } \
            /* This polygon is going to rasterise directly.  If we \
             * have ANY buffered deferred polygons we must flush them \
             * FIRST, so they observe the game's intended draw order \
             * (PS1 has no depth buffer -- later draws paint over \
             * earlier ones).  Without this flush, deferred untextured \
             * polygons could end up emitted AFTER textured polygons \
             * that the game submitted later, painting on top of them \
             * (Battle Arena Toshinden shadows over feet).  Cost when \
             * off: one inline load + branch (tt_subdiv_flush_if_pending \
             * is inline and S_pending_count is always 0 when \
             * subdivision is off). \
             * \
             * Guarded on RSX_SOFTWARE because when HW is primary \
             * (and this SW path is the shadow renderer for VRAM \
             * emulation), the HW intercept further up is doing the \
             * buffering and flushing -- a tail flush here would \
             * drain the buffer after every single polygon, defeating \
             * mesh-based subdivision (Loop needs to see multiple \
             * connected triangles to find shared edges). */ \
            if (rsx_intf_is_type() == RSX_SOFTWARE) \
               tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL); \
         } \
         DrawTriangle_g##GOURAUD_LIT##_t##TEXTURED_LIT##_##BM_TAG##_TM##TM_LIT##_MO##MO_LIT##_ME##ME_LIT(gpu, vertices, pct, NULL); \
      } \
      /* Line Render: Overwrite vertices with those of the second triangle */ \
      if ((lineFound) && (NV_LIT == 3) && (TEXTURED_LIT)) \
         memcpy(&vertices[0], &lineVertices[0], 3 * sizeof(tri_vertex)); \
   } while (lineFound); \
}

/* CMD_DRAWPOLY_T0_BMGROUP and CMD_DRAWPOLY_T1_BMGROUP emit the
 * 10 (BlendMode * MaskEval) specs for given outer parameters,
 * keeping the explicit-instantiation list short. */

#define CMD_DRAWPOLY_T0_BMGROUP(RAWNV, RAWG, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMopaque_TM0_MO0_ME0_PG##RAWPG, RAWNV, RAWG, 0, -1, BMopaque, 0, 0, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMopaque_TM0_MO0_ME1_PG##RAWPG, RAWNV, RAWG, 0, -1, BMopaque, 0, 0, 1, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMavg_TM0_MO0_ME0_PG##RAWPG, RAWNV, RAWG, 0,  0, BMavg,    0, 0, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMavg_TM0_MO0_ME1_PG##RAWPG, RAWNV, RAWG, 0,  0, BMavg,    0, 0, 1, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMadd_TM0_MO0_ME0_PG##RAWPG, RAWNV, RAWG, 0,  1, BMadd,    0, 0, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMadd_TM0_MO0_ME1_PG##RAWPG, RAWNV, RAWG, 0,  1, BMadd,    0, 0, 1, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMsub_TM0_MO0_ME0_PG##RAWPG, RAWNV, RAWG, 0,  2, BMsub,    0, 0, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMsub_TM0_MO0_ME1_PG##RAWPG, RAWNV, RAWG, 0,  2, BMsub,    0, 0, 1, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMaddq_TM0_MO0_ME0_PG##RAWPG, RAWNV, RAWG, 0,  3, BMaddq,   0, 0, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T0_BMaddq_TM0_MO0_ME1_PG##RAWPG, RAWNV, RAWG, 0,  3, BMaddq,   0, 0, 1, RAWPG)

#define CMD_DRAWPOLY_T1_BMGROUP(RAWNV, RAWG, TM, MO, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMopaque_TM##TM##_MO##MO##_ME0_PG##RAWPG, RAWNV, RAWG, 1, -1, BMopaque, TM, MO, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMopaque_TM##TM##_MO##MO##_ME1_PG##RAWPG, RAWNV, RAWG, 1, -1, BMopaque, TM, MO, 1, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMavg_TM##TM##_MO##MO##_ME0_PG##RAWPG, RAWNV, RAWG, 1,  0, BMavg,    TM, MO, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMavg_TM##TM##_MO##MO##_ME1_PG##RAWPG, RAWNV, RAWG, 1,  0, BMavg,    TM, MO, 1, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMadd_TM##TM##_MO##MO##_ME0_PG##RAWPG, RAWNV, RAWG, 1,  1, BMadd,    TM, MO, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMadd_TM##TM##_MO##MO##_ME1_PG##RAWPG, RAWNV, RAWG, 1,  1, BMadd,    TM, MO, 1, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMsub_TM##TM##_MO##MO##_ME0_PG##RAWPG, RAWNV, RAWG, 1,  2, BMsub,    TM, MO, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMsub_TM##TM##_MO##MO##_ME1_PG##RAWPG, RAWNV, RAWG, 1,  2, BMsub,    TM, MO, 1, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMaddq_TM##TM##_MO##MO##_ME0_PG##RAWPG, RAWNV, RAWG, 1,  3, BMaddq,   TM, MO, 0, RAWPG) \
   DEFINE_Command_DrawPolygon(NV##RAWNV##_G##RAWG##_T1_BMaddq_TM##TM##_MO##MO##_ME1_PG##RAWPG, RAWNV, RAWG, 1,  3, BMaddq,   TM, MO, 1, RAWPG)

/* CMD_DRAWPOLY_BMGROUP_ALL emits all 70 (10 non-textured + 60
 * textured) specs for one (NV, G, PG) combination. */
#define CMD_DRAWPOLY_BMGROUP_ALL(RAWNV, RAWG, RAWPG) \
   CMD_DRAWPOLY_T0_BMGROUP(RAWNV, RAWG, RAWPG) \
   CMD_DRAWPOLY_T1_BMGROUP(RAWNV, RAWG, 0, 0, RAWPG) \
   CMD_DRAWPOLY_T1_BMGROUP(RAWNV, RAWG, 0, 1, RAWPG) \
   CMD_DRAWPOLY_T1_BMGROUP(RAWNV, RAWG, 0, 2, RAWPG) \
   CMD_DRAWPOLY_T1_BMGROUP(RAWNV, RAWG, 1, 0, RAWPG) \
   CMD_DRAWPOLY_T1_BMGROUP(RAWNV, RAWG, 1, 1, RAWPG) \
   CMD_DRAWPOLY_T1_BMGROUP(RAWNV, RAWG, 1, 2, RAWPG)

/* Emit all 560 Command_DrawPolygon specialisations:
 *   2 NV * 2 G * 2 PG * 70 (BM cross) = 560 */
CMD_DRAWPOLY_BMGROUP_ALL(3, 0, 0)
CMD_DRAWPOLY_BMGROUP_ALL(3, 0, 1)
CMD_DRAWPOLY_BMGROUP_ALL(3, 1, 0)
CMD_DRAWPOLY_BMGROUP_ALL(3, 1, 1)
CMD_DRAWPOLY_BMGROUP_ALL(4, 0, 0)
CMD_DRAWPOLY_BMGROUP_ALL(4, 0, 1)
CMD_DRAWPOLY_BMGROUP_ALL(4, 1, 0)
CMD_DRAWPOLY_BMGROUP_ALL(4, 1, 1)

#undef COORD_POST_PADDING
#undef COORD_FBS
#undef COORD_MF_INT

/* ---------------------------------------------------------------------
 * Loop-subdivision flush bridge
 *
 * tt_subdiv_flush() needs to emit refined triangles back to a
 * concrete DrawTriangle specialisation.  Buffered triangles are
 * always untextured/opaque/mask-eval-off (enforced by
 * tt_subdiv_is_eligible() and the polygon-path push gate), so the
 * dispatch is between just two specialisations: flat-shaded and
 * gouraud-shaded.  Everything else stays direct.
 *
 * gpu_polygon_subdiv_flush() is the public entry point command-site
 * callers use.  When subdivision is off (psx_gpu_subdivision_level
 * == 0) the pending buffer is necessarily empty (we never pushed),
 * so the inline-guarded tt_subdiv_has_pending() check folds to a
 * single integer load that returns false, and call sites pay just
 * that.
 * ------------------------------------------------------------------ */

/* SW emit: subdivided child triangle -> SW rasteriser via the
 * override-idl path that suppresses per-triangle gouraud-rounding
 * seams.  See "parent-delta override" design notes in the original
 * subdivision commit.
 *
 * NOTE: This SW path is currently NOT used at runtime even with the
 * SW renderer active -- the SW rasteriser's edge-walking math is
 * not watertight across triangle boundaries (adjacent triangles
 * sharing an edge can disagree on which pixels each fills), and no
 * amount of upstream fixing closes the resulting fill-pattern
 * gaps.  Subdivision is therefore HW-only for now; the SW emit
 * stays in the tree as dormant code, ready to be re-enabled if/
 * when the rasteriser's watertightness gets addressed. */
static void gpu_subdiv_emit_one_sw(PS_GPU *gpu, tri_vertex *vertices,
      uint8_t flags, const tri_vertex *parent_vertices)
{
   i_deltas parent_idl;
   const tri_vertex *p0 = &parent_vertices[0];
   const tri_vertex *p1 = &parent_vertices[1];
   const tri_vertex *p2 = &parent_vertices[2];
   bool gouraud = (flags & TT_SUBDIV_F_GOURAUD) != 0;

   if (gouraud)
   {
      if (!CalcIDeltas_g1_t0(&parent_idl, p0, p1, p2))
      {
         DrawTriangle_g1_t0_BMopaque_TM0_MO0_ME0(gpu, vertices, false, NULL);
         return;
      }
   }
   else
   {
      if (!CalcIDeltas_g0_t0(&parent_idl, p0, p1, p2))
      {
         DrawTriangle_g0_t0_BMopaque_TM0_MO0_ME0(gpu, vertices, false, NULL);
         return;
      }
   }

   if (gouraud)
   {
      int32_t dx0 = p1->x - p0->x;
      int32_t dy0 = p1->y - p0->y;
      int32_t dx1 = p2->x - p0->x;
      int32_t dy1 = p2->y - p0->y;
      int64_t denom = (int64_t)dx0 * dy1 - (int64_t)dy0 * dx1;
      int j;
      if (denom != 0)
      {
         float inv = 1.0f / (float)denom;
         for (j = 0; j < 3; j++)
         {
            int32_t cx = vertices[j].x - p0->x;
            int32_t cy = vertices[j].y - p0->y;
            float w1 = (float)((int64_t)cx * dy1 - (int64_t)cy * dx1) * inv;
            float w2 = (float)((int64_t)dx0 * cy - (int64_t)dy0 * cx) * inv;
            float w0 = 1.0f - w1 - w2;
            float r  = w0 * p0->r + w1 * p1->r + w2 * p2->r;
            float g  = w0 * p0->g + w1 * p1->g + w2 * p2->g;
            float b  = w0 * p0->b + w1 * p1->b + w2 * p2->b;
            if (r < 0.0f) r = 0.0f; else if (r > 255.0f) r = 255.0f;
            if (g < 0.0f) g = 0.0f; else if (g > 255.0f) g = 255.0f;
            if (b < 0.0f) b = 0.0f; else if (b > 255.0f) b = 255.0f;
            vertices[j].r = (int32_t)(r + 0.5f);
            vertices[j].g = (int32_t)(g + 0.5f);
            vertices[j].b = (int32_t)(b + 0.5f);
         }
      }
   }
   else
   {
      vertices[0].r = vertices[1].r = vertices[2].r = p0->r;
      vertices[0].g = vertices[1].g = vertices[2].g = p0->g;
      vertices[0].b = vertices[1].b = vertices[2].b = p0->b;
   }

   if (gouraud)
      DrawTriangle_g1_t0_BMopaque_TM0_MO0_ME0(gpu, vertices, false, &parent_idl);
   else
      DrawTriangle_g0_t0_BMopaque_TM0_MO0_ME0(gpu, vertices, false, &parent_idl);
}

/* HW emit: subdivided child triangle -> rsx_intf_push_triangle for
 * the active hardware renderer (GL or Vulkan).  No per-parent
 * gradient gymnastics needed: HW renderers do barycentric
 * interpolation in shader, so adjacent triangles sharing an edge
 * with identical endpoint colours produce continuous colour
 * automatically.  HW rasterisation is also watertight (each pixel
 * touched by at most one triangle along a shared edge), so the
 * fill-pattern issues that defeat SW subdivision do not occur.
 *
 * Parameters to rsx_intf_push_triangle:
 *   - precise[]/colour: from the subdivided child vertex
 *   - texture state: untextured, so UVs are zero, texpage and CLUT
 *     are passed but unused; texture_blend_mode 0 (no texture)
 *   - blend_mode -1 (opaque) since subdivision filter excludes
 *     blended polys
 *   - mask test off, set mask off (eligibility filter rejects
 *     mask-modifying polys)
 *   - dither: read from current GPU state */
static void gpu_subdiv_emit_one_hw(PS_GPU *gpu, tri_vertex *vertices,
      uint8_t flags)
{
   int  blend_mode;
   bool mask_test;
   /* Decode PSX semi-transparency from flags.  BLEND_ENABLE clear
    * means opaque (blend_mode -1); when set, BLEND0/BLEND1 give the
    * low/high bit of the 0..3 mode value.  Previously this was
    * hardcoded -1 (opaque) which made subdivided shadows and other
    * semi-transparent geometry render fully opaque. */
   blend_mode = -1;
   if (flags & TT_SUBDIV_F_BLEND_ENABLE)
   {
      blend_mode = ((flags & TT_SUBDIV_F_BLEND0) ? 1 : 0)
                 | ((flags & TT_SUBDIV_F_BLEND1) ? 2 : 0);
   }
   /* Mask-test was also hardcoded false; restore from flags. */
   mask_test = (flags & TT_SUBDIV_F_MASKEVAL) != 0;
   rsx_intf_push_triangle(
         vertices[0].precise[0], vertices[0].precise[1], vertices[0].precise[2],
         vertices[1].precise[0], vertices[1].precise[1], vertices[1].precise[2],
         vertices[2].precise[0], vertices[2].precise[1], vertices[2].precise[2],
         ((uint32_t)vertices[0].r) | ((uint32_t)vertices[0].g << 8) | ((uint32_t)vertices[0].b << 16),
         ((uint32_t)vertices[1].r) | ((uint32_t)vertices[1].g << 8) | ((uint32_t)vertices[1].b << 16),
         ((uint32_t)vertices[2].r) | ((uint32_t)vertices[2].g << 8) | ((uint32_t)vertices[2].b << 16),
         0, 0, 0, 0, 0, 0,           /* UVs zero (untextured) */
         0, 0, 0xFFFF, 0xFFFF,       /* UV limits (don't care) */
         gpu->TexPageX, gpu->TexPageY,
         0, 0,                        /* clut_x, clut_y unused */
         0,                           /* texture_blend_mode 0 = no texture */
         2,                           /* depth_shift unused for untextured */
         DitherEnabled(gpu),
         blend_mode,                  /* PSX semi-transparency mode from flags */
         mask_test,                   /* mask test from flags */
         gpu->MaskSetOR != 0);        /* set_mask preserved */
}

/* Top-level emit dispatcher.  Subdivision is enabled for both SW
 * and HW renderers in the polygon path, but the SW emit path is
 * currently a no-op because the SW rasteriser produces visible
 * fill artefacts on subdivided meshes (see gpu_subdiv_emit_one_sw
 * doc comment).  Routing decision is per-emit because the
 * eligibility/push decision in the polygon path is renderer-
 * agnostic but the rasterisation step is not. */
static void gpu_subdiv_emit_one(PS_GPU *gpu, tri_vertex *vertices,
      uint8_t flags, const tri_vertex *parent_vertices, void *tag)
{
   (void)tag;
   if (rsx_intf_is_type() == RSX_SOFTWARE)
      gpu_subdiv_emit_one_sw(gpu, vertices, flags, parent_vertices);
   else
      gpu_subdiv_emit_one_hw(gpu, vertices, flags);
}

void gpu_polygon_subdiv_flush(PS_GPU *gpu)
{
   tt_subdiv_flush_if_pending(gpu, gpu_subdiv_emit_one, NULL);
}

