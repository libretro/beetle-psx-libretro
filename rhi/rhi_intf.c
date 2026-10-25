#include "rhi_intf.h"
#include "tt_trace.h"
#if defined(TEXTURE_DUMPING_ENABLED) /* defined exactly when rhi_tt.c is built */
#include "rhi_tt.h" /* texture_tracker_session_reset */
#endif

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

extern retro_log_printf_t log_cb;
extern bool fast_pal;
extern unsigned int image_height;

enum rhi_renderer_type rhi_type                 = RHI_SOFTWARE;

static bool gl_initialized                      = false;
static bool vk_initialized                      = false;

/* GPU reset defaults */
static int rhi_width_mode = WIDTH_MODE_256;
static int rhi_height_mode = HEIGHT_MODE_240;
/* GP1(06h) range: x1 in the low 16 bits, x2 in the high 16 bits. */
static uint32_t rhi_hrange = 0x200u | (0xC00u << 16);

#include "rhi_coherence.h"

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
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   struct retro_variable var;
#endif
   unsigned preferred        = 0; /* Will be set to RETRO_HW_CONTEXT_DUMMY if GET_PREFERRED_HW_RENDER is not supported by frontend */
   vk_initialized            = false;
   gl_initialized            = false;

   tt_coh_reset();

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   var.key                   = BEETLE_OPT(renderer);
   var.value                 = NULL;
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

#if defined(TEXTURE_DUMPING_ENABLED)
   /* The game is going: its Replace Textures on/off (kept across renderer
    * rebuilds) must not carry over to the next one. */
   texture_tracker_session_reset();
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

void rhi_intf_invalidate_clut_cache(void)
{
   switch (rhi_type)
   {
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_invalidate_clut_cache();
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_invalidate_clut_cache();
#endif
         break;
      default:
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
   const uint32_t hrange = (uint32_t)x1 | ((uint32_t)x2 << 16);

   if (hrange != rhi_hrange)
   {
      /* The window's width is what the aspect ratio follows. */
      if (   RHI_CROP_WIN_WIDTH(rhi_common_crop_window(crop_overscan,
                  (int32_t)(rhi_hrange & 0xFFFFu), (int32_t)(rhi_hrange >> 16)))
          != RHI_CROP_WIN_WIDTH(rhi_common_crop_window(crop_overscan, x1, x2)))
         crop_window_dirty = true;
      rhi_hrange = hrange;
   }

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
#ifdef RHI_DUMP
   rhi_dump_set_display_mode(depth_24bpp, is_pal, is_480i, width_mode);
#endif

   /* During the first 20 GP1(0x08) writes (the BIOS boot animation),
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
   if (startup_frame_count < 20)
      startup_frame_count++;
   else
   {
      /* Enforce weird interlaced non-480i modes as interlaced */
      if (rhi_type == RHI_SOFTWARE && image_height > MEDNAFEN_CORE_GEOMETRY_BASE_H)
         is_480i = true;

      /* Is this check accurate for 240i timing? May need to be fixed later */
      if (currently_interlaced != is_480i)
      {
         currently_interlaced = is_480i;
         interlace_setting_dirty = true;
      }

      /* Also verify if this is accurate for 240i */
      if ((rhi_width_mode != width_mode) || (rhi_height_mode != (int)is_480i))
      {
         rhi_width_mode = width_mode;
         rhi_height_mode = (int)is_480i;
         aspect_ratio_dirty = true;
      }
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
      const float *precise_rgb,
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
      bool set_mask,
      bool may_be_2d)
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

   /* Scissor-clipped to the draw area, so it is a guaranteed superset. */
   tt_coh_mark_draw_area();

   switch (rhi_type)
   {
      case RHI_SOFTWARE:
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rhi_gl_push_triangle(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w,
               c0, c1, c2, precise_rgb, t0x, t0y, t1x, t1y, t2x, t2y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode, mask_test, set_mask, may_be_2d);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_push_triangle(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w,
               c0, c1, c2, precise_rgb, t0x, t0y, t1x, t1y, t2x, t2y,
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
   const float *precise_rgb,
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
   bool may_be_2d,
   uint32_t uv_offset)
{
   /* Sprites are never dithered on hardware (the software renderer
    * uses the zero dither offset for them); resolve that once here so
    * every backend and the dump see the same value. */
   const bool quad_dither = dither && !is_sprite;

#ifdef RHI_DUMP
   const rhi_dump_vertex vertices[4] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
      { p3x, p3y, p3w, c3, t3x, t3y },
   };
   const rhi_render_state state = {
      texpage_x, texpage_y, clut_x, clut_y, texture_blend_mode, depth_shift, quad_dither, blend_mode,
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
               c0, c1, c2, c3, precise_rgb,
               t0x, t0y, t1x, t1y, t2x, t2y, t3x, t3y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               quad_dither,
               blend_mode, mask_test, set_mask, is_sprite, may_be_2d,
               uv_offset);
#endif
         break;
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         rhi_vulkan_push_quad(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w, p3x, p3y, p3w,
               c0, c1, c2, c3, precise_rgb,
               t0x, t0y, t1x, t1y, t2x, t2y, t3x, t3y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               quad_dither,
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

/* BEETLE_RHI_COHERENCE_CHECK=1: after every read, read the whole rect
 * again and log any pixel the coherence map left stale in the CPU copy.
 * tools/rhi_coherence/rollback_lanes.sh runs with it. */
static int rhi_coh_check = -1;

static bool rhi_intf_read_rects(const tt_coh_rect *r, unsigned n, uint16_t *vram);

static void rhi_intf_check_read(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
      const uint16_t *vram, unsigned parts)
{
   static uint16_t *full;
   tt_coh_rect      whole;
   unsigned         xx, yy, bad = 0;
   if ((unsigned)x + w > TT_COH_VRAM_W || (unsigned)y + h > TT_COH_VRAM_H)
      return;
   if (!full && !(full = (uint16_t *)malloc(TT_COH_VRAM_W * TT_COH_VRAM_H * sizeof(uint16_t))))
      return;
   memcpy(full, vram, TT_COH_VRAM_W * TT_COH_VRAM_H * sizeof(uint16_t));
   whole.x = x; whole.y = y; whole.w = w; whole.h = h;
   if (!rhi_intf_read_rects(&whole, 1, full))
      return;
   for (yy = y; yy < (unsigned)y + h; yy++)
      for (xx = x; xx < (unsigned)x + w; xx++)
         if (full[yy * TT_COH_VRAM_W + xx] != vram[yy * TT_COH_VRAM_W + xx])
            bad++;
   if (bad && log_cb)
      log_cb(RETRO_LOG_ERROR, "[rhi_coherence] stale: read (%u,%u %ux%u) in %u part(s) "
            "left %u pixel(s) differing from the GPU\n",
            (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, parts, bad);
   else if (log_cb)
      log_cb(RETRO_LOG_DEBUG, "[rhi_coherence] read (%u,%u %ux%u) in %u part(s) checked\n",
            (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h, parts);
}

bool rhi_intf_read_vram(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *vram)
{
   bool        ret = false;
   tt_coh_rect r[TT_COH_MAX_RECTS];
   unsigned    n;
#ifdef DEBUG
   tt_log("read_vram rect=(%u,%u %ux%u)\n",
         (unsigned)x, (unsigned)y, (unsigned)w, (unsigned)h);
#endif

   /* Skip the blocking GPU->CPU fence when the rect is already coherent
    * in g->vram, and otherwise fetch only its dirty parts. */
   if (rhi_coh_check < 0)
   {
      const char *e = getenv("BEETLE_RHI_COHERENCE_CHECK");
      rhi_coh_check = (e && *e && *e != '0') ? 1 : 0;
   }

   n = tt_coh_read_rects(x, y, w, h, r);
   if (!n)
   {
      if (rhi_coh_check)
         rhi_intf_check_read(x, y, w, h, vram, 0);
      return true;
   }

   ret = rhi_intf_read_rects(r, n, vram);
   if (ret && rhi_coh_check)
      rhi_intf_check_read(x, y, w, h, vram, n);

   /* On a successful readback the rect now mirrors the GPU. */
   if (ret)
   {
      tt_coh_clean_rect(x, y, w, h);
      tt_coh_da_pending = true;
   }

   return ret;
}

/* Fetch the rects from the active backend. */
static bool rhi_intf_read_rects(const tt_coh_rect *r, unsigned n, uint16_t *vram)
{
   bool     ret = false;
   unsigned i;

   switch (rhi_type)
   {
      case RHI_VULKAN:
#if defined(HAVE_VULKAN)
         {
            /* All parts in one submission and one fence. */
            uint16_t q[TT_COH_MAX_RECTS][4];
            for (i = 0; i < n; i++)
            {
               q[i][0] = (uint16_t)r[i].x;
               q[i][1] = (uint16_t)r[i].y;
               q[i][2] = (uint16_t)r[i].w;
               q[i][3] = (uint16_t)r[i].h;
            }
            ret = rhi_vulkan_read_vram_rects((const uint16_t (*)[4])q, n, vram);
         }
#endif
         break;
      case RHI_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         /* The first read waits for rendering; the rest find it done. */
         ret = true;
         for (i = 0; i < n && ret; i++)
            ret = rhi_gl_read_vram((uint16_t)r[i].x, (uint16_t)r[i].y,
                  (uint16_t)r[i].w, (uint16_t)r[i].h, vram);
#endif
         break;
      default:
         break;
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

uint32_t rhi_common_crop_window(int crop_overscan, int32_t x1, int32_t x2)
{
   int32_t width = x2 - x1;
   int32_t start;

   if (width <= 2560)
      return 608u | (2560u << 16);

   if (width > 2800)
      width = 2800;

   /* Every window stays on the 2800-clock line, which starts at clock
    * 488.  Where the dynamic crop pulls the picture over to the window,
    * the window moves left as far as the picture needs; elsewhere it
    * starts with the picture, and the line's end cuts what runs past. */
   if (crop_overscan == 2 && x1 < 938)
   {
      start = 3288 - width;
      if (start > 608)
         start = 608;
   }
   else
   {
      start = x1 < 488 ? 488 : x1;
      if (start > 3288 - 2560)
         start = 3288 - 2560;
      if (width > 3288 - start)
         width = 3288 - start;
   }

   return (uint32_t)start | ((uint32_t)width << 16);
}

float rhi_common_get_aspect_ratio(bool pal_content, int crop_overscan,
                                  int first_visible_scanline, int last_visible_scanline,
                                  int aspect_ratio_setting, bool vram_override, bool widescreen_override,
                                  int widescreen_hack_aspect_ratio_setting)
{
   float ar              = (4.0 / 3.0);
   const int crop_clocks = RHI_CROP_WIN_WIDTH(rhi_common_crop_window(crop_overscan,
            (int32_t)(rhi_hrange & 0xFFFFu), (int32_t)(rhi_hrange >> 16)));

   /* Current assumptions
    *    When crop_overscan isn't 0, the crop window (rhi_common_crop_window) is kept
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
      ar *= (crop_overscan ? (crop_clocks / 2800.0) : 1.0);

      /* Calculate vertical scaling in terms of visible scanline count */
      ar *= (pal_content ? (288.0 / num_vis_scanlines) : (240.0 / num_vis_scanlines));

      return ar;
   }
   else if (aspect_ratio_setting == 1) /* Uncorrected */
   {
      int width_base;
      int clock_div;
      double height_base;

      switch (rhi_width_mode)
      {
         case WIDTH_MODE_256:
            clock_div = 10;
            break;
         case WIDTH_MODE_512:
            clock_div = 5;
            break;
         case WIDTH_MODE_640:
            clock_div = 4;
            break;
         case WIDTH_MODE_368:
            clock_div = 7;
            break;
         default:
            clock_div = 8;
            break;
      }

      /* Rounded to the dot, as the software crop rounds it */
      width_base = ((crop_overscan ? crop_clocks : 2800) + clock_div / 2) / clock_div;

      height_base = (last_visible_scanline - first_visible_scanline + 1) *
                    (rhi_height_mode == HEIGHT_MODE_480 ? 2.0 : 1.0);

      /* Calculate aspect ratio as quotient of raw native framebuffer width and height */
      return width_base / height_base;
   }
   else if (aspect_ratio_setting == 3) /* Force NTSC */
   {
      int num_vis_scanlines = last_visible_scanline - first_visible_scanline + 1;

      ar *= (crop_overscan ? (crop_clocks / 2800.0) : 1.0);
      ar *= (240.0 / num_vis_scanlines);

      return ar;
   }
	else if (aspect_ratio_setting == 4) /* Force 16:9 */
   {
      return (16.0 / 9.0);
   }

   return ar; /* 4:3 */
}
