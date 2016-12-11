#include "retrogl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // memcpy()
#include <stdexcept>

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
        puts("Can't set pixel format");
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
        puts("Failed to init hardware context");
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
    puts("OpenGL context reset");
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

GlRenderer* RetroGl::gl_renderer()
{
    switch (this->state)
    {
    case GlState_Valid:
        return this->state_data.r;
    default:
        puts("Attempted to get GL state without GL context!");
        exit(EXIT_FAILURE);
    }
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
        puts("Attempted to render a frame without GL context");
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
        puts("Attempted to render a frame without GL context");
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
            puts("Couldn't change frontend resolution");
            puts("Try resetting to enable the new configuration");
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

    var.key = "beetle_psx_internal_resolution";
    uint8_t upscaling = 1;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      /* Same limitations as libretro.cpp */
      upscaling = var.value[0] -'0';
    }

    var.key = "beetle_psx_display_vram";
    bool display_vram = false;
    if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value) {
      if (!strcmp(var.value, "enabled"))
	display_vram = true;
      else
	display_vram = false;
    }

    var.key = "beetle_psx_widescreen_hack";
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
