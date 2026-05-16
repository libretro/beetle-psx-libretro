/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Phong tessellation for untextured 3D polygons, renderer-agnostic.
 *
 * Each eligible source triangle is subdivided independently via
 * Phong tessellation (Boubekeur & Alexa 2008).  For each sub-vertex
 * with barycentric (u, v, w):
 *
 *   p_flat    = u*A + v*B + w*C
 *   proj_X(p) = p - dot(p - X, N_X) * N_X
 *   p_out     = u*proj_A(p_flat) + v*proj_B(p_flat) + w*proj_C(p_flat)
 *
 * Per-vertex normals N_X are reconstructed from face normals via
 * a frame-scoped first-write-wins cache, keyed on the source PSX
 * command's integer screen (x, y) -- bit-stable across submissions
 * for the same logical 3D point (GTE projection is deterministic).
 *
 * Phong tess is structurally crack-free: edge midpoints depend only
 * on the two edge endpoints' cached normals (third corner's
 * barycentric weight is zero on the edge), and corners don't move
 * (proj_X(X) = X).  Adjacent triangles ABC and ABD that share edge
 * AB produce identical edge subdivisions whenever their cached N_A
 * and N_B agree -- which first-write-wins guarantees.  A zero
 * normal in the cache yields flat tessellation at that vertex;
 * tt_subdiv_add_pin_hints inserts zero normals for ineligible-
 * polygon vertices so subdivided edges meet unsubdivided neighbours
 * exactly.
 *
 * Why this instead of Loop subdivision:  PSX game artists
 * pre-subdivide character meshes heavily to defeat affine
 * texturing and integer math.  The polygon stream we receive is
 * already fine-grained tessellation, not a coarse mesh awaiting
 * smoothing.  Loop needs macro silhouette and curvature signal
 * (not present) AND connected-mesh context (destroyed by PSX
 * state-change fragmentation that interleaves textured and
 * untextured polys into 2-7 micro-triangle batches).  Phong
 * tessellation needs neither -- it operates per-triangle and only
 * uses cross-batch context through a single per-vertex normal
 * lookup that is bit-stable by construction.
 *
 * Design constraints:
 *   - Zero runtime cost when disabled (psx_gpu_subdivision_level==0).
 *     All hot-path hooks are predicated on that single integer; when
 *     it is zero, the existing rasteriser path runs unchanged with no
 *     extra calls, allocations, or state to touch.
 *   - Strictly opt-in via the libretro core option.
 *   - Renderer-agnostic.  The push/flush API hands subdivided
 *     children to a caller-supplied emit callback; SW and HW
 *     backends each provide their own callback.
 *
 * Lifetime:
 *   - tt_subdiv_init() allocates the pending buffer and normal
 *     cache.  Lazy from the first push().  Safe to call defensively.
 *   - tt_subdiv_shutdown() frees everything.
 *   - tt_subdiv_frame_end() clears the per-vertex normal cache.
 *     Must be called once per frame (after the end-of-frame flush)
 *     so animated meshes get fresh normals; the cache being
 *     frame-scoped is what makes first-write-wins safe to use as
 *     a deterministic source of cross-triangle consistency.
 *
 * Use from the polygon path:
 *   - tt_subdiv_push() takes a triangle already normalised to
 *     (vertices[3], flags).  Returns true if accepted, false on
 *     buffer-full (caller should fall back to direct rasterisation).
 *     Callers must check psx_gpu_subdivision_level and
 *     tt_subdiv_is_eligible() before calling.
 *   - tt_subdiv_flush() tessellates every pending triangle and
 *     emits sub-triangles via the rasteriser callback.  Idempotent
 *     on an empty buffer.
 *
 * The renderer must call tt_subdiv_flush() on every state change
 * that could invalidate the assumption that buffered polygons are
 * part of one coherent mesh: texpage, CLUT, drawing-area, drawing-
 * offset, mask, dither, FBWrite/FBFill/FBCopy, sprite, line, display
 * flip, end of frame.  In practice this is wired by calling
 * tt_subdiv_flush_if_pending() at the head of every command other
 * than the polygon path itself.
 */

#ifndef GPU_SUBDIV_H__
#define GPU_SUBDIV_H__

#include <stdbool.h>
#include <stdint.h>

#include "gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum triangles held in the deferred buffer.  Hit on overflow
 * triggers an early flush.  Sized to cover a single character mesh's
 * worth of draws without forcing flushes mid-mesh.
 */
#define TT_SUBDIV_MAX_PENDING_TRIS 1024

/* Caller-supplied callback that rasterises a single refined
 * triangle.  Receives the sub-triangle's vertices (positions
 * already produced by Phong tessellation, colours sampled
 * perspective-correctly from the parent gradient), the original
 * draw-flags, and the THREE PARENT VERTICES of the original
 * (un-subdivided) input triangle from which this sub-triangle
 * descends.
 *
 * The parent vertices let the rasteriser callback re-derive the
 * parent's gouraud (and UV, if textured) gradient and apply it
 * to all of the parent's descendants identically.  This is what
 * eliminates the network of seams that appears when each child
 * triangle independently computes its own gradient with fixed-
 * point rounding -- two adjacent children of one parent share an
 * edge along which the parent's gradient is mathematically
 * continuous, but per-triangle fixed-point rounding makes the
 * children's reconstructed gradients disagree by a few colour
 * units, producing a visible seam.  Sharing the parent's
 * gradient guarantees exact continuity across the edge.
 *
 * `parent_vertices` points to a 3-element tri_vertex array
 * holding the original parent's vertices in their original order
 * (NOT Y-sorted).  Callback should compute deltas from these
 * before doing any sorting/mutation of `vertices`.
 *
 * `tag` is an opaque pointer the caller passed to tt_subdiv_flush().
 */
typedef void (*tt_subdiv_emit_fn)(PS_GPU *gpu,
      tri_vertex *vertices,
      uint8_t flags,
      const tri_vertex *parent_vertices,
      void *tag);

/* Bit flags stored alongside each buffered triangle so the emit
 * callback can re-dispatch to the right rasteriser variant.  These
 * mirror the spec bits the polygon-rasteriser DEFINE_* macros
 * combine: gouraud, textured, semi-transparency mode, mask-eval.
 * Textured tris are never buffered (eligibility rejects), but the
 * bit is preserved for future expansion.
 *
 * Semi-transparency is encoded as a 2-bit field in BLEND0/BLEND1
 * (low bit / high bit of the PSX BM value 0..3) plus a separate
 * BLEND_ENABLE bit.  When BLEND_ENABLE is clear the primitive is
 * opaque and the BLEND0/BLEND1 bits are don't-care.  When set,
 * BLEND0/BLEND1 encode which of the four PSX blend formulas
 * applies (0: B/2+F/2, 1: B+F, 2: B-F, 3: B+F/4).  The emit
 * callback decodes these back into a -1/0..3 mode for the
 * rasteriser hand-off.
 *
 * PGXP_VALID is required for eligibility: Phong tessellation needs
 * sub-pixel positions from precise[0..1] and a depth axis from
 * precise[2] (PGXP's 1/w).  When clear the eligibility gate rejects
 * the polygon. */
#define TT_SUBDIV_F_GOURAUD      (1u << 0)
#define TT_SUBDIV_F_TEXTURED     (1u << 1)
#define TT_SUBDIV_F_BLEND0       (1u << 2)
#define TT_SUBDIV_F_BLEND1       (1u << 3)
#define TT_SUBDIV_F_MASKEVAL     (1u << 4)
#define TT_SUBDIV_F_PGXP_VALID   (1u << 5)
#define TT_SUBDIV_F_BLEND_ENABLE (1u << 6)

/* Lifecycle. */
void tt_subdiv_init(void);
void tt_subdiv_shutdown(void);

/* Returns 1 if there's anything pending in the deferred buffer, 0
 * otherwise.  Cheap (loads a single int).  Used to short-circuit
 * tt_subdiv_flush_if_pending() at command sites where the common
 * case is "no buffer". */
int  tt_subdiv_has_pending(void);

/* Test whether a triangle is eligible for subdivision.  Pure
 * function of the vertices and flags.  Used by the polygon path to
 * decide between deferring and direct rasterisation.  Rejects
 * textured polys (UV refinement not implemented) and PGXP-invalid
 * polys (Phong needs precise[]). */
bool tt_subdiv_is_eligible(const tri_vertex *vertices,
      uint8_t flags);

/* Push a triangle into the deferred buffer.  Caller is responsible
 * for having checked psx_gpu_subdivision_level > 0 and
 * tt_subdiv_is_eligible(); this function does not re-check. Returns
 * true on success, false on buffer-full (caller should flush then
 * either retry or rasterise directly). */
bool tt_subdiv_push(const tri_vertex *vertices, uint8_t flags);

/* Record an ineligible polygon's three vertex positions as pinning
 * hints for the next subdivision flush.  Called from the polygon
 * path's "this primitive is not eligible for subdivision" branch
 * INSTEAD of an eager flush -- the ineligible polygon goes on to
 * rasterise directly in submission order, while its vertex
 * positions tell subsequent eligible-poly tessellation which
 * vertices are shared with (already-drawn) unsubdivided neighbours.
 *
 * Implementation: inserts a zero-normal entry in the per-vertex
 * normal cache for each of the three vertices.  Zero normal means
 * Phong projection at that corner is the identity -- p_out depends
 * only on the other two corners, with the zero-normal corner
 * contributing flat.  Edges shared between an eligible polygon and
 * an ineligible neighbour at that vertex therefore tessellate flat
 * along the shared edge, matching the unsubdivided side exactly.
 *
 * First-write-wins: an ineligible-polygon hint at a vertex blocks
 * later eligible-polygon writes of a real face normal there.
 * Eligible writes that happen before any ineligible hint win.
 * Either way the cache stays deterministic.
 *
 * Eligible/ineligible distinction is the same as for tt_subdiv_push:
 * the caller is responsible for having determined this primitive is
 * not subdivision-eligible (textured, blended, mask-eval).  Costs
 * nothing when subdivision is disabled because the caller short-
 * circuits on psx_gpu_subdivision_level == 0 before entering the
 * ineligible branch's pin-hint call.
 *
 * Keys on integer source (x, y) -- same identity as the eligible-
 * poly normal lookup, so an ineligible-polygon vertex and a
 * buffered eligible-mesh vertex at the same source 3D point hash
 * to identical keys. */
void tt_subdiv_add_pin_hints(const tri_vertex *vertices);

/* Returns true if `vertices` form a triangle that would be drawn
 * "inside" a buffered region -- specifically, whose integer-XY
 * bounding box is strictly contained inside the union bounding
 * box of currently-pending triangles.  When this returns true,
 * the caller should flush the buffer BEFORE pushing this triangle,
 * so the new triangle is rasterised on top of (after) the buffered
 * region rather than batched into the same Loop-smoothed mesh.
 * This preserves PS1's painter's-algorithm semantics for overlay
 * draws like character face details, decals on walls, etc. */
bool tt_subdiv_would_overlap(const tri_vertex *vertices);

/* Returns true if `vertices` form a triangle whose integer-XY
 * bounding box INTERSECTS the buffered region (not necessarily
 * strict containment).  Used to detect paint-order inversions
 * where an ineligible primitive about to draw immediately would
 * be overpainted at flush time by the buffered eligible mesh.
 * Different rule from tt_subdiv_would_overlap (which uses strict
 * containment for the eligible-on-eligible decal case).  See the
 * function definition for the rationale on why these two cases
 * need different rules. */
bool tt_subdiv_would_intersect_ineligible(const tri_vertex *vertices);

/* Run Phong tessellation on the deferred buffer at the current
 * psx_gpu_subdivision_level (1..3), then for every resulting sub-
 * triangle call `emit(gpu, vertices, flags, parent_vertices, tag)`.
 * Empties the buffer.  Safe to call when the buffer is empty
 * (becomes a no-op). */
void tt_subdiv_flush(PS_GPU *gpu, tt_subdiv_emit_fn emit, void *tag);

/* Clear the per-vertex normal cache.  Must be called once per
 * frame, after the end-of-frame flush, so animated meshes get
 * fresh face normals each frame.  The cache being frame-scoped is
 * what makes first-write-wins safe to use for cross-triangle
 * consistency: within a frame the same logical vertex always
 * resolves to the same normal; across frames the normal can
 * change as the mesh deforms. */
void tt_subdiv_frame_end(void);

/* Convenience wrapper: flush only if anything is pending.  Inline
 * one-liner so command-site callers pay just a single load + branch
 * when the buffer is empty. */
static inline void tt_subdiv_flush_if_pending(PS_GPU *gpu,
      tt_subdiv_emit_fn emit, void *tag)
{
   if (tt_subdiv_has_pending())
      tt_subdiv_flush(gpu, emit, tag);
}

#ifdef __cplusplus
}
#endif

#endif /* GPU_SUBDIV_H__ */
