#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <boolean.h>

#include "rsx_intf.h"
#include "rsx.h"
#include "../libretro_cbs.h"
#ifdef HAVE_OPENGL
   #include "rsx_lib_gl.h"
#endif
#ifdef HAVE_VULKAN
   #include "rsx_lib_vulkan.h"
#endif
#include "rsx_lib_soft.h"

#ifdef RSX_DUMP
   #include "rsx_dump.h"
#include <stdlib.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif
   extern retro_environment_t environ_cb;
#ifdef __cplusplus
}
#endif

static enum rsx_renderer_type rsx_type          = RSX_SOFTWARE;

static bool gl_initialized                      = false;
static bool vk_initialized                      = false;

void rsx_intf_set_environment(retro_environment_t cb)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         rsx_soft_set_environment(cb);
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_environment(cb);
#endif
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
         rsx_soft_set_video_refresh(cb);
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_video_refresh(cb);
#endif
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
         rsx_soft_get_system_av_info(info);
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

bool rsx_intf_open(bool is_pal)
{
   struct retro_variable var = {0};
   bool software_selected    = false;
   vk_initialized            = false;
   gl_initialized            = false;

   var.key                   = BEETLE_OPT(renderer);

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      if (!strcmp(var.value, "software"))
         software_selected = true;

#if defined(HAVE_VULKAN)
   if (!software_selected && rsx_vulkan_open(is_pal))
   {
      rsx_type       = RSX_VULKAN;
      vk_initialized = true;
      goto end;
   }
#endif

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   if (!software_selected && rsx_gl_open(is_pal))
   {
      rsx_type       = RSX_OPENGL;
      gl_initialized = true;
      goto end;
   }
#endif

   if (rsx_soft_open(is_pal))
      goto end;

   rsx_type          = RSX_SOFTWARE;
   return rsx_intf_open(is_pal);

end:
#if defined(RSX_DUMP)
   {
      const char *env = getenv("RSX_DUMP");
      if (env)
         rsx_dump_init(env);
   }
#endif
   return true;
}

void rsx_intf_close(void)
{
#if defined(RSX_DUMP)
   rsx_dump_deinit();
#endif

#if defined(HAVE_VULKAN)
   if (rsx_type != RSX_SOFTWARE && vk_initialized)
   {
      rsx_vulkan_close();
      return;
   }
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
         rsx_soft_finalize_frame(fb, width, height, pitch);
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

void rsx_intf_set_display_mode(uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h,
                               bool depth_24bpp)
{
#ifdef RSX_DUMP
   rsx_dump_set_display_mode(x, y, w, h, depth_24bpp);
#endif

   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_display_mode(x, y, w, h, depth_24bpp);
#endif
         break;
      case RSX_VULKAN:
#if defined(HAVE_VULKAN)
         rsx_vulkan_set_display_mode(x, y, w, h, depth_24bpp);
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
			texpage_x, texpage_y, clut_x, clut_y,
			texture_blend_mode,
			depth_shift,
			dither,
			blend_mode, mask_test != 0, set_mask != 0);
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
         rsx_vulkan_copy_rect(src_x, src_y, dst_x, dst_y,
               w, h, mask_test, set_mask);
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
#else
         break;
#endif
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
