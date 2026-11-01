/* SPDX-License-Identifier: MIT */
#ifndef RHI_UPSCALER_H
#define RHI_UPSCALER_H

/* The geometry side of a frontend temporal upscaler (DLSS, FSR, XeSS
 * through RETRO_ENVIRONMENT_GET_HW_RENDER_UPSCALER_INTERFACE): the jitter
 * each frame is drawn under and the motion vectors of what it drew.
 *
 * The PSX GPU gets screen-space primitives and no camera. Each polygon
 * and sprite drawn is kept, unjittered, under the drawing area it went
 * into, and when that area is displayed it is matched with the primitive
 * of the frame shown before that was drawn under the same state (texture,
 * palette, blending, texture coordinates, flat colour) with the same
 * ordinal among those, else with one of the same family (that state
 * less the texture coordinates and colour: an animated texture, a
 * fade) that nothing else matched; the motion vector pass draws where
 * each of its vertices went. A primitive without a match, or one that
 * went further than anything moves in a frame, stands still. Lines and fills are not
 * kept: they stand still with what is under them.
 *
 * Plain C89, no Vulkan: the renderer feeds it and draws its geometry. */

#include <stdint.h>
#include <boolean.h>
#include <retro_common_api.h>

RETRO_BEGIN_DECLS

typedef struct psx_upscaler psx_upscaler_t;

/* A vertex in native VRAM pixels, the drawing offset applied, before
 * the jitter; w is PGXP's depth of it (the GTE's view-space z) or 1
 * when there is none */
typedef struct psx_upscaler_point
{
   float x, y, w;
} psx_upscaler_point_t;

/* Blended over what is behind it (semi-transparent): the upscaler takes
 * this frame's colour there over its history */
#define PSX_UPSCALER_TRANSLUCENT (1 << 0)
/* A sprite or a rectangle: its corners are the ones of its box */
#define PSX_UPSCALER_SPRITE      (1 << 1)

/* In render pixels: where the vertex is, its order (0 the last drawn,
 * 1 the first: what the pass tests with, as the GPU draws in order),
 * its depth for the upscaler (0 near, 1 far: PGXP's where a primitive
 * has it, else its order) and how far it is from where it was the frame
 * before */
typedef struct psx_upscaler_vertex
{
   float x, y, z, depth;
   float motion_x, motion_y;
} psx_upscaler_vertex_t;

/* A run of triangles with one translucency (PSX_UPSCALER_TRANSLUCENT) */
typedef struct psx_upscaler_draw
{
   unsigned first;
   unsigned count;
   unsigned flags;
} psx_upscaler_draw_t;

/* The motion vector pass of a frame, valid until the next resolve */
typedef struct psx_upscaler_geometry
{
   const psx_upscaler_vertex_t *vertices;
   const psx_upscaler_draw_t *draws;
   unsigned num_vertices;
   unsigned num_draws;
   /* Render pixels the frame's primitives were moved by, x right, y down */
   float jitter_x;
   float jitter_y;
   /* Nothing of the frames before applies */
   bool reset;
} psx_upscaler_geometry_t;

psx_upscaler_t *psx_upscaler_new(void);
void psx_upscaler_free(psx_upscaler_t *u);

/* Before a frame. Primitives are jittered while @enable is set;
 * @jitter_phase frames make the sequence. */
void psx_upscaler_configure(psx_upscaler_t *u, bool enable,
      unsigned jitter_phase, bool reset);
bool psx_upscaler_is_active(const psx_upscaler_t *u);

/* The jitter the primitives drawn next move by, in render pixels; 0
 * while inactive */
void psx_upscaler_jitter(psx_upscaler_t *u, float *jitter_x,
      float *jitter_y);

/* GP0(E3h/E4h): the drawing area, in native VRAM pixels, inclusive */
void psx_upscaler_set_draw_area(psx_upscaler_t *u, unsigned x0,
      unsigned y0, unsigned x1, unsigned y1);

/* Before a polygon (3 or 4 vertices, a quad's in the command's order) or
 * sprite (4 corners: top left, top right, bottom left, bottom right) is
 * drawn: keeps it. @signature tells primitives drawn alike apart from
 * others (psx_upscaler_hash); @family is the signature less what the
 * same thing changes from frame to frame (texture coordinates, a flat
 * colour), matched by when nothing matches the signature. */
void psx_upscaler_primitive(psx_upscaler_t *u,
      const psx_upscaler_point_t *points, unsigned count,
      unsigned flags, uint64_t signature, uint64_t family);

/* At a scanout: the frame shown is the area of VRAM whose top left is
 * (@x, @y), @width x @height native pixels, rendered at @scale times
 * that; its image's pixel (0, 0) shows VRAM (@x, @y) */
void psx_upscaler_resolve(psx_upscaler_t *u, int x, int y,
      unsigned width, unsigned height, unsigned scale,
      psx_upscaler_geometry_t *out);

/* What the frontend's upscaler asks of a frame, in render pixels: the
 * width it wants, and the sizes it takes */
typedef struct psx_upscaler_range
{
   unsigned want_width;
   unsigned min_width, min_height;
   unsigned max_width, max_height;
} psx_upscaler_range_t;

/* The factor (1, 2, 4, 8, 16, at most @max_factor) to draw frames of
 * @w x @h native pixels at for @range: the one of those fitting whose
 * width is nearest the wanted one, the larger on a tie; 0 when none
 * fits. @current, chosen for frames of @current_w x @current_h, stands
 * while it fits, unless frames of another size have a larger factor
 * nearer the wanted width: a game entered from a wide menu is not left
 * at the factor the menu needed */
unsigned psx_upscaler_factor(const psx_upscaler_range_t *range,
      unsigned w, unsigned h, unsigned max_factor, unsigned current,
      unsigned current_w, unsigned current_h);

/* Folds @word into the signature @h; start from psx_upscaler_hash_init() */
uint64_t psx_upscaler_hash_init(void);
uint64_t psx_upscaler_hash(uint64_t h, uint32_t word);

RETRO_END_DECLS

#endif
