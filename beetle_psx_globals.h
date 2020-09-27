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
extern uint8_t psx_gpu_upscale_shift;
extern uint8_t psx_gpu_upscale_shift_hw;
extern int line_render_mode;
extern int filter_mode;
extern bool opaque_check;
extern bool semitrans_check;
extern bool crop_overscan;

enum core_timing_fps_modes
{
   FORCE_PROGRESSIVE_TIMING = 0,
   FORCE_INTERLACED_TIMING,
   AUTO_TOGGLE_TIMING
};

extern int core_timing_fps_mode;
extern bool currently_interlaced;
extern bool interlace_setting_dirty;

extern int aspect_ratio_setting;
extern bool aspect_ratio_dirty;

#ifdef __cplusplus
}
#endif

#endif
