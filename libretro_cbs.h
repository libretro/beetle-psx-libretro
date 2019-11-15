#ifndef __LIBRETRO_CBS_H
#define __LIBRETRO_CBS_H

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool content_is_pal;
extern retro_video_refresh_t video_cb;
extern retro_environment_t environ_cb;
extern uint8_t widescreen_hack;
extern uint8_t psx_gpu_upscale_shift;
extern int lineRenderMode;
extern int filter_mode;
extern bool opaque_check;
extern bool semitrans_check;
/* Warns the libretro implementation that it needs to update its static MDFN_Surface object  */
extern bool need_new_surface; 

/* Whether or not the libretro core is starting up i.e. not in retro_run()
 * Prevents HW renderers from calling SET_SYSTEM_AV_INFO */
extern bool is_startup;

#ifdef __cplusplus
}
#endif

#endif
