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
extern bool psx_pgxp_gl_line_expansion;

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
extern bool gpu_fbwrite_fifo_delay;

/* Output color format ("Color Format" core option).
 *
 * PSX_COLOR_FORMAT_24BIT is the historical path (8 bits per channel,
 * XRGB8888 for the truecolor renderers).  PSX_COLOR_FORMAT_30BIT_HDR
 * requests 10-bit-per-channel PQ-encoded Rec.2020 (HDR10) output.
 *
 * The user's *request* lives in psx_color_format; whether HDR is
 * actually engaged lives in psx_hdr_active, which is only ever true
 * once the renderer is confirmed to be Vulkan AND the frontend has
 * accepted the HDR10 format.  Everything downstream must gate on
 * psx_hdr_active, never on psx_color_format alone, so the SW/GL paths
 * and HDR-incapable frontends fall back cleanly to 24-bit. */
enum psx_color_format_e
{
   PSX_COLOR_FORMAT_24BIT = 0,
   PSX_COLOR_FORMAT_30BIT_HDR
};

extern int   psx_color_format;         /* enum psx_color_format_e; core option */
extern bool  psx_hdr_active;           /* true only when HDR10 is really engaged */
extern float psx_hdr_paper_white_nits;
extern float psx_hdr_max_nits;         /* display peak, GET_HDR_MAX_NITS, 1000 fallback */ /* frontend SDR white, default 200        */
extern int   psx_hdr_expand_gamut;     /* frontend "Colour Boost" rotation        */
extern int   psx_hdr_output_mode;      /* frontend HDR output mode (0 off/1 HDR10) */
extern int   psx_hdr_sdr_eotf;         /* reference SDR transfer: 0 2.4, 1 2.2, 2 sRGB */
extern int   psx_src_primaries;        /* authoring gamut: 0 709, 1 SMPTE-C, 2 EBU, 3 NTSC1953 */
extern int   psx_video_cable;
extern int   psx_black_setup;          /* NTSC pedestal mismatch: 0 none, 1 lifted, 2 crushed */
extern float psx_phase_error;          /* decoder carrier misalignment, cycles */          /* 0 = RGB/bypass, 1 = S-Video, 2 = composite */

#ifdef __cplusplus
}
#endif

#endif
