#include "rsx_lib_gl.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h> /* exit() */

#include <stdexcept>

#include <glsm/glsmsym.h>

#include <boolean.h>

#include "../rustation-libretro/src/renderer/GlRenderer.h"

#include "libretro.h"
#include "libretro_options.h"

enum VideoClock {
    VideoClock_Ntsc,
    VideoClock_Pal
};

/// State machine dealing with OpenGL context
/// destruction/reconstruction
enum GlState {
    // OpenGL context is ready
    GlState_Valid,
    /// OpenGL context has been destroyed (or is not created yet)
    GlState_Invalid
};

struct GlStateData {
    GlRenderer* r;
    DrawConfig c;
};

class RetroGl {
/* Everything private is the singleton requirements... */
private:
     // new(video_clock: VideoClock)
    RetroGl(VideoClock video_clock);
    static bool isCreated;
public:
    static RetroGl* getInstance(VideoClock video_clock);
    static RetroGl* getInstance();
    /* 
    Rust's enums members can contain data. To emulate that,
    I'll use a helper struct to save the data.  
    */
    GlStateData state_data;
    GlState state;
    VideoClock video_clock;

    ~RetroGl();

    void context_reset();
    void context_destroy();
    void prepare_render();
    void finalize_frame();
    void refresh_variables();
    retro_system_av_info get_system_av_info();
    bool has_software_renderer();

    /* This was stolen from rsx_lib_gl */
    bool context_framebuffer_lock(void *data);
};

/* This was originally in rustation-libretro/lib.rs */
retro_system_av_info get_av_info(VideoClock std);

/* TODO - Get rid of these shims */
static void shim_context_reset();
static void shim_context_destroy();
static bool shim_context_framebuffer_lock(void* data);

/*
*
*   THIS CLASS IS A SINGLETON!
*   TODO: Fix the above.
*
*/

bool RetroGl::isCreated = false;

RetroGl* RetroGl::getInstance(VideoClock video_clock)
{
   static RetroGl *single = NULL;
   if (single && isCreated)
   {
      return single;
   } else {
      try {
         single = new RetroGl(video_clock);
      } catch (const std::runtime_error &) {
         return NULL;
      }
      isCreated = true;
      return single;
   }
}

RetroGl* RetroGl::getInstance()
{
    return RetroGl::getInstance(VideoClock_Ntsc);
}

RetroGl::RetroGl(VideoClock video_clock)
{
    retro_pixel_format f = RETRO_PIXEL_FORMAT_XRGB8888;
    if ( !environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &f) ) {
        puts("Can't set pixel format\n");
        exit(EXIT_FAILURE);
    }

    /* glsm related setup */
    glsm_ctx_params_t params = {0};

    params.context_reset         = shim_context_reset;
    params.context_destroy       = shim_context_destroy;
    params.framebuffer_lock      = shim_context_framebuffer_lock;
    params.environ_cb            = environ_cb;
    params.stencil               = false;
    params.imm_vbo_draw          = NULL;
    params.imm_vbo_disable       = NULL;

    if ( !glsm_ctl(GLSM_CTL_STATE_CONTEXT_INIT, &params) ) {
        puts("Failed to init hardware context\n");
        // TODO: Move this out to a init function to avoid exceptions?
        throw std::runtime_error("Failed to init GLSM context.");
    }

    static DrawConfig config = {
        {0, 0},         // display_top_left
        {1024, 512},    // display_resolution
        false,          // display_24bpp
		true,           // display_off
        {0, 0},         // draw_area_top_left
        {0, 0},         // draw_area_dimensions
        {0, 0},         // draw_offset
    };

    // No context until `context_reset` is called
    this->state = GlState_Invalid;
    this->state_data.c = config;
    this->state_data.r = NULL;

    this->video_clock = video_clock;

}

RetroGl::~RetroGl() {
    if (this->state_data.r) {
        delete this->state_data.r;
        this->state_data.r = NULL;
    }
}

void RetroGl::context_reset() {
    puts("OpenGL context reset\n");
    glsm_ctl(GLSM_CTL_STATE_CONTEXT_RESET, NULL);

    if (!glsm_ctl(GLSM_CTL_STATE_SETUP, NULL))
        return;

    /* Save this on the stack, I'm unsure if saving a ptr would
    would cause trouble because of the 'delete' below  */
    static DrawConfig config;

    switch (this->state)
    {
    case GlState_Valid:
        config = *this->state_data.r->draw_config();
        break;
    case GlState_Invalid:
        config = this->state_data.c;
        break;
    }

    if (this->state_data.r) {
        delete this->state_data.r;
        this->state_data.r = NULL;
    }

    /* GlRenderer will own this copy and delete it in its dtor */
    DrawConfig* copy_of_config  = new DrawConfig;
    memcpy(copy_of_config, &config, sizeof(config));
    this->state_data.r = new GlRenderer(copy_of_config);
    this->state = GlState_Valid;
}

void RetroGl::context_destroy()
{
	printf("OpenGL context destroy\n");

    DrawConfig config;

    switch (this->state)
    {
    case GlState_Valid:
        config = *this->state_data.r->draw_config();
        break;
    case GlState_Invalid:
        // Looks like we didn't have an OpenGL context anyway...
        return;
    }

    glsm_ctl(GLSM_CTL_STATE_CONTEXT_DESTROY, NULL);

    this->state = GlState_Invalid;
    this->state_data.c = config;
}

void RetroGl::prepare_render()
{
    GlRenderer* renderer = NULL;
    switch (this->state)
    {
    case GlState_Valid:
        renderer = this->state_data.r;
        break;
    case GlState_Invalid:
        puts("Attempted to render a frame without GL context\n");
        exit(EXIT_FAILURE);
    }

    renderer->prepare_render();
}

void RetroGl::finalize_frame()
{
    GlRenderer* renderer = NULL;
    switch (this->state)
    {
    case GlState_Valid:
        renderer = this->state_data.r;
        break;
    case GlState_Invalid:
        puts("Attempted to render a frame without GL context\n");
        exit(EXIT_FAILURE);
    }

    renderer->finalize_frame();
}

bool RetroGl::has_software_renderer()
{
    GlRenderer* renderer = NULL;
    switch (this->state)
    {
    case GlState_Valid:
        renderer = this->state_data.r;
        break;
    case GlState_Invalid:
        return false;
    }

    return renderer->has_software_renderer();
}

void RetroGl::refresh_variables()
{
    GlRenderer* renderer = NULL;
    switch (this->state)
    {
    case GlState_Valid:
        renderer = this->state_data.r;
        break;
    case GlState_Invalid:
        // Nothing to be done if we don't have a GL context
        return;
    }

    bool reconfigure_frontend = renderer->refresh_variables();
    if (reconfigure_frontend) {
        // The resolution has changed, we must tell the frontend
        // to change its format
        struct retro_variable var = {0};

        struct retro_system_av_info av_info = get_av_info(this->video_clock);

        // This call can potentially (but not necessarily) call
        // `context_destroy` and `context_reset` to reinitialize
        // the entire OpenGL context, so beware.
        bool ok = environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av_info);

        if (!ok)
        {
            puts("Couldn't change frontend resolution\n");
            puts("Try resetting to enable the new configuration\n");
        }
    }
}

struct retro_system_av_info RetroGl::get_system_av_info()
{
    return get_av_info(this->video_clock);
}

bool RetroGl::context_framebuffer_lock(void *data)
{
    /* If the state is invalid, lock the framebuffer (return true) */
    switch (this->state) {
    case GlState_Valid:
        return false;
    case GlState_Invalid:
    default:
        return true;
    }
}


struct retro_system_av_info get_av_info(VideoClock std)
{
    struct retro_variable var = {0};

    var.key = option_internal_resolution;
    uint8_t upscaling = 1;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      /* Same limitations as libretro.cpp */
      upscaling = var.value[0] -'0';
    }

    var.key = option_display_vram;
    bool display_vram = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      if (!strcmp(var.value, "enabled"))
	display_vram = true;
      else
	display_vram = false;
    }

    var.key = option_widescreen_hack;
    bool widescreen_hack = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
        if (!strcmp(var.value, "enabled"))
            widescreen_hack = true;
        else if (!strcmp(var.value, "disabled"))
            widescreen_hack = false;
    }

    unsigned int max_width = 0;
    unsigned int max_height = 0;

    if (display_vram) {
      max_width = 1024;
      max_height = 512;
    } else {
      // Maximum resolution supported by the PlayStation video
      // output is 640x480
      max_width = 640;
      max_height = 480;
    }

    max_width *= upscaling;
    max_height *= upscaling;

    struct retro_system_av_info info;
    memset(&info, 0, sizeof(info));

    // The base resolution will be overriden using
    // ENVIRONMENT_SET_GEOMETRY before rendering a frame so
    // this base value is not really important
    info.geometry.base_width    = max_width;
    info.geometry.base_height   = max_height;
    info.geometry.max_width     = max_width;
    info.geometry.max_height    = max_height;
    if (display_vram) {
      info.geometry.aspect_ratio = 2./1.;
    } else {
      /* TODO: Replace 4/3 with MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO */
      info.geometry.aspect_ratio  = widescreen_hack ? 16.0/9.0 : 4.0/3.0;
    }
    info.timing.sample_rate     = 44100;

    // Precise FPS values for the video output for the given
    // VideoClock. It's actually possible to configure the PlayStation GPU
    // to output with NTSC timings with the PAL clock (and vice-versa)
    // which would make this code invalid but it wouldn't make a lot of
    // sense for a game to do that.
    switch (std) {
    case VideoClock_Ntsc:
        info.timing.fps = 59.941;
        break;
    case VideoClock_Pal:
        info.timing.fps = 49.76;
        break;
    }

    return info;
}

static void shim_context_reset()
{
    RetroGl::getInstance()->context_reset();
}

static void shim_context_destroy()
{
    RetroGl::getInstance()->context_destroy();
}

static bool shim_context_framebuffer_lock(void* data)
{
    return RetroGl::getInstance()->context_framebuffer_lock(data);
}

static RetroGl* static_renderer = NULL; 

RetroGl* renderer(void)
{
  RetroGl* r = static_renderer;
  if (r)
    return r;

  printf("Attempted to use a NULL renderer\n");
  exit(EXIT_FAILURE);
}

void rsx_gl_init(void)
{
}

bool rsx_gl_open(bool is_pal)
{
   VideoClock clock = is_pal ? VideoClock_Pal : VideoClock_Ntsc;
   static_renderer = RetroGl::getInstance(clock);
   return static_renderer != NULL;
}

void rsx_gl_close(void)
{
   static_renderer = NULL;  
}

void rsx_gl_refresh_variables(void)
{
   if (static_renderer)
      static_renderer->refresh_variables();
}

bool rsx_gl_has_software_renderer(void)
{
   if (!static_renderer)
      return false;
   return static_renderer->has_software_renderer();
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
}

void rsx_gl_set_video_refresh(retro_video_refresh_t callback)
{
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

void rsx_gl_set_mask_setting(uint32_t mask_set_or, uint32_t mask_eval_and)
{
   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->set_mask_setting(mask_set_or, mask_eval_and);
}

void rsx_gl_set_draw_offset(int16_t x, int16_t y)
{
   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->set_draw_offset(x, y);
}

void rsx_gl_set_tex_window(uint8_t tww, uint8_t twh,
      uint8_t twx, uint8_t twy)
{
   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->set_tex_window(tww, twh, twx, twy);
}

void  rsx_gl_set_draw_area(uint16_t x0,
			   uint16_t y0,
			   uint16_t x1,
			   uint16_t y1)
{
   uint16_t top_left[2]   = {x0, y0};
   uint16_t bot_right[2] = {x1, y1};
   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->set_draw_area(top_left, bot_right);
}

void rsx_gl_set_display_mode(uint16_t x,
      uint16_t y,
      uint16_t w,
      uint16_t h,
      bool depth_24bpp)
{
   uint16_t top_left[2]   = {x, y};
   uint16_t dimensions[2] = {w, h};
   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->set_display_mode(top_left, dimensions, depth_24bpp);
}

void rsx_gl_push_quad(
      float p0x,
      float p0y,
	  float p0w,
      float p1x,
      float p1y,
	  float p1w,
      float p2x,
      float p2y,
	  float p2w,
      float p3x,
      float p3y,
	  float p3w,
      uint32_t c0,
      uint32_t c1,
      uint32_t c2,
      uint32_t c3,
      uint16_t t0x,
      uint16_t t0y,
      uint16_t t1x,
      uint16_t t1y,
      uint16_t t2x,
      uint16_t t2y,
      uint16_t t3x,
      uint16_t t3y,
      uint16_t texpage_x,
      uint16_t texpage_y,
      uint16_t clut_x,
      uint16_t clut_y,
      uint8_t texture_blend_mode,
      uint8_t depth_shift,
      bool dither,
      int blend_mode)
{
   SemiTransparencyMode semi_transparency_mode = SemiTransparencyMode_Add;
   bool semi_transparent = false;
   switch (blend_mode) {
   case -1:
      semi_transparent = false;
      semi_transparency_mode = SemiTransparencyMode_Add;
      break;
   case 0:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_Average;
      break;
   case 1:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_Add;
      break;
   case 2:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_SubtractSource;
      break;
   case 3:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_AddQuarterSource;
      break;
   default:
      exit(EXIT_FAILURE);
   }

   CommandVertex v[4] = 
   {
      {
          {p0x, p0y, 0.95, p0w},   /* position */
          {(uint8_t) c0, (uint8_t) (c0 >> 8), (uint8_t) (c0 >> 16)}, /* color */
          {t0x, t0y},   /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},         
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent,
      },
      {
          {p1x, p1y, 0.95, p1w }, /* position */
          {(uint8_t) c1, (uint8_t) (c1 >> 8), (uint8_t) (c1 >> 16)}, /* color */
          {t1x, t1y}, /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},   
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent,
      },
      {
          {p2x, p2y, 0.95, p2w }, /* position */
          {(uint8_t) c2, (uint8_t) (c2 >> 8), (uint8_t) (c2 >> 16)}, /* color */
          {t2x, t2y}, /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},   
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent,
      },
      {
          {p3x, p3y, 0.95, p3w }, /* position */
          {(uint8_t) c3, (uint8_t) (c3 >> 8), (uint8_t) (c3 >> 16)}, /* color */
          {t3x, t3y}, /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},   
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent,
      },
   };

   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->push_quad(v, semi_transparency_mode);
}

void rsx_gl_push_triangle(
	  float p0x,
	  float p0y,
	  float p0w,
	  float p1x,
      float p1y,
	  float p1w,
      float p2x,
      float p2y,
	  float p2w,
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
   SemiTransparencyMode semi_transparency_mode = SemiTransparencyMode_Add;
   bool semi_transparent = false;
   switch (blend_mode) {
   case -1:
      semi_transparent = false;
      semi_transparency_mode = SemiTransparencyMode_Add;
      break;
   case 0:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_Average;
      break;
   case 1:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_Add;
      break;
   case 2:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_SubtractSource;
      break;
   case 3:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_AddQuarterSource;
      break;
   default:
      exit(EXIT_FAILURE);
   }

   CommandVertex v[3] = 
   {
      {
          {p0x, p0y, 0.95, p0w},   /* position */
          {(uint8_t) c0, (uint8_t) (c0 >> 8), (uint8_t) (c0 >> 16)}, /* color */
          {t0x, t0y},   /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},         
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent,
      },
      {
          {p1x, p1y, 0.95, p1w }, /* position */
          {(uint8_t) c1, (uint8_t) (c1 >> 8), (uint8_t) (c1 >> 16)}, /* color */
          {t1x, t1y}, /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},   
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent,
      },
      {
          {p2x, p2y, 0.95, p2w }, /* position */
          {(uint8_t) c2, (uint8_t) (c2 >> 8), (uint8_t) (c2 >> 16)}, /* color */
          {t2x, t2y}, /* texture_coord */
          {texpage_x, texpage_y}, 
          {clut_x, clut_y},   
          texture_blend_mode,
          depth_shift,
          (uint8_t) dither,
          semi_transparent,
      }
   };

   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->push_triangle(v, semi_transparency_mode);
}

void rsx_gl_fill_rect(uint32_t color,
      uint16_t x, uint16_t y,
      uint16_t w, uint16_t h)
{

   uint16_t top_left[2]   = {x, y};
   uint16_t dimensions[2] = {w, h};
   uint8_t col[3] = {(uint8_t) color, (uint8_t) (color >> 8), (uint8_t) (color >> 16)};  

   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->fill_rect(col, top_left, dimensions);
}

void rsx_gl_copy_rect(
      uint16_t src_x, uint16_t src_y,
      uint16_t dst_x, uint16_t dst_y,
      uint16_t w, uint16_t h)
{
    uint16_t src_pos[2] = {src_x, src_y};
    uint16_t dst_pos[2] = {dst_x, dst_y};
    uint16_t dimensions[2] = {w, h}; 

    if (static_renderer->state == GlState_Valid)
       static_renderer->state_data.r->copy_rect(src_pos, dst_pos, dimensions);
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
   SemiTransparencyMode semi_transparency_mode = SemiTransparencyMode_Add;
   bool semi_transparent = false;
   switch (blend_mode) {
   case -1:
      semi_transparent = false;
      semi_transparency_mode = SemiTransparencyMode_Add;
      break;
   case 0:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_Average;
      break;
   case 1:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_Add;
      break;
   case 2:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_SubtractSource;
      break;
   case 3:
      semi_transparent = true;
      semi_transparency_mode = SemiTransparencyMode_AddQuarterSource;
      break;
   default:
      exit(EXIT_FAILURE);
   }

   CommandVertex v[2] = {
      {
          {(float)p0x, (float)p0y, 0., 1.0}, /* position */
          {(uint8_t) c0, (uint8_t) (c0 >> 8), (uint8_t) (c0 >> 16)}, /* color */
          {0, 0}, /* texture_coord */
          {0, 0}, /* texture_page */
          {0, 0}, /* clut */
          0,      /* texture_blend_mode */
          0,      /* depth_shift */
          (uint8_t) dither,
          semi_transparent,
      },
      {
          {(float)p1x, (float)p1y, 0., 1.0}, /* position */
          {(uint8_t) c1, (uint8_t) (c1 >> 8), (uint8_t) (c1 >> 16)}, /* color */
          {0, 0}, /* texture_coord */
          {0, 0}, /* texture_page */
          {0, 0}, /* clut */
          0,      /* texture_blend_mode */
          0,      /* depth_shift */
          (uint8_t) dither,
          semi_transparent,
      }
   };

   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->push_line(v, semi_transparency_mode);
}

void rsx_gl_load_image(uint16_t x, uint16_t y,
      uint16_t w, uint16_t h,
      uint16_t *vram)
{
   uint16_t top_left[2]   = {x, y};
   uint16_t dimensions[2] = {w, h};

   /* TODO FIXME - upload_vram_window expects a 
  uint16_t[VRAM_HEIGHT*VRAM_WIDTH_PIXELS] array arg instead of a ptr */
   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->upload_vram_window(top_left, dimensions, vram);
}

void rsx_gl_toggle_display(bool status)
{
   if (static_renderer->state == GlState_Valid)
      static_renderer->state_data.r->set_display_off(status);
}
