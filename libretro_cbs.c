#include <boolean.h>
#include "libretro.h"

bool content_is_pal = false;
retro_video_refresh_t video_cb;
retro_environment_t environ_cb;
uint8_t widescreen_hack;
uint8_t psx_gpu_upscale_shift;

float video_output_framerate(void)
{
   return content_is_pal ? 49.842 : 59.941;
}
