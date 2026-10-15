/* Check the shipping packed planes against an independent fixed-point walk.
 * GPU coverage, filtering, blending and scanout require renderer tests. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "rhi/rhi_interpolation.h"

static unsigned failures;
static unsigned checks;
static uint32_t random_state = 0x6d2b79f5u;

static uint32_t next_random(void)
{
   random_state ^= random_state << 13;
   random_state ^= random_state >> 17;
   random_state ^= random_state << 5;
   return random_state;
}

static void check(const char *kind, uint32_t actual, uint32_t expected)
{
   checks++;
   if (actual != expected)
   {
      if (failures < 12)
         fprintf(stderr, "%s: got %u, expected %u\n", kind,
               (unsigned)actual, (unsigned)expected);
      failures++;
   }
}

static uint32_t evaluate(const uint32_t *plane, int32_t x, int32_t y)
{
   uint32_t origin = plane[0] & 0xfffffu;
   uint32_t dx = (plane[0] >> 20) | ((plane[1] & 0xffu) << 12);
   uint32_t dy = plane[1] >> 8;
   return ((origin + dx * (uint32_t)x + dy * (uint32_t)y) >> 12) & 255u;
}

/* Integer equivalent of primitive_line_coverage() and the GL fragment
 * shader's native-line block. Keep this expression in sync with both shaders;
 * compare its selected pixel against the independent 32-fractional-bit walk. */
static int32_t shader_line_minor(int32_t dx, int32_t dy, int32_t k)
{
   bool horizontal = abs(dx) > abs(dy);
   int32_t steps = horizontal ? abs(dx) : abs(dy);
   int32_t d = horizontal ? dy : dx;
   int32_t n, divisor;
   if (!steps)
      return 0;
   n = 2 * d * k + steps - ((!horizontal || d < 0) ? 1 : 0);
   divisor = 2 * steps;
   return (n - (n < 0 ? divisor - 1 : 0)) / divisor;
}

static void check_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
   int32_t x[2] = {x0, x1}, y[2] = {y0, y1};
   uint32_t colors[2] = {0x7fff00u, 0x4000ffu};
   uint32_t planes[6], coverage[2];
   unsigned anchor = x0 > x1 ? 1 : 0;
   int32_t dx = x[1 - anchor] - x[anchor];
   int32_t dy = y[1 - anchor] - y[anchor];
   int32_t ax = dx < 0 ? -dx : dx;
   int32_t ay = dy < 0 ? -dy : dy;
   int32_t steps = ax > ay ? ax : ay;
   bool valid = rhi_line_color_planes(planes, x, y, colors);
   unsigned channel;

   rhi_pack_line_coverage(coverage, x, y);
   check("line anchor X", (uint16_t)coverage[0], (uint16_t)x[anchor]);
   check("line anchor Y", coverage[0] >> 16, (uint16_t)y[anchor]);
   check("line delta X", (uint16_t)coverage[1], (uint16_t)dx);
   check("line delta Y", coverage[1] >> 16, (uint16_t)dy);
   check("zero-length line", valid, steps != 0);
   check("zero-length coverage", shader_line_minor(0, 0, 0), 0);
   if (!steps)
      return;

   for (channel = 0; channel < 3; channel++)
   {
      /* gpu_line.c walks 32 fractional coordinate bits, with ceil-magnitude
       * steps and a 1024-unit initial bias. Sample the packed color plane at
       * those pixels, rather than using the shader's coverage expression. */
      int32_t first = (colors[anchor] >> (channel * 8)) & 255u;
      int32_t last = (colors[1 - anchor] >> (channel * 8)) & 255u;
      int32_t color_step = (last - first) * 4096 / steps;
      uint32_t color = ((uint32_t)first << 12) + 2048u;
      int64_t sx = (int64_t)dx * INT64_C(4294967296);
      int64_t sy = (int64_t)dy * INT64_C(4294967296);
      int64_t px = (int64_t)x[anchor] * INT64_C(4294967296) +
            INT64_C(2147483648) - 1024;
      int64_t py = (int64_t)y[anchor] * INT64_C(4294967296) +
            INT64_C(2147483648) - (dy < 0 ? 1024 : 0);
      int32_t k;
      sx = (sx + (sx < 0 ? -(steps - 1) : sx > 0 ? steps - 1 : 0)) / steps;
      sy = (sy + (sy < 0 ? -(steps - 1) : sy > 0 ? steps - 1 : 0)) / steps;
      for (k = 0; k <= steps; k++)
      {
         if (channel == 0)
         {
            bool horizontal = ax > ay;
            int32_t sx_pixel = x[anchor] + (horizontal ? k :
                  shader_line_minor(dx, dy, k));
            int32_t sy_pixel = y[anchor] + (horizontal ?
                  shader_line_minor(dx, dy, k) : dy < 0 ? -k : k);
            check("shader line X at DDA step", (uint32_t)sx_pixel,
                  (uint32_t)(int32_t)(px >> 32));
            check("shader line Y at DDA step", (uint32_t)sy_pixel,
                  (uint32_t)(int32_t)(py >> 32));
         }
         check("line color at DDA pixel", evaluate(planes + channel * 2,
               (int32_t)(px >> 32), (int32_t)(py >> 32)), (color >> 12) & 255u);
         px += sx;
         py += sy;
         color += (uint32_t)color_step;
      }
   }
}

static void check_triangle(const int32_t *x, const int32_t *y,
      const int32_t *value)
{
   int64_t area = (int64_t)x[0] * (y[1] - y[2]) +
         (int64_t)x[1] * (y[2] - y[0]) + (int64_t)x[2] * (y[0] - y[1]);
   int64_t nx, ny;
   uint32_t dx, dy, packed[2];
   unsigned anchor, point;
   if (!area)
      return; /* The renderer rejects degenerate triangles before packing. */
   anchor = rhi_triangle_anchor(x);
   rhi_triangle_interpolant(packed, x, y, value, area, anchor);

   /* Barycentric plane derivatives, computed from the three absolute vertex
    * values. The reference then walks truncated 8.12 steps from the anchor. */
   nx = (int64_t)value[0] * (y[1] - y[2]) +
         (int64_t)value[1] * (y[2] - y[0]) + (int64_t)value[2] * (y[0] - y[1]);
   ny = (int64_t)value[0] * (x[2] - x[1]) +
         (int64_t)value[1] * (x[0] - x[2]) + (int64_t)value[2] * (x[1] - x[0]);
   dx = (uint32_t)(nx * 4096 / area);
   dy = (uint32_t)(ny * 4096 / area);
   for (point = 0; point < 4; point++)
   {
      int32_t px = point < 3 ? x[point] : 0;
      int32_t py = point < 3 ? y[point] : 0;
      uint32_t expected = ((uint32_t)value[anchor] << 12) + 2048u;
      expected += dx * (uint32_t)(px - x[anchor]);
      expected += dy * (uint32_t)(py - y[anchor]);
      check("triangle fixed-point plane", evaluate(packed, px, py),
            (expected >> 12) & 255u);
   }
}

int main(void)
{
   int32_t x0, y0, x1, y1;
   unsigned i;
   static const int32_t ties[][3] = {
      {0, 0, 1}, {0, 1, 0}, {1, 0, 0}, {0, 0, 0}, {0, 1, 1}, {1, 0, 1}
   };
   static const unsigned anchors[] = {1, 0, 2, 2, 0, 1};
   for (i = 0; i < sizeof(anchors) / sizeof(anchors[0]); i++)
      check("triangle anchor tie", rhi_triangle_anchor(ties[i]), anchors[i]);
   for (x0 = -4; x0 <= 4; x0++)
      for (y0 = -4; y0 <= 4; y0++)
         for (x1 = -4; x1 <= 4; x1++)
            for (y1 = -4; y1 <= 4; y1++)
               check_line(x0, y0, x1, y1);
   /* Cover the GP0 maximum legal spans and both signs, including the
    * long inexact steps for which ceil-magnitude coordinate division matters.
    * Translation and endpoint reversal must not change tie selection. */
   for (i = 0; i < 4096; i++)
   {
      int32_t dx = next_random() % 1024u;
      int32_t dy = (int32_t)(next_random() % 1023u) - 511;
      int32_t ox = (int32_t)(next_random() % 2048u) - 1024;
      int32_t oy = (int32_t)(next_random() % 2048u) - 1024;
      check_line(ox, oy, ox + dx, oy + dy);
      check_line(ox + dx, oy + dy, ox, oy);
   }
   check_line(0, 0, 1, 2);
   check_line(0, 2, 1, 0);
   check_line(-1024, -1024, -1, -513);
   check_line(1023, 511, 0, 0);
   for (i = 0; i < 20000; i++)
   {
      int32_t x[3], y[3], value[3];
      unsigned v;
      for (v = 0; v < 3; v++)
      {
         x[v] = (int32_t)(next_random() % 2048u) - 1024;
         y[v] = (int32_t)(next_random() % 2048u) - 1024;
         value[v] = next_random() & 255u;
      }
      if ((i & 7u) == 0)
         value[1] = value[2] = value[0]; /* Exercise the constant shortcut. */
      check_triangle(x, y, value);
   }
   printf("%u checks, %u failures\n", checks, failures);
   return failures != 0;
}
