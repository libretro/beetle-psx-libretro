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
bool has_new_geometry = false;
bool crop_overscan = false;
unsigned pix_offset = 0;
unsigned image_offset = 0;
unsigned image_crop = 0;
unsigned total_width_crop = 0;
unsigned total_height_crop = 0; /* TODO - Only used by the SW renderer in the RSX intf... */
int initial_scanline = 0;
int initial_scanline_pal = 0;
int last_scanline = 239;
int last_scanline_pal = 287;
bool need_new_surface = false;
bool is_startup = true;
