/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Phong tessellation for untextured 3D polygons, renderer-agnostic.
 *
 * Each eligible source triangle is subdivided independently via
 * Phong tessellation (Boubekeur & Alexa 2008).  For each sub-vertex
 * with barycentric (u, v, w):
 *
 *   p_flat    = u*A + v*B + w*C
 *   proj_X(p) = p - dot(p - X, N_X) * N_X    (project onto X's tangent plane)
 *   p_out     = u*proj_A(p_flat) + v*proj_B(p_flat) + w*proj_C(p_flat)
 *
 * Per-vertex normals N_X are read from a frame-scoped cache, keyed
 * on the source PSX command's integer screen (x, y).  First write
 * for a given (x, y) inserts the face normal of the triangle that
 * brought it; subsequent writes are ignored.  Result: every
 * triangle sharing vertex X within a frame uses the same N_X.
 *
 * Why this instead of Loop subdivision:
 *
 * PSX game artists pre-subdivide character meshes heavily to defeat
 * affine texturing and integer math; the polygon stream we receive
 * is already fine-grained tessellation, not a coarse mesh awaiting
 * smoothing.  Loop needs macro silhouette and curvature signal
 * which is not present, AND requires connected-mesh context which
 * PSX state-change fragmentation (paint-order flushes interleave
 * textured face/hair/weapon polys between untextured body slices)
 * destroys -- each batch is 2-7 micro-triangles where "boundary of
 * batch" rarely corresponds to "silhouette of mesh".  Loop's
 * boundary vertex rule applied per-batch produces visible cracks
 * because shared vertices smooth toward different neighbour sets
 * in different batches.
 *
 * Phong tess is structurally crack-free:
 *   - On edge AB (w=0): the contribution of C drops out entirely;
 *     p_out depends only on A, B, N_A, N_B.  Adjacent triangles
 *     ABC and ABD that share AB produce identical edge subdivisions
 *     regardless of which batch each is in, provided their cached
 *     N_A and N_B agree -- which first-write-wins guarantees.
 *   - At corner A (u=1, v=w=0): proj_A(A) = A.  Corners don't move.
 *   - Zero normal at X: proj_X is identity, X contributes flat.
 *     Pin-hints inserted by ineligible neighbours force flat
 *     tessellation along edges shared with ineligible geometry,
 *     so subdivided side meets unsubdivided side exactly.
 *
 * Identity is keyed on integer source (x, y) (GTE projection output,
 * bit-stable across submissions for the same logical vertex).  Same
 * scheme as the SW subdivision path; works for both PGXP-valid
 * eligible polys and PGXP-invalid ineligible-poly pin hints.
 *
 * Cost when disabled: a single integer compare at the polygon-path
 * eligibility gate.  All allocations and state are lazy from first
 * push.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <libretro.h>

#include "gpu_subdiv.h"

extern int psx_gpu_subdivision_level;

extern retro_log_printf_t log_cb;

/* ---------------------------------------------------------------------
 * Diagnostic infrastructure (compile-time gated)
 * ------------------------------------------------------------------ */

#ifndef TT_SUBDIV_DEBUG
#define TT_SUBDIV_DEBUG 0
#endif

#if TT_SUBDIV_DEBUG
static FILE    *sd_dbg_file        = NULL;
static uint32_t sd_dbg_push_count  = 0;
static uint32_t sd_dbg_flush_count = 0;

static void sd_dbg_open(void)
{
   if (sd_dbg_file != NULL)
      return;
   sd_dbg_file = fopen("subdiv_debug.txt", "w");
   if (log_cb)
      log_cb(RETRO_LOG_INFO,
            "[subdiv] diagnostic active (TT_SUBDIV_DEBUG=1, "
            "file=subdiv_debug.txt %s)\n",
            sd_dbg_file ? "OPEN" : "FAILED");
}

#define SD_DBG_LOG(fmt, ...) do { \
   if (sd_dbg_file) { \
      fprintf(sd_dbg_file, fmt, ##__VA_ARGS__); \
      fflush(sd_dbg_file); \
   } \
   if (log_cb) \
      log_cb(RETRO_LOG_INFO, fmt, ##__VA_ARGS__); \
} while (0)
#endif

/* ---------------------------------------------------------------------
 * Internal data model
 * ------------------------------------------------------------------ */

/* Sub-vertex in tessellation working space.  Positions are PGXP
 * precise[] coordinates; colour is barycentric-interpolated from
 * the parent corners as a fallback (sd_emit_one overrides with a
 * perspective-correct sample from the parent's exact gradient). */
typedef struct
{
   float px, py, pw;
   float r,  g,  b;
} sd_vertex;

/* Deferred submission record.  Untextured eligible polygons are
 * buffered here between flushes; flush runs Phong tessellation on
 * each and hands the sub-triangles to the emit callback. */
typedef struct
{
   tri_vertex v[3];
   uint8_t    flags;
} sd_pending_tri;

static sd_pending_tri *S_pending;
static uint32_t        S_pending_count;

/* Combined bounding box of all pending triangles' integer screen
 * coords.  Used by tt_subdiv_would_overlap (eye-on-face detection,
 * strict containment) and tt_subdiv_would_intersect_ineligible
 * (paint-order protection, intersection test). */
static int32_t S_pending_min_x;
static int32_t S_pending_min_y;
static int32_t S_pending_max_x;
static int32_t S_pending_max_y;

/* Per-vertex normal cache, frame-scoped.  Keyed on integer source
 * screen (x, y); stores either a unit normal (proper Phong vertex
 * normal: area-weighted average of face normals from every batch
 * triangle touching the vertex) or a zero normal (pin-hint marker
 * for ineligible neighbours -- forces flat tessellation at that
 * vertex so adjacent eligible mesh meets the unsubdivided side
 * exactly).
 *
 * Three-state lifecycle per slot:
 *   0: empty.
 *   1: accumulating.  nx,ny,nz hold an unnormalised running sum of
 *      face-normal cross products from every triangle in the
 *      current batch that has touched this vertex during Pass A
 *      (the accumulate pass at the head of tt_subdiv_flush).
 *      Pin-hints from previous batches do not enter this state.
 *   2: committed.  nx,ny,nz hold either a unit normal (the
 *      accumulated sum normalised on first read in Pass B) or a
 *      zero normal (from tt_subdiv_add_pin_hints).  Subsequent
 *      accumulations at this slot are skipped (first-batch-wins
 *      cross-batch).
 *
 * Averaging multiple face normals per vertex is what makes Phong
 * tessellation visually stable.  A single triangle's face normal
 * is highly sensitive to PGXP precise[] floating-point noise:
 * cross products mix screen-pixel-scale edge components with 1/w
 * (precise[2]) components ~1e-4 in magnitude, so a 0.5-unit jitter
 * in precise[0..1] propagates to 20-30% relative noise in the
 * x/y components of the unit normal, rotating it by several
 * degrees per frame and producing visible position wobble after
 * Phong projection.  Summing N face normals reduces this noise by
 * ~sqrt(N); the area weighting (unnormalised cross products
 * naturally carry triangle area) gives larger triangles greater
 * influence, which is what proper smoothed vertex normals do. */
typedef struct
{
   int32_t x, y;
   float   nx, ny, nz;
   uint8_t valid;
} sd_nc_entry;

static sd_nc_entry *S_nc;
static uint32_t     S_nc_size;
static uint32_t     S_nc_mask;

/* ---------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */

static uint32_t sd_hash_xy(int32_t x, int32_t y)
{
   uint32_t h  = (uint32_t)x * 0x9E3779B1u;
   h          ^= (uint32_t)y * 0x85EBCA77u;
   h          ^= h >> 16;
   h          *= 0x7FEB352Du;
   h          ^= h >> 15;
   return h;
}

/* ---------------------------------------------------------------------
 * Normal cache
 * ------------------------------------------------------------------ */

#define SD_NC_INITIAL_SIZE 16384u
#define SD_NC_PROBE_MAX    32u

static void sd_nc_ensure(void)
{
   if (S_nc == NULL)
   {
      S_nc_size = SD_NC_INITIAL_SIZE;
      S_nc_mask = S_nc_size - 1u;
      S_nc      = (sd_nc_entry *)calloc(S_nc_size, sizeof(sd_nc_entry));
   }
}

static void sd_nc_clear(void)
{
   if (S_nc != NULL)
      memset(S_nc, 0, S_nc_size * sizeof(sd_nc_entry));
}

/* Pass A primitive: accumulate one face normal contribution at
 * (x, y).  Only touches empty (creates accumulating slot) or
 * accumulating (adds to running sum) slots; committed slots are
 * left alone, which is how first-batch-wins is enforced
 * cross-batch and how pin-hints take precedence within a batch.
 * Linear probing bounded by SD_NC_PROBE_MAX; probe overflow
 * degrades to "this contribution is dropped" which keeps the
 * cache consistent (the cached vertex normal just averages fewer
 * face normals than it might have) at the cost of slightly noisier
 * tessellation at that vertex. */
static void sd_nc_accumulate(int32_t x, int32_t y,
      float cx, float cy, float cz)
{
   uint32_t h, step;
   sd_nc_ensure();
   h = sd_hash_xy(x, y);
   for (step = 0; step < SD_NC_PROBE_MAX; step++)
   {
      sd_nc_entry *e = &S_nc[(h + step) & S_nc_mask];
      if (!e->valid)
      {
         e->valid = 1;
         e->x     = x;
         e->y     = y;
         e->nx    = cx;
         e->ny    = cy;
         e->nz    = cz;
         return;
      }
      if (e->x == x && e->y == y)
      {
         if (e->valid == 1)
         {
            e->nx += cx;
            e->ny += cy;
            e->nz += cz;
         }
         return;
      }
   }
}

/* Pass B primitive: read the unit normal at (x, y), normalising in
 * place and committing on first read.  Returns zero if the slot
 * is missing (vertex was never accumulated, e.g. probe overflow)
 * or the accumulated sum was degenerate.  Subsequent reads of a
 * committed slot return the same value. */
static void sd_nc_read_commit(int32_t x, int32_t y,
      float *out_nx, float *out_ny, float *out_nz)
{
   uint32_t h, step;
   sd_nc_ensure();
   h = sd_hash_xy(x, y);
   for (step = 0; step < SD_NC_PROBE_MAX; step++)
   {
      sd_nc_entry *e = &S_nc[(h + step) & S_nc_mask];
      if (!e->valid)
      {
         *out_nx = 0.0f;
         *out_ny = 0.0f;
         *out_nz = 0.0f;
         return;
      }
      if (e->x == x && e->y == y)
      {
         if (e->valid == 1)
         {
            float len2 = e->nx * e->nx + e->ny * e->ny + e->nz * e->nz;
            if (len2 > 1e-12f)
            {
               float inv = 1.0f / sqrtf(len2);
               e->nx *= inv;
               e->ny *= inv;
               e->nz *= inv;
            }
            else
            {
               e->nx = 0.0f;
               e->ny = 0.0f;
               e->nz = 0.0f;
            }
            e->valid = 2;
         }
         *out_nx = e->nx;
         *out_ny = e->ny;
         *out_nz = e->nz;
         return;
      }
   }
   *out_nx = 0.0f;
   *out_ny = 0.0f;
   *out_nz = 0.0f;
}

/* Pin-hint: commit a zero normal at (x, y), but only if the slot
 * is empty.  First-write-wins: if any eligible accumulation has
 * already started here in a previous batch, the pin is too late
 * (that batch's geometry is already emitted with a real normal,
 * and the crack is unavoidable from this submission ordering).
 * The polygon-path's tt_subdiv_would_intersect_ineligible test is
 * what prevents the eligibles-already-flushed-then-ineligible-
 * neighbour case from arising in normal play. */
static void sd_nc_insert_zero(int32_t x, int32_t y)
{
   uint32_t h, step;
   sd_nc_ensure();
   h = sd_hash_xy(x, y);
   for (step = 0; step < SD_NC_PROBE_MAX; step++)
   {
      sd_nc_entry *e = &S_nc[(h + step) & S_nc_mask];
      if (!e->valid)
      {
         e->valid = 2;
         e->x     = x;
         e->y     = y;
         e->nx    = 0.0f;
         e->ny    = 0.0f;
         e->nz    = 0.0f;
         return;
      }
      if (e->x == x && e->y == y)
         return;
   }
}

/* ---------------------------------------------------------------------
 * Face normal
 *
 * Cross product of two edges in (precise[0], precise[1], precise[2])
 * space.  precise[2] is PGXP's 1/w, providing a monotonic depth
 * coordinate; the cross product direction is consistent with the
 * surface's actual 3D orientation up to a scale, which is all
 * Phong tessellation needs.
 *
 * The raw form returns the unnormalised cross product, used by
 * Pass A accumulation -- magnitude reflects (precise-space)
 * triangle area, so summing raw cross products from every triangle
 * touching a vertex yields a proper area-weighted vertex normal
 * after a single normalisation in Pass B.
 * ------------------------------------------------------------------ */

static void sd_face_normal_raw(const tri_vertex *v0, const tri_vertex *v1,
      const tri_vertex *v2, float *cx, float *cy, float *cz)
{
   float ax = v1->precise[0] - v0->precise[0];
   float ay = v1->precise[1] - v0->precise[1];
   float aw = v1->precise[2] - v0->precise[2];
   float bx = v2->precise[0] - v0->precise[0];
   float by = v2->precise[1] - v0->precise[1];
   float bw = v2->precise[2] - v0->precise[2];
   *cx = ay * bw - aw * by;
   *cy = aw * bx - ax * bw;
   *cz = ax * by - ay * bx;
}

/* ---------------------------------------------------------------------
 * Eligibility / lifecycle / push / overlap
 * ------------------------------------------------------------------ */

bool tt_subdiv_is_eligible(const tri_vertex *v, uint8_t flags)
{
   (void)v;
   /* Reject textured polys (UV refinement not implemented).  Reject
    * PGXP-invalid polys (Phong needs sub-pixel positions and the
    * 1/w depth axis from precise[2]). */
   if (flags & TT_SUBDIV_F_TEXTURED)
      return false;
   if (!(flags & TT_SUBDIV_F_PGXP_VALID))
      return false;
   return true;
}

void tt_subdiv_init(void)
{
   if (S_pending == NULL)
   {
      S_pending = (sd_pending_tri *)calloc(TT_SUBDIV_MAX_PENDING_TRIS,
            sizeof(sd_pending_tri));
      S_pending_count = 0;
   }
   sd_nc_ensure();
#if TT_SUBDIV_DEBUG
   sd_dbg_open();
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "[subdiv] tt_subdiv_init\n");
#endif
}

void tt_subdiv_shutdown(void)
{
   free(S_pending);
   S_pending       = NULL;
   S_pending_count = 0;
   free(S_nc);
   S_nc            = NULL;
   S_nc_size       = 0;
   S_nc_mask       = 0;
#if TT_SUBDIV_DEBUG
   if (sd_dbg_file)
   {
      fclose(sd_dbg_file);
      sd_dbg_file = NULL;
   }
#endif
}

int tt_subdiv_has_pending(void)
{
   return S_pending_count > 0;
}

bool tt_subdiv_push(const tri_vertex *vertices, uint8_t flags)
{
   sd_pending_tri *p;
   int i;
   if (S_pending == NULL)
      tt_subdiv_init();
   if (S_pending_count >= TT_SUBDIV_MAX_PENDING_TRIS)
      return false;
   p        = &S_pending[S_pending_count++];
   p->v[0]  = vertices[0];
   p->v[1]  = vertices[1];
   p->v[2]  = vertices[2];
   p->flags = flags;
   if (S_pending_count == 1)
   {
      S_pending_min_x = vertices[0].x;
      S_pending_min_y = vertices[0].y;
      S_pending_max_x = vertices[0].x;
      S_pending_max_y = vertices[0].y;
   }
   for (i = (S_pending_count == 1) ? 1 : 0; i < 3; i++)
   {
      if (vertices[i].x < S_pending_min_x) S_pending_min_x = vertices[i].x;
      if (vertices[i].y < S_pending_min_y) S_pending_min_y = vertices[i].y;
      if (vertices[i].x > S_pending_max_x) S_pending_max_x = vertices[i].x;
      if (vertices[i].y > S_pending_max_y) S_pending_max_y = vertices[i].y;
   }
#if TT_SUBDIV_DEBUG
   sd_dbg_push_count++;
   if (sd_dbg_push_count <= 3u)
   {
      SD_DBG_LOG("[subdiv] tt_subdiv_push #%u (pending was %u)\n",
            sd_dbg_push_count, S_pending_count - 1);
   }
#endif
   return true;
}

void tt_subdiv_add_pin_hints(const tri_vertex *vertices)
{
   int i;
   for (i = 0; i < 3; i++)
      sd_nc_insert_zero(vertices[i].x, vertices[i].y);
}

bool tt_subdiv_would_overlap(const tri_vertex *vertices)
{
   int32_t x0, y0, x1, y1, x2, y2;
   int32_t minx, miny, maxx, maxy;
   if (S_pending_count == 0)
      return false;
   x0 = vertices[0].x; y0 = vertices[0].y;
   x1 = vertices[1].x; y1 = vertices[1].y;
   x2 = vertices[2].x; y2 = vertices[2].y;
   minx = x0; if (x1 < minx) minx = x1; if (x2 < minx) minx = x2;
   miny = y0; if (y1 < miny) miny = y1; if (y2 < miny) miny = y2;
   maxx = x0; if (x1 > maxx) maxx = x1; if (x2 > maxx) maxx = x2;
   maxy = y0; if (y1 > maxy) maxy = y1; if (y2 > maxy) maxy = y2;
   return    minx >= S_pending_min_x
          && miny >= S_pending_min_y
          && maxx <= S_pending_max_x
          && maxy <= S_pending_max_y;
}

bool tt_subdiv_would_intersect_ineligible(const tri_vertex *vertices)
{
   int32_t x0, y0, x1, y1, x2, y2;
   int32_t minx, miny, maxx, maxy;
   if (S_pending_count == 0)
      return false;
   x0 = vertices[0].x; y0 = vertices[0].y;
   x1 = vertices[1].x; y1 = vertices[1].y;
   x2 = vertices[2].x; y2 = vertices[2].y;
   minx = x0; if (x1 < minx) minx = x1; if (x2 < minx) minx = x2;
   miny = y0; if (y1 < miny) miny = y1; if (y2 < miny) miny = y2;
   maxx = x0; if (x1 > maxx) maxx = x1; if (x2 > maxx) maxx = x2;
   maxy = y0; if (y1 > maxy) maxy = y1; if (y2 > maxy) maxy = y2;
   return !(   maxx < S_pending_min_x
            || minx > S_pending_max_x
            || maxy < S_pending_min_y
            || miny > S_pending_max_y);
}

/* ---------------------------------------------------------------------
 * Emission: project precise[] -> integer screen, perspective-correct
 * gouraud sample from parent, hand to caller's emit callback
 *
 * Each sub-triangle vertex carries its precise[] position; the
 * rasteriser-facing tri_vertex needs integer screen coords plus a
 * colour.  Colour is sampled from the PARENT's exact perspective-
 * correct gradient using the sub-vertex's screen-space barycentric
 * coordinates within the parent triangle.  This guarantees that
 * sub-triangles sharing an edge (whether within one parent or
 * between two parents that share a source edge) get identical
 * colour samples on the shared edge, eliminating gouraud seams.
 * ------------------------------------------------------------------ */

static void sd_emit_one(PS_GPU *gpu, const sd_vertex *v0,
      const sd_vertex *v1, const sd_vertex *v2, uint8_t flags,
      const tri_vertex *parent_vertices,
      tt_subdiv_emit_fn emit, void *tag)
{
   tri_vertex out[3];
   const sd_vertex *src[3];
   int i;
   float p0x      = parent_vertices[0].precise[0];
   float p0y      = parent_vertices[0].precise[1];
   float p1x      = parent_vertices[1].precise[0];
   float p1y      = parent_vertices[1].precise[1];
   float p2x      = parent_vertices[2].precise[0];
   float p2y      = parent_vertices[2].precise[1];
   float inv_w0   = (parent_vertices[0].precise[2] > 1e-6f) ? 1.0f / parent_vertices[0].precise[2] : 1.0f;
   float inv_w1   = (parent_vertices[1].precise[2] > 1e-6f) ? 1.0f / parent_vertices[1].precise[2] : 1.0f;
   float inv_w2   = (parent_vertices[2].precise[2] > 1e-6f) ? 1.0f / parent_vertices[2].precise[2] : 1.0f;
   float r0_w     = (float)parent_vertices[0].r * inv_w0;
   float g0_w     = (float)parent_vertices[0].g * inv_w0;
   float b0_w     = (float)parent_vertices[0].b * inv_w0;
   float r1_w     = (float)parent_vertices[1].r * inv_w1;
   float g1_w     = (float)parent_vertices[1].g * inv_w1;
   float b1_w     = (float)parent_vertices[1].b * inv_w1;
   float r2_w     = (float)parent_vertices[2].r * inv_w2;
   float g2_w     = (float)parent_vertices[2].g * inv_w2;
   float b2_w     = (float)parent_vertices[2].b * inv_w2;
   float area2    = (p1x - p0x) * (p2y - p0y) - (p2x - p0x) * (p1y - p0y);
   float inv_area2 = (fabsf(area2) > 1e-6f) ? 1.0f / area2 : 0.0f;

   src[0] = v0; src[1] = v1; src[2] = v2;
   for (i = 0; i < 3; i++)
   {
      const sd_vertex *s = src[i];
      tri_vertex      *d = &out[i];
      float b0, b1, b2;
      float inv_w_at;
      float r, g, b;
      d->x = (int32_t)floorf(s->px + 0.5f);
      d->y = (int32_t)floorf(s->py + 0.5f);
      d->u = 0;
      d->v = 0;

      if (inv_area2 != 0.0f)
      {
         b1 = ((s->px - p0x) * (p2y - p0y) - (p2x - p0x) * (s->py - p0y)) * inv_area2;
         b2 = ((p1x - p0x) * (s->py - p0y) - (s->px - p0x) * (p1y - p0y)) * inv_area2;
         b0 = 1.0f - b1 - b2;
      }
      else
      {
         b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
      }

      inv_w_at = b0 * inv_w0 + b1 * inv_w1 + b2 * inv_w2;
      if (inv_w_at > 1e-6f)
      {
         float inv_inv_w = 1.0f / inv_w_at;
         float r_over_w  = b0 * r0_w + b1 * r1_w + b2 * r2_w;
         float g_over_w  = b0 * g0_w + b1 * g1_w + b2 * g2_w;
         float b_over_w  = b0 * b0_w + b1 * b1_w + b2 * b2_w;
         r = r_over_w * inv_inv_w;
         g = g_over_w * inv_inv_w;
         b = b_over_w * inv_inv_w;
      }
      else
      {
         r = s->r; g = s->g; b = s->b;
      }
      if (r < 0.0f) r = 0.0f; else if (r > 255.0f) r = 255.0f;
      if (g < 0.0f) g = 0.0f; else if (g > 255.0f) g = 255.0f;
      if (b < 0.0f) b = 0.0f; else if (b > 255.0f) b = 255.0f;
      d->r = (uint8_t)r;
      d->g = (uint8_t)g;
      d->b = (uint8_t)b;
      d->precise[0] = s->px;
      d->precise[1] = s->py;
      d->precise[2] = s->pw;
   }
   emit(gpu, out, flags, parent_vertices, tag);
}

/* ---------------------------------------------------------------------
 * Phong tessellation kernel
 *
 * Max level 3 gives N = 8 edge subdivisions = 64 sub-triangles per
 * source triangle = 45 sub-vertices.  Scratch is file-scope static
 * (single-threaded GPU context).
 *
 * SD_PHONG_ALPHA blends between flat barycentric position and the
 * full Phong-projected position:
 *
 *   p_out = (1 - alpha) * p_flat + alpha * p_phong
 *
 * alpha=1 is full Phong (maximum smoothing, maximum noise
 * amplification on jittery face normals).  alpha=0 is flat
 * (no subdivision benefit).  alpha=0.5 is a practical middle that
 * keeps visible smoothing while halving the displacement
 * amplitude -- and therefore halving the cross-frame wobble
 * amplitude on animated characters where the cache resets each
 * frame and face normals re-derive from jittery precise[] inputs.
 * Crack-freeness and corner-exactness are both preserved under
 * the alpha blend (linear combination of two per-vertex Phong-like
 * functions is itself per-vertex; edge midpoints still depend
 * only on the two edge endpoints' positions and normals; corners
 * still satisfy proj_X(X) = X). */

#define SD_MAX_LEVEL  3
#define SD_MAX_N      (1 << SD_MAX_LEVEL)
#define SD_MAX_SUBV   ((SD_MAX_N + 1) * (SD_MAX_N + 2) / 2)
#ifndef SD_PHONG_ALPHA
#define SD_PHONG_ALPHA 0.5f
#endif

static sd_vertex sd_subv[SD_MAX_SUBV];
static int       sd_row_start[SD_MAX_N + 2];

static void sd_tessellate_phong(PS_GPU *gpu, const sd_pending_tri *pt,
      int levels, tt_subdiv_emit_fn emit, void *tag,
      uint32_t *dbg_out_count)
{
   const tri_vertex *V0 = &pt->v[0];
   const tri_vertex *V1 = &pt->v[1];
   const tri_vertex *V2 = &pt->v[2];
   float Ax, Ay, Aw, Bx, By, Bw, Cx, Cy, Cw;
   float Ar, Ag, Ab, Br, Bg, Bb, Cr, Cg, Cb;
   float n0x, n0y, n0z;
   float n1x, n1y, n1z;
   float n2x, n2y, n2z;
   int N;
   int i, j;
   int idx;
   float invN;
   int total_rows;

   if (levels < 1)
   {
      /* Defensive: caller should have gated on level > 0.  Pass
       * through as a single sub-triangle so nothing is dropped. */
      sd_vertex v[3];
      v[0].px = V0->precise[0]; v[0].py = V0->precise[1]; v[0].pw = V0->precise[2];
      v[0].r  = (float)V0->r;   v[0].g  = (float)V0->g;   v[0].b  = (float)V0->b;
      v[1].px = V1->precise[0]; v[1].py = V1->precise[1]; v[1].pw = V1->precise[2];
      v[1].r  = (float)V1->r;   v[1].g  = (float)V1->g;   v[1].b  = (float)V1->b;
      v[2].px = V2->precise[0]; v[2].py = V2->precise[1]; v[2].pw = V2->precise[2];
      v[2].r  = (float)V2->r;   v[2].g  = (float)V2->g;   v[2].b  = (float)V2->b;
      sd_emit_one(gpu, &v[0], &v[1], &v[2], pt->flags, pt->v, emit, tag);
      (*dbg_out_count)++;
      return;
   }
   if (levels > SD_MAX_LEVEL)
      levels = SD_MAX_LEVEL;
   N = 1 << levels;

   Ax = V0->precise[0]; Ay = V0->precise[1]; Aw = V0->precise[2];
   Bx = V1->precise[0]; By = V1->precise[1]; Bw = V1->precise[2];
   Cx = V2->precise[0]; Cy = V2->precise[1]; Cw = V2->precise[2];
   Ar = (float)V0->r; Ag = (float)V0->g; Ab = (float)V0->b;
   Br = (float)V1->r; Bg = (float)V1->g; Bb = (float)V1->b;
   Cr = (float)V2->r; Cg = (float)V2->g; Cb = (float)V2->b;

   /* Vertex normals were accumulated in Pass A; read & normalise
    * (first read commits the slot, subsequent reads return the
    * cached unit vector). */
   sd_nc_read_commit(V0->x, V0->y, &n0x, &n0y, &n0z);
   sd_nc_read_commit(V1->x, V1->y, &n1x, &n1y, &n1z);
   sd_nc_read_commit(V2->x, V2->y, &n2x, &n2y, &n2z);

   /* Triangular grid row offsets.  Row i (0 <= i <= N) holds
    * (N - i + 1) vertices at j in [0, N - i].  Linear index for
    * (i, j) is sd_row_start[i] + j. */
   total_rows = N + 1;
   sd_row_start[0] = 0;
   for (i = 1; i <= total_rows; i++)
      sd_row_start[i] = sd_row_start[i - 1] + (N - i + 2);

   /* Generate sub-vertex positions via Phong projection, blended
    * with flat barycentric position at SD_PHONG_ALPHA. */
   invN = 1.0f / (float)N;
   idx  = 0;
   for (i = 0; i <= N; i++)
   {
      for (j = 0; j <= N - i; j++)
      {
         float u     = (float)(N - i - j) * invN;
         float v     = (float)i           * invN;
         float w     = (float)j           * invN;
         float fx    = u * Ax + v * Bx + w * Cx;
         float fy    = u * Ay + v * By + w * Cy;
         float fw    = u * Aw + v * Bw + w * Cw;
         float dot_a = (fx - Ax) * n0x + (fy - Ay) * n0y + (fw - Aw) * n0z;
         float dot_b = (fx - Bx) * n1x + (fy - By) * n1y + (fw - Bw) * n1z;
         float dot_c = (fx - Cx) * n2x + (fy - Cy) * n2y + (fw - Cw) * n2z;
         float pa_x  = fx - dot_a * n0x;
         float pa_y  = fy - dot_a * n0y;
         float pa_w  = fw - dot_a * n0z;
         float pb_x  = fx - dot_b * n1x;
         float pb_y  = fy - dot_b * n1y;
         float pb_w  = fw - dot_b * n1z;
         float pc_x  = fx - dot_c * n2x;
         float pc_y  = fy - dot_c * n2y;
         float pc_w  = fw - dot_c * n2z;
         float phong_x = u * pa_x + v * pb_x + w * pc_x;
         float phong_y = u * pa_y + v * pb_y + w * pc_y;
         float phong_w = u * pa_w + v * pb_w + w * pc_w;
         sd_vertex *sv = &sd_subv[idx++];
         sv->px = (1.0f - SD_PHONG_ALPHA) * fx + SD_PHONG_ALPHA * phong_x;
         sv->py = (1.0f - SD_PHONG_ALPHA) * fy + SD_PHONG_ALPHA * phong_y;
         sv->pw = (1.0f - SD_PHONG_ALPHA) * fw + SD_PHONG_ALPHA * phong_w;
         sv->r  = u * Ar + v * Br + w * Cr;
         sv->g  = u * Ag + v * Bg + w * Cg;
         sv->b  = u * Ab + v * Bb + w * Cb;
      }
   }

   /* Emit sub-triangles.  Winding (i, j) -> (i+1, j) -> (i, j+1)
    * maps to A-direction -> B-direction -> C-direction in
    * barycentric space; the affine baryc->screen map preserves
    * orientation, so parent winding is propagated correctly to
    * every sub-triangle. */
   for (i = 0; i < N; i++)
   {
      for (j = 0; j < N - i; j++)
      {
         int v_a = sd_row_start[i]     + j;
         int v_b = sd_row_start[i + 1] + j;
         int v_c = sd_row_start[i]     + j + 1;
         sd_emit_one(gpu, &sd_subv[v_a], &sd_subv[v_b], &sd_subv[v_c],
               pt->flags, pt->v, emit, tag);
         (*dbg_out_count)++;
         if (j < N - i - 1)
         {
            int d_a = sd_row_start[i + 1] + j;
            int d_b = sd_row_start[i + 1] + j + 1;
            int d_c = sd_row_start[i]     + j + 1;
            sd_emit_one(gpu, &sd_subv[d_a], &sd_subv[d_b], &sd_subv[d_c],
                  pt->flags, pt->v, emit, tag);
            (*dbg_out_count)++;
         }
      }
   }
}

/* ---------------------------------------------------------------------
 * Public flush + frame-end
 * ------------------------------------------------------------------ */

void tt_subdiv_flush(PS_GPU *gpu, tt_subdiv_emit_fn emit, void *tag)
{
   int      levels;
   uint32_t i;
   uint32_t dbg_pending   = S_pending_count;
   uint32_t dbg_out_count = 0;

   if (S_pending_count == 0)
      return;

   levels = psx_gpu_subdivision_level;
   if (levels < 0)            levels = 0;
   if (levels > SD_MAX_LEVEL) levels = SD_MAX_LEVEL;

   /* Pass A: accumulate unnormalised face normals at every
    * triangle's three corner slots.  Empty slots create
    * accumulating entries; existing accumulating entries sum in
    * this triangle's contribution; committed slots (pin-hints
    * from the ineligible-poly branch, or accumulations committed
    * by a previous batch in the same frame) are left alone.  The
    * sum of unnormalised cross products is an area-weighted
    * vertex normal, which is what proper smoothed Phong
    * tessellation uses; the averaging is what reduces single-
    * triangle face-normal jitter to a stable per-vertex value
    * across frames. */
   for (i = 0; i < S_pending_count; i++)
   {
      sd_pending_tri *pt = &S_pending[i];
      float cx, cy, cz;
      sd_face_normal_raw(&pt->v[0], &pt->v[1], &pt->v[2], &cx, &cy, &cz);
      sd_nc_accumulate(pt->v[0].x, pt->v[0].y, cx, cy, cz);
      sd_nc_accumulate(pt->v[1].x, pt->v[1].y, cx, cy, cz);
      sd_nc_accumulate(pt->v[2].x, pt->v[2].y, cx, cy, cz);
   }

   /* Pass B: tessellate each triangle.  sd_tessellate_phong
    * reads the now-accumulated cache slots via sd_nc_read_commit,
    * which normalises on first read and commits the slot;
    * subsequent triangles reading the same slot get the same
    * unit vector. */
   for (i = 0; i < S_pending_count; i++)
      sd_tessellate_phong(gpu, &S_pending[i], levels, emit, tag,
            &dbg_out_count);

   S_pending_count = 0;

#if TT_SUBDIV_DEBUG
   sd_dbg_flush_count++;
   if (sd_dbg_flush_count <= 50u ||
       (sd_dbg_flush_count % 100u) == 0u)
   {
      SD_DBG_LOG("[subdiv] flush #%u pending=%u out=%u level=%d\n",
            sd_dbg_flush_count, dbg_pending, dbg_out_count, levels);
   }
#else
   (void)dbg_pending;
   (void)dbg_out_count;
#endif
}

void tt_subdiv_frame_end(void)
{
   sd_nc_clear();
}
