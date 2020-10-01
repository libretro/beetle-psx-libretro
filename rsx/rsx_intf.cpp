#include "rsx/rsx_intf.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boolean.h"
#include "libretro.h"

#include "beetle_psx_globals.h"
#include "libretro_cbs.h"
#include "libretro_options.h"
#include "mednafen/mednafen.h"
#include "mednafen/psx/gpu.h"
#include "mednafen/settings.h"

#ifdef RSX_DUMP
#include "rsx_dump.h"
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
#include "rsx_lib_gl.h"
#endif

#if defined(HAVE_VULKAN)
#include "rsx_lib_vulkan.h"
#endif

extern bool fast_pal;

static enum rsx_renderer_type rsx_type          = RSX_SOFTWARE;

static bool gl_initialized                      = false;
static bool vk_initialized                      = false;

// GPU reset defaults
static int rsx_width_mode = WIDTH_MODE_256;
static int rsx_height_mode = HEIGHT_MODE_240;

static bool rsx_soft_open(bool is_pal)
{
   content_is_pal = is_pal;
   return true;
}

void rsx_intf_set_environment(retro_environment_t cb)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_environment(cb);
#endif
         break;
   }
}

void rsx_intf_set_video_refresh(retro_video_refresh_t cb)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_video_refresh(cb);
#endif
         break;
   }
}

void rsx_intf_get_system_av_info(struct retro_system_av_info *info)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         memset(info, 0, sizeof(*info));
         info->timing.fps            = rsx_common_get_timing_fps();
         info->timing.sample_rate    = SOUND_FREQUENCY;
         info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W;
         info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H;
         info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W  << psx_gpu_upscale_shift;
         info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H  << psx_gpu_upscale_shift;
         info->geometry.aspect_ratio = rsx_common_get_aspect_ratio(content_is_pal, crop_overscan,
                                          MDFN_GetSettingI(content_is_pal ? "psx.slstartp" : "psx.slstart"),
                                          MDFN_GetSettingI(content_is_pal ? "psx.slendp" : "psx.slend"),
                                          aspect_ratio_setting, false, widescreen_hack);
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_get_system_av_info(info);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_get_system_av_info(info);
#endif
         break;
   }
}

enum rsx_renderer_type rsx_intf_is_type(void)
{
   return rsx_type;
}

inline void rsx_intf_dump_init(void)
{
#if defined(RSX_DUMP)
   {
      const char *env = getenv("RSX_DUMP");
      if (env)
         rsx_dump_init(env);
   }
#endif
}

bool rsx_intf_open(bool is_pal, bool force_software)
{
   struct retro_variable var = {0};
   bool software_selected    = false;
   vk_initialized            = false;
   gl_initialized            = false;

   enum force_renderer_type force_type = AUTO;

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
         if (rsx_vulkan_open(is_pal))
         {
            rsx_type = RSX_VULKAN;
            vk_initialized = true;
            rsx_intf_dump_init();
            return true;
         }
         else
         {
            MDFND_DispMessage(3, RETRO_LOG_ERROR,
                  RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
                  "Could not force Vulkan renderer. Falling back to software renderer.");

            goto soft;
         }
#else
         MDFND_DispMessage(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "Attempted to force Vulkan renderer, but core was built without it. Falling back to software renderer.");

         goto soft;
#endif
      }
      else if (force_type == FORCE_OPENGL)
      {
#if defined (HAVE_OPENGL) || defined(HAVE_OPENGLES)
         if (rsx_gl_open(is_pal))
         {
            rsx_type = RSX_OPENGL;
            gl_initialized = true;
            rsx_intf_dump_init();
            return true;
         }
         else
         {
            MDFND_DispMessage(3, RETRO_LOG_ERROR,
                  RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
                  "Could not force OpenGL renderer. Falling back to software renderer.");

            goto soft;
         }
#else
         MDFND_DispMessage(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "Attempted to force OpenGL renderer, but core was built without it. Falling back to software renderer.");

         goto soft;
#endif
      }
      /* End forces section */

      unsigned preferred = 0; // This will be set to RETRO_HW_CONTEXT_DUMMY if GET_PREFERRED_HW_RENDER is not supported by frontend
      if (!environ_cb(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &preferred))
      {
         preferred = RETRO_HW_CONTEXT_DUMMY;
      }
      /* If GET_PREFERRED_HW_RENDER is not supported by frontend, then we just go
       * down the list attempting to open a hardware renderer until we get one */

#if defined(HAVE_VULKAN)
      if ((preferred == RETRO_HW_CONTEXT_DUMMY ||
           preferred == RETRO_HW_CONTEXT_VULKAN)
          && rsx_vulkan_open(is_pal))
      {
         rsx_type       = RSX_VULKAN;
         vk_initialized = true;
         rsx_intf_dump_init();
         return true;
      }
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
      if ((preferred == RETRO_HW_CONTEXT_DUMMY ||
           preferred == RETRO_HW_CONTEXT_OPENGL_CORE)
          && rsx_gl_core_open(is_pal))
      {
         rsx_type       = RSX_OPENGL;
         gl_initialized = true;
         rsx_intf_dump_init();
         return true;
      }

      if ((preferred == RETRO_HW_CONTEXT_DUMMY ||
           preferred == RETRO_HW_CONTEXT_OPENGL)
          && rsx_gl_open(is_pal))
      {
         rsx_type       = RSX_OPENGL;
         gl_initialized = true;
         rsx_intf_dump_init();
         return true;
      }
#endif

      if (preferred == RETRO_HW_CONTEXT_DUMMY)
         MDFND_DispMessage(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "No hardware renderers could be opened. Falling back to software renderer.");
      else
         MDFND_DispMessage(3, RETRO_LOG_ERROR,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "Unable to find or open hardware renderer for frontend preferred hardware context. Falling back to software renderer.");
   }

soft:
   // rsx_soft_open(is_pal) always returns true
   if (rsx_soft_open(is_pal))
   {
      rsx_type = RSX_SOFTWARE;
      rsx_intf_dump_init();
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

void rsx_intf_close(void)
{
#if defined(RSX_DUMP)
   rsx_dump_deinit();
#endif

#if defined(HAVE_VULKAN)
   if (rsx_type != RSX_SOFTWARE && vk_initialized)
      return;
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   if (rsx_type != RSX_SOFTWARE && gl_initialized)
   {
      rsx_gl_close();
      return;
   }
#endif
}

void rsx_intf_refresh_variables(void)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_refresh_variables();
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_refresh_variables();
#endif
         break;
   }
}

void rsx_intf_prepare_frame(void)
{
#ifdef RSX_DUMP
   rsx_dump_prepare_frame();
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_prepare_frame();
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_prepare_frame();
#endif
         break;
   }
}

void rsx_intf_finalize_frame(const void *fb, unsigned width, 
                             unsigned height, unsigned pitch)
{
#ifdef RSX_DUMP
   rsx_dump_finalize_frame();
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         video_cb(fb, width, height, pitch);
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_finalize_frame(fb, width, height, pitch);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_finalize_frame(fb, width, height, pitch);
#endif
         break;
   }
}

void rsx_intf_set_tex_window(uint8_t tww, uint8_t twh,
                             uint8_t twx, uint8_t twy)
{
#ifdef RSX_DUMP
   rsx_dump_set_tex_window(tww, twh, twx, twy);
#endif

   switch (rsx_type)
   {
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_tex_window(tww, twh, twx, twy);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_tex_window(tww, twh, twx, twy);
#endif
         break;
      default:
         break;
   }
}

void rsx_intf_set_mask_setting(uint32_t mask_set_or, uint32_t mask_eval_and)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
         break;
      case RSX_VULKAN:
         /* TODO/FIXME */
         break;
   }
}

void rsx_intf_set_draw_offset(int16_t x, int16_t y)
{
#ifdef RSX_DUMP
   rsx_dump_set_draw_offset(x, y);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_draw_offset(x, y);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_draw_offset(x, y);
#endif
         break;
   }
}

void rsx_intf_set_draw_area(uint16_t x0, uint16_t y0,
                            uint16_t x1, uint16_t y1)
{
#ifdef RSX_DUMP
   rsx_dump_set_draw_area(x0, y0, x1, y1);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_draw_area(x0, y0, x1, y1);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_draw_area(x0, y0, x1, y1);
#endif
         break;
   }
}

void rsx_intf_set_vram_framebuffer_coords(uint32_t xstart, uint32_t ystart)
{
#ifdef RSX_DUMP
   rsx_dump_set_vram_framebuffer_coords(xstart, ystart);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_vram_framebuffer_coords(xstart, ystart);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_vram_framebuffer_coords(xstart, ystart);
#endif
         break;
   }
}

void rsx_intf_set_horizontal_display_range(uint16_t x1, uint16_t x2)
{
#ifdef RSX_DUMP
   rsx_dump_set_horizontal_display_range(x1, x2);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_horizontal_display_range(x1, x2);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_horizontal_display_range(x1, x2);
#endif
         break;
   }
}

void rsx_intf_set_vertical_display_range(uint16_t y1, uint16_t y2)
{
#ifdef RSX_DUMP
   rsx_dump_set_vertical_display_range(y1, y2);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_vertical_display_range(y1, y2);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_vertical_display_range(y1, y2);
#endif
         break;
   }
}

void rsx_intf_set_display_mode(bool depth_24bpp,
                               bool is_pal, 
                               bool is_480i,
                               int width_mode)
{
#ifdef RSX_DUMP
   rsx_dump_set_display_mode(depth_24bpp, is_pal, is_480i, width_mode);
#endif

   // Is this check accurate for 240i timing? May need to be fixed later
   if (currently_interlaced != is_480i)
   {
      currently_interlaced = is_480i;
      interlace_setting_dirty = true;
   }

   // Also verify if this is accurate for 240i
   if ((rsx_width_mode != width_mode) || (rsx_height_mode != (int)is_480i))
   {
      rsx_width_mode = width_mode;
      rsx_height_mode = (int)is_480i;
      aspect_ratio_dirty = true;
   }

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_display_mode(depth_24bpp, is_pal, is_480i, width_mode);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_display_mode(depth_24bpp, is_pal, is_480i, width_mode);
#endif
         break;
   }
}

void rsx_intf_push_triangle(
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
      uint32_t mask_test,
      uint32_t set_mask)
{
#ifdef RSX_DUMP
   const rsx_dump_vertex vertices[3] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
   };
   const rsx_render_state state = {
      texpage_x, texpage_y, clut_x, clut_y, texture_blend_mode, depth_shift, dither, blend_mode,
      mask_test, set_mask,
   };
   rsx_dump_triangle(vertices, &state);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_push_triangle(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w,
               c0, c1, c2, t0x, t0y, t1x, t1y, t2x, t2y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode, mask_test != 0, set_mask != 0);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_push_triangle(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w,
               c0, c1, c2, t0x, t0y, t1x, t1y, t2x, t2y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode, mask_test != 0, set_mask != 0);
#endif
         break;
   }
}

void rsx_intf_push_quad(
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
   uint32_t mask_test,
   uint32_t set_mask,
   bool is_sprite,
   bool may_be_2d)
{
#ifdef RSX_DUMP
   const rsx_dump_vertex vertices[4] = {
      { p0x, p0y, p0w, c0, t0x, t0y },
      { p1x, p1y, p1w, c1, t1x, t1y },
      { p2x, p2y, p2w, c2, t2x, t2y },
      { p3x, p3y, p3w, c3, t3x, t3y },
   };
   const rsx_render_state state = {
      texpage_x, texpage_y, clut_x, clut_y, texture_blend_mode, depth_shift, dither, blend_mode,
      mask_test, set_mask,
   };
   rsx_dump_quad(vertices, &state);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_push_quad(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w, p3x, p3y, p3w,
               c0, c1, c2, c3,
               t0x, t0y, t1x, t1y, t2x, t2y, t3x, t3y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode, mask_test != 0, set_mask != 0);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_push_quad(p0x, p0y, p0w, p1x, p1y, p1w, p2x, p2y, p2w, p3x, p3y, p3w,
               c0, c1, c2, c3,
               t0x, t0y, t1x, t1y, t2x, t2y, t3x, t3y,
               min_u, min_v, max_u, max_v,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode, mask_test != 0, set_mask != 0, is_sprite, may_be_2d);
#endif
         break;
   }
}

void rsx_intf_push_line(int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0, uint32_t c1,
      bool dither,
      int blend_mode,
      uint32_t mask_test,
      uint32_t set_mask)
{
#ifdef RSX_DUMP
   const rsx_dump_line_data line = {
      p0x, p0y, p1x, p1y, c0, c1, dither, blend_mode,
      mask_test != 0, set_mask != 0,
   };
   rsx_dump_line(&line);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_push_line(p0x, p0y, p1x, p1y, c0, c1, dither, blend_mode, mask_test != 0, set_mask != 0);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_push_line(p0x, p0y, p1x, p1y, c0, c1, dither, blend_mode, mask_test != 0, set_mask != 0);
#endif
         break;
   }
}

bool rsx_intf_read_vram(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *vram)
{
   switch (rsx_type)
   {
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         return rsx_vulkan_read_vram(x, y, w, h, vram);
#endif
         break;
      default:
         break;
   }

   return false;
}

void rsx_intf_load_image(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram, uint32_t mask_test, uint32_t set_mask)
{
#ifdef RSX_DUMP
   rsx_dump_load_image(x, y, w, h, vram, mask_test != 0, set_mask != 0);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_load_image(x, y, w, h, vram, mask_test, set_mask);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_load_image(x, y, w, h, vram, mask_test, set_mask);
#endif
         break;
   }
}

void rsx_intf_fill_rect(uint32_t color,
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{
#ifdef RSX_DUMP
   rsx_dump_fill_rect(color, x, y, w, h);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_fill_rect(color, x, y, w, h);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_fill_rect(color, x, y, w, h);
#endif
         break;
   }
}

void rsx_intf_copy_rect(uint16_t src_x, uint16_t src_y,
      uint16_t dst_x, uint16_t dst_y,
      uint16_t w, uint16_t h, 
      uint32_t mask_test, uint32_t set_mask)
{
#ifdef RSX_DUMP
   rsx_dump_copy_rect(src_x, src_y, dst_x, dst_y, w, h, mask_test != 0, set_mask != 0);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_copy_rect(src_x, src_y, dst_x, dst_y,
               w, h, mask_test, set_mask);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_copy_rect(src_x, src_y, dst_x, dst_y, w, h, mask_test, set_mask);
#endif
         break;
   }
}

bool rsx_intf_has_software_renderer(void)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         return true;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         return rsx_gl_has_software_renderer();
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         return rsx_vulkan_has_software_renderer();
#else
         break;
#endif
   }

   return false;
}

void rsx_intf_toggle_display(bool status)
{
#ifdef RSX_DUMP
   rsx_dump_toggle_display(status);
#endif

    switch (rsx_type)
    {
    case RSX_SOFTWARE:
        break;
    case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
      rsx_gl_toggle_display(status);
#endif
        break;
    case RSX_VULKAN:
#if defined(HAVE_VULKAN)
      rsx_vulkan_toggle_display(status);
#endif
        break;
    }
}

double rsx_common_get_timing_fps(void)
{
   bool pal_timings = content_is_pal && !fast_pal;

   if (core_timing_fps_mode == FORCE_PROGRESSIVE_TIMING)
      return (pal_timings ? FPS_PAL_NONINTERLACED : FPS_NTSC_NONINTERLACED);

   else if (core_timing_fps_mode == FORCE_INTERLACED_TIMING)
      return (pal_timings ? FPS_PAL_INTERLACED : FPS_NTSC_INTERLACED);

   //else AUTO_TOGGLE_TIMING
   return (pal_timings ?
               (currently_interlaced ? FPS_PAL_INTERLACED : FPS_PAL_NONINTERLACED) :
               (currently_interlaced ? FPS_NTSC_INTERLACED : FPS_NTSC_NONINTERLACED));
}


float rsx_common_get_aspect_ratio(bool pal_content, bool crop_overscan,
                                  int first_visible_scanline, int last_visible_scanline,
                                  int aspect_ratio_setting, bool vram_override, bool widescreen_override)
{
   // Current assumptions
   //    A fixed percentage of width is cropped when crop_overscan is true
   //    aspect_ratio_setting is one of the following:
   //          0 - Corrected
   //          1 - Uncorrected (1:1 PAR)
   //          2 - Force 4:3 (traditionally what Beetle PSX has done prior to adding in this setting)
   //          3 - Force NTSC (get corrected NTSC aspect ratio even with PAL games)

   // Aspect ratio overrides - VRAM and widescreen take precedence

   if (vram_override)
      return 2.0 / 1.0;

   if (widescreen_override)
      return 16.0 / 9.0;

   float ar = (4.0 / 3.0);

   if (aspect_ratio_setting == 0) // Corrected
   {
      // Calculate horizontal scaling in terms of gpu clock cycles
      ar *= (crop_overscan ? (2560.0 / 2800.0) : 1.0);

      // Calculate vertical scaling in terms of visible scanline count
      int num_vis_scanlines = last_visible_scanline - first_visible_scanline + 1;
      ar *= (pal_content ? (288.0 / num_vis_scanlines) : (240.0 / num_vis_scanlines));

      return ar;
   }
   else if (aspect_ratio_setting == 1) // Uncorrected
   {
      int width_base = 0;
      switch (rsx_width_mode)
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
            // Probably slightly off because of rounding, see libretro.cpp comments
            width_base = crop_overscan ? 366 : 400;
            break;
      }

      double height_base = (last_visible_scanline - first_visible_scanline + 1) *
                           (rsx_height_mode == HEIGHT_MODE_480 ? 2.0 : 1.0);

      // Calculate aspect ratio as quotient of raw native framebuffer width and height
      return width_base / height_base;
   }
   else if (aspect_ratio_setting == 3) // Force NTSC
   {
      ar *= (crop_overscan ? (2560.0 / 2800.0) : 1.0);

      int num_vis_scanlines = last_visible_scanline - first_visible_scanline + 1;
      ar *= (240.0 / num_vis_scanlines);

      return ar;
   }

   return ar; // 4:3
}
