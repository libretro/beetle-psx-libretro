#ifndef __LIBRETRO_CBS_H
#define __LIBRETRO_CBS_H

#ifdef __cplusplus
extern "C" {
#endif

extern retro_video_refresh_t video_cb;
extern uint8_t widescreen_hack;
extern uint8_t psx_gpu_upscale_shift;

#ifdef __cplusplus
}
#endif

#endif
