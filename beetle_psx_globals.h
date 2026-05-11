#ifndef BEETLE_PSX_GLOBALS_H__
#define BEETLE_PSX_GLOBALS_H__

#include <boolean.h>
#include <stdint.h>

/* Global state variables used by the Beetle PSX Core.
 * These are typically set by core options and are used
 * by methods in the Mednafen PSX module that have been
 * modified for Beetle PSX.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern bool content_is_pal;
extern uint8_t widescreen_hack;
extern uint8_t widescreen_hack_aspect_ratio_setting;
extern uint8_t psx_gpu_upscale_shift;
extern uint8_t psx_gpu_upscale_shift_hw;
/*
 * When true, the SW renderer:
 *   - bypasses LineSkipTest so polygons rasterise to every VRAM
 *     line every frame regardless of the game's `dfe` flag;
 *   - defers the per-scanline VRAM-to-surface conversion to a
 *     single end-of-frame flush (GPU_FlushDeferredScanout) so
 *     scanout doesn't race with rasterisation in titles that
 *     double-buffer through dfe gating.
 *
 * Together these make the SW renderer produce HW-equivalent
 * output for 480i interlaced games: combless on motion (because
 * both halves of VRAM are populated with current-frame content
 * before scanout reads them), no torn intermediates (because
 * scanout runs only once the frame is settled).  Set by
 * libretro.c when the user picks Deinterlace Method = Off.
 */
extern bool psx_gpu_rasterize_both_fields;
extern int line_render_mode;
extern int filter_mode;
extern int crop_overscan;

enum core_timing_fps_modes
{
   FORCE_PROGRESSIVE_TIMING = 0,
   FORCE_INTERLACED_TIMING,
   AUTO_TOGGLE_TIMING
};

extern enum core_timing_fps_modes core_timing_fps_mode;
extern bool currently_interlaced;
extern bool interlace_setting_dirty;
extern uint8_t startup_frame_count;

extern int aspect_ratio_setting;
extern bool aspect_ratio_dirty;
extern bool is_monkey_hero;

#ifdef __cplusplus
}
#endif

#endif
