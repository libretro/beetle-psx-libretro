/* gpu_line.c is textually #included into gpu.c, which already
 * includes <stdint.h> via psx.h.  No additional headers needed
 * here. */

struct line_fxp_coord
{
   uint64_t x, y;
   uint32_t r, g, b;
};
typedef struct line_fxp_coord line_fxp_coord;

struct line_fxp_step
{
   int64_t dx_dk, dy_dk;
   int32_t dr_dk, dg_dk, db_dk;
};
typedef struct line_fxp_step line_fxp_step;

#define LINE_XY_FRACTBITS  32
#define LINE_RGB_FRACTBITS 12

/*
 * line_point_to_fixed_point_coord - convert a line endpoint
 * (line_point) plus its per-pixel step (line_fxp_step) into the
 * fixed-point bias-adjusted starting coord (line_fxp_coord) used
 * by the per-pixel walker.
 *
 * `gouraud` (literal 0/1) elides the r/g/b initialisation for
 * non-gouraud lines (single-colour path) so the unused fields
 * aren't written.
 */
#define DEFINE_line_point_to_fixed_point_coord(SUFFIX, GOURAUD_LIT) \
static INLINE void line_point_to_fixed_point_coord_##SUFFIX(const line_point *point, \
      const line_fxp_step *step, line_fxp_coord *coord) \
{ \
   coord->x  = ((uint64_t)point->x << LINE_XY_FRACTBITS) | (UINT64_C(1) << (LINE_XY_FRACTBITS - 1)); \
   coord->y  = ((uint64_t)point->y << LINE_XY_FRACTBITS) | (UINT64_C(1) << (LINE_XY_FRACTBITS - 1)); \
   coord->x -= 1024; \
   if (step->dy_dk < 0) \
      coord->y -= 1024; \
   if (GOURAUD_LIT) \
   { \
      coord->r = (point->r << LINE_RGB_FRACTBITS) | (1 << (LINE_RGB_FRACTBITS - 1)); \
      coord->g = (point->g << LINE_RGB_FRACTBITS) | (1 << (LINE_RGB_FRACTBITS - 1)); \
      coord->b = (point->b << LINE_RGB_FRACTBITS) | (1 << (LINE_RGB_FRACTBITS - 1)); \
   } \
}

DEFINE_line_point_to_fixed_point_coord(g0, 0)
DEFINE_line_point_to_fixed_point_coord(g1, 1)

static INLINE int64_t line_divide(int64_t delta, int32_t dk)
{
   delta = (uint64_t)delta << LINE_XY_FRACTBITS;

   if(delta < 0)
      delta -= dk - 1;
   if(delta > 0)
      delta += dk - 1;

   return(delta / dk);
}

/*
 * line_points_to_fixed_point_step - compute the per-pixel
 * fixed-point delta in (x, y, r, g, b) for a line from point0 to
 * point1, given the dominant-axis pixel count `dk`.
 *
 * `gouraud` elides the r/g/b deltas when not needed.
 */
#define DEFINE_line_points_to_fixed_point_step(SUFFIX, GOURAUD_LIT) \
static INLINE void line_points_to_fixed_point_step_##SUFFIX(const line_point *point0, \
      const line_point *point1, const int32_t dk, line_fxp_step *step) \
{ \
   if (!dk) \
   { \
      step->dx_dk = 0; \
      step->dy_dk = 0; \
      if (GOURAUD_LIT) \
      { \
         step->dr_dk = 0; \
         step->dg_dk = 0; \
         step->db_dk = 0; \
      } \
      return; \
   } \
   step->dx_dk = line_divide(point1->x - point0->x, dk); \
   step->dy_dk = line_divide(point1->y - point0->y, dk); \
   if (GOURAUD_LIT) \
   { \
      step->dr_dk = (int32_t)((uint32_t)(point1->r - point0->r) << LINE_RGB_FRACTBITS) / dk; \
      step->dg_dk = (int32_t)((uint32_t)(point1->g - point0->g) << LINE_RGB_FRACTBITS) / dk; \
      step->db_dk = (int32_t)((uint32_t)(point1->b - point0->b) << LINE_RGB_FRACTBITS) / dk; \
   } \
}

DEFINE_line_points_to_fixed_point_step(g0, 0)
DEFINE_line_points_to_fixed_point_step(g1, 1)

/*
 * AddLineStep - advance the per-pixel coordinate by one step
 * along the line. `gouraud` elides the r/g/b advance.
 */
#define DEFINE_AddLineStep(SUFFIX, GOURAUD_LIT) \
static INLINE void AddLineStep_##SUFFIX(line_fxp_coord *point, const line_fxp_step *step) \
{ \
   point->x += step->dx_dk; \
   point->y += step->dy_dk; \
   if (GOURAUD_LIT) \
   { \
      point->r += step->dr_dk; \
      point->g += step->dg_dk; \
      point->b += step->db_dk; \
   } \
}

DEFINE_AddLineStep(g0, 0)
DEFINE_AddLineStep(g1, 1)

enum {
   GPU_SW_LineBlend_BMopaque = -1, GPU_SW_LineBlend_BMavg = 0,
   GPU_SW_LineBlend_BMadd = 1, GPU_SW_LineBlend_BMsub = 2, GPU_SW_LineBlend_BMaddq = 3
};

#if defined(GPU_HAVE_SSE2) || defined(GPU_HAVE_NEON)
static INLINE void GPU_SW_LinePixels(GPU_SW_Packet *p, uint32_t r, uint32_t g,
      uint32_t b, uint32_t dr, uint32_t dg, uint32_t db, unsigned x, unsigned y, bool dither)
{
   unsigned i = 0;
   GPU_SW_Colors(p->red, r, dr, p->count);
   GPU_SW_Colors(p->green, g, dg, p->count);
   GPU_SW_Colors(p->blue, b, db, p->count);
   GPU_SW_DitherOffsets(p->dither, p->count, x, y, 0, dither);
   for (; i + 4 <= p->count;)
   {
      unsigned count = p->count - i >= 8 ? 8 : 4;
#if defined(GPU_HAVE_SSE2)
      __m128i d = GPU_SW_Load16((const uint16_t *)(p->dither + i), count);
      __m128i red = GPU_SW_Load16(p->red + i, count);
      __m128i green = GPU_SW_Load16(p->green + i, count);
      __m128i blue = GPU_SW_Load16(p->blue + i, count);
#define GPU_SW_LINE_CHANNEL(c) c = _mm_min_epi16(_mm_set1_epi16(31), _mm_max_epi16(_mm_setzero_si128(), _mm_srai_epi16(_mm_add_epi16(c, d), 3)))
      GPU_SW_LINE_CHANNEL(red); GPU_SW_LINE_CHANNEL(green); GPU_SW_LINE_CHANNEL(blue);
#undef GPU_SW_LINE_CHANNEL
      GPU_SW_Store16(p->source + i, _mm_or_si128(_mm_set1_epi16((short)0x8000),
               _mm_or_si128(red, _mm_or_si128(_mm_slli_epi16(green, 5), _mm_slli_epi16(blue, 10)))), count);
#else
      int16x8_t d = vreinterpretq_s16_u16(GPU_SW_Load16((const uint16_t *)(p->dither + i), count));
      int16x8_t red = vreinterpretq_s16_u16(GPU_SW_Load16(p->red + i, count));
      int16x8_t green = vreinterpretq_s16_u16(GPU_SW_Load16(p->green + i, count));
      int16x8_t blue = vreinterpretq_s16_u16(GPU_SW_Load16(p->blue + i, count));
#define GPU_SW_LINE_CHANNEL(c) c = vminq_s16(vdupq_n_s16(31), vmaxq_s16(vdupq_n_s16(0), vshrq_n_s16(vaddq_s16(c, d), 3)))
      GPU_SW_LINE_CHANNEL(red); GPU_SW_LINE_CHANNEL(green); GPU_SW_LINE_CHANNEL(blue);
#undef GPU_SW_LINE_CHANNEL
      GPU_SW_Store16(p->source + i, vorrq_u16(vdupq_n_u16(0x8000),
               vorrq_u16(vreinterpretq_u16_s16(red), vorrq_u16(vshlq_n_u16(vreinterpretq_u16_s16(green), 5),
                     vshlq_n_u16(vreinterpretq_u16_s16(blue), 10)))), count);
#endif
      i += count;
   }
   for (; i < p->count; i++)
   {
      unsigned c, pixel = 0x8000;
      const uint16_t channels[3] = { p->red[i], p->green[i], p->blue[i] };
      for (c = 0; c < 3; c++)
      {
         int value = ((int)channels[c] + p->dither[i]) >> 3;
         if (value < 0) value = 0;
         if (value > 31) value = 31;
         pixel |= value << (c * 5);
      }
      p->source[i] = pixel;
   }
}

static INLINE bool GPU_SW_HorizontalLine(PS_GPU *gpu, const line_point *point,
      const line_fxp_coord *cur, const line_fxp_step *step, unsigned length,
      bool gouraud, int blend, bool mask_eval)
{
   unsigned first = (cur->x >> LINE_XY_FRACTBITS) & 2047;
   unsigned y = (cur->y >> LINE_XY_FRACTBITS) & 2047;
   unsigned x, end, offset, width, done;
   i_group ig;
   i_deltas idl;
   GPU_SW_Packet p;
   /* A wrap in the DDA's 11-bit coordinate domain retains scalar order. */
   if (first + length > 2048) return false;
   if (y < gpu->ClipY0 || y > gpu->ClipY1 || LineSkipTest(gpu, y)) return true;
   x = first < gpu->ClipX0 ? gpu->ClipX0 : first;
   end = first + length < gpu->ClipX1 + 1u ? first + length : gpu->ClipX1 + 1u;
   if (end <= x) return true;
   offset = x - first; width = end - x;
   /* Promote the line's 12-bit colour fractions into polygon-style 24-bit
    * fractions. Unsigned shifts preserve the scalar byte truncation exactly. */
   idl.dr_dx = gouraud ? (uint32_t)step->dr_dk << 12 : 0;
   idl.dg_dx = gouraud ? (uint32_t)step->dg_dk << 12 : 0;
   idl.db_dx = gouraud ? (uint32_t)step->db_dk << 12 : 0;
   ig.r = (gouraud ? cur->r << 12 : (uint32_t)point->r << 24) + idl.dr_dx * offset;
   ig.g = (gouraud ? cur->g << 12 : (uint32_t)point->g << 24) + idl.dg_dx * offset;
   ig.b = (gouraud ? cur->b << 12 : (uint32_t)point->b << 24) + idl.db_dx * offset;
   if (!gpu->upscale_shift)
   {
      done = DrawSpanVec_NT(gpu, y, x, width, &ig, &idl, 1, DitherEnabled(gpu), blend, mask_eval);
      x += done; width -= done;
      ig.r += idl.dr_dx * done; ig.g += idl.dg_dx * done; ig.b += idl.db_dx * done;
   }
   p.execute = NULL; p.modulate = 0; p.textured = 0;
   p.native_shift = gpu->upscale_shift; p.stride = 1024u << gpu->upscale_shift;
   p.blend = blend; p.mask_eval = mask_eval; p.mask_set = gpu->MaskSetOR;
   p.dest = &texel_fetch(gpu, x, y);
   GPU_SW_Fill16(p.covered, GPU_SW_BLOCK, 0xffff);
   while (width)
   {
      p.count = width < GPU_SW_BLOCK ? width : GPU_SW_BLOCK;
      GPU_SW_LinePixels(&p, ig.r, ig.g, ig.b, idl.dr_dx, idl.dg_dx, idl.db_dx, x, y, DitherEnabled(gpu));
      GPU_SW_Execute(&p);
      ig.r += idl.dr_dx * p.count; ig.g += idl.dg_dx * p.count; ig.b += idl.db_dx * p.count;
      x += p.count; width -= p.count; p.dest += p.count << p.native_shift;
   }
   return true;
}
#else
static INLINE bool GPU_SW_HorizontalLine(PS_GPU *gpu, const line_point *point,
      const line_fxp_coord *cur, const line_fxp_step *step, unsigned length,
      bool gouraud, int blend, bool mask_eval)
{
   return false;
}
#endif

/*
 * DrawLine - rasterise one line segment.
 *
 * Walks pixels along the dominant axis (whichever of dx/dy is
 * larger) using a simple DDA in fixed-point, calling
 * PlotNativePixel at each step.
 *
 * Macro parameters:
 *   SUFFIX        - mangled-name suffix encoding (gouraud, BM, ME)
 *   GOURAUD_LIT   - 0/1 literal: per-vertex r/g/b interpolation
 *   BM_TAG        - blend specialisation suffix (BMopaque/BMavg/BMadd/
 *                   BMsub/BMaddq) used to call PlotNativePixel_<...>
 *   MASKEVAL_LIT  - 0/1 literal: mask-bit gate on destination pixel
 *
 * Lines are never textured on PS1 hardware, so there's no
 * texture-mode dimension here.  PlotNativePixel always called with
 * textured=false (PlotNativePixel_BMxxx_MEx_T0).
 */
#define DEFINE_DrawLine(SUFFIX, GOURAUD_LIT, BM_TAG, MASKEVAL_LIT) \
static void DrawLine_##SUFFIX(PS_GPU *gpu, line_point *points) \
{ \
   line_fxp_coord cur_point; \
   line_fxp_step  step; \
   int32_t        i; \
   int32_t delta_x = abs(points[1].x - points[0].x); \
   int32_t delta_y = abs(points[1].y - points[0].y); \
   int32_t k       = (delta_x > delta_y) ? delta_x : delta_y; \
   GPU_SW_Sync(); \
   if (points[0].x > points[1].x && k) \
      vertex_swap(line_point, points[1], points[0]); \
   gpu->DrawTimeAvail -= k * 2; \
   line_points_to_fixed_point_step_g##GOURAUD_LIT(&points[0], &points[1], k, &step); \
   line_point_to_fixed_point_coord_g##GOURAUD_LIT(&points[0], &step, &cur_point); \
   if (!delta_y && k >= 7 && GPU_SW_HorizontalLine(gpu, &points[0], &cur_point, &step, k + 1, \
            GOURAUD_LIT, GPU_SW_LineBlend_##BM_TAG, MASKEVAL_LIT)) return; \
   for (i = 0; i <= k; i++)  /* <= is not a typo. */ \
   { \
      /* Sign extension is not necessary here for x and y, due to the maximum values that ClipX1 and ClipY1 can contain. */ \
      int32_t x = (cur_point.x >> LINE_XY_FRACTBITS) & 2047; \
      int32_t y = (cur_point.y >> LINE_XY_FRACTBITS) & 2047; \
      if (!LineSkipTest(gpu, y)) \
      { \
         uint8_t r, g, b; \
         uint16_t pix; \
         if (GOURAUD_LIT) \
         { \
            r = cur_point.r >> LINE_RGB_FRACTBITS; \
            g = cur_point.g >> LINE_RGB_FRACTBITS; \
            b = cur_point.b >> LINE_RGB_FRACTBITS; \
         } \
         else \
         { \
            r = points[0].r; \
            g = points[0].g; \
            b = points[0].b; \
         } \
         if (DitherEnabled(gpu)) \
         { \
            pix = 0x8000 | (gpu->DitherLUT[y & 3][r][x & 3] << 0) | \
                           (gpu->DitherLUT[y & 3][g][x & 3] << 5) | \
                           (gpu->DitherLUT[y & 3][b][x & 3] << 10); \
         } \
         else \
            pix = 0x8000 | ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10); \
         /* FIXME: There has to be a faster way than checking for being inside the drawing area for each pixel. */ \
         if (x >= gpu->ClipX0 && x <= gpu->ClipX1 && y >= gpu->ClipY0 && y <= gpu->ClipY1) \
            PlotNativePixel_##BM_TAG##_ME##MASKEVAL_LIT##_T0(gpu, x, y, pix); \
      } \
      AddLineStep_g##GOURAUD_LIT(&cur_point, &step); \
   } \
}

/* 20 DrawLine specialisations: 2 gouraud * 5 blend * 2 maskeval. */
DEFINE_DrawLine(g0_BMopaque_ME0, 0, BMopaque, 0)
DEFINE_DrawLine(g0_BMopaque_ME1, 0, BMopaque, 1)
DEFINE_DrawLine(g0_BMavg_ME0,    0, BMavg,    0)
DEFINE_DrawLine(g0_BMavg_ME1,    0, BMavg,    1)
DEFINE_DrawLine(g0_BMadd_ME0,    0, BMadd,    0)
DEFINE_DrawLine(g0_BMadd_ME1,    0, BMadd,    1)
DEFINE_DrawLine(g0_BMsub_ME0,    0, BMsub,    0)
DEFINE_DrawLine(g0_BMsub_ME1,    0, BMsub,    1)
DEFINE_DrawLine(g0_BMaddq_ME0,   0, BMaddq,   0)
DEFINE_DrawLine(g0_BMaddq_ME1,   0, BMaddq,   1)
DEFINE_DrawLine(g1_BMopaque_ME0, 1, BMopaque, 0)
DEFINE_DrawLine(g1_BMopaque_ME1, 1, BMopaque, 1)
DEFINE_DrawLine(g1_BMavg_ME0,    1, BMavg,    0)
DEFINE_DrawLine(g1_BMavg_ME1,    1, BMavg,    1)
DEFINE_DrawLine(g1_BMadd_ME0,    1, BMadd,    0)
DEFINE_DrawLine(g1_BMadd_ME1,    1, BMadd,    1)
DEFINE_DrawLine(g1_BMsub_ME0,    1, BMsub,    0)
DEFINE_DrawLine(g1_BMsub_ME1,    1, BMsub,    1)
DEFINE_DrawLine(g1_BMaddq_ME0,   1, BMaddq,   0)
DEFINE_DrawLine(g1_BMaddq_ME1,   1, BMaddq,   1)

/* The optional rhi_intf_push_line() call factored out for the
 * Command_DrawLine macro since it conditionally compiles based on
 * backend availability and we want the macro body compact. */
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
#define GPU_LINE_RHI_PUSH_HOOK(gpu, points, BM_VAL, MASKEVAL_LIT) \
   do { \
      if (rhi_intf_is_type() == RHI_OPENGL || rhi_intf_is_type() == RHI_VULKAN) \
      { \
         rhi_intf_push_line(  (points)[0].x, (points)[0].y, \
               (points)[1].x, (points)[1].y, \
               ((uint32_t)(points)[0].r) | ((uint32_t)(points)[0].g << 8) | ((uint32_t)(points)[0].b << 16), \
               ((uint32_t)(points)[1].r) | ((uint32_t)(points)[1].g << 8) | ((uint32_t)(points)[1].b << 16), \
               DitherEnabled(gpu), \
               (BM_VAL), \
               (MASKEVAL_LIT), \
               (gpu)->MaskSetOR != 0); \
      } \
   } while (0)
#else
#define GPU_LINE_RHI_PUSH_HOOK(gpu, points, BM_VAL, MASKEVAL_LIT) ((void)0)
#endif

/*
 * Command_DrawLine - top-level GP0 line/polyline command handler.
 *
 * Parses the command buffer for endpoint(s) and per-vertex
 * colour data, then invokes DrawLine once per segment.
 *
 * Macro parameters:
 *   SUFFIX        - mangled-name suffix encoding (P, G, BM, ME)
 *   POLYLINE_LIT  - 0/1: 0 = two-point line, 1 = N-point polyline
 *                   with terminator-word detection
 *   GOURAUD_LIT   - 0/1: per-vertex colour
 *   BM_VAL        - integer literal blend mode (-1 / 0..3)
 *   BM_TAG        - matching blend tag (BMopaque/BMavg/...) used in
 *                   the DrawLine_<...> mangled name
 *   MASKEVAL_LIT  - 0/1: mask-bit gate
 *
 * Reached from the GP0 dispatch via Commands[0x40..0x5F].
 */
#define DEFINE_Command_DrawLine(SUFFIX, POLYLINE_LIT, GOURAUD_LIT, BM_VAL, BM_TAG, MASKEVAL_LIT) \
static void Command_DrawLine_##SUFFIX(PS_GPU *gpu, const uint32_t *cb) \
{ \
   line_point points[2]; \
   const uint8_t cc = cb[0] >> 24; /* For pline handling later. */ \
   int32_t delta_x; \
   int32_t delta_y; \
   gpu->DrawTimeAvail   -= 16;     /* FIXME, correct time. */ \
   if (POLYLINE_LIT && gpu->InCmd == INCMD_PLINE) \
   { \
      /*printf("PLINE N\n");*/ \
      points[0] = gpu->InPLine_PrevPoint; \
   } \
   else \
   { \
      points[0].r = (*cb >> 0)  & 0xFF; \
      points[0].g = (*cb >> 8)  & 0xFF; \
      points[0].b = (*cb >> 16) & 0xFF; \
      cb++; \
      points[0].x = sign_x_to_s32(11, ((*cb >> 0)  & 0xFFFF)) + gpu->OffsX; \
      points[0].y = sign_x_to_s32(11, ((*cb >> 16) & 0xFFFF)) + gpu->OffsY; \
      cb++; \
   } \
   if (GOURAUD_LIT) \
   { \
      points[1].r = (*cb >> 0)  & 0xFF; \
      points[1].g = (*cb >> 8)  & 0xFF; \
      points[1].b = (*cb >> 16) & 0xFF; \
      cb++; \
   } \
   else \
   { \
      points[1].r = points[0].r; \
      points[1].g = points[0].g; \
      points[1].b = points[0].b; \
   } \
   points[1].x = sign_x_to_s32(11, ((*cb >> 0)  & 0xFFFF)) + gpu->OffsX; \
   points[1].y = sign_x_to_s32(11, ((*cb >> 16) & 0xFFFF)) + gpu->OffsY; \
   cb++; \
   if (POLYLINE_LIT) \
   { \
      gpu->InPLine_PrevPoint = points[1]; \
      if (gpu->InCmd != INCMD_PLINE) \
      { \
         gpu->InCmd    = INCMD_PLINE; \
         gpu->InCmd_CC = cc; \
      } \
   } \
   delta_x = abs(points[1].x - points[0].x); \
   delta_y = abs(points[1].y - points[0].y); \
   if (delta_x >= 1024) \
      return; \
   if (delta_y >= 512) \
      return; \
   GPU_LINE_RHI_PUSH_HOOK(gpu, points, BM_VAL, MASKEVAL_LIT); \
   if (rhi_intf_has_software_renderer()) \
      DrawLine_g##GOURAUD_LIT##_##BM_TAG##_ME##MASKEVAL_LIT(gpu, points); \
}

/* 40 Command_DrawLine specialisations: 2 polyline * 2 gouraud * 5 blend * 2 maskeval. */
DEFINE_Command_DrawLine(P0_G0_BMopaque_ME0, 0, 0, -1, BMopaque, 0)
DEFINE_Command_DrawLine(P0_G0_BMopaque_ME1, 0, 0, -1, BMopaque, 1)
DEFINE_Command_DrawLine(P0_G0_BMavg_ME0,    0, 0,  0, BMavg,    0)
DEFINE_Command_DrawLine(P0_G0_BMavg_ME1,    0, 0,  0, BMavg,    1)
DEFINE_Command_DrawLine(P0_G0_BMadd_ME0,    0, 0,  1, BMadd,    0)
DEFINE_Command_DrawLine(P0_G0_BMadd_ME1,    0, 0,  1, BMadd,    1)
DEFINE_Command_DrawLine(P0_G0_BMsub_ME0,    0, 0,  2, BMsub,    0)
DEFINE_Command_DrawLine(P0_G0_BMsub_ME1,    0, 0,  2, BMsub,    1)
DEFINE_Command_DrawLine(P0_G0_BMaddq_ME0,   0, 0,  3, BMaddq,   0)
DEFINE_Command_DrawLine(P0_G0_BMaddq_ME1,   0, 0,  3, BMaddq,   1)
DEFINE_Command_DrawLine(P1_G0_BMopaque_ME0, 1, 0, -1, BMopaque, 0)
DEFINE_Command_DrawLine(P1_G0_BMopaque_ME1, 1, 0, -1, BMopaque, 1)
DEFINE_Command_DrawLine(P1_G0_BMavg_ME0,    1, 0,  0, BMavg,    0)
DEFINE_Command_DrawLine(P1_G0_BMavg_ME1,    1, 0,  0, BMavg,    1)
DEFINE_Command_DrawLine(P1_G0_BMadd_ME0,    1, 0,  1, BMadd,    0)
DEFINE_Command_DrawLine(P1_G0_BMadd_ME1,    1, 0,  1, BMadd,    1)
DEFINE_Command_DrawLine(P1_G0_BMsub_ME0,    1, 0,  2, BMsub,    0)
DEFINE_Command_DrawLine(P1_G0_BMsub_ME1,    1, 0,  2, BMsub,    1)
DEFINE_Command_DrawLine(P1_G0_BMaddq_ME0,   1, 0,  3, BMaddq,   0)
DEFINE_Command_DrawLine(P1_G0_BMaddq_ME1,   1, 0,  3, BMaddq,   1)
DEFINE_Command_DrawLine(P0_G1_BMopaque_ME0, 0, 1, -1, BMopaque, 0)
DEFINE_Command_DrawLine(P0_G1_BMopaque_ME1, 0, 1, -1, BMopaque, 1)
DEFINE_Command_DrawLine(P0_G1_BMavg_ME0,    0, 1,  0, BMavg,    0)
DEFINE_Command_DrawLine(P0_G1_BMavg_ME1,    0, 1,  0, BMavg,    1)
DEFINE_Command_DrawLine(P0_G1_BMadd_ME0,    0, 1,  1, BMadd,    0)
DEFINE_Command_DrawLine(P0_G1_BMadd_ME1,    0, 1,  1, BMadd,    1)
DEFINE_Command_DrawLine(P0_G1_BMsub_ME0,    0, 1,  2, BMsub,    0)
DEFINE_Command_DrawLine(P0_G1_BMsub_ME1,    0, 1,  2, BMsub,    1)
DEFINE_Command_DrawLine(P0_G1_BMaddq_ME0,   0, 1,  3, BMaddq,   0)
DEFINE_Command_DrawLine(P0_G1_BMaddq_ME1,   0, 1,  3, BMaddq,   1)
DEFINE_Command_DrawLine(P1_G1_BMopaque_ME0, 1, 1, -1, BMopaque, 0)
DEFINE_Command_DrawLine(P1_G1_BMopaque_ME1, 1, 1, -1, BMopaque, 1)
DEFINE_Command_DrawLine(P1_G1_BMavg_ME0,    1, 1,  0, BMavg,    0)
DEFINE_Command_DrawLine(P1_G1_BMavg_ME1,    1, 1,  0, BMavg,    1)
DEFINE_Command_DrawLine(P1_G1_BMadd_ME0,    1, 1,  1, BMadd,    0)
DEFINE_Command_DrawLine(P1_G1_BMadd_ME1,    1, 1,  1, BMadd,    1)
DEFINE_Command_DrawLine(P1_G1_BMsub_ME0,    1, 1,  2, BMsub,    0)
DEFINE_Command_DrawLine(P1_G1_BMsub_ME1,    1, 1,  2, BMsub,    1)
DEFINE_Command_DrawLine(P1_G1_BMaddq_ME0,   1, 1,  3, BMaddq,   0)
DEFINE_Command_DrawLine(P1_G1_BMaddq_ME1,   1, 1,  3, BMaddq,   1)
