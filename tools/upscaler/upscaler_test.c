/* upscaler_test -- the Vulkan renderer's temporal upscaler geometry.
 *
 * Drives psx_upscaler (rhi/rhi_upscaler.c) the way the renderer does:
 * drawing areas, polygons and sprites, scanouts, and checks what it
 * makes of them:
 *
 * - primitives are jittered with the Halton (2, 3) sequence while it is
 *   on, each frame of a double buffer with its own jitter;
 * - a primitive that moved gets the motion back to where it was, each
 *   vertex its own, relative to the display however the buffers lie in
 *   VRAM; a sprite too;
 * - what stays standing still, as does a jump too far, a stranger, and
 *   a frame shown twice;
 * - the depth is the drawing order, or PGXP's where a primitive has it,
 *   and translucency splits the draws;
 * - the factor follows the request;
 * - turning it on or a reset start the history over.
 *
 * Built by tools/upscaler/run.sh. Exit status 0 on pass. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "rhi/rhi_upscaler.h"

static int s_failures;

#define CHECK(cond, what) \
   do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

#define SCALE 2u

static int near(double a, double b, double eps)
{
   return fabs(a - b) <= eps;
}

static uint64_t sig(uint32_t a)
{
   return psx_upscaler_hash(psx_upscaler_hash_init(), a);
}

static void tri_w(psx_upscaler_t *u, double x0, double y0, double x1, double y1,
      double x2, double y2, double w, unsigned flags, uint64_t s)
{
   psx_upscaler_point_t p[3];
   p[0].x = (float)x0; p[0].y = (float)y0; p[0].w = (float)w;
   p[1].x = (float)x1; p[1].y = (float)y1; p[1].w = (float)w;
   p[2].x = (float)x2; p[2].y = (float)y2; p[2].w = (float)w;
   psx_upscaler_primitive(u, p, 3, flags, s);
}

static void tri(psx_upscaler_t *u, double x0, double y0, double x1, double y1,
      double x2, double y2, unsigned flags, uint64_t s)
{
   tri_w(u, x0, y0, x1, y1, x2, y2, 1.0, flags, s);
}

static void sprite(psx_upscaler_t *u, double x, double y, double w, double h,
      uint64_t s)
{
   psx_upscaler_point_t p[4];
   p[0].x = (float)x;       p[0].y = (float)y;
   p[1].x = (float)(x + w); p[1].y = (float)y;
   p[2].x = (float)x;       p[2].y = (float)(y + h);
   p[3].x = (float)(x + w); p[3].y = (float)(y + h);
   p[0].w = p[1].w = p[2].w = p[3].w = 1.0f;
   psx_upscaler_primitive(u, p, 4, PSX_UPSCALER_SPRITE, s);
}

/* The vertex of @g at render pixel (x, y) within a pixel, if any */
static const psx_upscaler_vertex_t *vertex_at(const psx_upscaler_geometry_t *g,
      double x, double y)
{
   unsigned i;
   for (i = 0; i < g->num_vertices; i++)
      if (near(g->vertices[i].x, x, 1.0) && near(g->vertices[i].y, y, 1.0))
         return &g->vertices[i];
   return NULL;
}

static void test_inactive_and_jitter(void)
{
   psx_upscaler_t *u = psx_upscaler_new();
   psx_upscaler_geometry_t g;
   float jx, jy, jx2, jy2;

   psx_upscaler_jitter(u, &jx, &jy);
   CHECK(jx == 0.0f && jy == 0.0f, "no jitter while off");
   tri(u, 10, 10, 50, 10, 10, 50, 0, sig(1));
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);
   CHECK(g.num_vertices == 0, "nothing kept while off");

   psx_upscaler_configure(u, true, 8, false);
   psx_upscaler_jitter(u, &jx, &jy);
   /* Halton (2, 3), the first point after the origin */
   CHECK(near(jx, 0.0, 1e-6) && near(jy, 1.0 / 3.0 - 0.5, 1e-6),
         "the jitter starts the Halton (2, 3) sequence");
   tri(u, 10, 10, 50, 10, 10, 50, 0, sig(1));
   psx_upscaler_jitter(u, &jx2, &jy2);
   CHECK(jx2 == jx && jy2 == jy, "the frame keeps its jitter while drawn");
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);
   CHECK(g.reset, "the first frame resets the history");
   CHECK(g.jitter_x == jx && g.jitter_y == jy, "the frame reports the jitter it was drawn with");
   CHECK(g.num_vertices == 3 && g.num_draws == 1, "the triangle is kept");
   CHECK(near(g.vertices[0].x, 10 * SCALE + jx, 1e-5) && near(g.vertices[0].y, 10 * SCALE + jy, 1e-5),
         "the pass draws the triangle where it was drawn, jittered");

   psx_upscaler_jitter(u, &jx2, &jy2);
   CHECK(jx2 != jx || jy2 != jy, "the next frame has the next jitter");
   CHECK(jx2 >= -0.5f && jx2 <= 0.5f && jy2 >= -0.5f && jy2 <= 0.5f, "the jitter is within half a pixel");
   psx_upscaler_free(u);
}

static void test_motion(void)
{
   psx_upscaler_t *u = psx_upscaler_new();
   psx_upscaler_geometry_t g;
   const psx_upscaler_vertex_t *v;

   psx_upscaler_configure(u, true, 8, false);
   /* a double buffer: the display shows the other half of VRAM */
   psx_upscaler_set_draw_area(u, 0, 0, 319, 239);
   tri(u, 100, 50, 60, 100, 150, 150, 0, sig(7));
   sprite(u, 200, 20, 16, 16, sig(8));
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);

   psx_upscaler_set_draw_area(u, 0, 256, 319, 495);
   tri(u, 110, 256 + 55, 70, 256 + 105, 160, 256 + 150, 0, sig(7));
   sprite(u, 197, 256 + 20, 16, 16, sig(8));
   psx_upscaler_resolve(u, 0, 256, 320, 240, SCALE, &g);
   CHECK(!g.reset, "the next frame follows");
   CHECK(near(g.jitter_x, -0.25, 1e-6) && near(g.jitter_y, 2.0 / 3.0 - 0.5, 1e-6),
         "the other buffer's frame has the next jitter");
   CHECK(g.num_vertices == 9, "a triangle and a sprite's two triangles");
   v = vertex_at(&g, 110 * SCALE + g.jitter_x, 55 * SCALE + g.jitter_y);
   CHECK(v && near(v->motion_x, 10 * SCALE, 1e-4) && near(v->motion_y, 5 * SCALE, 1e-4),
         "the first vertex moved by its own motion");
   v = vertex_at(&g, 160 * SCALE + g.jitter_x, 150 * SCALE + g.jitter_y);
   CHECK(v && near(v->motion_x, 10 * SCALE, 1e-4) && near(v->motion_y, 0.0, 1e-4),
         "the third vertex moved by its own motion");
   v = vertex_at(&g, 197 * SCALE + g.jitter_x, 20 * SCALE + g.jitter_y);
   CHECK(v && near(v->motion_x, -3.0 * SCALE, 1e-4) && near(v->motion_y, 0.0, 1e-4),
         "the sprite moved left");

   /* shown again: nothing moves */
   psx_upscaler_resolve(u, 0, 256, 320, 240, SCALE, &g);
   v = vertex_at(&g, 110 * SCALE + g.jitter_x, 55 * SCALE + g.jitter_y);
   CHECK(v && v->motion_x == 0.0f && v->motion_y == 0.0f, "a frame shown twice stands still");

   /* a jump too far, and a stranger, stand still */
   psx_upscaler_set_draw_area(u, 0, 0, 319, 239);
   tri(u, 300, 55, 260, 105, 350, 150, 0, sig(7));
   tri(u, 10, 10, 20, 10, 10, 20, 0, sig(9));
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);
   v = vertex_at(&g, 300 * SCALE + g.jitter_x, 55 * SCALE + g.jitter_y);
   CHECK(v && v->motion_x == 0.0f, "a jump too far stands still");
   v = vertex_at(&g, 10 * SCALE + g.jitter_x, 10 * SCALE + g.jitter_y);
   CHECK(v && v->motion_x == 0.0f && v->motion_y == 0.0f, "a stranger stands still");
   psx_upscaler_free(u);
}

static void test_ordinals_depth_and_draws(void)
{
   psx_upscaler_t *u = psx_upscaler_new();
   psx_upscaler_geometry_t g;
   const psx_upscaler_vertex_t *v;
   unsigned i;

   psx_upscaler_configure(u, true, 8, false);
   psx_upscaler_set_draw_area(u, 0, 0, 319, 239);
   for (i = 0; i < 3; i++)
      tri(u, 20 + 60 * i, 20, 60 + 60 * i, 20, 20 + 60 * i, 60, 0, sig(3));
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);
   /* the second of three alike is culled: the third keeps its match */
   tri(u, 21, 20, 61, 20, 21, 60, 0, sig(3));
   tri(u, 142, 22, 182, 22, 142, 62, PSX_UPSCALER_TRANSLUCENT, sig(3));
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);
   v = vertex_at(&g, 142 * SCALE + g.jitter_x, 22 * SCALE + g.jitter_y);
   CHECK(v && near(v->motion_x, 2 * SCALE, 1e-4) && near(v->motion_y, 2 * SCALE, 1e-4),
         "a primitive finds its match past one culled before it");
   CHECK(g.num_draws == 2 && g.draws[0].flags == 0 && g.draws[1].flags == PSX_UPSCALER_TRANSLUCENT
         && g.draws[0].count == 3 && g.draws[1].count == 3,
         "translucency splits the draws");
   CHECK(g.vertices[0].z > g.vertices[3].z && g.vertices[3].z > 0.0f,
         "what is drawn later is nearer");
   CHECK(g.vertices[0].depth == g.vertices[0].z, "without PGXP the depth is the order");

   /* with PGXP, a primitive's depth is its own: the far one drawn last
    * is still tested as nearer, but handed over as farther */
   tri_w(u, 20, 20, 60, 20, 20, 60, 100.0, 0, sig(3));
   tri_w(u, 142, 22, 182, 22, 142, 62, 4000.0, 0, sig(3));
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);
   CHECK(g.vertices[0].z > g.vertices[3].z, "the order is tested with PGXP too");
   CHECK(g.vertices[0].depth < 0.5f && g.vertices[3].depth > 0.5f
         && g.vertices[0].depth < g.vertices[3].depth, "PGXP's depth is handed over");
   psx_upscaler_free(u);
}

static void test_reset(void)
{
   psx_upscaler_t *u = psx_upscaler_new();
   psx_upscaler_geometry_t g;
   const psx_upscaler_vertex_t *v;

   psx_upscaler_configure(u, true, 8, false);
   psx_upscaler_set_draw_area(u, 0, 0, 319, 239);
   tri(u, 100, 50, 60, 100, 150, 150, 0, sig(7));
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);
   psx_upscaler_configure(u, true, 8, true);
   tri(u, 110, 50, 70, 100, 160, 150, 0, sig(7));
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);
   CHECK(g.reset, "a reset starts the history over");
   v = vertex_at(&g, 110 * SCALE + g.jitter_x, 50 * SCALE + g.jitter_y);
   CHECK(v && v->motion_x == 0.0f, "nothing moved after a reset");
   CHECK(near(g.jitter_x, 0.0, 1e-6), "the jitter starts over");

   psx_upscaler_configure(u, false, 8, false);
   tri(u, 110, 50, 70, 100, 160, 150, 0, sig(7));
   psx_upscaler_resolve(u, 0, 0, 320, 240, SCALE, &g);
   CHECK(g.num_vertices == 0 && g.jitter_x == 0.0f, "nothing while off");
   psx_upscaler_free(u);
}

static void test_factor(void)
{
   psx_upscaler_range_t r;
   r.want_width = 1280;
   r.min_width  = 320;  r.min_height = 240;
   r.max_width  = 2560; r.max_height = 1920;
   CHECK(psx_upscaler_factor(&r, 320, 240, 16, 0, 0, 0) == 4, "the factor nearest the wanted width");
   CHECK(psx_upscaler_factor(&r, 320, 240, 2, 0, 0, 0) == 2, "capped by the renderer's most");
   CHECK(psx_upscaler_factor(&r, 640, 480, 16, 0, 0, 0) == 2, "a wide frame takes a smaller factor");
   r.max_width = 2000;
   CHECK(psx_upscaler_factor(&r, 640, 480, 16, 4, 320, 240) == 2, "that does not fit, the factor changes");
   r.max_width = 2560;
   CHECK(psx_upscaler_factor(&r, 320, 240, 16, 2, 320, 240) == 2, "a factor that fits stands");
   CHECK(psx_upscaler_factor(&r, 320, 240, 16, 2, 640, 480) == 4, "a frame of another size takes a larger factor nearer the width");
   CHECK(psx_upscaler_factor(&r, 256, 224, 16, 4, 320, 240) == 4, "and never a smaller one");
   r.want_width = 3000;
   CHECK(psx_upscaler_factor(&r, 320, 240, 16, 0, 0, 0) == 8, "the largest that fits");
   r.min_width = 4000;
   CHECK(psx_upscaler_factor(&r, 320, 240, 16, 0, 0, 0) == 0, "none when none fits");
}

int main(void)
{
   test_inactive_and_jitter();
   test_motion();
   test_ordinals_depth_and_draws();
   test_reset();
   test_factor();
   if (s_failures)
   {
      printf("%d failure(s)\n", s_failures);
      return 1;
   }
   printf("upscaler: ok\n");
   return 0;
}
