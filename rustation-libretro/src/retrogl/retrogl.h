// PlayStation OpenGL 3.3 renderer playing nice with libretro
#ifndef RETROGL_H
#define RETROGL_H

#include "libretro.h"
#include <glsm/glsmsym.h>

#include "../renderer/GlRenderer.h"

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
    GlRenderer* gl_renderer();
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

#endif
