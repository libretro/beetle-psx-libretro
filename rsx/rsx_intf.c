#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <boolean.h>

#include "rsx_intf.h"
#ifdef HAVE_RUST
#include "rsx.h"
#endif
#ifdef HAVE_OPENGL
#include "rsx_lib_gl.h"
#endif
uint8_t psx_gpu_upscale_shift;
uint8_t widescreen_hack;

static enum rsx_renderer_type rsx_type = 
#ifdef HAVE_RUST
RSX_EXTERNAL_RUST
#else
RSX_SOFTWARE
#endif
;
static bool rsx_is_pal = false;
static retro_video_refresh_t rsx_video_cb;
static retro_environment_t rsx_environ_cb;

void rsx_intf_set_environment(retro_environment_t cb)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         rsx_environ_cb = cb;
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_environment(cb);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_set_environment(cb);
#endif
         break;
   }
}

void rsx_intf_set_video_refresh(retro_video_refresh_t cb)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         rsx_video_cb   = cb;
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_video_refresh(cb);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_set_video_refresh(cb);
#endif
         break;
   }
}

static float video_output_framerate(void)
{
   return rsx_is_pal ? 49.842 : 59.941;
}

void rsx_intf_get_system_av_info(struct retro_system_av_info *info)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         memset(info, 0, sizeof(*info));
         info->timing.fps            = video_output_framerate();
         info->timing.sample_rate    = 44100;
         info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W << psx_gpu_upscale_shift;
         info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H << psx_gpu_upscale_shift;
         info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W << psx_gpu_upscale_shift;
         info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H << psx_gpu_upscale_shift;
         info->geometry.aspect_ratio = !widescreen_hack ? MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO : (float)16/9;
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_get_system_av_info(info);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_get_system_av_info(info);
#endif
         break;
   }
}

void rsx_intf_init(enum rsx_renderer_type type)
{
   rsx_type = type;

   switch (rsx_type)
   {
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_init();
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_init();
#endif
         break;
      default:
         break;
   }
}

enum rsx_renderer_type rsx_intf_is_type(void)
{
   return rsx_type;
}

bool rsx_intf_open(bool is_pal)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         rsx_is_pal = is_pal;
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         if (!rsx_gl_open(is_pal))
            return false;
         rsx_type   = RSX_OPENGL;
         rsx_is_pal = is_pal;
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_open(is_pal);
#endif
         break;
   }


   return true;
}

void rsx_intf_close(void)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_close();
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_close();
#endif
         break;
   }
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
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_refresh_variables();
#endif
         break;
   }
}

void rsx_intf_prepare_frame(void)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_prepare_frame();
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_prepare_frame();
#endif
         break;
   }
}

void rsx_intf_finalize_frame(const void *fb, unsigned width, 
      unsigned height, unsigned pitch)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         rsx_video_cb(fb, width, height, pitch);
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_finalize_frame(fb, width, height, pitch);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_finalize_frame();
#endif
         break;
   }
}

void rsx_intf_set_draw_offset(int16_t x, int16_t y)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_draw_offset(x, y);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_set_draw_offset(x, y);
#endif
         break;
   }
}

void rsx_intf_set_draw_area(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_draw_area(x, y, w, h);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_set_draw_area(x, y, w, h);
#endif
         break;
   }
}

void rsx_intf_set_display_mode(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      bool depth_24bpp)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_display_mode(x, y, w, h, depth_24bpp);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_set_display_mode(x, y, w, h, depth_24bpp);
#endif
         break;
   }
}

void rsx_intf_push_triangle(int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      int16_t p2x, int16_t p2y,
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
      bool dither)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_push_triangle(p0x, p0y, p1x, p1y, p2x, p2y,
               c0, c1, c2, t0x, t0y, t1x, t1y, t2x, t2y,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_push_triangle(p0x, p0y, p1x, p1y, p2x, p2y,
               c0, c1, c2, t0x, t0y, t1x, t1y, t2x, t2y,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither);
#endif
         break;
   }
}

void rsx_intf_push_line(int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0,
      uint32_t c1,
      bool dither)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_push_line(p0x, p0y, p1x, p1y, c0, c1, dither);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_push_line(p0x, p0y, p1x, p1y, c0, c1, dither);
#endif
         break;
   }
}

void rsx_intf_load_image(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_load_image(x, y, w, h, vram);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_load_image(x, y, w, h, vram);
#endif
         break;
   }
}

void rsx_intf_fill_rect(uint32_t color,
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_fill_rect(color, x, y, w, h);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_fill_rect(color, x, y, w, h);
#endif
         break;
   }
}

void rsx_intf_copy_rect(uint16_t src_x, uint16_t src_y,
      uint16_t dst_x, uint16_t dst_y,
      uint16_t w, uint16_t h)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_copy_rect(src_x, src_y, dst_x, dst_y,
               w, h);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_copy_rect(src_x, src_y, dst_x, dst_y,
               w, h);
#endif
         break;
   }
}
