#include <boolean.h>
#include "libretro.h"

bool content_is_pal = false;
retro_video_refresh_t video_cb;
retro_environment_t environ_cb;
uint8_t widescreen_hack;
uint8_t psx_gpu_upscale_shift;
int lineRenderMode;
int filter_mode;
bool opaque_check;
bool semitrans_check;
bool has_new_geometry = false;
bool crop_overscan = false;
unsigned pix_offset = 0;
unsigned image_offset = 0;
unsigned image_crop = 0;
unsigned total_width_crop = 0;
