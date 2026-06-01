/*
 * rhi_defer - implementation. See rhi_defer.h for the rationale and the
 * full description of which operations are deferred (and which are
 * deliberately not).
 *
 * The queue is a flat array of tagged op structs. Growth is geometric
 * (capacity doubles on overflow, starting at 16). Realistically the
 * queue never holds more than a few dozen entries during the
 * SET_HW_RENDER -> context_reset window, so the doubling cap is plenty
 * and we don't bother with a free-list or any reuse logic.
 */

#include "rhi_defer.h"

#include <stdlib.h>
#include <string.h>

/* Initial capacity allocated on first push. Chosen to comfortably hold
 * the steady-state pre-context_reset traffic (a handful of state sets
 * plus the initial GPU_RestoreStateP3() VRAM upload at most) without an
 * immediate regrow. */
#define RHI_DEFER_INITIAL_CAP 16

/*
 * Ensure the queue has room for at least one more op.
 * Returns true on success; false on allocation failure (in which case
 * the caller must drop the push - we deliberately don't abort).
 */
static bool rhi_defer_reserve_one(rhi_defer_queue_t *q)
{
   size_t          new_cap;
   rhi_defer_op_t *new_ops;

   if (q->count < q->capacity)
      return true;

   new_cap = (q->capacity == 0) ? RHI_DEFER_INITIAL_CAP : (q->capacity * 2);
   new_ops = (rhi_defer_op_t *)realloc(q->ops, new_cap * sizeof(*new_ops));
   if (!new_ops)
      return false;

   q->ops      = new_ops;
   q->capacity = new_cap;
   return true;
}

/* Append-and-zero. Returns a pointer to the new (uninitialised) slot or
 * NULL on OOM. The slot is bumped into `count` only on success so the
 * queue stays consistent on failure. */
static rhi_defer_op_t *rhi_defer_alloc_slot(rhi_defer_queue_t *q,
                                            rhi_defer_kind_t kind)
{
   rhi_defer_op_t *slot;

   if (!q)
      return NULL;
   if (!rhi_defer_reserve_one(q))
      return NULL;

   slot = &q->ops[q->count++];
   /* Zero the union so any unused fields are deterministic - cheap and
    * keeps valgrind/MSan happy if the dispatcher ever reads through the
    * wrong arm of the union by accident. */
   memset(slot, 0, sizeof(*slot));
   slot->kind = kind;
   return slot;
}

void rhi_defer_clear(rhi_defer_queue_t *q)
{
   if (!q)
      return;
   free(q->ops);
   q->ops      = NULL;
   q->count    = 0;
   q->capacity = 0;
}

size_t rhi_defer_count(const rhi_defer_queue_t *q)
{
   return q ? q->count : 0;
}

void rhi_defer_drain(rhi_defer_queue_t *q,
                     rhi_defer_dispatch_fn dispatch,
                     void *user)
{
   size_t i;
   size_t n;

   if (!q || !dispatch)
      return;

   /* Snapshot the count so we replay exactly what was queued at entry.
    * If a dispatcher implementation accidentally re-enters the entry
    * point that pushes onto this same queue (it shouldn't - the renderer
    * is up by drain time), any further pushes would land past `n` and
    * get silently leaked when we clear() below. That's preferable to
    * looping forever, and the comment in the header tells callers not
    * to do this. */
   n = q->count;
   for (i = 0; i < n; ++i)
      dispatch(user, &q->ops[i]);

   /* Reset to empty but keep the backing storage around if it's small;
    * if it's grown large from an unusual workload, free it so we don't
    * hold the high-water mark forever. */
   if (q->capacity > RHI_DEFER_INITIAL_CAP * 4)
   {
      rhi_defer_clear(q);
   }
   else
   {
      q->count = 0;
   }
}

/* ---- per-op push helpers --------------------------------------------- */

void rhi_defer_push_set_tex_window(rhi_defer_queue_t *q,
                                   uint8_t tww, uint8_t twh,
                                   uint8_t twx, uint8_t twy)
{
   rhi_defer_op_t *op = rhi_defer_alloc_slot(q, RHI_DEFER_SET_TEX_WINDOW);
   if (!op)
      return;
   op->u.set_tex_window.tww = tww;
   op->u.set_tex_window.twh = twh;
   op->u.set_tex_window.twx = twx;
   op->u.set_tex_window.twy = twy;
}

void rhi_defer_push_set_draw_offset(rhi_defer_queue_t *q,
                                    int16_t x, int16_t y)
{
   rhi_defer_op_t *op = rhi_defer_alloc_slot(q, RHI_DEFER_SET_DRAW_OFFSET);
   if (!op)
      return;
   op->u.set_draw_offset.x = x;
   op->u.set_draw_offset.y = y;
}

void rhi_defer_push_set_draw_area(rhi_defer_queue_t *q,
                                  uint16_t x0, uint16_t y0,
                                  uint16_t x1, uint16_t y1)
{
   rhi_defer_op_t *op = rhi_defer_alloc_slot(q, RHI_DEFER_SET_DRAW_AREA);
   if (!op)
      return;
   op->u.set_draw_area.x0 = x0;
   op->u.set_draw_area.y0 = y0;
   op->u.set_draw_area.x1 = x1;
   op->u.set_draw_area.y1 = y1;
}

void rhi_defer_push_set_vram_framebuffer_coords(rhi_defer_queue_t *q,
                                                uint32_t xstart,
                                                uint32_t ystart)
{
   rhi_defer_op_t *op = rhi_defer_alloc_slot(q,
         RHI_DEFER_SET_VRAM_FRAMEBUFFER_COORDS);
   if (!op)
      return;
   op->u.set_vram_framebuffer_coords.xstart = xstart;
   op->u.set_vram_framebuffer_coords.ystart = ystart;
}

void rhi_defer_push_set_horizontal_display_range(rhi_defer_queue_t *q,
                                                 uint16_t x1, uint16_t x2)
{
   rhi_defer_op_t *op = rhi_defer_alloc_slot(q,
         RHI_DEFER_SET_HORIZONTAL_DISPLAY_RANGE);
   if (!op)
      return;
   op->u.set_horizontal_display_range.x1 = x1;
   op->u.set_horizontal_display_range.x2 = x2;
}

void rhi_defer_push_set_vertical_display_range(rhi_defer_queue_t *q,
                                               uint16_t y1, uint16_t y2)
{
   rhi_defer_op_t *op = rhi_defer_alloc_slot(q,
         RHI_DEFER_SET_VERTICAL_DISPLAY_RANGE);
   if (!op)
      return;
   op->u.set_vertical_display_range.y1 = y1;
   op->u.set_vertical_display_range.y2 = y2;
}

void rhi_defer_push_set_display_mode(rhi_defer_queue_t *q,
                                     bool depth_24bpp,
                                     bool is_pal,
                                     bool is_480i,
                                     int  width_mode)
{
   rhi_defer_op_t *op = rhi_defer_alloc_slot(q,
         RHI_DEFER_SET_DISPLAY_MODE);
   if (!op)
      return;
   op->u.set_display_mode.depth_24bpp = depth_24bpp;
   op->u.set_display_mode.is_pal      = is_pal;
   op->u.set_display_mode.is_480i     = is_480i;
   op->u.set_display_mode.width_mode  = width_mode;
}

void rhi_defer_push_load_image(rhi_defer_queue_t *q,
                               uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h,
                               uint16_t *vram,
                               bool mask_test, bool set_mask)
{
   rhi_defer_op_t *op = rhi_defer_alloc_slot(q, RHI_DEFER_LOAD_IMAGE);
   if (!op)
      return;
   op->u.load_image.x         = x;
   op->u.load_image.y         = y;
   op->u.load_image.w         = w;
   op->u.load_image.h         = h;
   op->u.load_image.vram      = vram;
   op->u.load_image.mask_test = mask_test;
   op->u.load_image.set_mask  = set_mask;
}

void rhi_defer_push_toggle_display(rhi_defer_queue_t *q, bool status)
{
   rhi_defer_op_t *op = rhi_defer_alloc_slot(q, RHI_DEFER_TOGGLE_DISPLAY);
   if (!op)
      return;
   op->u.toggle_display.status = status;
}
