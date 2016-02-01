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
static INLINE void LinePointToFXPCoord(const line_point &point, const line_fxp_step &step, line_fxp_coord &coord)
{
   coord.x = ((uint64_t)point.x << LINE_XY_FRACTBITS) | (UINT64_C(1) << (LINE_XY_FRACTBITS - 1));
   coord.y = ((uint64_t)point.y << LINE_XY_FRACTBITS) | (UINT64_C(1) << (LINE_XY_FRACTBITS - 1));

   coord.x -= 1024;

   if(step.dy_dk < 0)
      coord.y -= 1024;

   if(goraud)
   {
      coord.r = (point.r << LINE_RGB_FRACTBITS) | (1 << (LINE_RGB_FRACTBITS - 1));
      coord.g = (point.g << LINE_RGB_FRACTBITS) | (1 << (LINE_RGB_FRACTBITS - 1));
      coord.b = (point.b << LINE_RGB_FRACTBITS) | (1 << (LINE_RGB_FRACTBITS - 1));
   }
}

static INLINE int64_t LineDivide(int64_t delta, int32_t dk)
{
   delta = (uint64_t)delta << LINE_XY_FRACTBITS;

   if(delta < 0)
      delta -= dk - 1;
   if(delta > 0)
      delta += dk - 1;

   return(delta / dk);
}

template<bool goraud>
static INLINE void LinePointsToFXPStep(const line_point &point0, const line_point &point1, const int32_t dk, line_fxp_step &step)
{
   if(!dk)
   {
      step.dx_dk = 0;
      step.dy_dk = 0;

      if(goraud)
      {
         step.dr_dk = 0;
         step.dg_dk = 0;
         step.db_dk = 0;
      }
      return;
   }

   step.dx_dk = LineDivide(point1.x - point0.x, dk);
   step.dy_dk = LineDivide(point1.y - point0.y, dk);

   if(goraud)
   {
      step.dr_dk = (int32_t)((uint32_t)(point1.r - point0.r) << LINE_RGB_FRACTBITS) / dk;
      step.dg_dk = (int32_t)((uint32_t)(point1.g - point0.g) << LINE_RGB_FRACTBITS) / dk;
      step.db_dk = (int32_t)((uint32_t)(point1.b - point0.b) << LINE_RGB_FRACTBITS) / dk;
   }
}

template<bool goraud>
static INLINE void AddLineStep(line_fxp_coord &point, const line_fxp_step &step)
{
   point.x += step.dx_dk;
   point.y += step.dy_dk;

   if(goraud)
   {
      point.r += step.dr_dk;
      point.g += step.dg_dk;
      point.b += step.db_dk;
   }
}

template<bool goraud, int BlendMode, bool MaskEval_TA>
void PS_GPU::DrawLine(line_point *points)
{
   int32_t delta_x;
   int32_t delta_y;
   int32_t k;
   line_fxp_coord cur_point;
   line_fxp_step step;

   delta_x = abs(points[1].x - points[0].x);
   delta_y = abs(points[1].y - points[0].y);
   k = (delta_x > delta_y) ? delta_x : delta_y;

   if(delta_x >= 1024)
      return;

   if(delta_y >= 512)
      return;

   if(points[0].x > points[1].x && k)
      vertex_swap(line_point, points[1], points[0]);

   DrawTimeAvail -= k * 2;

   LinePointsToFXPStep<goraud>(points[0], points[1], k, step);
   LinePointToFXPCoord<goraud>(points[0], step, cur_point);

   for(int32_t i = 0; i <= k; i++)	// <= is not a typo.
   {
      // Sign extension is not necessary here for x and y, due to the maximum values that ClipX1 and ClipY1 can contain.
      int32_t x = (cur_point.x >> LINE_XY_FRACTBITS) & 2047;
      int32_t y = (cur_point.y >> LINE_XY_FRACTBITS) & 2047;

      if(!LineSkipTest(this, y))
      {
         uint8_t r, g, b;
         uint16_t pix = 0x8000;

         if(goraud)
         {
            r = cur_point.r >> LINE_RGB_FRACTBITS;
            g = cur_point.g >> LINE_RGB_FRACTBITS;
            b = cur_point.b >> LINE_RGB_FRACTBITS;
         }
         else
         {
            r = points[0].r;
            g = points[0].g;
            b = points[0].b;
         }

         if(dtd)
         {
            pix |= DitherLUT[y & 3][x & 3][r] << 0;
            pix |= DitherLUT[y & 3][x & 3][g] << 5;
            pix |= DitherLUT[y & 3][x & 3][b] << 10;
         }
         else
         {
            pix |= (r >> 3) << 0;
            pix |= (g >> 3) << 5;
            pix |= (b >> 3) << 10;
         }

         // FIXME: There has to be a faster way than checking for being inside the drawing area for each pixel.
         if(x >= ClipX0 && x <= ClipX1 && y >= ClipY0 && y <= ClipY1)
            PlotNativePixel<BlendMode, MaskEval_TA, false>(x, y, pix);
      }

      AddLineStep<goraud>(cur_point, step);
   }
}

template<bool polyline, bool goraud, int BlendMode, bool MaskEval_TA>
INLINE void PS_GPU::Command_DrawLine(const uint32_t *cb)
{
   const uint8_t cc = cb[0] >> 24; // For pline handling later.
   line_point points[2];

   DrawTimeAvail -= 16;	// FIXME, correct time.

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

   DrawLine<goraud, BlendMode, MaskEval_TA>(points);
}
