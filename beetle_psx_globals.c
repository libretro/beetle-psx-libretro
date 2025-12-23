#include <boolean.h>
#include <stdint.h>
#include "beetle_psx_globals.h"

bool content_is_pal = false;
uint8_t widescreen_hack;
uint8_t widescreen_hack_aspect_ratio_setting;
uint8_t psx_gpu_upscale_shift;
uint8_t psx_gpu_upscale_shift_hw;
int line_render_mode;
int filter_mode;
bool opaque_check;
bool semitrans_check;
int crop_overscan = 0;

enum core_timing_fps_modes core_timing_fps_mode = AUTO_TOGGLE_TIMING;
bool currently_interlaced = true;
bool interlace_setting_dirty = false;
uint8_t startup_frame_count = 0;

int aspect_ratio_setting = 0;
bool aspect_ratio_dirty = false;
bool is_monkey_hero = false;
