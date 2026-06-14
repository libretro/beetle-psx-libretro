#include <math.h>
#include "beetle_psx_globals.h"

/* Defined later in the same translation unit (gpu.c includes this
 * file before the dither_table definition).  Forward-declared here so
 * the non-textured SSE2/NEON span helper below can read it; the later
 * definition in gpu.c provides storage. */
static const int8_t dither_table[4][4];

#define COORD_FBS 12
#define PCT_UV_BLOCK 64
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

#if defined(__SSE2__) || defined(GPU_HAVE_NEON)
/*
 * Vectorised inner loop for the *non-textured* span at native
 * resolution.  Processes pixels in SIMD chunks and returns the count
 * consumed; the macro's scalar do/while finishes the remainder (and
 * runs the whole span when this returns 0).
 *
 * Bit-exactness contract (matches the scalar T0 path exactly):
 *   - colour comes from the affine interpolants ig.r/g/b, each a
 *     uint32 stepped by idl->d{r,g,b}_dx per pixel; lane i carries
 *     base + i*delta with native uint32 wrap (GOURAUD only - flat
 *     fill has zero deltas so all lanes share the base, still exact).
 *   - 555 build: dithered  -> DitherLUT[y&3][x&3][chan] which is
 *     clamp((chan + dither_table[y&3][x&3]) >> 3, 0, 0x1F);
 *     non-dithered -> chan >> 3.  We reproduce the LUT arithmetic
 *     directly (add period-4 offset, arithmetic >>3, clamp).
 *   - blend: the same SWAR math as PlotPixelBlend_*, widened.  AVG/
 *     ADD/ADD_FOURTH stay in 16-bit lanes (8 px/iter).  SUBTRACT's
 *     borrow mask 0x108420 has a bit above lane 15, so it runs in
 *     32-bit lanes (4 px/iter); still ahead of scalar.
 *   - mask-eval (ME_LIT): keep the destination word where its 0x8000
 *     bit is set, else take the new pixel.  Native-res second fetch
 *     equals the blend's bg fetch (no write between), so one load
 *     serves both.
 *   - store: (pix & 0x7FFF) | MaskSetOR.
 *
 * Gated by the caller on upscale_shift == 0 (which also forces
 * dither_upscale_shift == 0, so the dither phase is plain x&3) and on
 * the run being contiguous in a single VRAM row (y already masked,
 * x in [0,1024)); the caller passes a span that does not cross the
 * 1024 column wrap.
 *
 * GOURAUD_LIT/DITHER_ON/BM_VAL/ME_LIT are passed as literal ints from
 * the macro so this fully specialises and inlines per call site.
 */
static INLINE int DrawSpanVec_NT(PS_GPU *gpu, int y, int32_t x, int32_t w,
      const i_group *igp, const i_deltas *idl,
      const int GOURAUD_LIT, const int DITHER_ON,
      const int BM_VAL, const int ME_LIT)
{
   uint16_t *row       = &gpu->vram[(uint32_t)(y & 511) << 10];
   const uint16_t mso  = gpu->MaskSetOR;
   uint32_t cr         = igp->r;
   uint32_t cg         = igp->g;
   uint32_t cb         = igp->b;
   const uint32_t dr   = GOURAUD_LIT ? idl->dr_dx : 0;
   const uint32_t dg   = GOURAUD_LIT ? idl->dg_dx : 0;
   const uint32_t db   = GOURAUD_LIT ? idl->db_dx : 0;
   const int      step = (BM_VAL == BLEND_MODE_SUBTRACT) ? 4 : 8;
   int32_t        done = 0;
   const int      use_dither = (GOURAUD_LIT && DITHER_ON);
#define CHAN(base, d, l) (((base) + (d) * (uint32_t)(l)) >> (COORD_FBS + COORD_POST_PADDING))

#if defined(__SSE2__)
   const __m128i  v7FFF = _mm_set1_epi16(0x7FFF);
   const __m128i  v8000 = _mm_set1_epi16((short)0x8000);
   const __m128i  vmso  = _mm_set1_epi16((short)mso);
#else
   const uint16x8_t v7FFF = vdupq_n_u16(0x7FFF);
   const uint16x8_t v8000 = vdupq_n_u16(0x8000);
   const uint16x8_t vmso  = vdupq_n_u16(mso);
#endif

   for (; done + step <= w; done += step)
   {
      const int32_t bx = x + done;
      uint16_t      R8[8], G8[8], B8[8];
      int           l;

      for (l = 0; l < step; l++)
      {
         R8[l] = (uint16_t)CHAN(cr, dr, l);
         G8[l] = (uint16_t)CHAN(cg, dg, l);
         B8[l] = (uint16_t)CHAN(cb, db, l);
      }

#if defined(__SSE2__)
      {
         __m128i R, G, B, pr, pg, pb, pix, bg, out;
         if (step == 4) { R8[4]=R8[5]=R8[6]=R8[7]=0; G8[4]=G8[5]=G8[6]=G8[7]=0; B8[4]=B8[5]=B8[6]=B8[7]=0; }
         R = _mm_loadu_si128((const __m128i*)R8);
         G = _mm_loadu_si128((const __m128i*)G8);
         B = _mm_loadu_si128((const __m128i*)B8);
         if (use_dither)
         {
            int16_t doff[8];
            for (l = 0; l < step; l++) doff[l] = dither_table[y & 3][(bx + l) & 3];
            if (step == 4) doff[4]=doff[5]=doff[6]=doff[7]=0;
            {
               __m128i d = _mm_loadu_si128((const __m128i*)doff);
               __m128i z = _mm_setzero_si128(), m1F = _mm_set1_epi16(0x1F);
               pr = _mm_min_epi16(_mm_max_epi16(_mm_srai_epi16(_mm_add_epi16(R, d), 3), z), m1F);
               pg = _mm_min_epi16(_mm_max_epi16(_mm_srai_epi16(_mm_add_epi16(G, d), 3), z), m1F);
               pb = _mm_min_epi16(_mm_max_epi16(_mm_srai_epi16(_mm_add_epi16(B, d), 3), z), m1F);
            }
         }
         else
         {
            pr = _mm_srli_epi16(R, 3);
            pg = _mm_srli_epi16(G, 3);
            pb = _mm_srli_epi16(B, 3);
         }
         pix = _mm_or_si128(v8000,
               _mm_or_si128(pr, _mm_or_si128(_mm_slli_epi16(pg, 5), _mm_slli_epi16(pb, 10))));
         bg  = _mm_loadu_si128((const __m128i*)&row[bx]);

         if (BM_VAL == BLEND_MODE_AVERAGE)
         {
            __m128i bgo = _mm_or_si128(bg, v8000);
            __m128i xr  = _mm_and_si128(_mm_xor_si128(pix, bgo), _mm_set1_epi16(0x0421));
            pix = _mm_srli_epi16(_mm_sub_epi16(_mm_add_epi16(pix, bgo), xr), 1);
         }
         else if (BM_VAL == BLEND_MODE_ADD)
         {
            __m128i b2  = _mm_andnot_si128(v8000, bg);
            __m128i sum = _mm_add_epi16(pix, b2);
            __m128i car = _mm_and_si128(_mm_sub_epi16(sum, _mm_and_si128(_mm_xor_si128(pix, b2), _mm_set1_epi16((short)0x8421))), _mm_set1_epi16((short)0x8420));
            pix = _mm_or_si128(_mm_sub_epi16(sum, car), _mm_sub_epi16(car, _mm_srli_epi16(car, 5)));
         }
         else if (BM_VAL == BLEND_MODE_ADD_FOURTH)
         {
            __m128i b2  = _mm_andnot_si128(v8000, bg);
            __m128i fp2 = _mm_or_si128(_mm_and_si128(_mm_srli_epi16(pix, 2), _mm_set1_epi16(0x1CE7)), v8000);
            __m128i sum = _mm_add_epi16(fp2, b2);
            __m128i car = _mm_and_si128(_mm_sub_epi16(sum, _mm_and_si128(_mm_xor_si128(fp2, b2), _mm_set1_epi16((short)0x8421))), _mm_set1_epi16((short)0x8420));
            pix = _mm_or_si128(_mm_sub_epi16(sum, car), _mm_sub_epi16(car, _mm_srli_epi16(car, 5)));
         }
         else if (BM_VAL == BLEND_MODE_SUBTRACT)
         {
            /* 32-bit lanes: widen pix/bg low 4 lanes, do the 0x108420 SWAR, repack. */
            __m128i z   = _mm_setzero_si128();
            __m128i m84 = _mm_set1_epi32(0x108420), m80 = _mm_set1_epi32(0x8000);
            __m128i fp32 = _mm_unpacklo_epi16(pix, z);
            __m128i bg32 = _mm_unpacklo_epi16(bg, z);
            __m128i bgo  = _mm_or_si128(bg32, m80);
            __m128i fpc  = _mm_andnot_si128(m80, fp32);
            __m128i diff = _mm_add_epi32(_mm_sub_epi32(bgo, fpc), m84);
            __m128i xr   = _mm_and_si128(_mm_xor_si128(bgo, fpc), m84);
            __m128i bor  = _mm_and_si128(_mm_sub_epi32(diff, xr), m84);
            __m128i res  = _mm_and_si128(_mm_sub_epi32(diff, bor), _mm_sub_epi32(bor, _mm_srli_epi32(bor, 5)));
            /* Truncate each 32-bit lane to 16 bits into lanes 0..3.
             * SSE2 has no unsigned 32->16 pack (packus_epi32 is
             * SSE4.1) and packs_epi32 signed-saturates (the subtract
             * result can have bit 15 set, e.g. 0x900e), so emulate
             * packus via the bias trick: subtract 0x8000 to bring the
             * value into signed-16 range, signed-pack, then add 0x8000
             * back in 16-bit lanes.  Only the low 4 lanes are stored. */
            res = _mm_and_si128(res, _mm_set1_epi32(0xFFFF));
            pix = _mm_add_epi16(_mm_packs_epi32(_mm_sub_epi32(res, _mm_set1_epi32(0x8000)), z),
                                _mm_set1_epi16((short)0x8000));
         }

         out = _mm_or_si128(_mm_and_si128(pix, v7FFF), vmso);
         if (ME_LIT)
         {
            __m128i keep = _mm_srai_epi16(bg, 15);
            out = _mm_or_si128(_mm_and_si128(keep, bg), _mm_andnot_si128(keep, out));
         }
         if (step == 8)
            _mm_storeu_si128((__m128i*)&row[bx], out);
         else
            _mm_storel_epi64((__m128i*)&row[bx], out); /* 4 px */
      }
#else /* GPU_HAVE_NEON */
      {
         uint16x8_t R, G, B, pr, pg, pb, pix, bg, out;
         if (step == 4) { R8[4]=R8[5]=R8[6]=R8[7]=0; G8[4]=G8[5]=G8[6]=G8[7]=0; B8[4]=B8[5]=B8[6]=B8[7]=0; }
         R = vld1q_u16(R8); G = vld1q_u16(G8); B = vld1q_u16(B8);
         if (use_dither)
         {
            int16_t doff[8];
            for (l = 0; l < step; l++) doff[l] = dither_table[y & 3][(bx + l) & 3];
            if (step == 4) doff[4]=doff[5]=doff[6]=doff[7]=0;
            {
               int16x8_t d  = vld1q_s16(doff);
               int16x8_t m1F = vdupq_n_s16(0x1F), z = vdupq_n_s16(0);
               int16x8_t sr = vshrq_n_s16(vaddq_s16(vreinterpretq_s16_u16(R), d), 3);
               int16x8_t sg = vshrq_n_s16(vaddq_s16(vreinterpretq_s16_u16(G), d), 3);
               int16x8_t sb = vshrq_n_s16(vaddq_s16(vreinterpretq_s16_u16(B), d), 3);
               pr = vreinterpretq_u16_s16(vminq_s16(vmaxq_s16(sr, z), m1F));
               pg = vreinterpretq_u16_s16(vminq_s16(vmaxq_s16(sg, z), m1F));
               pb = vreinterpretq_u16_s16(vminq_s16(vmaxq_s16(sb, z), m1F));
            }
         }
         else
         {
            pr = vshrq_n_u16(R, 3); pg = vshrq_n_u16(G, 3); pb = vshrq_n_u16(B, 3);
         }
         pix = vorrq_u16(v8000, vorrq_u16(pr, vorrq_u16(vshlq_n_u16(pg, 5), vshlq_n_u16(pb, 10))));
         bg  = vld1q_u16(&row[bx]);

         if (BM_VAL == BLEND_MODE_AVERAGE)
         {
            uint16x8_t bgo = vorrq_u16(bg, v8000);
            uint16x8_t xr  = vandq_u16(veorq_u16(pix, bgo), vdupq_n_u16(0x0421));
            pix = vshrq_n_u16(vsubq_u16(vaddq_u16(pix, bgo), xr), 1);
         }
         else if (BM_VAL == BLEND_MODE_ADD)
         {
            uint16x8_t b2  = vbicq_u16(bg, v8000);
            uint16x8_t sum = vaddq_u16(pix, b2);
            uint16x8_t car = vandq_u16(vsubq_u16(sum, vandq_u16(veorq_u16(pix, b2), vdupq_n_u16(0x8421))), vdupq_n_u16(0x8420));
            pix = vorrq_u16(vsubq_u16(sum, car), vsubq_u16(car, vshrq_n_u16(car, 5)));
         }
         else if (BM_VAL == BLEND_MODE_ADD_FOURTH)
         {
            uint16x8_t b2  = vbicq_u16(bg, v8000);
            uint16x8_t fp2 = vorrq_u16(vandq_u16(vshrq_n_u16(pix, 2), vdupq_n_u16(0x1CE7)), v8000);
            uint16x8_t sum = vaddq_u16(fp2, b2);
            uint16x8_t car = vandq_u16(vsubq_u16(sum, vandq_u16(veorq_u16(fp2, b2), vdupq_n_u16(0x8421))), vdupq_n_u16(0x8420));
            pix = vorrq_u16(vsubq_u16(sum, car), vsubq_u16(car, vshrq_n_u16(car, 5)));
         }
         else if (BM_VAL == BLEND_MODE_SUBTRACT)
         {
            uint32x4_t m84 = vdupq_n_u32(0x108420), m80 = vdupq_n_u32(0x8000);
            uint32x4_t fp32 = vmovl_u16(vget_low_u16(pix));
            uint32x4_t bg32 = vmovl_u16(vget_low_u16(bg));
            uint32x4_t bgo  = vorrq_u32(bg32, m80);
            uint32x4_t fpc  = vbicq_u32(fp32, m80);
            uint32x4_t diff = vaddq_u32(vsubq_u32(bgo, fpc), m84);
            uint32x4_t xr   = vandq_u32(veorq_u32(bgo, fpc), m84);
            uint32x4_t bor  = vandq_u32(vsubq_u32(diff, xr), m84);
            uint32x4_t res  = vandq_u32(vsubq_u32(diff, bor), vsubq_u32(bor, vshrq_n_u32(bor, 5)));
            pix = vcombine_u16(vmovn_u32(res), vdup_n_u16(0));
         }

         out = vorrq_u16(vandq_u16(pix, v7FFF), vmso);
         if (ME_LIT)
         {
            uint16x8_t keep = vcgeq_u16(bg, v8000); /* 0xFFFF where high bit set (bg >= 0x8000) */
            out = vbslq_u16(keep, bg, out);
         }
         if (step == 8)
            vst1q_u16(&row[bx], out);
         else
            vst1_u16(&row[bx], vget_low_u16(out)); /* 4 px */
      }
#endif
      cr += dr * (uint32_t)step;
      cg += dg * (uint32_t)step;
      cb += db * (uint32_t)step;
   }
#undef CHAN
   return done;
}
#else /* SSE2 || NEON */
/* Scalar-only build (e.g. baseline i686): no vector fast path. The
 * caller treats the return value as the number of pixels consumed by
 * the vector path and lets the scalar loop finish the remainder, so
 * returning zero hands the whole span to the scalar loop. */
static INLINE int DrawSpanVec_NT(PS_GPU *gpu, int y, int32_t x, int32_t w,
      const i_group *igp, const i_deltas *idl,
      const int GOURAUD_LIT, const int DITHER_ON,
      const int BM_VAL, const int ME_LIT)
{
   return 0;
}
#endif /* SSE2 || NEON */

/* The vectorised UV recovery needs a true IEEE single-precision divide so the
 * recovered coordinates are bit-identical to the scalar 1.0f / inv_w.  SSE2 has
 * _mm_div_ps and AArch64 NEON has vdivq_f32; 32-bit ARMv7 NEON has no vector
 * float divide (only the vrecpe_f32 estimate + Newton-Raphson, which is not
 * bit-exact), so ARMv7 falls through to the bit-exact scalar tail below. */
#if defined(__SSE2__) || ((defined(GPU_HAVE_NEON)) && (defined(__aarch64__) || defined(_M_ARM64)))
#define PCT_UV_SIMD 1
#endif

/*
 * Perspective-correct (PGXP texture-correction) UV batch.
 *
 * The textured span's pct path computes, per pixel, the float work
 * 1/inv_w, two multiply-adds, four clamps and two truncations to recover
 * (tex_u, tex_v) before the (scalar, gather-bound) texel fetch.  gcc does
 * not autovectorise that loop at either -O2 or -O3 (the inv_w > eps branch
 * and the clamps defeat it), so it stays scalar even in -O3 builds.  This
 * helper does the same arithmetic four pixels at a time, filling caller
 * scratch arrays that the plot loop then indexes.  The texel fetch / blend
 * / plot stay scalar - only the float coordinate recovery is vectorised.
 *
 * Bit-exactness: the lanes are seeded by the *same* sequential float add
 * chain the scalar path walks (lane k = base stepped k times, not
 * base + k*delta computed by multiply), because float addition is not
 * associative and a multiply-seeded lane can differ in the last bit and
 * flip an integer-truncation boundary.  The per-batch base is likewise
 * advanced by four sequential adds.  Lanes with inv_w <= 1e-6 are written
 * as -1 so the caller takes the affine fallback for them, exactly as the
 * scalar `if (pct && ig.pct_inv_w > 1e-6f)` guard does.
 *
 * Output matches, pixel for pixel, the scalar expression
 *   fu = clamp(u_over_w / inv_w + bias, min_u, max_u); ((int)fu) & 0xFF
 * (and likewise for v), advancing inv_w / u_over_w / v_over_w by their dx
 * deltas each pixel.
 */
static INLINE void PCT_UVBatch(int n, float bias,
      float *p_inv_w, float *p_u_over_w, float *p_v_over_w,
      float d_inv_w, float d_u_over_w, float d_v_over_w,
      float min_u, float max_u, float min_v, float max_v,
      int32_t *out_u, int32_t *out_v)
{
   float inv_w     = *p_inv_w;
   float u_over_w  = *p_u_over_w;
   float v_over_w  = *p_v_over_w;
   int   i         = 0;
#if defined(PCT_UV_SIMD)
   int   batch     = n & ~3;
#if defined(__SSE2__)
   const __m128  vbias = _mm_set1_ps(bias);
   const __m128  veps  = _mm_set1_ps(1e-6f);
   const __m128  vmnu  = _mm_set1_ps(min_u);
   const __m128  vmxu  = _mm_set1_ps(max_u);
   const __m128  vmnv  = _mm_set1_ps(min_v);
   const __m128  vmxv  = _mm_set1_ps(max_v);
   const __m128i vmask = _mm_set1_epi32(0xFF);
   const __m128i vsent = _mm_set1_epi32(-1);
   for (; i < batch; i += 4)
   {
      /* Seed four lanes by sequential adds (bit-identical to scalar). */
      float iw0 = inv_w,    iw1 = iw0 + d_inv_w,    iw2 = iw1 + d_inv_w,    iw3 = iw2 + d_inv_w;
      float uo0 = u_over_w, uo1 = uo0 + d_u_over_w, uo2 = uo1 + d_u_over_w, uo3 = uo2 + d_u_over_w;
      float vo0 = v_over_w, vo1 = vo0 + d_v_over_w, vo2 = vo1 + d_v_over_w, vo3 = vo2 + d_v_over_w;
      __m128 iw = _mm_setr_ps(iw0, iw1, iw2, iw3);
      __m128 uo = _mm_setr_ps(uo0, uo1, uo2, uo3);
      __m128 vo = _mm_setr_ps(vo0, vo1, vo2, vo3);
      __m128 inv = _mm_div_ps(_mm_set1_ps(1.0f), iw);
      __m128 fu  = _mm_add_ps(_mm_mul_ps(uo, inv), vbias);
      __m128 fv  = _mm_add_ps(_mm_mul_ps(vo, inv), vbias);
      __m128i iu, iv, ok;
      fu = _mm_min_ps(_mm_max_ps(fu, vmnu), vmxu);
      fv = _mm_min_ps(_mm_max_ps(fv, vmnv), vmxv);
      iu = _mm_and_si128(_mm_cvttps_epi32(fu), vmask);
      iv = _mm_and_si128(_mm_cvttps_epi32(fv), vmask);
      ok = _mm_castps_si128(_mm_cmpgt_ps(iw, veps));
      iu = _mm_or_si128(_mm_and_si128(ok, iu), _mm_andnot_si128(ok, vsent));
      iv = _mm_or_si128(_mm_and_si128(ok, iv), _mm_andnot_si128(ok, vsent));
      _mm_storeu_si128((__m128i *)&out_u[i], iu);
      _mm_storeu_si128((__m128i *)&out_v[i], iv);
      inv_w = iw3 + d_inv_w; u_over_w = uo3 + d_u_over_w; v_over_w = vo3 + d_v_over_w;
   }
#else /* AArch64 NEON (PCT_UV_SIMD set, not SSE2) */
   const float32x4_t vbias = vdupq_n_f32(bias);
   const float32x4_t veps  = vdupq_n_f32(1e-6f);
   const float32x4_t vmnu  = vdupq_n_f32(min_u);
   const float32x4_t vmxu  = vdupq_n_f32(max_u);
   const float32x4_t vmnv  = vdupq_n_f32(min_v);
   const float32x4_t vmxv  = vdupq_n_f32(max_v);
   const int32x4_t   vmask = vdupq_n_s32(0xFF);
   const int32x4_t   vsent = vdupq_n_s32(-1);
   for (; i < batch; i += 4)
   {
      float iw0 = inv_w,    iw1 = iw0 + d_inv_w,    iw2 = iw1 + d_inv_w,    iw3 = iw2 + d_inv_w;
      float uo0 = u_over_w, uo1 = uo0 + d_u_over_w, uo2 = uo1 + d_u_over_w, uo3 = uo2 + d_u_over_w;
      float vo0 = v_over_w, vo1 = vo0 + d_v_over_w, vo2 = vo1 + d_v_over_w, vo3 = vo2 + d_v_over_w;
      float32x4_t iw, uo, vo, inv, fu, fv;
      int32x4_t   iu, iv;
      uint32x4_t  ok;
      float       iwa[4] = { iw0, iw1, iw2, iw3 };
      float       uoa[4] = { uo0, uo1, uo2, uo3 };
      float       voa[4] = { vo0, vo1, vo2, vo3 };
      iw = vld1q_f32(iwa); uo = vld1q_f32(uoa); vo = vld1q_f32(voa);
      /* 1/iw: one Newton-Raphson step after the estimate is not bit-exact
       * with the scalar 1.0f/iw, so use a true divide. */
      inv = vdivq_f32(vdupq_n_f32(1.0f), iw);
      fu  = vaddq_f32(vmulq_f32(uo, inv), vbias);
      fv  = vaddq_f32(vmulq_f32(vo, inv), vbias);
      fu  = vminq_f32(vmaxq_f32(fu, vmnu), vmxu);
      fv  = vminq_f32(vmaxq_f32(fv, vmnv), vmxv);
      iu  = vandq_s32(vcvtq_s32_f32(fu), vmask);
      iv  = vandq_s32(vcvtq_s32_f32(fv), vmask);
      ok  = vcgtq_f32(iw, veps);
      iu  = vbslq_s32(ok, iu, vsent);
      iv  = vbslq_s32(ok, iv, vsent);
      vst1q_s32(&out_u[i], iu);
      vst1q_s32(&out_v[i], iv);
      inv_w = iw3 + d_inv_w; u_over_w = uo3 + d_u_over_w; v_over_w = vo3 + d_v_over_w;
   }
#endif /* SSE2 vs AArch64-NEON loop body */
#endif /* PCT_UV_SIMD (batch decl + loops) */
   /* Scalar tail (and the whole batch on a non-SIMD build). */
   for (; i < n; i++)
   {
      if (inv_w > 1e-6f)
      {
         float inv = 1.0f / inv_w;
         float fu  = u_over_w * inv + bias;
         float fv  = v_over_w * inv + bias;
         if (fu < min_u) fu = min_u; else if (fu > max_u) fu = max_u;
         if (fv < min_v) fv = min_v; else if (fv > max_v) fv = max_v;
         out_u[i] = ((int32_t)fu) & 0xFF;
         out_v[i] = ((int32_t)fv) & 0xFF;
      }
      else
      {
         out_u[i] = -1;
         out_v[i] = -1;
      }
      inv_w += d_inv_w; u_over_w += d_u_over_w; v_over_w += d_v_over_w;
   }
   *p_inv_w = inv_w; *p_u_over_w = u_over_w; *p_v_over_w = v_over_w;
}

#ifdef PCT_UV_SIMD
#undef PCT_UV_SIMD
#endif

#define DEFINE_DrawSpan(SUFFIX, GOURAUD_LIT, TEXTURED_LIT, BM_VAL, BM_TAG, TM_LIT, MO_LIT, ME_LIT) \
static INLINE void DrawSpan_##SUFFIX(PS_GPU *gpu, int y, const int32_t x_start, const int32_t x_bound, i_group ig, const i_deltas *idl, const bool pct) \
{ \
   int32_t clipx0; \
   int32_t clipx1; \
   int32_t x_ig_adjust; \
   int32_t w; \
   int32_t x; \
   /* pct UV precompute scratch.  Only used on the textured perspective- \
    * correct path; PCT_UV_BLOCK pixels are SIMD-computed at a time and the \
    * plot loop reads them back.  pct_idx counts down the pixels remaining \
    * in the current block (0 forces a refill).  Kept on stack, fixed size \
    * regardless of span width / upscale. */ \
   int32_t pct_u_buf[PCT_UV_BLOCK]; \
   int32_t pct_v_buf[PCT_UV_BLOCK]; \
   int     pct_idx = 0; \
   int     pct_pos = 0; \
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
   /* Native-res, non-textured fast path: vectorise the contiguous run \
    * (the span lies within [ClipX0,ClipX1] in [0,1023], so it never \
    * crosses the x==1024 VRAM wrap).  Advances x/w and steps the affine \
    * colour interpolants past the consumed pixels; the scalar do/while \
    * below finishes any 4/8-pixel remainder.  Textured spans and any \
    * upscale are left entirely to the scalar loop. */ \
   if (!(TEXTURED_LIT) && gpu->upscale_shift == 0) \
   { \
      int32_t _vn = DrawSpanVec_NT(gpu, y, x, w, &ig, idl, \
            (GOURAUD_LIT), DitherEnabled(gpu), (BM_VAL), (ME_LIT)); \
      if (_vn > 0) \
      { \
         if (GOURAUD_LIT) \
         { \
            ig.r += idl->dr_dx * (uint32_t)_vn; \
            ig.g += idl->dg_dx * (uint32_t)_vn; \
            ig.b += idl->db_dx * (uint32_t)_vn; \
         } \
         x += _vn; \
         w -= _vn; \
         if (w <= 0) \
            return; \
      } \
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
         if (pct) \
         { \
            /* Pull the perspective-correct UV from the SIMD-precomputed \
             * block, refilling it when exhausted.  PCT_UVBatch advances \
             * ig.pct_* by exactly the per-pixel scalar chain and encodes \
             * the per-pixel inv_w > eps decision in its output: a -1 \
             * entry means that pixel's inv_w was <= eps, so fall through \
             * to the affine path for it (matching the old scalar guard \
             * `pct && ig.pct_inv_w > 1e-6f`).  Because the batch consumes \
             * ig.pct_*, the per-pixel advance at the loop bottom is \
             * skipped for the pct path. */ \
            int32_t pu, pv; \
            if (pct_idx == 0) \
            { \
               int remain = (int)w; \
               int blk    = (remain < PCT_UV_BLOCK) ? remain : PCT_UV_BLOCK; \
               float bias = 0.5f / (float)(1 << gpu->upscale_shift); \
               PCT_UVBatch(blk, bias, &ig.pct_inv_w, &ig.pct_u_over_w, \
                     &ig.pct_v_over_w, idl->d_inv_w_dx, idl->d_u_over_w_dx, \
                     idl->d_v_over_w_dx, idl->min_u, idl->max_u, \
                     idl->min_v, idl->max_v, pct_u_buf, pct_v_buf); \
               pct_idx = blk; \
               pct_pos = 0; \
            } \
            pu = pct_u_buf[pct_pos]; \
            pv = pct_v_buf[pct_pos]; \
            pct_pos++; \
            pct_idx--; \
            if (pu >= 0) \
            { \
               tex_u = pu; \
               tex_v = pv; \
            } \
            else \
            { \
               tex_u = ig.u >> (COORD_FBS + COORD_POST_PADDING); \
               tex_v = ig.v >> (COORD_FBS + COORD_POST_PADDING); \
            } \
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
      /* ig.pct_* are advanced by PCT_UVBatch, not here. */ \
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
static INLINE void DrawTriangle_##SUFFIX(PS_GPU *gpu, tri_vertex *vertices, const bool pct) \
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
   if (!CalcIDeltas_g##GOURAUD_LIT##_t##TEXTURED_LIT(&idl, &vertices[0], &vertices[1], &vertices[2])) \
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

/* No runtime switch on BlendMode / TexMode_TA / TexMult / MaskEval is
 * needed since every parameter is a literal in the macro context.*/
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
      if ((rhi_intf_is_type() == RHI_SOFTWARE) || (gpu->killQuadPart != 2)) \
         return; \
   } \
   if (abs(vertices[2].x - vertices[0].x) >= (1024 << gpu->upscale_shift) || \
       abs(vertices[2].x - vertices[1].x) >= (1024 << gpu->upscale_shift) || \
       abs(vertices[1].x - vertices[0].x) >= (1024 << gpu->upscale_shift)) \
   { \
      if (NV_LIT == 4) \
         gpu->killQuadPart |= (gpu->InCmd == INCMD_QUAD) ? 1 : 2; \
      /* hardware renderer still needs to render first triangle */ \
      if ((rhi_intf_is_type() == RHI_SOFTWARE) || (gpu->killQuadPart != 2)) \
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
      if (rhi_intf_is_type() == RHI_OPENGL || rhi_intf_is_type() == RHI_VULKAN) \
      { \
         Reset_UVLimits(gpu); \
         if ((NV_LIT == 4) && (!gpu->killQuadPart)) \
         { \
            if (gpu->InCmd == INCMD_NONE) \
            { \
               /* We have 4 quad vertices, we can push that at once */ \
               tri_vertex *first = &gpu->InQuad_F3Vertices[0]; \
               Extend_UVLimits(gpu, first, 1); \
               Extend_UVLimits(gpu, vertices, 3); \
               Finalise_UVLimits(gpu); \
               rhi_intf_push_quad(first->precise[0], \
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
            rhi_intf_push_triangle(verts[0].precise[0], \
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
      if (rhi_intf_is_type() == RHI_SOFTWARE) \
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
      if (rhi_intf_has_software_renderer()) \
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
         DrawTriangle_g##GOURAUD_LIT##_t##TEXTURED_LIT##_##BM_TAG##_TM##TM_LIT##_MO##MO_LIT##_ME##ME_LIT(gpu, vertices, pct); \
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

