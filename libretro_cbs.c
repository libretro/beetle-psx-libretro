#include <boolean.h>
#include "libretro.h"

bool content_is_pal = false;
retro_video_refresh_t video_cb;
retro_environment_t environ_cb;
uint8_t widescreen_hack;
uint8_t psx_gpu_upscale_shift; /* Used to keep track of the SW renderer upscale */
int lineRenderMode;
int filter_mode;
bool opaque_check;
bool semitrans_check;
bool need_new_surface = false;
bool is_startup = true;
