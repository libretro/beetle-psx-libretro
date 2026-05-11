#ifndef __RSX_DEFER_H__
#define __RSX_DEFER_H__

/*
 * rsx_defer
 * ---------
 * A small FIFO queue for RSX-backend operations that arrive before the
 * hardware renderer (GL or Vulkan) has finished initialising.
 *
 * Background. The libretro frontend registers a `context_reset` callback
 * via RETRO_ENVIRONMENT_SET_HW_RENDER and is allowed to invoke it at any
 * point after `retro_load_game` returns - in particular it may invoke the
 * core's GPU side (push primitives, upload VRAM, set state) *before* the
 * first context_reset fires. Without buffering, every `rsx_*_*` entry
 * point either had to no-op (silently dropping side-effects) or risk
 * crashing on a NULL renderer pointer.
 *
 * Both backends previously chose "silently drop". The Vulkan backend later
 * gained an inline std::vector<std::function<void()>> defer queue, but the
 * GL backend (a C TU) never got an equivalent and continued to drop. This
 * caused real symptoms on at least King's Field with the GL backend: the
 * HUD glyph VRAM uploads delivered between SET_HW_RENDER and the first
 * context_reset were lost, and the glyphs only appeared after a savestate
 * load (which replays the full 1MiB VRAM blob via GPU_RestoreStateP3()).
 *
 * This module provides a single C-callable defer mechanism that both
 * backends share. Each backend maintains its own queue instance (via
 * rsx_defer_queue_t), pushes the operations it wants to replay during the
 * pre-renderer window, and drains the queue at the end of its own
 * context_reset once the renderer is up.
 *
 * Policy. We defer the same set of operations the Vulkan backend already
 * deferred:
 *   - state setters (tex window, draw offset, draw area, display ranges,
 *     display mode, vram framebuffer coords)
 *   - load_image (VRAM upload from CPU)
 *   - toggle_display
 * We do NOT defer push_triangle/push_quad/push_line, fill_rect, copy_rect,
 * or read_vram. Pre-context geometry has nowhere to draw to and matches
 * the existing Vulkan policy of dropping it; read_vram needs a synchronous
 * answer that an empty renderer cannot give.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tag identifying which deferred operation a queue entry represents. */
typedef enum
{
   RSX_DEFER_SET_TEX_WINDOW = 0,
   RSX_DEFER_SET_DRAW_OFFSET,
   RSX_DEFER_SET_DRAW_AREA,
   RSX_DEFER_SET_VRAM_FRAMEBUFFER_COORDS,
   RSX_DEFER_SET_HORIZONTAL_DISPLAY_RANGE,
   RSX_DEFER_SET_VERTICAL_DISPLAY_RANGE,
   RSX_DEFER_SET_DISPLAY_MODE,
   RSX_DEFER_LOAD_IMAGE,
   RSX_DEFER_TOGGLE_DISPLAY
} rsx_defer_kind_t;

/*
 * A single deferred operation. The struct stores the raw arguments the
 * caller would have passed to the corresponding rsx_*_<op> function; the
 * drain routine replays them by calling the backend-supplied callback
 * with the same arguments. We capture the *raw* inputs (tww/twh/twx/twy
 * etc.) rather than any pre-computed values so a single struct shape
 * works across both backends and the per-backend computation logic stays
 * in the entry point.
 *
 * For RSX_DEFER_LOAD_IMAGE the `vram` pointer captured here aliases
 * GPU.vram (the long-lived host-side mirror in mednafen/psx/gpu.c).
 * That pointer is allocated once at startup and torn down only on
 * `Cleanup()`; both lifetimes are strictly outside the
 * SET_HW_RENDER -> context_reset window we're buffering across, so
 * capturing the pointer (rather than memcpying a 1024 * height slice)
 * is safe and matches what the prior C++ Vulkan defer did.
 */
typedef struct
{
   rsx_defer_kind_t kind;
   union
   {
      struct
      {
         uint8_t tww;
         uint8_t twh;
         uint8_t twx;
         uint8_t twy;
      } set_tex_window;

      struct
      {
         int16_t x;
         int16_t y;
      } set_draw_offset;

      struct
      {
         uint16_t x0;
         uint16_t y0;
         uint16_t x1;
         uint16_t y1;
      } set_draw_area;

      struct
      {
         uint32_t xstart;
         uint32_t ystart;
      } set_vram_framebuffer_coords;

      struct
      {
         uint16_t x1;
         uint16_t x2;
      } set_horizontal_display_range;

      struct
      {
         uint16_t y1;
         uint16_t y2;
      } set_vertical_display_range;

      struct
      {
         bool depth_24bpp;
         bool is_pal;
         bool is_480i;
         int  width_mode;
      } set_display_mode;

      struct
      {
         uint16_t  x;
         uint16_t  y;
         uint16_t  w;
         uint16_t  h;
         uint16_t *vram;
         bool      mask_test;
         bool      set_mask;
      } load_image;

      struct
      {
         bool status;
      } toggle_display;
   } u;
} rsx_defer_op_t;

/*
 * Queue handle. Opaque to callers; the fields below are exposed only
 * because both files need to embed an instance as a static. Callers must
 * not touch these fields directly - use the API.
 */
typedef struct
{
   rsx_defer_op_t *ops;       /* heap-allocated, grown on demand           */
   size_t          count;     /* number of valid entries                   */
   size_t          capacity;  /* allocated slots                           */
} rsx_defer_queue_t;

/*
 * Backend-supplied dispatcher invoked once per queued op during a drain.
 * Receives an opaque user pointer (typically NULL; the backend already
 * has all renderer state in its own statics) plus a const pointer to the
 * op being replayed. Implementation should switch on op->kind and call
 * the appropriate rsx_<backend>_<op>(...) function with op->u.<op>.*
 * fields. Returning has no effect; errors are the dispatcher's problem.
 */
typedef void (*rsx_defer_dispatch_fn)(void *user, const rsx_defer_op_t *op);

/* Discard every queued op and free the backing storage. Safe on an
 * empty / never-used queue. After this the queue is left in a usable
 * empty state; subsequent rsx_defer_push_* calls will reallocate. */
void rsx_defer_clear(rsx_defer_queue_t *q);

/* Number of currently queued ops. Cheap. */
size_t rsx_defer_count(const rsx_defer_queue_t *q);

/*
 * Drain the queue in FIFO order, invoking `dispatch(user, op)` once per
 * entry, then clear. The dispatcher is allowed to call back into the
 * same rsx_<backend>_* entry point that originally queued the op - by
 * the time drain runs the renderer is up, so the entry point's
 * "renderer present" branch will execute and actually perform the work.
 * It is *not* safe for the dispatcher to push new ops onto the same
 * queue during drain (we snapshot count up-front but reuse storage).
 */
void rsx_defer_drain(rsx_defer_queue_t *q,
                     rsx_defer_dispatch_fn dispatch,
                     void *user);

/* Per-op push helpers. Each appends a single tagged entry. They allocate
 * on first use and grow geometrically; an OOM during growth drops the
 * push (logged once per process) rather than aborting, since losing a
 * deferred state-set is preferable to crashing the frontend. */
void rsx_defer_push_set_tex_window(rsx_defer_queue_t *q,
                                   uint8_t tww, uint8_t twh,
                                   uint8_t twx, uint8_t twy);

void rsx_defer_push_set_draw_offset(rsx_defer_queue_t *q,
                                    int16_t x, int16_t y);

void rsx_defer_push_set_draw_area(rsx_defer_queue_t *q,
                                  uint16_t x0, uint16_t y0,
                                  uint16_t x1, uint16_t y1);

void rsx_defer_push_set_vram_framebuffer_coords(rsx_defer_queue_t *q,
                                                uint32_t xstart,
                                                uint32_t ystart);

void rsx_defer_push_set_horizontal_display_range(rsx_defer_queue_t *q,
                                                 uint16_t x1, uint16_t x2);

void rsx_defer_push_set_vertical_display_range(rsx_defer_queue_t *q,
                                               uint16_t y1, uint16_t y2);

void rsx_defer_push_set_display_mode(rsx_defer_queue_t *q,
                                     bool depth_24bpp,
                                     bool is_pal,
                                     bool is_480i,
                                     int  width_mode);

void rsx_defer_push_load_image(rsx_defer_queue_t *q,
                               uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h,
                               uint16_t *vram,
                               bool mask_test, bool set_mask);

void rsx_defer_push_toggle_display(rsx_defer_queue_t *q, bool status);

#ifdef __cplusplus
}
#endif

#endif /* __RSX_DEFER_H__ */
