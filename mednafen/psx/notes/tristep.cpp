#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#define INLINE inline

static INLINE int64_t MakePolyXFP(int32_t x)
{
 return ((int64_t)x << 32) + ((1LL << 32) - (1 << 11));
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


// Test X delta of -1023 ... 1023
// Test Y delta of 1 ... 511
int main()
{
 for(int xbase = -1; xbase < 1025; xbase += 1025)
 {
  for(int dx = -1023; dx <= 1023; dx++)
  {
   for(int dy = 1; dy <= 511; dy++)
   {
    int64_t x_coord, x_step;
    int32_t alt_x_coord;
    int32_t alt_x_error;

    x_coord = MakePolyXFP(xbase);
    x_step = MakePolyXFPStep(dx, dy);

    alt_x_coord = xbase;

    if(dx >= 0)
     alt_x_error = dy - 1;
    else
     alt_x_error = 0;

    for(int step = 0; step < dy; step++)
    {
     if(GetPolyXFP_Int(x_coord) != alt_x_coord)
     {
      printf("xbase=%d, dx=%d, dy=%d, step=%d --- xfpx=%d, altx=%d\n", xbase, dx, dy, step, GetPolyXFP_Int(x_coord), alt_x_coord);
     }
     x_coord += x_step;
 
     alt_x_error += abs(dx);
     while(alt_x_error >= dy)
     {
      if(dx < 0)
       alt_x_coord--;
      else
       alt_x_coord++;
      alt_x_error -= dy;
     }
    }
   }
  }
 }
}
