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
#include "rsx_lib_soft.h"

static enum rsx_renderer_type rsx_type = 
#ifdef HAVE_RUST
RSX_EXTERNAL_RUST
#elif defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
RSX_OPENGL
#else
RSX_SOFTWARE
#endif
;

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
         rsx_soft_set_video_refresh(cb);
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

void rsx_intf_set_type(enum rsx_renderer_type type)
{
   rsx_type = type;
}

bool rsx_intf_open(bool is_pal)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         if (!rsx_soft_open(is_pal))
            return false;
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         if (!rsx_gl_open(is_pal))
            return false;
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         if (!rsx_open(is_pal))
            return false;
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
         rsx_soft_finalize_frame(fb, width, height, pitch);
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

void rsx_intf_set_tex_window(uint8_t tww, uint8_t twh,
      uint8_t twx, uint8_t twy)
{
   switch (rsx_type)
   {
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_set_tex_window(tww, twh, twx, twy);
#endif
         break;
      default:
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

void rsx_intf_push_triangle(
      int16_t p0x, int16_t p0y,
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
      bool dither,
      int blend_mode)
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
               dither,
               blend_mode);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_push_triangle(p0x, p0y, p1x, p1y, p2x, p2y,
               c0, c1, c2, t0x, t0y, t1x, t1y, t2x, t2y,
               texpage_x, texpage_y, clut_x, clut_y,
               texture_blend_mode,
               depth_shift,
               dither,
               blend_mode);
#endif
         break;
   }
}

void rsx_intf_push_line(int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0,
      uint32_t c1,
      bool dither,
      int blend_mode)
{
   switch (rsx_type)
   {
      case RSX_SOFTWARE:
         break;
      case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         rsx_gl_push_line(p0x, p0y, p1x, p1y, c0, c1, dither, blend_mode);
#endif
         break;
      case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
         rsx_push_line(p0x, p0y, p1x, p1y, c0, c1, dither, blend_mode);
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

void rsx_intf_toggle_display(bool status)
{
    switch (rsx_type)
    {
    case RSX_SOFTWARE:
        break;
    case RSX_OPENGL:
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
        rsx_gl_toggle_display(status);
#endif
        break;
    case RSX_EXTERNAL_RUST:
#ifdef HAVE_RUST
        puts("rsx_toggle_display: NOT IMPLEMENTED!");
#endif
        break;
    }
}
