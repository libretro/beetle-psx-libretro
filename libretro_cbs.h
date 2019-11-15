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
extern bool has_new_geometry;
extern bool crop_overscan;
extern unsigned pix_offset;
extern unsigned image_offset;
extern unsigned image_crop;
extern unsigned total_width_crop;
extern unsigned total_height_crop; /* TODO - Only used by the SW renderer in the RSX intf... */
extern int initial_scanline;
extern int initial_scanline_pal;
extern int last_scanline;
extern int last_scanline_pal;
  
/* Warns the libretro implementation that it needs to update its static MDFN_Surface object  */
extern bool need_new_surface; 

/* Whether or not the libretro core is starting up i.e. not in retro_run()
 * Prevents HW renderers from calling SET_SYSTEM_AV_INFO */
extern bool is_startup;

#ifdef __cplusplus
}
#endif

#endif
