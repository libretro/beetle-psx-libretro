#include "rsx_lib_gl.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h> /* exit() */

#include <boolean.h>

static RetroGl* static_renderer = NULL; 

static bool rsx_gl_is_pal = false;

void renderer_gl_free(void)
{
#if 0
   if !static_renderer.is_null()
   {
      let _ = Box::from_raw(static_renderer);
      static_renderer = ptr::null_mut();
   }
#endif
}

RetroGl* renderer(void)
{
  RetroGl* r = static_renderer;
  if (r)
    return r;

  printf("Attempted to use a NULL renderer\n");
  exit(EXIT_FAILURE);
}

static void set_renderer(RetroGl* renderer)
{
  static_renderer = renderer;
}

static void drop_renderer()
{
  static_renderer = NULL;  
}

void rsx_gl_init(void)
{
}

bool rsx_gl_open(bool is_pal)
{
   rsx_gl_is_pal = is_pal;

   VideoClock clock = is_pal ? VideoClock::Pal : VideoClock::Ntsc;
   set_renderer( RetroGl::getInstance(clock) );

   return true;
}

void rsx_gl_close(void)
{
    drop_renderer();
}

void rsx_gl_refresh_variables(void)
{
   if (static_renderer)
      static_renderer->refresh_variables();
}

void rsx_gl_prepare_frame(void)
{
   renderer()->prepare_render();
}

void rsx_gl_finalize_frame(const void *fb, unsigned width,
      unsigned height, unsigned pitch)
{
   renderer()->finalize_frame();

}

void rsx_gl_set_environment(retro_environment_t callback)
{
   /* TODO/FIXME - might not be necessary */
}

void rsx_gl_set_video_refresh(retro_video_refresh_t callback)
{
   /* TODO/FIXME - might not be necessary */
}

/* Precise FPS values for the video output for the given
 * VideoClock. It's actually possible to configure the PlayStation GPU
 * to output with NTSC timings with the PAL clock (and vice-versa)
 * which would make this code invalid but it wouldn't make a lot of
 * sense for a game to do that.
 */
static float video_output_framerate(void)
{
   /* NTSC - 53.690MHz GPU clock frequency, 263 lines per field,
    * 3413 cycles per line */
   return rsx_gl_is_pal ? 49.76 : 59.81;
}

void rsx_gl_get_system_av_info(struct retro_system_av_info *info)
{
   /* TODO/FIXME - This definition seems very backwards and duplicating work */

   /* This will possibly trigger the frontend to reconfigure itself */
   rsx_gl_refresh_variables();

   struct retro_system_av_info result = renderer()->get_system_av_info();
   memcpy(info, &result, sizeof(result));
}

/* Draw commands */

void rsx_gl_set_draw_offset(int16_t x, int16_t y)
{
   renderer()->gl_renderer()->set_draw_offset(x, y);
}

void  rsx_gl_set_draw_area(uint16_t x,
      uint16_t y,
      uint16_t w,
      uint16_t h)
{
   uint16_t top_left[2]   = {x, y};
   uint16_t dimensions[2] = {w, h};
   renderer()->gl_renderer()->set_draw_area(top_left, dimensions);
}

void rsx_gl_set_display_mode(uint16_t x,
      uint16_t y,
      uint16_t w,
      uint16_t h,
      bool depth_24bpp)
{
   uint16_t top_left[2]   = {x, y};
   uint16_t dimensions[2] = {w, h};
   renderer()->gl_renderer()->set_display_mode(top_left, dimensions, depth_24bpp);
}

void rsx_gl_push_triangle(
      int16_t p0x,
      int16_t p0y,
      int16_t p1x,
      int16_t p1y,
      int16_t p2x,
      int16_t p2y,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint16_t t0x,
      uint16_t t0y,
      uint16_t t1x,
      uint16_t t1y,
      uint16_t t2x,
      uint16_t t2y,
      uint16_t texpage_x,
      uint16_t texpage_y,
      uint16_t clut_x,
      uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode)
{
   SemiTransparencyMode semi_transparency_mode = SemiTransparencyMode::Add;
   bool semi_transparent = false;
   switch (blend_mode) {
   case -1:
      semi_transparent = false;
      semi_transparency_mode = SemiTransparencyMode::Add;
      break;
   case 0:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode::Average;
      break;
   case 1:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode::Add;
      break;
   case 2:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode::SubtractSource;
      break;
   case 3:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode::AddQuarterSource;
      break;
   default:
      exit(EXIT_FAILURE);
   }

   CommandVertex v[3] = 
   {
      {
          {p0x, p0y},   /* position */
          {(uint8_t) c0, (uint8_t) (c0 >> 8), (uint8_t) (c0 >> 16)}, /* color */
          {t0x, t0y},   /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},         
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent
      },
      {
          {p1x, p1y}, /* position */
          {(uint8_t) c1, (uint8_t) (c1 >> 8), (uint8_t) (c1 >> 16)}, /* color */
          {t1x, t1y}, /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},   
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent
      },
      {
          {p2x, p2y}, /* position */
          {(uint8_t) c2, (uint8_t) (c2 >> 8), (uint8_t) (c2 >> 16)}, /* color */
          {t2x, t2y}, /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},   
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent
      }
   };

   renderer()->gl_renderer()->push_triangle(v, semi_transparency_mode);
}

void rsx_gl_fill_rect(uint32_t color,
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{

   uint16_t top_left[2]   = {x, y};
   uint16_t dimensions[2] = {w, h};
   uint8_t col[3] = {(uint8_t) color, (uint8_t) (color >> 8), (uint8_t) (color >> 16)};  

   renderer()->gl_renderer()->fill_rect(col, top_left, dimensions);
}

void rsx_gl_copy_rect(
      uint16_t src_x, uint16_t src_y,
      uint16_t dst_x, uint16_t dst_y,
      uint16_t w, uint16_t h)
{
    uint16_t src_pos[2] = {src_x, src_y};
    uint16_t dst_pos[2] = {dst_x, dst_y};
    uint16_t dimensions[2] = {w, h}; 

    renderer()->gl_renderer()->copy_rect(src_pos, dst_pos, dimensions);
}

void rsx_gl_push_line(int16_t p0x,
      int16_t p0y,
      int16_t p1x,
      int16_t p1y,
      uint32_t c0,
      uint32_t c1,
      bool dither,
      int blend_mode)
{
   SemiTransparencyMode semi_transparency_mode = SemiTransparencyMode::Add;
   bool semi_transparent = false;
   switch (blend_mode) {
   case -1:
      semi_transparent = false;
      semi_transparency_mode = SemiTransparencyMode::Add;
      break;
   case 0:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode::Average;
      break;
   case 1:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode::Add;
      break;
   case 2:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode::SubtractSource;
      break;
   case 3:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode::AddQuarterSource;
      break;
   default:
      exit(EXIT_FAILURE);
   }

   CommandVertex v[2] = {
      {
          {p0x, p0y}, /* position */
          {(uint8_t) c0, (uint8_t) (c0 >> 8), (uint8_t) (c0 >> 16)}, /* color */
          {0, 0}, /* texture_coord */
          {0, 0}, /* texture_page */
          {0, 0}, /* clut */
          0,      /* texture_blend_mode */
          0,      /* depth_shift */
          (uint8_t) dither,
          semi_transparent
      },
      {
          {p1x, p1y}, /* position */
          {(uint8_t) c1, (uint8_t) (c1 >> 8), (uint8_t) (c1 >> 16)}, /* color */
          {0, 0}, /* texture_coord */
          {0, 0}, /* texture_page */
          {0, 0}, /* clut */
          0,      /* texture_blend_mode */
          0,      /* depth_shift */
          (uint8_t) dither,
          semi_transparent
      }
   };

   renderer()->gl_renderer()->push_line(v, semi_transparency_mode);
}

void rsx_gl_load_image(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram)
{
   uint16_t top_left[2]   = {x, y};
   uint16_t dimensions[2] = {w, h};

   /* TODO FIXME - upload_vram_window expects a 
  uint16_t[VRAM_HEIGHT*VRAM_WIDTH_PIXELS] array arg instead of a ptr */
   renderer()->gl_renderer()->upload_vram_window(top_left, dimensions, vram);
}
