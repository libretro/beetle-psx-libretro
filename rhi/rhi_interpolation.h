#ifndef RHI_INTERPOLATION_H
#define RHI_INTERPOLATION_H

#include <stdint.h>
#include <boolean.h>
#include <retro_inline.h>

/* Only the low 20 bits survive an 8.12 accumulator. Colour and UV share
 * origin20/dx12, followed by dx8/dy20. Arithmetic wraps as on the PS1. */
static INLINE void rhi_pack_interpolant(uint32_t *packed,
      uint32_t origin, uint32_t dx, uint32_t dy)
{
   packed[0] = (origin & 0xfffffu) | (dx << 20);
   packed[1] = ((dx >> 12) & 0xffu) | ((dy & 0xfffffu) << 8);
}

static INLINE unsigned rhi_triangle_anchor(const int32_t *x)
{
   /* DrawTriangle's core vertex, including equal-X ties. */
   return x[1] <= x[0] ? (x[2] <= x[1] ? 2 : 1) :
         (x[2] < x[0] ? 2 : 0);
}

static INLINE void rhi_triangle_interpolant(uint32_t *packed,
      const int32_t *x, const int32_t *y, const int32_t *value,
      int64_t denom, unsigned anchor)
{
   uint32_t dx, dy, origin;
   /* A constant channel has zero slopes, regardless of triangle geometry. */
   if (value[0] == value[1] && value[0] == value[2])
   {
      rhi_pack_interpolant(packed,
            ((uint32_t)value[0] << 12) + 2048u, 0u, 0u);
      return;
   }
   dx = (uint32_t)(((int64_t)(value[1] - value[0]) * (y[2] - y[1]) -
         (int64_t)(value[2] - value[1]) * (y[1] - y[0])) * 4096 / denom);
   dy = (uint32_t)(((int64_t)(x[1] - x[0]) * (value[2] - value[1]) -
         (int64_t)(x[2] - x[1]) * (value[1] - value[0])) * 4096 / denom);
   origin = ((uint32_t)value[anchor] << 12) + 2048u -
         dx * (uint32_t)x[anchor] - dy * (uint32_t)y[anchor];
   rhi_pack_interpolant(packed, origin, dx, dy);
}

/* DrawLine swaps descending-X endpoints before walking both endpoints.
 * Pack the same anchor and signed delta for GL/Vulkan pixel selection.
 * GP0 rejects spans of 1024 X or 512 Y pixels before reaching the RHI. */
static INLINE void rhi_pack_line_coverage(uint32_t *packed,
      const int32_t *x, const int32_t *y)
{
   unsigned anchor = x[0] > x[1] ? 1 : 0;
   packed[0] = (uint16_t)x[anchor] |
         ((uint32_t)(uint16_t)y[anchor] << 16);
   packed[1] = (uint16_t)(x[1 - anchor] - x[anchor]) |
         ((uint32_t)(uint16_t)(y[1 - anchor] - y[anchor]) << 16);
}

/* DrawLine advances once per dominant-axis pixel, including the endpoint.
 * It swaps endpoints on descending X, including equal-major ties. */
static INLINE bool rhi_line_color_planes(uint32_t *planes,
      const int32_t *x, const int32_t *y, const uint32_t *colors)
{
   int32_t dx = x[1] - x[0], dy = y[1] - y[0];
   int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
   bool horizontal = ax > ay;
   int32_t steps = horizontal ? ax : ay;
   unsigned c, anchor = x[0] > x[1] ? 1 : 0;
   if (!steps)
      return false;
   for (c = 0; c < 3; c++)
   {
      int32_t first = (colors[anchor] >> (c * 8)) & 255;
      int32_t last = (colors[1 - anchor] >> (c * 8)) & 255;
      int32_t step = (last - first) * 4096 / steps;
      int32_t direction = horizontal ? x[1 - anchor] - x[anchor] :
            y[1 - anchor] - y[anchor];
      uint32_t slope = (uint32_t)(direction < 0 ? -step : step);
      uint32_t origin = ((uint32_t)first << 12) + 2048u - slope *
            (uint32_t)(horizontal ? x[anchor] : y[anchor]);
      rhi_pack_interpolant(planes + c * 2, origin,
            horizontal ? slope : 0u, horizontal ? 0u : slope);
   }
   return true;
}

#endif
