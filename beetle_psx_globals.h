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
extern int line_render_mode;
extern int filter_mode;
extern bool opaque_check;
extern bool semitrans_check;
extern bool crop_overscan;

#ifdef __cplusplus
}
#endif

#endif
