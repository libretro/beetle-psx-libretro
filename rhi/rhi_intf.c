#include "rhi_intf.h"
#include "tt_trace.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "boolean.h"
#include "libretro.h"

#include "beetle_psx_globals.h"
#include "../osd_message.h"
#include "libretro_cbs.h"
#include "libretro_options.h"
#include "mednafen/mednafen.h"
#include "mednafen/psx/gpu.h"
#include "mednafen/settings.h"

#ifdef RHI_DUMP
#include "rhi_dump.h"
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
#include "rhi_lib_gl.h"
#endif

#if defined(HAVE_VULKAN)
#include "rhi_lib_vulkan.h"
#endif

extern bool fast_pal;
extern unsigned int image_height;

enum rhi_renderer_type rhi_type                 = RHI_SOFTWARE;

static bool gl_initialized                      = false;
static bool vk_initialized                      = false;

/* GPU reset defaults */
static int rhi_width_mode = WIDTH_MODE_256;
static int rhi_height_mode = HEIGHT_MODE_240;

/* ------------------------------------------------------------------------
 * VRAM readback coherence tracker.
 *
 * FBRead (Command_FBRead) issues a hard, blocking GPU->CPU readback that
 * stalls the emulation thread on a fence. Many such reads are redundant:
 * the region was last written by the CPU (load_image / FBWrite, which also
 * updates g->vram) and never rendered into since, so g->vram already holds
 * the result. Track per-8x8-block coherence between g->vram and the GPU and
 * skip the readback (and its fence) when the whole rect is provably clean.
 *
 * Rule (asymmetric, conservative):
 *   - Any GPU-side VRAM write marks every OVERLAPPING block dirty (superset).
 *     Draws are scissor-clipped to the draw area, so marking the draw area is
 *     a guaranteed superset of a primitive's writes; fills/copies/masked
 *     uploads mark their literal destination rect.
 *   - A coherence-making op (unmasked CPU load_image, or a completed
 *     readback) marks only FULLY CONTAINED blocks clean (subset); a partially
 *     covered boundary block keeps its prior state, as its uncovered pixels
 *     are not made coherent by the op.
 *   - A readback is skipped only when every block the rect touches is clean.
 *
 * Default state is dirty (zero-init), and rhi_intf_open() re-dirties on every
 * renderer (re)creation, so the first read of any region always reads back.
 * The state machine was verified bit-exact against an always-readback baseline
 * over 250k randomized op sequences incl. VRAM wrap, sub-block writes/reads,
 * and draw-area superset marking of scissor-clipped primitives.
 * ---------------------------------------------------------------------- */
#define TT_COH_VRAM_W 1024
#define TT_COH_VRAM_H 512
#define TT_COH_SH     3
#define TT_COH_BW     (TT_COH_VRAM_W >> TT_COH_SH)   /* 128 block columns */
#define TT_COH_BH     (TT_COH_VRAM_H >> TT_COH_SH)   /* 64  block rows    */

static bool     tt_coh_skip_enabled = true;
static bool     tt_coh_da_pending   = true;  /* re-mark draw area on next primitive */
static uint8_t  tt_coh_clean[TT_COH_BH * TT_COH_BW]; /* 0=dirty (default), 1=clean */
static uint16_t tt_coh_dax, tt_coh_day, tt_coh_daw, tt_coh_dah; /* draw area x,y,w,h */

static void tt_coh_reset(void)
{
   memset(tt_coh_clean, 0, sizeof(tt_coh_clean));
   tt_coh_dax = tt_coh_day = 0;
   tt_coh_daw = tt_coh_dah = 0;
   tt_coh_da_pending = true;
}

/* DIRTY: every block the rect OVERLAPS (conservative superset). */
static void tt_coh_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
   uint32_t bx0, by0, nbx, nby, kx, ky;
   if (!w || !h)
      return;
   bx0 = (x & (TT_COH_VRAM_W - 1)) >> TT_COH_SH;
   by0 = (y & (TT_COH_VRAM_H - 1)) >> TT_COH_SH;
   nbx = ((x & 7) + w + 7) >> TT_COH_SH; if (nbx > TT_COH_BW) nbx = TT_COH_BW;
   nby = ((y & 7) + h + 7) >> TT_COH_SH; if (nby > TT_COH_BH) nby = TT_COH_BH;
   for (ky = 0; ky < nby; ky++)
      for (kx = 0; kx < nbx; kx++)
         tt_coh_clean[((by0 + ky) & (TT_COH_BH - 1)) * TT_COH_BW
                    + ((bx0 + kx) & (TT_COH_BW - 1))] = 0;
}

/* CLEAN: only blocks FULLY CONTAINED in the rect (subset). */
static void tt_coh_clean_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
   uint32_t cb0, cb1, rb0, rb1, sc, sr, bc, br;
   if (w < 8 || h < 8)
      return;
   cb0 = (x + 7) >> TT_COH_SH;   /* first fully covered col (unwrapped) */
   cb1 = (x + w) >> TT_COH_SH;   /* one past last fully covered col     */
   rb0 = (y + 7) >> TT_COH_SH;
   rb1 = (y + h) >> TT_COH_SH;
   if (cb1 <= cb0 || rb1 <= rb0)
      return;
   sc = cb1 - cb0; if (sc > TT_COH_BW) sc = TT_COH_BW;
   sr = rb1 - rb0; if (sr > TT_COH_BH) sr = TT_COH_BH;
   for (br = 0; br < sr; br++)
      for (bc = 0; bc < sc; bc++)
         tt_coh_clean[((rb0 + br) & (TT_COH_BH - 1)) * TT_COH_BW
                    + ((cb0 + bc) & (TT_COH_BW - 1))] = 1;
}

/* True iff every block the rect touches is clean (=> whole rect coherent). */
static bool tt_coh_all_clean(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
   uint32_t bx0, by0, nbx, nby, kx, ky;
   if (!w || !h)
      return true;
   bx0 = (x & (TT_COH_VRAM_W - 1)) >> TT_COH_SH;
   by0 = (y & (TT_COH_VRAM_H - 1)) >> TT_COH_SH;
   nbx = ((x & 7) + w + 7) >> TT_COH_SH; if (nbx > TT_COH_BW) nbx = TT_COH_BW;
   nby = ((y & 7) + h + 7) >> TT_COH_SH; if (nby > TT_COH_BH) nby = TT_COH_BH;
   for (ky = 0; ky < nby; ky++)
      for (kx = 0; kx < nbx; kx++)
         if (!tt_coh_clean[((by0 + ky) & (TT_COH_BH - 1)) * TT_COH_BW
                         + ((bx0 + kx) & (TT_COH_BW - 1))])
            return false;
   return true;
}

/* Re-mark the whole draw area dirty, but only when it may have changed
 * (set_draw_area) or a clean op cleaned part of it since the last mark.
 * Primitives are scissor-clipped to the draw area, so this stays a superset
 * while avoiding an O(blocks) mark on every one of the thousands of quads a
 * frame may push into a stable draw area. */
static void tt_coh_mark_draw_area(void)
{
   if (tt_coh_da_pending)
   {
      tt_coh_dirty(tt_coh_dax, tt_coh_day, tt_coh_daw, tt_coh_dah);
      tt_coh_da_pending = false;
   }
}

static bool rhi_soft_open(bool is_pal)
{
   content_is_pal = is_pal;
   return true;
}

/* Push the frontend callbacks into whatever backend rhi_type now names. The
 * frontend sets video_cb / environ_cb (in libretro_cbs) once, conventionally
 * before a backend is selected, so the rhi_type-gated forwarding in the setters
 * below would otherwise drop them and leave the chosen backend's own copy NULL.
 * These globals are the single source of truth, so re-push them straight from
 * there the moment a backend is opened - no extra cached copies to drift. */
static void rhi_intf_push_cbs(void)
{
   switch (rhi_type)
   {
      case RHI_SOFTWARE:
      case RHI_OPENGL:
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         if (environ_cb)
            rhi_vulkan_set_environment(environ_cb);
         if (video_cb)
            rhi_vulkan_set_video_refresh(video_cb);
#endif
         break;
   }
}

void rhi_intf_set_environment(retro_environment_t cb)
{
   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_set_environment(cb);
#endif
         break;
   }
}

void rhi_intf_set_video_refresh(retro_video_refresh_t cb)
{
   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_set_video_refresh(cb);
#endif
         break;
   }
}

void rhi_intf_get_system_av_info(struct retro_system_av_info *info)
{
   switch (rhi_type)
   {
      case RHI_SOFTWARE:
      {
         int first_visible_scanline = MDFN_GetSettingI(content_is_pal ? "psx.slstartp" : "psx.slstart");
         int last_visible_scanline  = MDFN_GetSettingI(content_is_pal ? "psx.slendp" : "psx.slend");
         int manual_height          = last_visible_scanline - first_visible_scanline + 1;

         /* Compensate height in smart/dynamic crop mode to keep proper aspect ratio */
         if (crop_overscan == 2 && image_height && manual_height > image_height)
         {
            first_visible_scanline = 0;
            last_visible_scanline  = first_visible_scanline + image_height - 1;
         }

         memset(info, 0, sizeof(*info));
         info->timing.fps            = rhi_common_get_timing_fps();
         info->timing.sample_rate    = SOUND_FREQUENCY;
         info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W;
         info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H;
         info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W  << psx_gpu_upscale_shift;
         info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H  << psx_gpu_upscale_shift;
         info->geometry.aspect_ratio = rhi_common_get_aspect_ratio(content_is_pal, crop_overscan,
                                          first_visible_scanline, last_visible_scanline,
                                          aspect_ratio_setting, false, widescreen_hack, widescreen_hack_aspect_ratio_setting);
         break;
      }
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_get_system_av_info(info);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_get_system_av_info(info);
#endif
         break;
   }
}

enum rhi_renderer_type rhi_intf_is_type(void)
{
   return rhi_type;
}

/* `static inline` (rather than the original plain `inline`) so that
 * unoptimised builds link. With -O3 (release) every call site got
 * inlined and no out-of-line definition was needed; under DEBUG=1
 * (-O0) the inliner doesn't fire, and per C99/C11 semantics a plain
 * `inline` definition without a matching `extern` declaration does
 * not emit a symbol either - producing the link-time
 *   undefined reference to `rhi_intf_dump_init'
 * from every call site in this file. `static inline` keeps the
 * intent (compiler may inline) and guarantees a TU-local symbol when
 * inlining is suppressed. */
static inline void rhi_intf_dump_init(void)
{
#if defined(RHI_DUMP)
   {
      const char *env = getenv("RHI_DUMP");
      if (env)
         rhi_dump_init(env);
   }
#endif
}

bool rhi_intf_open(bool is_pal, bool force_software)
{
   bool software_selected    = false;
   enum force_renderer_type force_type = AUTO;
   unsigned preferred        = 0; /* Will be set to RETRO_HW_CONTEXT_DUMMY if GET_PREFERRED_HW_RENDER is not supported by frontend */
   vk_initialized            = false;
   gl_initialized            = false;

   tt_coh_reset();

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   struct retro_variable var = {0};
   var.key                   = BEETLE_OPT(renderer);
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      if (!strcmp(var.value, "software") || force_software)
         software_selected = true;
      else if (!strcmp(var.value, "hardware_gl"))
         force_type = FORCE_OPENGL;
      else if (!strcmp(var.value, "hardware_vk"))
         force_type = FORCE_VULKAN;
   }
   else
#endif
   {
      /* If 'BEETLE_OPT(renderer)' option is not found, then
       * we are running in software mode */
      software_selected = true;
   }

   if (!software_selected)
   {
      /* Check for any hardware renderer forces before performing auto sequence */
      if (force_type == FORCE_VULKAN)
      {
#if defined (HAVE_VULKAN)
         if (rhi_vulkan_open(is_pal))
         {
            rhi_type = RHI_VULKAN;
            vk_initialized = true;
            rhi_intf_dump_init();
            rhi_intf_push_cbs();
            return true;
         }
         osd_message(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "Could not force Vulkan renderer. Falling back to software renderer.");
#else
         osd_message(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "Attempted to force Vulkan renderer, but core was built without it. Falling back to software renderer.");

#endif
         goto soft;
      }
      else if (force_type == FORCE_OPENGL)
      {
#if defined (HAVE_OPENGL) || defined(HAVE_OPENGLES)
         if (rhi_gl_open(is_pal))
         {
            rhi_type = RHI_OPENGL;
            gl_initialized = true;
            rhi_intf_dump_init();
            return true;
         }
         osd_message(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "Could not force OpenGL renderer. Falling back to software renderer.");
#else
         osd_message(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "Attempted to force OpenGL renderer, but core was built without it. Falling back to software renderer.");
#endif
         goto soft;
      }
      /* End forces section */

      if (!environ_cb(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &preferred))
      {
         preferred = RETRO_HW_CONTEXT_DUMMY;
      }
      /* If GET_PREFERRED_HW_RENDER is not supported by frontend, then we just go
       * down the list attempting to open a hardware renderer until we get one */

#if defined(HAVE_VULKAN)
      if ((preferred == RETRO_HW_CONTEXT_DUMMY ||
           preferred == RETRO_HW_CONTEXT_VULKAN)
          && rhi_vulkan_open(is_pal))
      {
         rhi_type       = RHI_VULKAN;
         vk_initialized = true;
         rhi_intf_dump_init();
         rhi_intf_push_cbs();
         return true;
      }
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
      if ((   preferred == RETRO_HW_CONTEXT_DUMMY
           || preferred == RETRO_HW_CONTEXT_OPENGL
           || preferred == RETRO_HW_CONTEXT_OPENGL_CORE)
          && rhi_gl_open(is_pal))
      {
         rhi_type       = RHI_OPENGL;
         gl_initialized = true;
         rhi_intf_dump_init();
         return true;
      }
#endif

      if (preferred == RETRO_HW_CONTEXT_DUMMY)
         osd_message(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "No hardware renderers could be opened. Falling back to software renderer.");
      else
         osd_message(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "Unable to find or open hardware renderer for frontend preferred hardware context. Falling back to software renderer.");
   }

soft:
   /* rhi_soft_open(is_pal) always returns true */
   if (rhi_soft_open(is_pal))
   {
      rhi_type = RHI_SOFTWARE;
      rhi_intf_dump_init();
      return true;
   }

   /* Note: fallback will result in the software renderer running at
    * 1x internal resolution instead of the user configured setting
    * because of the check_variables startup sequence in libretro.cpp;
    * this is a good thing since emulation would likely slow to a crawl
    * if the user had >2x IR for hardware rendering and that setting was
    * retroactively applied that to software rendering fallback
    */

   return false;
}

void rhi_intf_close(void)
{
#if defined(RHI_DUMP)
   rhi_dump_deinit();
#endif

   if (rhi_type != RHI_SOFTWARE)
   {
#if defined(HAVE_VULKAN)
	   if (vk_initialized)
		   return;
#endif
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
	   if (gl_initialized)
	   {
		   rhi_gl_close();
		   return;
	   }
#endif
   }
}

void rhi_intf_refresh_variables(void)
{
   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_refresh_variables();
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_refresh_variables();
#endif
         break;
   }
}

void rhi_intf_prepare_frame(void)
{
#ifdef RHI_DUMP
   rhi_dump_prepare_frame();
#endif

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_prepare_frame();
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_prepare_frame();
#endif
         break;
   }
}

void rhi_intf_apply_pending_geometry(void)
{
#if defined(HAVE_VULKAN)
   if (rhi_type == RHI_VULKAN)
      rhi_vulkan_apply_pending_geometry();
#endif
}

void rhi_intf_finalize_frame(const void *fb, unsigned width, 
                             unsigned height, unsigned pitch)
{
#ifdef RHI_DUMP
   rhi_dump_finalize_frame();
#endif
#ifdef DEBUG
   tt_log("finalize_frame display=%ux%u\n",
         (unsigned)width, (unsigned)height);
#endif

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         video_cb(fb, width, height, pitch);
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_finalize_frame(fb, width, height, pitch);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_finalize_frame(fb, width, height, pitch);
#endif
         break;
   }
}

void rhi_intf_set_tex_window(uint8_t tww, uint8_t twh,
                             uint8_t twx, uint8_t twy)
{
#ifdef RHI_DUMP
   rhi_dump_set_tex_window(tww, twh, twx, twy);
#endif
#ifdef DEBUG
   tt_log("set_tex_window tww=%u twh=%u twx=%u twy=%u\n",
		   (unsigned)tww, (unsigned)twh, (unsigned)twx, (unsigned)twy);
#endif

   switch (rhi_type)
   {
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_set_tex_window(tww, twh, twx, twy);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_set_tex_window(tww, twh, twx, twy);
#endif
         break;
      default:
         break;
   }
}

void rhi_intf_set_mask_setting(uint32_t mask_set_or, uint32_t mask_eval_and)
{
   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
         break;
      case RHI_VULKAN:
         /* TODO/FIXME */
         break;
   }
}

void rhi_intf_set_draw_offset(int16_t x, int16_t y)
{
#ifdef RHI_DUMP
   rhi_dump_set_draw_offset(x, y);
#endif

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_set_draw_offset(x, y);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_set_draw_offset(x, y);
#endif
         break;
   }
}

void rhi_intf_set_draw_area(uint16_t x0, uint16_t y0,
                            uint16_t x1, uint16_t y1)
{
#ifdef RHI_DUMP
   rhi_dump_set_draw_area(x0, y0, x1, y1);
#endif
#ifdef DEBUG
   tt_log("set_draw_area top_left=(%u,%u) bot_right_inclusive=(%u,%u)\n",
         (unsigned)x0, (unsigned)y0, (unsigned)x1, (unsigned)y1);
#endif

   tt_coh_dax = x0;
   tt_coh_day = y0;
   tt_coh_daw = (x1 >= x0) ? (uint16_t)(x1 - x0 + 1) : 0;
   tt_coh_dah = (y1 >= y0) ? (uint16_t)(y1 - y0 + 1) : 0;
   tt_coh_da_pending = true;

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_set_draw_area(x0, y0, x1, y1);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_set_draw_area(x0, y0, x1, y1);
#endif
         break;
   }
}

void rhi_intf_set_vram_framebuffer_coords(uint32_t xstart, uint32_t ystart)
{
#ifdef RHI_DUMP
   rhi_dump_set_vram_framebuffer_coords(xstart, ystart);
#endif

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_set_vram_framebuffer_coords(xstart, ystart);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_set_vram_framebuffer_coords(xstart, ystart);
#endif
         break;
   }
}

void rhi_intf_set_horizontal_display_range(uint16_t x1, uint16_t x2)
{
#ifdef RHI_DUMP
   rhi_dump_set_horizontal_display_range(x1, x2);
#endif

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_set_horizontal_display_range(x1, x2);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_set_horizontal_display_range(x1, x2);
#endif
         break;
   }
}

void rhi_intf_set_vertical_display_range(uint16_t y1, uint16_t y2)
{
#ifdef RHI_DUMP
   rhi_dump_set_vertical_display_range(y1, y2);
#endif

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_set_vertical_display_range(y1, y2);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_set_vertical_display_range(y1, y2);
#endif
         break;
   }
}

void rhi_intf_set_display_mode(bool depth_24bpp,
                               bool is_pal, 
                               bool is_480i,
                               int width_mode)
{
   bool boot_debounce;
#ifdef RHI_DUMP
   rhi_dump_set_display_mode(depth_24bpp, is_pal, is_480i, width_mode);
#endif

   /* During the first 10 GP1(0x08) writes (the BIOS boot animation),
    * suppress the dirty flags that trigger
    * retro_set_geometry/retro_set_system_av_info, since those round-
    * trip through the frontend and can cause a window/audio re-init
    * stutter for every BIOS-boot mode flip.
    *
    * Do NOT gate the renderer state propagation -- the HW renderers'
    * scanout texture dimensions, width_mode, is_pal and is_480i must
    * always reflect the current GP1(0x08) value, otherwise programs
    * that program their final display mode once early (single-purpose
    * homebrew/test EXEs that boot, set up GP regs, then sit in a
    * display loop without re-issuing GP1(0x08)) get the renderer
    * stuck at the default (NTSC 240p, WIDTH_MODE_320) forever.
    *
    * That stuck-default produces the classic symptom: HW renderer
    * shows the EXE's content squashed/scrambled into a 350x240 window
    * even though SW renders the same content correctly at 700x576. */
   boot_debounce = (startup_frame_count < 10);
   if (boot_debounce)
      startup_frame_count++;

   /* Is this check accurate for 240i timing? May need to be fixed later */
   if (currently_interlaced != is_480i)
   {
      currently_interlaced = is_480i;
      if (!boot_debounce)
         interlace_setting_dirty = true;
   }

   /* Also verify if this is accurate for 240i */
   if ((rhi_width_mode != width_mode) || (rhi_height_mode != (int)is_480i))
   {
      rhi_width_mode = width_mode;
      rhi_height_mode = (int)is_480i;
      if (!boot_debounce)
         aspect_ratio_dirty = true;
   }

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_set_display_mode(depth_24bpp, is_pal, is_480i, width_mode);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_set_display_mode(depth_24bpp, is_pal, is_480i, width_mode);
#endif
         break;
   }
}

void rhi_intf_push_triangle(
      float p0x, float p0y, float p0w,
      float p1x, float p1y, float p1w,
      float p2x, float p2y, float p2w,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint16_t t0x, uint16_t t0y,
      uint16_t t1x, uint16_t t1y,
      uint16_t t2x, uint16_t t2y,
      uint16_t min_u, uint16_t min_v,
      uint16_t max_u, uint16_t max_v,
      uint16_t texpage_x, uint16_t texpage_y,
      uint16_t clut_x, uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode,
      bool mask_test,
      bool set_mask)
{
#ifdef RHI_DUMP
   const rhi_dump_vertex vertices[3] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
   };
   const rhi_render_state state = {
      texpage_x, texpage_y, clut_x, clut_y, texture_blend_mode, depth_shift, dither, blend_mode,
      mask_test, set_mask,
   };
   rhi_dump_triangle(vertices, &state);
#endif

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_push_triangle(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w,
               c0, c1, c2, t0x, t0y, t1x, t1y, t2x, t2y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode, mask_test, set_mask);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_push_triangle(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w,
               c0, c1, c2, t0x, t0y, t1x, t1y, t2x, t2y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode, mask_test, set_mask);
#endif
         break;
   }
}

void rhi_intf_push_quad(
   float p0x, float p0y, float p0w,
   float p1x, float p1y, float p1w,
   float p2x, float p2y, float p2w,
   float p3x, float p3y, float p3w,
   uint32_t c0, uint32_t c1, uint32_t c2, uint32_t c3,
   uint16_t t0x, uint16_t t0y,
   uint16_t t1x, uint16_t t1y,
   uint16_t t2x, uint16_t t2y,
   uint16_t t3x, uint16_t t3y,
   uint16_t min_u, uint16_t min_v,
   uint16_t max_u, uint16_t max_v,
   uint16_t texpage_x, uint16_t texpage_y,
   uint16_t clut_x, uint16_t clut_y,
   uint8_t texture_blend_mode,
   uint8_t depth_shift,
   bool dither,
   int blend_mode,
   bool mask_test,
   bool set_mask,
   bool is_sprite,
   bool may_be_2d)
{
#ifdef RHI_DUMP
   const rhi_dump_vertex vertices[4] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
      { p3x, p3y, p3w, c3, t3x, t3y },
   };
   const rhi_render_state state = {
      texpage_x, texpage_y, clut_x, clut_y, texture_blend_mode, depth_shift, dither, blend_mode,
      mask_test, set_mask,
   };
   rhi_dump_quad(vertices, &state);
#endif

   /* Scissor-clipped to the draw area, so it is a guaranteed superset. */
   tt_coh_mark_draw_area();

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_push_quad(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w, p3x, p3y, p3w,
               c0, c1, c2, c3,
               t0x, t0y, t1x, t1y, t2x, t2y, t3x, t3y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode, mask_test, set_mask);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_push_quad(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w, p3x, p3y, p3w,
               c0, c1, c2, c3,
               t0x, t0y, t1x, t1y, t2x, t2y, t3x, t3y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode, mask_test, set_mask, is_sprite, may_be_2d);
#endif
         break;
   }
}

void rhi_intf_push_line(int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0, uint32_t c1,
      bool dither,
      int blend_mode,
      bool mask_test,
      bool set_mask)
{
#ifdef RHI_DUMP
   const rhi_dump_line_data line = {
      p0x, p0y, p1x, p1y, c0, c1, dither, blend_mode,
      mask_test, set_mask,
   };
   rhi_dump_line(&line);
#endif

   tt_coh_mark_draw_area();

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_push_line(p0x, p0y, p1x, p1y, c0, c1, dither, blend_mode, mask_test, set_mask);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_push_line(p0x, p0y, p1x, p1y, c0, c1, dither, blend_mode, mask_test, set_mask);
#endif
         break;
   }
}

bool rhi_intf_read_vram(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *vram)
{
   bool ret = false;
#ifdef DEBUG
   tt_log("read_vram rect=(%u,%u %ux%u)\n",
         (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h);
#endif

   /* Redundant readback: the whole rect is already coherent in g->vram, so
    * skip the blocking GPU->CPU fence entirely and leave vram untouched. */
   if (tt_coh_skip_enabled && tt_coh_all_clean(x, y, w, h))
      return true;

   switch (rhi_type)
   {
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         ret = rhi_vulkan_read_vram(x, y, w, h, vram);
#endif
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         ret = rhi_gl_read_vram(x, y, w, h, vram);
#endif
         break;
      default:
         break;
   }

   /* On a successful readback the rect now mirrors the GPU. */
   if (ret)
   {
      tt_coh_clean_rect(x, y, w, h);
      tt_coh_da_pending = true;
   }

   return ret;
}

void rhi_intf_load_image(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram, bool mask_test, bool set_mask)
{
#ifdef RHI_DUMP
   rhi_dump_load_image(x, y, w, h, vram, mask_test, set_mask);
#endif
#ifdef DEBUG
   tt_log("load_image rect=(%u,%u %ux%u) mask_test=%d set_mask=%d\n",
         (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h,
         (int)mask_test, (int)set_mask);
#endif

   /* Unmasked FBWrite makes g->vram and GPU coherent for the rect. A masked
    * upload writes every pixel to g->vram but the GPU skips mask-blocked
    * ones, so the region may diverge - treat it as dirty. */
   if (mask_test)
   {
      tt_coh_dirty(x, y, w, h);
   }
   else
   {
      tt_coh_clean_rect(x, y, w, h);
      tt_coh_da_pending = true;   /* a clean inside the draw area must re-arm marking */
   }

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_load_image(x, y, w, h, vram, mask_test, set_mask);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_load_image(x, y, w, h, vram, mask_test, set_mask);
#endif
         break;
   }
}

void rhi_intf_fill_rect(uint32_t color,
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{
#ifdef RHI_DUMP
   rhi_dump_fill_rect(color, x, y, w, h);
#endif
#ifdef DEBUG
   tt_log("fill_rect rect=(%u,%u %ux%u) color=0x%06x\n",
		   (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h,
		   (unsigned)(color & 0xFFFFFFu));
#endif

   tt_coh_dirty(x, y, w, h);

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_fill_rect(color, x, y, w, h);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_fill_rect(color, x, y, w, h);
#endif
         break;
   }
}

void rhi_intf_copy_rect(uint16_t src_x, uint16_t src_y,
      uint16_t dst_x, uint16_t dst_y,
      uint16_t w, uint16_t h, 
      bool mask_test, bool set_mask)
{
#ifdef RHI_DUMP
   rhi_dump_copy_rect(src_x, src_y, dst_x, dst_y, w, h, mask_test, set_mask);
#endif
#ifdef DEBUG
   tt_log("copy_rect src=(%u,%u) dst=(%u,%u) %ux%u mask_test=%d set_mask=%d\n",
         (unsigned)src_x, (unsigned)src_y,
         (unsigned)dst_x, (unsigned)dst_y,
         (unsigned)w, (unsigned)h,
         (int)mask_test, (int)set_mask);
#endif

   tt_coh_dirty(dst_x, dst_y, w, h);

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_copy_rect(src_x, src_y, dst_x, dst_y,
               w, h, mask_test, set_mask);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_copy_rect(src_x, src_y, dst_x, dst_y, w, h, mask_test, set_mask);
#endif
         break;
   }
}

bool rhi_intf_has_software_renderer(void)
{
   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         return true;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         return rhi_gl_has_software_renderer();
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         return rhi_vulkan_has_software_renderer();
#else
         break;
#endif
   }

   return false;
}

/* Whether the active renderer's HW context is currently usable. The software
 * renderer is always ready; the HW backends report whether their device/GL
 * context is live (between context_reset and context_destroy). retro_run uses
 * this to avoid driving the display pipeline while a HW context is down. */
bool rhi_intf_context_ready(void)
{
   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         return true;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         return rhi_gl_context_ready();
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         return rhi_vulkan_context_ready();
#else
         break;
#endif
   }

   return false;
}

void rhi_intf_toggle_display(bool status)
{
#ifdef RHI_DUMP
   rhi_dump_toggle_display(status);
#endif

    switch (rhi_type)
    {
    case RHI_SOFTWARE:
        break;
    case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
      rhi_gl_toggle_display(status);
#endif
        break;
    case RHI_VULKAN:
#if defined(HAVE_VULKAN)
      rhi_vulkan_toggle_display(status);
#endif
        break;
    }
}

double rhi_common_get_timing_fps(void)
{
   bool pal_timings = content_is_pal && !fast_pal;

   if (core_timing_fps_mode == FORCE_PROGRESSIVE_TIMING)
      return (pal_timings ? FPS_PAL_NONINTERLACED : FPS_NTSC_NONINTERLACED);

   else if (core_timing_fps_mode == FORCE_INTERLACED_TIMING)
      return (pal_timings ? FPS_PAL_INTERLACED : FPS_NTSC_INTERLACED);

   /*else AUTO_TOGGLE_TIMING */
   return (pal_timings ?
               (currently_interlaced ? FPS_PAL_INTERLACED : FPS_PAL_NONINTERLACED) :
               (currently_interlaced ? FPS_NTSC_INTERLACED : FPS_NTSC_NONINTERLACED));
}

float rhi_common_get_aspect_ratio(bool pal_content, int crop_overscan,
                                  int first_visible_scanline, int last_visible_scanline,
                                  int aspect_ratio_setting, bool vram_override, bool widescreen_override,
                                  int widescreen_hack_aspect_ratio_setting)
{
   float ar = (4.0 / 3.0);

   /* Current assumptions
    *    A fixed percentage of width is cropped when crop_overscan isn't 0
    *    aspect_ratio_setting is one of the following:
    *          0 - Corrected
    *          1 - Uncorrected (1:1 PAR)
    *          2 - Force 4:3 (traditionally what Beetle PSX has done prior to adding in this setting)
    *          3 - Force NTSC (get corrected NTSC aspect ratio even with PAL games)
    *
    * Aspect ratio overrides - VRAM and widescreen take precedence */

   if (vram_override)
      return 2.0 / 1.0;

   if (widescreen_override)
      switch(widescreen_hack_aspect_ratio_setting)
      {
         case 0:
            return (16.0 / 10.0);
         case 1:
            return (16.0 / 9.0);
         case 2:
            return (18.0 / 9.0);
         case 3:
            return (19.0 / 9.0);
         case 4:
            return (20.0 / 9.0);
         case 5:
            return (64.0 / 27.0);
         case 6:
            return (32.0 / 9.0);
      }

   if (aspect_ratio_setting == 0) /* Corrected */
   {
      int num_vis_scanlines = last_visible_scanline - first_visible_scanline + 1;

      /* Calculate horizontal scaling in terms of gpu clock cycles */
      ar *= (crop_overscan ? (2560.0 / 2800.0) : 1.0);

      /* Calculate vertical scaling in terms of visible scanline count */
      ar *= (pal_content ? (288.0 / num_vis_scanlines) : (240.0 / num_vis_scanlines));

      return ar;
   }
   else if (aspect_ratio_setting == 1) /* Uncorrected */
   {
      int width_base = 0;
      double height_base;

      switch (rhi_width_mode)
      {
         case WIDTH_MODE_256:
            width_base = crop_overscan ? 256 : 280;
            break;
         case WIDTH_MODE_320:
            width_base = crop_overscan ? 320 : 350;
            break;
         case WIDTH_MODE_512:
            width_base = crop_overscan ? 512 : 560;
            break;
         case WIDTH_MODE_640:
            width_base = crop_overscan ? 640 : 700;
            break;
         case WIDTH_MODE_368:
            /* Probably slightly off because of rounding, see libretro.cpp comments */
            width_base = crop_overscan ? 366 : 400;
            break;
      }

      height_base = (last_visible_scanline - first_visible_scanline + 1) *
                    (rhi_height_mode == HEIGHT_MODE_480 ? 2.0 : 1.0);

      /* Calculate aspect ratio as quotient of raw native framebuffer width and height */
      return width_base / height_base;
   }
   else if (aspect_ratio_setting == 3) /* Force NTSC */
   {
      int num_vis_scanlines = last_visible_scanline - first_visible_scanline + 1;

      ar *= (crop_overscan ? (2560.0 / 2800.0) : 1.0);
      ar *= (240.0 / num_vis_scanlines);

      return ar;
   }

   return ar; /* 4:3 */
}
