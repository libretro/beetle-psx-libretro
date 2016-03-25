#include <stdint.h>

#include <boolean.h>

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
#include <glsm/glsm.h>
#endif

#include "rsx.h"

static bool rsx_is_pal = false;
static retro_video_refresh_t rsx_video_cb;
static retro_environment_t rsx_environ_cb;

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
static bool fb_ready = false;

static void context_reset(void)
{
   printf("context_reset.\n");
   glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL);

   if (!glsm_ctl(GLSM_CTL_STATE_SETUP, NULL))
      return;

   fb_ready = true;
}

static void context_destroy(void)
{
}

static bool context_framebuffer_lock(void *data)
{
   if (fb_ready)
      return false;
   return true;
}
#endif

void rsx_set_environment(retro_environment_t cb)
{
   rsx_environ_cb = cb;
}

void rsx_set_video_refresh(retro_video_refresh_t cb)
{
   rsx_video_cb   = cb;
}

void rsx_get_system_av_info(struct retro_system_av_info *info)
{
}

void rsx_init(void)
{
}

bool rsx_open(bool is_pal)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   glsm_ctx_params_t params = {0};
#endif
   rsx_is_pal = is_pal;

#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   params.context_reset         = context_reset;
   params.context_destroy       = context_destroy;
   params.environ_cb            = environ_cb;
   params.stencil               = true;
   params.imm_vbo_draw          = NULL;
   params.imm_vbo_disable       = NULL;
   params.framebuffer_lock      = context_framebuffer_lock;

   if (!glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params))
      return false;   
#endif

   return true;
}

void rsx_close(void)
{
}

void rsx_refresh_variables(void)
{
}

void rsx_prepare_frame(void)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   glsm_ctl(GLSM_CTL_STATE_BIND, NULL);
#endif
}

void rsx_finalize_frame(const void *fb, unsigned width, 
      unsigned height, unsigned pitch)
{
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   rsx_video_cb(RETRO_HW_FRAME_BUFFER_VALID,
         width, height, pitch);

   glsm_ctl(GLSM_CTL_STATE_UNBIND, NULL);
#else
   rsx_video_cb(fb, width, height, pitch);
#endif
}

void rsx_set_draw_offset(int16_t x, int16_t y)
{
}

void rsx_set_draw_area(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{
}

void rsx_set_display_mode(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      bool depth_24bpp)
{
}

void rsx_push_triangle(int16_t p0x, int16_t p0y,
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
}

void rsx_push_line(int16_t p0x, int16_t p0y,
      int16_t p1x, int16_t p1y,
      uint32_t c0,
      uint32_t c1,
      bool dither)
{
}

void rsx_load_image(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram)
{
}

void rsx_fill_rect(uint32_t color,
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{
}

void rsx_copy_rect(uint16_t src_x, uint16_t src_y,
      uint16_t dst_x, uint16_t dst_y,
      uint16_t w, uint16_t h)
{
}
