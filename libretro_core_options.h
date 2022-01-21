#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include <retro_inline.h>

#ifndef HAVE_NO_LANGEXTRA
#include "libretro_core_options_intl.h"
#endif

#include "libretro_options.h"

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition */

struct retro_core_option_v2_category option_cats_us[] = {
   {
      "video",
      "Video",
      "Change aspect ratio, display cropping, video filter and frame skipping settings."
   },
   {
      "osd",
      "Onscreen Notifications",
      "Change the notifications being shown onscreen."
   },
   {
      "input",
      "Input",
      "Change light gun, mouse and neGcon settings."
   },
   {
      "memcards",
      "Memory Card",
      "Change settings related to the virtual Memory Card(s) used by the system."
   },
   {
      "pgxp",
      "PGXP (Precision Geometry Transform Pipeline)",
      "These options control enhancements which can improve graphics compared to the original console. PGXP can get rid of warping textures and Z-fighting issues."
   },
   {
      "hacks",
      "Emulation hacks",
      "Change processor overclocking and emulation accuracy settings affecting low-level performance and compatibility."
   },
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
   {
      BEETLE_OPT(internal_resolution),
      "Internal GPU Resolution",
      NULL,
      "Choose internal resolution multiplier. Resolutions higher than '1x (Native)' improve fidelity of 3D models at the expense of increased performance requirements. 2D elements are generally unaffected by this setting.",
      NULL,
      "video",
      {
         { "1x(native)", "1x (Native)" },
         { "2x",         NULL },
         { "4x",         NULL },
         { "8x",         NULL },
         { "16x",        NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   {
      BEETLE_OPT(depth),
      "Internal Color Depth",
      NULL,
      "Choose internal color depth. Higher color depth can reduce color banding effects without the use of dithering. 16 bpp emulates original hardware but may have visible banding if dithering is not enabled. 'Dithering Pattern' is recommended to be disabled when this option is set to 32 bpp.",
      NULL,
      "video",
      {
         { "16bpp(native)", "16 bpp (Native)" },
         { "32bpp",         "32 bpp" },
         { NULL, NULL },
      },
      "16bpp(native)"
   },
   // Sort of, it's more like 15-bit high color and 24-bit true color for visible output. The alpha channel is used for mask bit. Vulkan renderer uses ABGR1555_555 for 31 bits internal? FMVs are always 24-bit on all renderers like original hardware (BGR888, no alpha)
#endif
   {
      BEETLE_OPT(dither_mode),
      "Dithering Pattern",
      NULL,
      "Choose dithering pattern configuration. '1x (Native)' emulates native low resolution dithering used by original hardware to smooth out color banding artifacts visible at native color depth. 'Internal Resolution' scales dithering granularity to the configured internal resolution for cleaner results. Recommended to be disabled when running at 32 bpp color depth. Note: On Vulkan, enabling this will force downsampling to native color depth, while disabling will automatically enable output at higher color depth.",
      NULL,
      "video",
      {
         { "1x(native)",          "1x (Native)" },
         { "internal resolution", "Internal Resolution" },
         { "disabled",            NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
#ifdef HAVE_VULKAN
   {
      BEETLE_OPT(scaled_uv_offset),
      "Texture UV Offset",
      NULL,
      "Sample textures for 3D polygons at an offset for higher than 1x internal resolution. Reduces texture seams but may cause unintended graphical glitches.",
      NULL,
      "video",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
#endif
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(filter),
      "Texture Filtering",
      NULL,
      "Choose texture filtering method. 'Nearest' emulates original hardware. 'Bilinear' and '3-Point' are smoothing filters, which reduce pixelation via blurring. 'SABR', 'xBR', and 'JINC2' are upscaling filters that may improve texture fidelity/sharpness at the expense of increased performance requirements. Only supported by the hardware renderers.",
      NULL,
      "video",
      {
         { "nearest",  "Nearest" },
         { "SABR",     NULL },
         { "xBR",      NULL },
         { "bilinear", "Bilinear" },
         { "3-point",  "3-Point" },
         { "JINC2",    NULL },
         { NULL, NULL },
      },
      "nearest"
   },
#ifdef HAVE_VULKAN
   {
      BEETLE_OPT(filter_exclude_sprite),
      "Exclude Sprites from Filtering",
      NULL,
      "Do not apply texture filtering to sprites. Prevents seams in various games with 2D sprite-rendered backgrounds. Use together with Adaptive Smoothing or another post-processing filter for best effect.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "opaque", "Opaque Only" },
         { "all", "Opaque and Semi-Transparent" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(filter_exclude_2d_polygon),
      "Exclude 2D Polygons from Filtering",
      NULL,
      "Do not apply texture filtering to 2D polygons. 2D polygons are detected with a heuristic and there may be glitches. Use together with Adaptive Smoothing or another post-processing filter for best effect.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "opaque", "Opaque Only" },
         { "all", "Opaque and Semi-Transparent" },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
#endif
#ifdef HAVE_VULKAN
   {
      BEETLE_OPT(adaptive_smoothing),
      "Adaptive Smoothing",
      NULL,
      "Smooth out 2D artwork and UI elements without blurring 3D rendered objects. Only supported by the Vulkan renderer.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(super_sampling),
      "Supersampling (Downsample to Native Resolution)",
      NULL,
      "Downsample rendered content from upscaled internal resolution down to native resolution. Combining this with higher internal resolution multipliers allows for games to be displayed with anti-aliased 3D objects at native low resolution. Produces best results when applied to titles that mix 2D and 3D elements (e.g. 3D characters on pre-rendered backgrounds), and works well in conjunction with CRT shaders. Only supported by the Vulkan renderer. Note: 'Dithering Pattern' is recommended to be disabled when enabling this option.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(msaa),
      "Multi-Sampled Anti Aliasing",
      NULL,
      "Choose MSAA level for rendered content. Improves the appearance of 3D objects. Only supported by the Vulkan renderer.",
      NULL,
      "video",
      {
         { "1x",  NULL },
         { "2x",  NULL },
         { "4x",  NULL },
         { "8x",  NULL },
         { "16x", NULL },
         { NULL, NULL },
      },
      "1x"
   },
   {
      BEETLE_OPT(mdec_yuv),
      "MDEC YUV Chroma Filter",
      NULL,
      "Improve the quality of FMV playback by reducing 'macroblocking' artifacts (squares/jagged edges). Only supported by the Vulkan renderer.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(track_textures),
      "Track Textures",
      NULL,
      "Prerequisite for texture dumping and replacement.  Will probably crash in most games.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#ifdef TEXTURE_DUMPING_ENABLED
   {
      BEETLE_OPT(dump_textures),
      "Dump Textures",
      NULL,
      "Dump used textures to <cd>-texture-dump/",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(replace_textures),
      "Replace Textures",
      NULL,
      "Replace textures using HD versions from <cd>-texture-replacements/",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   {
      BEETLE_OPT(wireframe),
      "Wireframe Mode (Debug)",
      NULL,
      "Render 3D models in outline form without textures or shading. Only supported by the OpenGL hardware renderer. Note: This is for debugging purposes, and should normally be disabled.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(frame_duping),
      "Frame Duping (Speedup)",
      NULL,
      "When enabled and supported by the libretro frontend, this provides a small performance increase by directing the frontend to repeat the previous frame if the core has nothing new to display.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(display_internal_fps),
      "Display Internal FPS",
      NULL,
      "Display the internal frame rate at which the emulated PlayStation system is rendering content. Note: Requires onscreen notifications to be enabled in the libretro frontend.",
      NULL,
      "osd",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(display_vram),
      "Display Full VRAM (Debug)",
      NULL,
      "Visualize the entire emulated console's VRAM. Only supported by the OpenGL and Vulkan hardware renderers. Note: This is for debugging purposes, and should normally be disabled.",
      NULL,
      "osd",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(analog_calibration),
      "Analog Self-Calibration",
      NULL,
      "When the input device is set to DualShock, Analog Controller, Analog Joystick, or neGcon, this option enables dynamic calibration of analog inputs. Maximum registered input values are monitored in real time and used to scale analog coordinates passed to the emulator. This should be used for games such as Mega Man Legends 2 that expect larger values than what modern controllers provide. For best results, analog sticks should be rotated at full extent to tune the calibration algorithm each time content is loaded.",
      NULL,
      "input",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(analog_toggle),
      "Enable DualShock Analog Mode Toggle",
      NULL,
      "When the input device type is DualShock, this option allows the emulated DualShock to be toggled between DIGITAL and ANALOG mode like original hardware. When disabled, the DualShock is locked to ANALOG mode and when enabled, the DualShock can be toggled between DIGITAL and ANALOG mode by pressing and holding START+SELECT+L1+L2+R1+R2.",
      NULL,
      "input",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(enable_multitap_port1),
      "Port 1: Multitap Enable",
      NULL,
      "Enable multitap functionality on port 1.",
      NULL,
      "input",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(enable_multitap_port2),
      "Port 2: Multitap Enable",
      NULL,
      "Enable multitap functionality on port 2.",
      NULL,
      "input",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(gun_input_mode),
      "Gun Input Mode",
      NULL,
      "Choose whether to use a mouse-controlled 'Light Gun' or a 'Touchscreen' input when device type is set to 'Guncon/G-Con 45' or 'Justifier'.",
      NULL,
      "input",
      {
         { "lightgun",    "Light Gun" },
         { "touchscreen", "Touchscreen" },
         { NULL, NULL },
      },
      "lightgun"
   },
   // Shouldn't the gun_input_mode just be Mouse vs. Touchscreen?
   {
      BEETLE_OPT(gun_cursor),
      "Gun Cursor",
      NULL,
      "Choose the gun cursor to be displayed on screen while using the 'Guncon/G-Con 45' and 'Justifier' input device types. When disabled, crosshairs are always hidden.",
      NULL,
      "input",
      {
         { "cross", "Cross" },
         { "dot",   "Dot" },
         { "off",   "No Cursor" },
         { NULL, NULL },
      },
      "cross"
   },
   {
      BEETLE_OPT(crosshair_color_p1),
      "Port 1: Gun Crosshair Color",
      NULL,
      "Choose the light gun crosshair color for port 1.",
      NULL,
      "input",
      {
         { "red", "Red" },
         { "blue", "Blue" },
         { "green", "Green" },
         { "orange", "Orange" },
         { "yellow", "Yellow" },
         { "cyan", "Cyan" },
         { "pink", "Pink" },
         { "purple", "Purple" },
         { "black", "Black" },
         { "white", "White" },
         { NULL, NULL },
      },
      "red"
   },
   {
      BEETLE_OPT(crosshair_color_p2),
      "Port 2: Gun Crosshair Color",
      NULL,
      "Choose the light gun crosshair color for port 2.",
      NULL,
      "input",
      {
         { "blue", "Blue" },
         { "red", "Red" },
         { "green", "Green" },
         { "orange", "Orange" },
         { "yellow", "Yellow" },
         { "cyan", "Cyan" },
         { "pink", "Pink" },
         { "purple", "Purple" },
         { "black", "Black" },
         { "white", "White" },
         { NULL, NULL },
      },
      "blue"
   },
   {
      BEETLE_OPT(mouse_sensitivity),
      "Mouse Sensitivity",
      NULL,
      "Choose the responsiveness of the 'Mouse' input device type.",
      NULL,
      "input",
      {
         { "5%",   NULL },
         { "10%",  NULL },
         { "15%",  NULL },
         { "20%",  NULL },
         { "25%",  NULL },
         { "30%",  NULL },
         { "35%",  NULL },
         { "40%",  NULL },
         { "45%",  NULL },
         { "50%",  NULL },
         { "55%",  NULL },
         { "60%",  NULL },
         { "65%",  NULL },
         { "70%",  NULL },
         { "75%",  NULL },
         { "80%",  NULL },
         { "85%",  NULL },
         { "90%",  NULL },
         { "95%",  NULL },
         { "100%", "100% (Default)" },
         { "105%", NULL },
         { "110%", NULL },
         { "115%", NULL },
         { "120%", NULL },
         { "125%", NULL },
         { "130%", NULL },
         { "135%", NULL },
         { "140%", NULL },
         { "145%", NULL },
         { "150%", NULL },
         { "155%", NULL },
         { "160%", NULL },
         { "165%", NULL },
         { "170%", NULL },
         { "175%", NULL },
         { "180%", NULL },
         { "185%", NULL },
         { "190%", NULL },
         { "195%", NULL },
         { "200%", NULL },
         { NULL, NULL },
      },
      "100%"
   },
   {
      BEETLE_OPT(negcon_response),
      "neGcon Twist Response",
      NULL,
      "Choose the response type of the RetroPad left analog stick when simulating the 'twist' action of emulated 'neGcon' input devices. Analog stick displacement may be mapped to neGcon rotation angle either linearly, quadratically or cubically. 'Quadratic' allows for greater precision than 'Linear' when making small movements. 'Cubic' further increases small movement precision, but 'exaggerates' larger movements. Note: 'Linear' is only recommended when using racing wheel peripherals. Conventional controllers implement analog input in a manner fundamentally different from the neGcon 'twist' mechanism, such that linear mapping over-amplifies small movements, impairing fine control. In most cases, 'Quadratic' provides the closest approximation of real hardware.",
      NULL,
      "input",
      {
         { "linear",    "Linear" },
         { "quadratic", "Quadratic" },
         { "cubic",     "Cubic" },
         { NULL, NULL },
      },
      "linear"
   },
   {
      BEETLE_OPT(negcon_deadzone),
      "neGcon Twist Deadzone",
      NULL,
      "Choose the deadzone of the RetroPad left analog stick when simulating the 'twist' action of emulated 'neGcon' input devices. Used to eliminate controller drift. Note: Most neGcon-compatible titles provide in-game options for setting a 'twist' deadzone value. To avoid loss of precision, the in-game deadzone should *always* be set to zero. Any required adjustments should *only* be applied via this core option. This is particularly important when 'neGcon Twist Response' is set to 'Quadratic' or 'Cubic'.",
      NULL,
      "input",
      {
         { "0%",  NULL },
         { "5%",  NULL },
         { "10%", NULL },
         { "15%", NULL },
         { "20%", NULL },
         { "25%", NULL },
         { "30%", NULL },
         { NULL, NULL },
      },
      "0%"
   },
   {
      BEETLE_OPT(use_mednafen_memcard0_method),
      "Memory Card 0 Method (Restart Required)",
      NULL,
      "Choose the save data format used for memory card 0. 'Mednafen' may be used for compatibility with the stand-alone version of Mednafen. When used with Beetle PSX, Libretro (.srm) and Mednafen (.mcr) saves have internally identical formats and can be converted between one another via renaming.",
      NULL,
      "memcards",
      {
         { "libretro", "Libretro" },
         { "mednafen", "Mednafen" },
         { NULL, NULL },
      },
      "libretro"
   },
   {
      BEETLE_OPT(enable_memcard1),
      "Enable Memory Card 1 (Restart Required)",
      NULL,
      "Emulate a second memory card in slot 1. When disabled, games can only access the memory card in slot 0. Note: Some games require this option to be disabled for correct operation (e.g. Codename Tenka). Note: Memory Card 1 uses the Mednafen (.mcr) save format.",
      NULL,
      "memcards",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(shared_memory_cards),
      "Shared Memory Cards (Restart Required)",
      NULL,
      "When enabled, all games will save to and load from the same memory card files. When disabled, separate memory card files will be generated for each item of loaded content. Note: if 'Memory Card 0 Method' is set to 'Libretro', only the right memory card will be affected.",
      NULL,
      "memcards",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(memcard_left_index),
      "Memory Card Left Index",
      NULL,
      "Change the memory card currently loaded in the left slot. This option will only work if Memory Card 0 method is set to Mednafen. The default card is index 0.",
      NULL,
      "memcards",
      {
         { "0",  NULL },
         { "1",  NULL },
         { "2",  NULL },
         { "3",  NULL },
         { "4",  NULL },
         { "5",  NULL },
         { "6",  NULL },
         { "7",  NULL },
         { "8",  NULL },
         { "9",  NULL },
         { "10",  NULL },
         { "11",  NULL },
         { "12",  NULL },
         { "13",  NULL },
         { "14",  NULL },
         { "15",  NULL },
         { "16",  NULL },
         { "17",  NULL },
         { "18",  NULL },
         { "19",  NULL },
         { "20",  NULL },
         { "21",  NULL },
         { "22",  NULL },
         { "23",  NULL },
         { "24",  NULL },
         { "25",  NULL },
         { "26",  NULL },
         { "27",  NULL },
         { "28",  NULL },
         { "29",  NULL },
         { "30",  NULL },
         { "31",  NULL },
         { "32",  NULL },
         { "33",  NULL },
         { "34",  NULL },
         { "35",  NULL },
         { "36",  NULL },
         { "37",  NULL },
         { "38",  NULL },
         { "39",  NULL },
         { "40",  NULL },
         { "41",  NULL },
         { "42",  NULL },
         { "43",  NULL },
         { "44",  NULL },
         { "45",  NULL },
         { "46",  NULL },
         { "47",  NULL },
         { "48",  NULL },
         { "49",  NULL },
         { "50",  NULL },
         { "51",  NULL },
         { "52",  NULL },
         { "53",  NULL },
         { "54",  NULL },
         { "55",  NULL },
         { "56",  NULL },
         { "57",  NULL },
         { "58",  NULL },
         { "59",  NULL },
         { "60",  NULL },
         { "61",  NULL },
         { "62",  NULL },
         { "63",  NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(memcard_right_index),
      "Memory Card Right Index",
      NULL,
      "Change the memory card currently loaded in the right slot. This option will only work if Memory Card 1 is enabled. The default card is index 1.",
      NULL,
      "memcards",
      {
         { "0",  NULL },
         { "1",  "1 (Default)" },
         { "2",  NULL },
         { "3",  NULL },
         { "4",  NULL },
         { "5",  NULL },
         { "6",  NULL },
         { "7",  NULL },
         { "8",  NULL },
         { "9",  NULL },
         { "10",  NULL },
         { "11",  NULL },
         { "12",  NULL },
         { "13",  NULL },
         { "14",  NULL },
         { "15",  NULL },
         { "16",  NULL },
         { "17",  NULL },
         { "18",  NULL },
         { "19",  NULL },
         { "20",  NULL },
         { "21",  NULL },
         { "22",  NULL },
         { "23",  NULL },
         { "24",  NULL },
         { "25",  NULL },
         { "26",  NULL },
         { "27",  NULL },
         { "28",  NULL },
         { "29",  NULL },
         { "30",  NULL },
         { "31",  NULL },
         { "32",  NULL },
         { "33",  NULL },
         { "34",  NULL },
         { "35",  NULL },
         { "36",  NULL },
         { "37",  NULL },
         { "38",  NULL },
         { "39",  NULL },
         { "40",  NULL },
         { "41",  NULL },
         { "42",  NULL },
         { "43",  NULL },
         { "44",  NULL },
         { "45",  NULL },
         { "46",  NULL },
         { "47",  NULL },
         { "48",  NULL },
         { "49",  NULL },
         { "50",  NULL },
         { "51",  NULL },
         { "52",  NULL },
         { "53",  NULL },
         { "54",  NULL },
         { "55",  NULL },
         { "56",  NULL },
         { "57",  NULL },
         { "58",  NULL },
         { "59",  NULL },
         { "60",  NULL },
         { "61",  NULL },
         { "62",  NULL },
         { "63",  NULL },
         { NULL, NULL },
      },
      "1"
   },
   {
      BEETLE_OPT(pgxp_mode),
      "PGXP Operation Mode",
      NULL,
      "Allows 3D objects to be rendered with subpixel precision, minimizing distortion and jitter of 3D objects seen on original hardware due to the usage of fixed point vertex coordinates. 'Memory Only' mode has minimal compatibility issues and is recommended for general use. 'Memory + CPU (Buggy)' mode can reduce jitter even further but has high performance requirements and may cause various geometry errors.",
      NULL,
      "pgxp",
      {
         { "disabled",     NULL },
         { "memory only",  "Memory Only" },
         { "memory + CPU", "Memory + CPU (Buggy)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_2d_tol),
      "PGXP 2D Geometry Tolerance",
      NULL,
      "Hide more glaring errors in PGXP operations: the value specifies the tolerance in which PGXP values will be kept in case of geometries without proper depth information.",
      NULL,
      "pgxp",
      {
         { "disabled", NULL },
         { "0px", NULL },
         { "1px", NULL },
         { "2px", NULL },
         { "3px", NULL },
         { "4px", NULL },
         { "5px", NULL },
         { "6px", NULL },
         { "7px", NULL },
         { "8px", NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_nclip),
      "PGXP Primitive Culling",
      NULL,
      "Use PGXP's NCLIP implementation. Improves appearance by reducing holes in geometries with PGXP coordinates. Known to cause some games to lock up in various circumstances.",
      NULL,
      "pgxp",
      {
         { "disabled", NULL },
         { "enabled", NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(pgxp_vertex),
      "PGXP Vertex Cache",
      NULL,
      "Cache PGXP-enhanced vertex positions for re-use across polygon draws. Can potentially improve object alignment and reduce visible seams when rendering textures, but false positives when querying the cache may produce graphical glitches. It is currently recommended to leave this option disabled. This option is applied only when PGXP Operation Mode is enabled. Only supported by the hardware renderers.",
      NULL,
      "pgxp",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_texture),
      "PGXP Perspective Correct Texturing",
      NULL,
      "Replace native PSX affine texture mapping with perspective correct texture mapping. Eliminates position-dependent distortion and warping of textures, resulting in properly aligned textures. This option is applied only when PGXP Operation Mode is enabled. Only supported by the hardware renderers.",
      NULL,
      "pgxp",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(line_render),
      "Line-to-Quad Hack",
      NULL,
      "Choose line-to-quad hack method. Some games (e.g. Doom, Hexen, Soul Blade, etc.) draw horizontal lines by stretching single-pixel-high triangles across the screen, which are rasterized as a row of pixels on original hardware. This hack detects these small triangles and converts them to quads as required, allowing them to be displayed properly on the hardware renderers and at upscaled internal resolutions. 'Aggressive' is required for some titles (e.g. Dark Forces, Duke Nukem) but may otherwise introduce graphical glitches. Leave at 'Default' if unsure.",
      NULL,
      "hacks",
      {
         { "default",    "Default" },
         { "aggressive", "Aggressive" },
         { "disabled",   NULL },
         { NULL, NULL },
      },
      "default"
   },
   {
      BEETLE_OPT(widescreen_hack),
      "Widescreen Mode Hack",
      NULL,
      "Render 3D content anamorphically and output the emulated framebuffer at a widescreen aspect ratio. Produces best results with fully 3D games. 2D elements will be horizontally stretched and may be misaligned.",
      NULL,
      "hacks",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(widescreen_hack_aspect_ratio),
      "Widescreen Mode Hack Aspect Ratio",
      NULL,
      "Choose the aspect ratio to be used by the Widescreen Mode Hack.",
      NULL,
      "hacks",
      {
         { "16:9",  NULL },
         { "16:10", NULL },
         { "18:9",  NULL },
         { "19:9",  NULL },
         { "20:9",  NULL },
         { "21:9",  NULL }, // 64:27
         { "32:9",  NULL },
         { NULL,    NULL },
      },
      "16:9"
   },
   {
      BEETLE_OPT(cpu_freq_scale),
      "CPU Frequency Scaling (Overclock)",
      NULL,
      "Overclock (or underclock) the emulated PSX CPU. Overclocking can eliminate slowdown and improve frame rates in certain games at the expense of increased performance requirements. Note that some games have an internal frame rate limiter and may not benefit from overclocking. May cause certain effects to animate faster than intended in some titles when overclocked.",
      NULL,
      "hacks",
      {
         { "50%",           NULL },
         { "60%",           NULL },
         { "70%",           NULL },
         { "80%",           NULL },
         { "90%",           NULL },
         { "100%(native)", "100% (Native)" },
         { "110%",          NULL },
         { "120%",          NULL },
         { "130%",          NULL },
         { "140%",          NULL },
         { "150%",          NULL },
         { "160%",          NULL },
         { "170%",          NULL },
         { "180%",          NULL },
         { "190%",          NULL },
         { "200%",          NULL },
         { "210%",          NULL },
         { "220%",          NULL },
         { "230%",          NULL },
         { "240%",          NULL },
         { "250%",          NULL },
         { "260%",          NULL },
         { "270%",          NULL },
         { "280%",          NULL },
         { "290%",          NULL },
         { "300%",          NULL },
         { "310%",          NULL },
         { "320%",          NULL },
         { "330%",          NULL },
         { "340%",          NULL },
         { "350%",          NULL },
         { "360%",          NULL },
         { "370%",          NULL },
         { "380%",          NULL },
         { "390%",          NULL },
         { "400%",          NULL },
         { "410%",          NULL },
         { "420%",          NULL },
         { "430%",          NULL },
         { "440%",          NULL },
         { "450%",          NULL },
         { "460%",          NULL },
         { "470%",          NULL },
         { "480%",          NULL },
         { "490%",          NULL },
         { "500%",          NULL },
         { "510%",          NULL },
         { "520%",          NULL },
         { "530%",          NULL },
         { "540%",          NULL },
         { "550%",          NULL },
         { "560%",          NULL },
         { "570%",          NULL },
         { "580%",          NULL },
         { "590%",          NULL },
         { "600%",          NULL },
         { "610%",          NULL },
         { "620%",          NULL },
         { "630%",          NULL },
         { "640%",          NULL },
         { "650%",          NULL },
         { "660%",          NULL },
         { "670%",          NULL },
         { "680%",          NULL },
         { "690%",          NULL },
         { "700%",          NULL },
         { "710%",          NULL },
         { "720%",          NULL },
         { "730%",          NULL },
         { "740%",          NULL },
         { "750%",          NULL },
         { NULL, NULL },
      },
      "100%(native)"
   },
   {
      BEETLE_OPT(gte_overclock),
      "GTE Overclock",
      NULL,
      "Lower all emulated GTE (CPU coprocessor for 3D graphics) operations to a constant one-cycle latency. For games that make heavy use of the GTE, this can greatly improve frame rate and frame time stability.",
      NULL,
      "hacks",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(skip_bios),
      "Skip BIOS",
      NULL,
      "Skip the PlayStation BIOS boot animation normally displayed when loading content. Note: Enabling this causes compatibility issues with a number of games (PAL copy protected games, Saga Frontier, etc.).",
      NULL,
      "hacks",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(override_bios),
      "Override BIOS (Restart Required)",
      NULL,
      "Override the standard region specific BIOS with a region-free one if found.",
      NULL,
      "hacks",
      {
         { "disabled", NULL },
         { "psxonpsp",  "PSP PS1 BIOS" },
         { "ps1_rom",  "PS3 PS1 BIOS" },
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(renderer),
      "Renderer (Restart Required)",
      NULL,
      "The software renderer is the most accurate but has steep performance requirements when running at increased internal GPU resolutions. The hardware renderers, while less accurate, improve performance over the software renderer at increased internal resolutions and enable various graphical enhancements. 'Hardware (Auto)' automatically selects the Vulkan or OpenGL renderer, depending upon the current libretro frontend video driver. If the provided video driver is not Vulkan or OpenGL 3.3-compatible, then the core will fall back to the software renderer.",
      NULL,
      NULL,
      {
         { "hardware",    "Hardware (Auto)" },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
         { "hardware_gl", "Hardware (OpenGL)" },
#endif
#if defined(HAVE_VULKAN)
         { "hardware_vk", "Hardware (Vulkan)" },
#endif
         { "software",    "Software" },
         { NULL, NULL },
      },
      "hardware"
   },
   {
      BEETLE_OPT(renderer_software_fb),
      "Software Framebuffer",
      NULL,
      "Enable accurate emulation of framebuffer effects (e.g. motion blur, FF7 battle swirl) when using hardware renderers by running a copy of the software renderer at native resolution in the background. If disabled, these operations are omitted (OpenGL) or rendered on the GPU (Vulkan). Disabling can improve performance but may cause severe graphical errors. Leave enabled if unsure.",
      NULL,
      "video",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
#endif
#if defined(HAVE_LIGHTREC)
   {
      BEETLE_OPT(cpu_dynarec),
      "CPU Dynarec",
      NULL,
      "Dynamically recompile CPU instructions to native instructions. Much faster than interpreter, but CPU timing is less accurate, and may have bugs.",
      NULL,
      NULL,
      {
         { "disabled", "Disabled (Beetle Interpreter)" },
         { "execute",  "Max Performance" },
         { "execute_one",  "Cycle Timing Check" },
         { "run_interpreter", "Lightrec Interpreter" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(dynarec_invalidate),
      "Dynarec Code Invalidation",
      NULL,
      "Some games require 'Full' invalidation, some require 'DMA Only'.",
      NULL,
      NULL,
      {
         { "full", "Full" },
         { "dma",  "DMA Only (Slightly Faster)" },
         { NULL, NULL },
      },
      "full"
   },
   {
      BEETLE_OPT(dynarec_eventcycles),
      "Dynarec DMA/GPU Event Cycles",
      NULL,
      "Max cycles run by CPU before a GPU or DMA Update is checked, higher number will be faster, has much less impact on beetle interpreter than dynarec.",
      NULL,
      NULL,
      {
         { "128",  NULL },
         { "256",  NULL },
         { "384",  NULL },
         { "512",  NULL },
         { "640",  NULL },
         { "768",  NULL },
         { "896",  NULL },
         { "1024",  NULL },
         { NULL, NULL },
      },
      "128"
   },
#endif
   {
      BEETLE_OPT(core_timing_fps),
      "Core-Reported FPS Timing",
      NULL,
      "Choose the FPS timing that the core will report to the frontend. Automatic Toggling will allow the core to switch between reporting progressive and interlaced rates, but may cause frontend video/audio driver re-inits.",
      NULL,
      NULL,
      {
         { "force_progressive", "Progressive Rate" },
         { "force_interlaced",  "Force Interlaced Rate" },
         { "auto_toggle", "Allow Automatic Toggling" },
      },
      "force_progressive"
   },
   {
      BEETLE_OPT(pal_video_timing_override),
      "PAL (European) Video Timing Override",
      NULL,
      "Due to different standards, PAL games often appear slowed down compared to the American or Japanese NTSC releases. This option can be used to override PAL timings in order to attempt to run these games with the NTSC framerate. This option has no effect when running NTSC content.",
      NULL,
      "video",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(crop_overscan),
      "Crop Overscan",
      NULL,
      "'None' retains padding (pillarboxes on either side of the image for NTSC, on all sides for PAL) to emulate the same black bars generated in analog video output by real PSX hardware. 'Static' only removes horizontal padding, 'Dynamic' removes all padding.",
      NULL,
      "video",
      {
         { "disabled",  "None" },
         { "static",  "Static" },
         { "smart", "Dynamic (Default)" },
         { NULL, NULL },
      },
      "smart"
   },
   {
      BEETLE_OPT(image_crop),
      "Additional Cropping",
      NULL,
      "When 'Crop Horizontal Overscan' is enabled, this option further reduces the width of the cropped image by the specified number of pixels.",
      NULL,
      "video",
      {
         { "disabled", "0" },
         { "1px",      NULL },
         { "2px",      NULL },
         { "3px",      NULL },
         { "4px",      NULL },
         { "5px",      NULL },
         { "6px",      NULL },
         { "7px",      NULL },
         { "8px",      NULL },
         { "9px",      NULL },
         { "10px",     NULL },
         { "11px",     NULL },
         { "12px",     NULL },
         { "13px",     NULL },
         { "14px",     NULL },
         { "15px",     NULL },
         { "16px",     NULL },
         { "17px",     NULL },
         { "18px",     NULL },
         { "19px",     NULL },
         { "20px",     NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(image_offset),
      "Offset Cropped Image",
      NULL,
      "When 'Crop Horizontal Overscan' is enabled, this allows the resultant cropped image to be offset horizontally to the right (positive) or left (negative) by the specified number of pixels. May be used to correct alignment issues. Only supported by the software renderer.",
      NULL,
      "video",
      {
         { "-12px",    NULL },
         { "-11px",    NULL },
         { "-10px",    NULL },
         { "-9px",     NULL },
         { "-8px",     NULL },
         { "-7px",     NULL },
         { "-6px",     NULL },
         { "-5px",     NULL },
         { "-4px",     NULL },
         { "-3px",     NULL },
         { "-2px",     NULL },
         { "-1px",     NULL },
         { "disabled", "0 (Default)" },
         { "+1px",     NULL },
         { "+2px",     NULL },
         { "+3px",     NULL },
         { "+4px",     NULL },
         { "+5px",     NULL },
         { "+6px",     NULL },
         { "+7px",     NULL },
         { "+8px",     NULL },
         { "+9px",     NULL },
         { "+10px",    NULL },
         { "+11px",    NULL },
         { "+12px",    NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(image_offset_cycles),
      "Horizontal Image Offset (GPU Cycles)",
      NULL,
      "Choose number of GPU cycles to offset image by. Positive values move image to the right, negative values move image to the left. Only supported by the hardware renderers.",
      NULL,
      "video",
      {
         { "-40",      NULL },
         { "-39",      NULL },
         { "-38",      NULL },
         { "-37",      NULL },
         { "-36",      NULL },
         { "-35",      NULL },
         { "-34",      NULL },
         { "-33",      NULL },
         { "-32",      NULL },
         { "-31",      NULL },
         { "-30",      NULL },
         { "-29",      NULL },
         { "-28",      NULL },
         { "-27",      NULL },
         { "-26",      NULL },
         { "-25",      NULL },
         { "-24",      NULL },
         { "-23",      NULL },
         { "-22",      NULL },
         { "-21",      NULL },
         { "-20",      NULL },
         { "-19",      NULL },
         { "-18",      NULL },
         { "-17",      NULL },
         { "-16",      NULL },
         { "-15",      NULL },
         { "-14",      NULL },
         { "-13",      NULL },
         { "-12",      NULL },
         { "-11",      NULL },
         { "-10",      NULL },
         { "-9",       NULL },
         { "-8",       NULL },
         { "-7",       NULL },
         { "-6",       NULL },
         { "-5",       NULL },
         { "-4",       NULL },
         { "-3",       NULL },
         { "-2",       NULL },
         { "-1",       NULL },
         { "0",        "0 (Default)" },
         { "+1",       NULL },
         { "+2",       NULL },
         { "+3",       NULL },
         { "+4",       NULL },
         { "+5",       NULL },
         { "+6",       NULL },
         { "+7",       NULL },
         { "+8",       NULL },
         { "+9",       NULL },
         { "+10",      NULL },
         { "+11",      NULL },
         { "+12",      NULL },
         { "+13",      NULL },
         { "+14",      NULL },
         { "+15",      NULL },
         { "+16",      NULL },
         { "+17",      NULL },
         { "+18",      NULL },
         { "+19",      NULL },
         { "+20",      NULL },
         { "+21",      NULL },
         { "+22",      NULL },
         { "+23",      NULL },
         { "+24",      NULL },
         { "+25",      NULL },
         { "+26",      NULL },
         { "+27",      NULL },
         { "+28",      NULL },
         { "+29",      NULL },
         { "+30",      NULL },
         { "+31",      NULL },
         { "+32",      NULL },
         { "+33",      NULL },
         { "+34",      NULL },
         { "+35",      NULL },
         { "+36",      NULL },
         { "+37",      NULL },
         { "+38",      NULL },
         { "+39",      NULL },
         { "+40",      NULL },
         { NULL, NULL},
      },
      "0"
   },
#endif
   {
      BEETLE_OPT(gpu_overclock),
      "GPU Rasterizer Overclock",
      NULL,
      "Enable overclocking of the 2D rasterizer contained within the emulated PSX's GPU. Does not improve 3D rendering, and in general has little effect.",
      NULL,
      "video",
      {
         { "1x(native)", "1x (Native)" },
         { "2x",         NULL },
         { "4x",         NULL },
         { "8x",         NULL },
         { "16x",        NULL },
         { "32x",        NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
   {
      BEETLE_OPT(aspect_ratio),
      "Core Aspect Ratio",
      NULL,
      "Choose core provided aspect ratio. This setting is ignored when the Widescreen Mode Hack or Display Full VRAM options are enabled.",
      NULL,
      "video",
      {
         { "corrected", "Corrected" },
         { "uncorrected", "Uncorrected" },
         { "4:3",  "Force 4:3" },
         { "ntsc", "Force NTSC" },
      },
      "corrected"
   },
   {
      BEETLE_OPT(initial_scanline),
      "Initial Scan Line - NTSC",
      NULL,
      "Choose the first displayed scan line when running NTSC content. Values greater than zero will reduce the height of output images by cropping pixels from the topmost edge. May be used to counteract letterboxing.",
      NULL,
      "video",
      {
         { "0",  NULL },
         { "1",  NULL },
         { "2",  NULL },
         { "3",  NULL },
         { "4",  NULL },
         { "5",  NULL },
         { "6",  NULL },
         { "7",  NULL },
         { "8",  NULL },
         { "9",  NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(last_scanline),
      "Last Scan Line - NTSC",
      NULL,
      "Choose the last displayed scan line when running NTSC content. Values less than 239 will reduce the height of output images by cropping pixels from the bottommost edge. May be used to counteract letterboxing.",
      NULL,
      "video",
      {
         { "210", NULL },
         { "211", NULL },
         { "212", NULL },
         { "213", NULL },
         { "214", NULL },
         { "215", NULL },
         { "216", NULL },
         { "217", NULL },
         { "218", NULL },
         { "219", NULL },
         { "220", NULL },
         { "221", NULL },
         { "222", NULL },
         { "223", NULL },
         { "224", NULL },
         { "225", NULL },
         { "226", NULL },
         { "227", NULL },
         { "228", NULL },
         { "229", NULL },
         { "230", NULL },
         { "231", NULL },
         { "232", NULL },
         { "233", NULL },
         { "234", NULL },
         { "235", NULL },
         { "236", NULL },
         { "237", NULL },
         { "238", NULL },
         { "239", "239 (Default)" },
         { NULL, NULL },
      },
      "239"
   },
   {
      BEETLE_OPT(initial_scanline_pal),
      "Initial Scan Line - PAL",
      NULL,
      "Choose the first displayed scan line when running PAL content. Values greater than zero will reduce the height of output images by cropping pixels from the topmost edge. May be used to counteract letterboxing.",
      NULL,
      "video",
      {
         { "0",  NULL },
         { "1",  NULL },
         { "2",  NULL },
         { "3",  NULL },
         { "4",  NULL },
         { "5",  NULL },
         { "6",  NULL },
         { "7",  NULL },
         { "8",  NULL },
         { "9",  NULL },
         { "10", NULL },
         { "11", NULL },
         { "12", NULL },
         { "13", NULL },
         { "14", NULL },
         { "15", NULL },
         { "16", NULL },
         { "17", NULL },
         { "18", NULL },
         { "19", NULL },
         { "20", NULL },
         { "21", NULL },
         { "22", NULL },
         { "23", NULL },
         { "24", NULL },
         { "25", NULL },
         { "26", NULL },
         { "27", NULL },
         { "28", NULL },
         { "29", NULL },
         { "30", NULL },
         { "31", NULL },
         { "32", NULL },
         { "33", NULL },
         { "34", NULL },
         { "35", NULL },
         { "36", NULL },
         { "37", NULL },
         { "38", NULL },
         { "39", NULL },
         { "40", NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(last_scanline_pal),
      "Last Scan Line - PAL",
      NULL,
      "Choose the last displayed scan line when running PAL content. Values below 287 will reduce the height of output images by cropping pixels from the bottommost edge. May be used to counteract letterboxing.",
      NULL,
      "video",
      {
         { "230", NULL },
         { "231", NULL },
         { "232", NULL },
         { "233", NULL },
         { "234", NULL },
         { "235", NULL },
         { "236", NULL },
         { "237", NULL },
         { "238", NULL },
         { "239", NULL },
         { "240", NULL },
         { "241", NULL },
         { "242", NULL },
         { "243", NULL },
         { "244", NULL },
         { "245", NULL },
         { "246", NULL },
         { "247", NULL },
         { "248", NULL },
         { "249", NULL },
         { "250", NULL },
         { "251", NULL },
         { "252", NULL },
         { "253", NULL },
         { "254", NULL },
         { "255", NULL },
         { "256", NULL },
         { "257", NULL },
         { "258", NULL },
         { "259", NULL },
         { "260", NULL },
         { "261", NULL },
         { "262", NULL },
         { "263", NULL },
         { "264", NULL },
         { "265", NULL },
         { "266", NULL },
         { "267", NULL },
         { "268", NULL },
         { "269", NULL },
         { "270", NULL },
         { "271", NULL },
         { "272", NULL },
         { "273", NULL },
         { "274", NULL },
         { "275", NULL },
         { "276", NULL },
         { "277", NULL },
         { "278", NULL },
         { "279", NULL },
         { "280", NULL },
         { "281", NULL },
         { "282", NULL },
         { "283", NULL },
         { "284", NULL },
         { "285", NULL },
         { "286", NULL },
         { "287", "287 (Default)" },
         { NULL, NULL },
      },
      "287"
   },
#ifndef EMSCRIPTEN
   {
      BEETLE_OPT(cd_access_method),
      "CD Access Method (Restart Required)",
      NULL,
      "Choose method used to read data from content disk images. 'Synchronous' mimics original hardware. 'Asynchronous' can reduce stuttering on devices with slow storage. 'Pre-Cache' loads the entire disk image into memory when launching content, which may improve in-game loading times at the cost of an initial delay at startup. 'Pre-Cache' may cause issues on systems with low RAM, and will fall back to 'Synchronous' for physical media.",
      NULL,
      NULL,
      {
         { "sync",     "Synchronous" },
         { "async",    "Asynchronous" },
         { "precache", "Pre-Cache" },
         { NULL, NULL },
      },
      "sync"
   },
#endif
   {
      BEETLE_OPT(cd_fastload),
      "CD Loading Speed",
      NULL,
      "Choose disk access speed multiplier. Values higher than '2x (Native)' can greatly reduce in-game loading times, but may introduce timing errors. Some games may not function properly above a certain value.",
      NULL,
      NULL,
      {
         { "2x(native)", "2x (Native)" },
         { "4x",          NULL },
         { "6x",          NULL },
         { "8x",          NULL },
         { "10x",         NULL },
         { "12x",         NULL },
         { "14x",         NULL },
         { NULL, NULL },
      },
      "2x(native)"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
   &options_us,    /* RETRO_LANGUAGE_ENGLISH */
   &options_ja,      /* RETRO_LANGUAGE_JAPANESE */
   &options_fr,      /* RETRO_LANGUAGE_FRENCH */
   &options_es,      /* RETRO_LANGUAGE_SPANISH */
   &options_de,      /* RETRO_LANGUAGE_GERMAN */
   &options_it,      /* RETRO_LANGUAGE_ITALIAN */
   &options_nl,      /* RETRO_LANGUAGE_DUTCH */
   &options_pt_br,   /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   &options_pt_pt,   /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   &options_ru,      /* RETRO_LANGUAGE_RUSSIAN */
   &options_ko,      /* RETRO_LANGUAGE_KOREAN */
   &options_cht,     /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   &options_chs,     /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   &options_eo,      /* RETRO_LANGUAGE_ESPERANTO */
   &options_pl,      /* RETRO_LANGUAGE_POLISH */
   &options_vn,      /* RETRO_LANGUAGE_VIETNAMESE */
   &options_ar,      /* RETRO_LANGUAGE_ARABIC */
   &options_el,      /* RETRO_LANGUAGE_GREEK */
   &options_tr,      /* RETRO_LANGUAGE_TURKISH */
   &options_sk,      /* RETRO_LANGUAGE_SLOVAK */
   &options_fa,      /* RETRO_LANGUAGE_PERSIAN */
   &options_he,      /* RETRO_LANGUAGE_HEBREW */
   &options_ast,     /* RETRO_LANGUAGE_ASTURIAN */
   &options_fi,      /* RETRO_LANGUAGE_FINNISH */
   &options_id,      /* RETRO_LANGUAGE_INDONESIAN */
   &options_sv,      /* RETRO_LANGUAGE_SWEDISH */
   &options_uk,      /* RETRO_LANGUAGE_UKRAINIAN */
};
#endif

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should be called as early as possible - ideally inside
 * retro_set_environment(), and no later than retro_load_game()
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static INLINE void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
   unsigned language = 0;
#endif

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
