#ifdef __cplusplus

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#ifdef _STDINT_H
#undef _STDINT_H
#endif

# include <stdint.h>
#endif

struct line_fxp_coord
{
   uint64_t x, y;
   uint32_t r, g, b;
};

struct line_fxp_step
{
   int64_t dx_dk, dy_dk;
   int32_t dr_dk, dg_dk, db_dk;
};

#define LINE_XY_FRACTBITS  32
#define LINE_RGB_FRACTBITS 12

template<bool goraud>
static INLINE void line_point_to_fixed_point_coord(const line_point *point,
      const line_fxp_step *step, line_fxp_coord *coord)
{
   coord->x  = ((uint64_t)fx32_shiftup(point->x)) | (UINT64_C(1) << (LINE_XY_FRACTBITS - 1));
   coord->y  = ((uint64_t)fx32_shiftup(point->y)) | (UINT64_C(1) << (LINE_XY_FRACTBITS - 1));

   coord->x -= 1024;

   if(step->dy_dk < 0)
      coord->y -= 1024;

   if(goraud)
   {
      coord->r = (fx32_shiftup(point->r)) | (1 << (LINE_RGB_FRACTBITS - 1));
      coord->g = (fx32_shiftup(point->g)) | (1 << (LINE_RGB_FRACTBITS - 1));
      coord->b = (fx32_shiftup(point->b)) | (1 << (LINE_RGB_FRACTBITS - 1));
   }
}

static INLINE int64_t line_divide(int64_t delta, int32_t dk)
{
   delta = (uint64_t)fx32_shiftup(delta);

   if(delta < 0)
      delta -= dk - 1;
   if(delta > 0)
      delta += dk - 1;

   return(delta / dk);
}

template<bool goraud>
static INLINE void line_points_to_fixed_point_step(const line_point *point0,
      const line_point *point1, const int32_t dk, line_fxp_step *step)
{
   if(!dk)
   {
      step->dx_dk = 0;
      step->dy_dk = 0;

      if(goraud)
      {
         step->dr_dk = 0;
         step->dg_dk = 0;
         step->db_dk = 0;
      }
      return;
   }

   step->dx_dk = line_divide(point1->x - point0->x, dk);
   step->dy_dk = line_divide(point1->y - point0->y, dk);

   if(goraud)
   {
      step->dr_dk = (int32_t)((uint32_t)fx32_shiftup(point1->r - point0->r) / dk);
      step->dg_dk = (int32_t)((uint32_t)fx32_shiftup(point1->g - point0->g) / dk);
      step->db_dk = (int32_t)((uint32_t)fx32_shiftup(point1->b - point0->b) / dk);
   }
}

template<bool goraud>
static INLINE void AddLineStep(line_fxp_coord *point, const line_fxp_step *step)
{
   point->x += step->dx_dk;
   point->y += step->dy_dk;

   if(goraud)
   {
      point->r += step->dr_dk;
      point->g += step->dg_dk;
      point->b += step->db_dk;
   }
}

template<bool goraud, int BlendMode, bool MaskEval_TA>
void PS_GPU::DrawLine(line_point *points)
{
   line_fxp_coord cur_point;
   line_fxp_step step;
   int32_t delta_x = abs(points[1].x - points[0].x);
   int32_t delta_y = abs(points[1].y - points[0].y);
   int32_t k       = (delta_x > delta_y) ? delta_x : delta_y;

   if(points[0].x > points[1].x && k)
      vertex_swap(line_point, points[1], points[0]);

   DrawTimeAvail -= k * 2;

   line_points_to_fixed_point_step<goraud>(&points[0], &points[1], k, &step);
   line_point_to_fixed_point_coord<goraud>(&points[0], &step, &cur_point);

   for(int32_t i = 0; i <= k; i++)	// <= is not a typo.
   {
      // Sign extension is not necessary here for x and y, due to the maximum values that ClipX1 and ClipY1 can contain.
      int32_t x = fx32_shiftdown(cur_point.x) & 2047;
      int32_t y = fx32_shiftdown(cur_point.y) & 2047;

      if(!LineSkipTest(this, y))
      {
         uint8_t r, g, b;
         uint16_t pix = 0x8000;

         if(goraud)
         {
            r = fx32_shiftdown(cur_point.r);
            g = fx32_shiftdown(cur_point.g);
            b = fx32_shiftdown(cur_point.b);
         }
         else
         {
            r = points[0].r;
            g = points[0].g;
            b = points[0].b;
         }

         if(DitherEnabled())
         {
            uint8_t *dither_offset = DitherLUT[y & 3][x & 3];
            pix = 0x8000 | (dither_offset[r] << 0) | (dither_offset[g] << 5) | 
               (dither_offset[b] << 10);
         }
         else
         {
            pix = 0x8000 | ((r >> 3) << 0) | ((g >> 3) << 5) | ((b >> 3) << 10);
         }

         // FIXME: There has to be a faster way than checking for being inside the drawing area for each pixel.
         if(x >= ClipX0 && x <= ClipX1 && y >= ClipY0 && y <= ClipY1)
            PlotNativePixel<BlendMode, MaskEval_TA, false>(x, y, pix);
      }

      AddLineStep<goraud>(&cur_point, &step);
   }
}

template<bool polyline, bool goraud, int BlendMode, bool MaskEval_TA>
INLINE void PS_GPU::Command_DrawLine(const uint32_t *cb)
{
   line_point points[2];
   const uint8_t cc = cb[0] >> 24; // For pline handling later.

   DrawTimeAvail   -= 16;	// FIXME, correct time.

   if(polyline && InCmd == INCMD_PLINE)
   {
      //printf("PLINE N\n");
      points[0] = InPLine_PrevPoint;
   }
   else
   {
      points[0].r = (*cb >> 0) & 0xFF;
      points[0].g = (*cb >> 8) & 0xFF;
      points[0].b = (*cb >> 16) & 0xFF;
      cb++;

      points[0].x = sign_x_to_s32(11, ((*cb >> 0) & 0xFFFF)) + OffsX;
      points[0].y = sign_x_to_s32(11, ((*cb >> 16) & 0xFFFF)) + OffsY;
      cb++;
   }

   if(goraud)
   {
      points[1].r = (*cb >> 0) & 0xFF;
      points[1].g = (*cb >> 8) & 0xFF;
      points[1].b = (*cb >> 16) & 0xFF;
      cb++;
   }
   else
   {
      points[1].r = points[0].r;
      points[1].g = points[0].g;
      points[1].b = points[0].b;
   }

   points[1].x = sign_x_to_s32(11, ((*cb >> 0) & 0xFFFF)) + OffsX;
   points[1].y = sign_x_to_s32(11, ((*cb >> 16) & 0xFFFF)) + OffsY;
   cb++;

   if(polyline)
   {
      InPLine_PrevPoint = points[1];

      if(InCmd != INCMD_PLINE)
      {
         InCmd = INCMD_PLINE;
         InCmd_CC = cc;
      }
   }

   int32_t delta_x = abs(points[1].x - points[0].x);
   int32_t delta_y = abs(points[1].y - points[0].y);

   if(delta_x >= 1024)
     return;

   if(delta_y >= 512)
     return;

   rsx_intf_push_line(points[0].x, points[0].y,
		      points[1].x, points[1].y,
		      ((uint32_t)points[0].r) | ((uint32_t)points[0].g << 8) | ((uint32_t)points[0].b << 16),
		      ((uint32_t)points[1].r) | ((uint32_t)points[1].g << 8) | ((uint32_t)points[1].b << 16),
		      DitherEnabled(),
		      BlendMode);

   if (rsx_intf_has_software_renderer())
      DrawLine<goraud, BlendMode, MaskEval_TA>(points);
}
