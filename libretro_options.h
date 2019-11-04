#ifndef LIBRETRO_OPTIONS_H__
#define LIBRETRO_OPTIONS_H__

#define MEDNAFEN_CORE_NAME_MODULE "psx"
#ifdef HAVE_HW
#define MEDNAFEN_CORE_NAME "Beetle PSX HW"
#else
#define MEDNAFEN_CORE_NAME "Beetle PSX"
#endif
#define MEDNAFEN_CORE_VERSION "0.9.44.1"
#define MEDNAFEN_CORE_EXTENSIONS "exe|cue|toc|ccd|m3u|pbp|chd"
#define MEDNAFEN_CORE_GEOMETRY_BASE_W 320
#define MEDNAFEN_CORE_GEOMETRY_BASE_H 240

/* Values from mednafen/gpu.cpp:GPU_FillVideoParams() */
/* Assuming gi->fb_* corresponds to libretro video max geometry */
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 768
#define MEDNAFEN_CORE_GEOMETRY_MAX_H_PAL 576
#define MEDNAFEN_CORE_GEOMETRY_MAX_H_NTSC 480

#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)

/* Values from mednafen/gpu.cpp:GPU_FillVideoParams() */
/* gi->fps is the real fps multiplied by 256 and 65536 */
#define MEDNAFEN_CORE_TIMING_FPS_NTSC 59.94099885225296
#define MEDNAFEN_CORE_TIMING_FPS_PAL  49.84158742427826
#define MEDNAFEN_CORE_TIMING_SAMPLERATE 44100.0

#ifdef HAVE_HW
#define BEETLE_OPT(_o) ("beetle_psx_hw_" # _o)
#else
#define BEETLE_OPT(_o) ("beetle_psx_" # _o)
#endif

#endif
