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
 * VERSION: 1.3
 ********************************
 *
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

struct retro_core_option_definition option_defs_us[] = {
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(renderer),
      "Renderer (Restart)",
      "Choose video renderer. 'Software' is the most accurate but has the highest performance requirements when running at increased internal GPU resolutions. 'Hardware' selects automatically the 'OpenGL' or 'Vulkan' renderer, depending upon the current RetroArch video driver setting. While less accurate, these renderers improve performance and enable various enhancements such as texture filtering and perspective correction.",
      {
         { "hardware", "Hardware" },
         { "software", "Software" },
         { NULL, NULL },
      },
      "hardware"
   },
   {
      BEETLE_OPT(renderer_software_fb),
      "Software Framebuffer",
      "Enables accurate emulation of framebuffer effects (e.g. motion blur, FF7 battle swirl) when using the 'Hardware' renderer. If disabled, certain operations are omitted or rendered on the GPU. This can improve performance but may cause graphical glitches/errors.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
#endif
#ifdef HAVE_VULKAN
   {
      BEETLE_OPT(adaptive_smoothing),
      "Adaptive Smoothing",
      "Enable smoothing of 2D artwork and UI elements without blurring 3D rendered objects. Only supported by the Vulkan renderer.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(super_sampling),
      "Supersampling (Downsample From Internal Upscale)",
      "When enabled, renders content at the specified 'Internal GPU Resolution' then downsamples the resultant image to ~240p. Allows games to be displayed at native (low) resolution but with clean anti-aliased 3D objects. Produces best results when applied to titles that mix 2D and 3D elements (e.g. 3D characters on pre-rendered backgrounds), and works well in conjunction with CRT shaders. Only supported by the Vulkan renderer. Note: When using this feature, the 'Dithering Pattern' option should be disabled.",
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
      "Apply multi-sample anti-aliasing (MSAA) to rendered content. This is a type of spatial anti-aliasing similar to supersampling, but of somewhat lower quality and with (correspondingly) lower performance requirements. Improves the appearance of 3D objects. Only supported by the Vulkan renderer.",
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
      "Improves the quality of FMV playback by reducing 'macroblocking' artefacts (squares/jagged edges). Only supported by the Vulkan renderer.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(internal_resolution),
      "Internal GPU Resolution",
      "Select internal resolution multiplier. Resolutions higher than '1x (Native)' improve the fidelity of 3D models at the expense of increased performance requirements. 2D elements are generally unaffected by this setting.",
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
      "The PSX has a limited color depth of 16 bits per pixel (bpp). This leads to 'banding' effects (uneven color gradients) which are 'smoothed out' by original hardware through the use of a dithering pattern. The 'Dithered 16bpp (Native)' setting emulates this behaviour. Selecting '32 bpp' increases the color depth such that smooth gradients can be achieved without dithering, allowing for a 'cleaner' output image. Only the OpenGL renderer supports this choice (the Software renderer forces 16 bpp, while the Vulkan renderer forces 32 bpp). Note: the 'Dithering Pattern' option should be disabled when using increased color depth.",
      {
         { "dithered 16bpp (native)", "Dithered 16 bpp (Native)" },
         { "32bpp",                   "32 bpp" },
         { NULL, NULL },
      },
      "dithered 16bpp (native)"
   },
   {
      BEETLE_OPT(wireframe),
      "Wireframe Mode",
      "When enabled, renders 3D models in outline form, without textures or shading. Only supported by the OpenGL renderer. Note: This is for debugging purposes, and should normally be disabled.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(display_vram),
      "Display Full VRAM",
      "When enabled, visualises the entire contents of the emulated console's video RAM. Only supported by the OpenGL renderer. Note: This is for debugging purposes, and should normally be disabled.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(filter),
      "Texture Filtering",
      "Enables the use of a filter to modify/enhance the appearance of polygon textures and 2D artwork. 'Nearest' emulates original hardware. 'Bilinear' and '3-Point' are smoothing filters, which reduce pixelation via blurring. 'SABR', 'xBR' and 'JINC2' are upscaling filters, which improve texture fidelity/sharpness at the expense of increased performance requirements. Only supported by the 'Hardware' renderers.",
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
   {
      BEETLE_OPT(pgxp_mode),
      "PGXP Operation Mode",
      "Due to engineering constraints, the PSX renders 3D models using fixed point mathematics. This means polygon vertices are 'rounded' to the nearest pixel, causing distortion and 'jitter' as objects move on screen. Enabling the Parallel/Precision Geometry Transform Pipeline (PGXP) removes this hardware limitation and allows polygons to be rendered with subpixel precision, greatly improving visual appearance at the expense of increased performance requirements. The 'Memory Only' mode has few compatibility issues and is recommended for general use. The 'Memory + CPU (Buggy)' mode can further reduce jitter, but is highly demanding and may cause geometry errors. Only supported by the 'Hardware' renderers.",
      {
         { "disabled",     NULL },
         { "memory only",  "Memory Only" },
         { "memory + CPU", "Memory + CPU (Buggy)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_vertex),
      "PGXP Vertex Cache",
      "When enabled, PGXP-enhanced polygon vertex positions are buffered in a RAM cache. This in theory allows subpixel-accurate values to be used across successive polygon draw operations, instead of rebasing from the native PSX data each time. This should improve object alignment and may thus reduce visible seams when rendering textures, but 'false positives' when querying the cache produce graphical glitches in most games. It is recommended to leave this option disabled. Only supported by the 'Hardware' renderers.",
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
      "Due to engineering constraints, the PSX renders 3D objects using 'affine' texture mapping, whereby texture coordinates are interpolated between polygon vertices in 2D screen space with no consideration of object depth. This causes significant position-dependent distortion/bending of textures (for example, warped lines across floors and walls). Enabling perspective correct texturing removes this hardware limitation, accounting correctly for vertex position in 3D space and eliminating texture distortion, at the expense of increased performance requirements. Only supported by the 'Hardware' renderers.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(lineRender),
      "Line-to-Quad Hack",
      "Certain games employ a special technique for drawing horizontal lines, which involves stretching single-pixel-high triangles across the screen in a manner that causes the PSX hardware to rasterise them as a row of pixels. Examples include Doom/Hexen, and the water effects in Soul Blade. When running such games with a 'Hardware' renderer and/or with an internal GPU resolution higher than native, these triangles no longer resolve as a line, causing gaps to appear in the output image. Setting 'Line-to-Quad Hack' to 'Default' solves this issue by detecting small triangles and converting them as required. The 'Aggressive' option will likely introduce visual glitches due to false positives, but is needed for correct rendering of some 'difficult' titles (e.g. Dark Forces, Duke Nukem).",
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
      "When enabled, forces content to be rendered with an aspect ratio of 16:9. Produces best results with fully 3D games. 2D artwork will be stretched horizontally.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(frame_duping),
      "Frame Duping (Speedup)",
      "When enabled, provides a small performance increase by redrawing the last rendered frame (instead of presenting a new one) if the content of the current frame is unchanged.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(cpu_freq_scale),
      "CPU Frequency Scaling (Overclock)",
      "Enable overclocking (or underclocking) of the emulated PSX's central processing unit. The default frequency of the MIPS R3000A-compatible 32-bit RISC CPU is 33.8688 MHz; running at higher frequencies can eliminate slowdown and improve frame rates in certain games at the expense of increased performance requirements. Note that some games have an internal frame rate limiter, and may not benefit from overclocking.",
      {
         { "50%",           NULL },
         { "60%",           NULL },
         { "70%",           NULL },
         { "80%",           NULL },
         { "90%",           NULL },
         { "100% (native)", "100% (Native)" },
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
         { NULL, NULL },
      },
      "100% (native)"
   },
   {
      BEETLE_OPT(gte_overclock),
      "GTE Overclock",
      "When enabled, eliminates virtually all the latency of operations involving the emulated PSX's Geometry Transform Engine (CPU coprocessor used for calculations related to 3D projection - i.e. all 3D graphics). For games that make heavy use of the GTE, this can greatly improve frame rate (and frame time) stability at the expense of increased performance requirements.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(gpu_overclock),
      "GPU Rasterizer Overclock",
      "Enable overclocking of the 2D rasterizer contained within the emulated PSX's GPU. Does not improve 3D rendering, and in general has little effect.",
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
      BEETLE_OPT(skip_bios),
      "Skip BIOS",
      "When enabled, skips the PSX BIOS boot animation that would normally be displayed when starting content. Note that this causes compatibility issues with a small minority of games (Saga Frontier, PAL copy protected games, etc).",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(dither_mode),
      "Dithering Pattern",
      "Configures emulation of the dithering pattern used by original hardware to 'smooth out' color banding artefacts caused by the PSX's limited color depth. '1x (Native)' is authentic, but when running at increased internal GPU resolution the 'Internal Resolution' setting produces cleaner results. Note: This option should be disabled when 'Internal Color Depth' is set to '32 bpp', or when using the Vulkan renderer.",
      {
         { "1x(native)",          "1x (Native)" },
         { "internal resolution", "Internal Resolution" },
         { "disabled",            NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
   {
      BEETLE_OPT(display_internal_fps),
      "Display Internal FPS",
      "When enabled, shows the frame rate at which the emulated PSX is rendering content. Note: This requires 'Onscreen Notifications' to be enabled in the RetroArch 'Onscreen Display' settings menu.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(crop_overscan),
      "Crop Overscan",
      "By default, the core adds horizontal padding (black bars or 'pillarboxes' either side of the screen) to simulate the horizontal overscan region of the NTSC or PAL broadcast signal (this is normally hidden by the bezel around the edge of a standard-definition television). It does not, however, add padding to simulate *vertical* overscan. This means video output is horizontally compressed, and thus presented with an incorrect aspect ratio. When 'Crop Overscan' is enabled, the horizontal padding is automatically removed, ensuring correct display dimensions.",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(image_crop),
      "Additional Cropping",
      "When 'Crop Overscan' is enabled, further reduces the width of the cropped image by the specified number of pixels. Note: This can have unintended consequences. While the absolute width is reduced, the resultant video is still scaled to the currently set aspect ratio. Enabling 'Additional Cropping' may therefore cause horizontal stretching.",
      {
         { "disabled", NULL },
         { "1 px",     NULL },
         { "2 px",     NULL },
         { "3 px",     NULL },
         { "4 px",     NULL },
         { "5 px",     NULL },
         { "6 px",     NULL },
         { "7 px",     NULL },
         { "8 px",     NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(image_offset),
      "Offset Cropped Image",
      "When 'Crop Overscan' is enabled, allows the resultant cropped image to be offset horizontally by the specified number of pixels. May be used to correct alignment issues.",
      {
         { "disabled", NULL },
         { "-4 px",    NULL },
         { "-3 px",    NULL },
         { "-2 px",    NULL },
         { "-1 px",    NULL },
         { "1 px",     NULL },
         { "2 px",     NULL },
         { "3 px",     NULL },
         { "4 px",     NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(analog_calibration),
      "Analog Self-Calibration",
      "When enabled, and when the input device type is set to 'DualShock', 'Analog Controller', 'Analog Joystick' or 'neGcon', allows dynamic calibration of analog inputs. Maximum registered input values are monitored in real time, and used to scale analog coordinates passed to the emulator. This is generally not required when using high quality controllers, but it can improve accuracy/response when using gamepads with 'flawed' analog sticks (i.e. that have range/symmetry issues). For best results, the left and right analog sticks should be rotated at full extent to 'tune' the calibration algorithm before playing (this must be done each time content is launched).",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(analog_toggle),
      "DualShock Analog Button Toggle",
      "When the input device type is 'DualShock', sets the state of the 'ANALOG' button found on original controller hardware. If disabled, the left and right analog sticks are always active. If enabled, the analog sticks are inactive by default, but may be toggled on by pressing and holding START+SELECT+L1+L2+R1+R2.",
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
      "Enables/Disables multitap functionality on port 1.",
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
      "Enables/Disables multitap functionality on port 2.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(gun_cursor),
      "Gun Cursor",
      "Select the gun cursor to be displayed on screen while using the the 'Guncon / G-Con 45' and 'Justifier' input device types. When disabled, cross hairs are always hidden.",
      {
         { "Cross", NULL },
         { "Dot",   NULL },
         { "Off",   "disabled" },
         { NULL, NULL },
      },
      "Cross"
   },
   {
      BEETLE_OPT(gun_input_mode),
      "Gun Input Mode",
      "When device type is set to 'Guncon / G-Con 45' or 'Justifier', specify whether to use a mouse-controlled 'Light Gun' or 'Touchscreen' input.",
      {
         { "Lightgun",    "Light Gun" },
         { "Touchscreen", NULL },
         { NULL, NULL },
      },
      "Lightgun"
   },
   {
      BEETLE_OPT(mouse_sensitivity),
      "Mouse Sensitivity",
      "Configure the response of the 'Mouse' input device type.",
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
         { "100%", NULL },
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
      BEETLE_OPT(negcon_deadzone),
      "NegCon Twist Deadzone (Percent)",
      "Sets the deadzone of the RetroPad left analog stick when simulating the 'twist' action of emulated 'neGcon' input devices. Used to eliminate controller drift. Note: Most negCon-compatible titles provide in-game options for setting a 'twist' deadzone value. To avoid loss of precision, the in-game deadzone should *always* be set to zero. Any required adjustments should *only* be applied via this core option. This is particularly important when 'NegCon Twist Response' is set to 'Quadratic' or 'Cubic'.",
      {
         { "0",  NULL },
         { "5",  NULL },
         { "10", NULL },
         { "15", NULL },
         { "20", NULL },
         { "25", NULL },
         { "30", NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(negcon_response),
      "NegCon Twist Response",
      "Specifies the response of the RetroPad left analog stick when simulating the 'twist' action of emulated 'neGcon' input devices. Analog stick displacement may be mapped to negCon rotation angle either linearly, quadratically or cubically. 'Quadratic' allows for greater precision than 'Linear' when making small movements. 'Cubic' further increases small movement precision, but 'exaggerates' larger movements. Note: 'Linear' is only recommended when using racing wheel peripherals. Conventional gamepads implement analog input in a manner fundamentally different from the neGcon 'twist' mechanism, such that linear mapping over-amplifies small movements, impairing fine control. In most cases, 'Quadratic' provides the closest approximation of real hardware.",
      {
         { "linear",    "Linear" },
         { "quadratic", "Quadratic" },
         { "cubic",     "Cubic" },
         { NULL, NULL },
      },
      "linear"
   },
#ifndef EMSCRIPTEN
   {
      BEETLE_OPT(cd_access_method),
      "CD Access Method (Restart)",
      "Select method used to read data from content disk images. 'Synchronous' mimics original hardware. 'Asynchronous' can reduce stuttering on devices with slow storage. 'Pre-Cache' loads the entire disk image into memory when launching content; this provides the best performance and may improve in-game loading times, at the expense of increased RAM usage and an initial delay at startup.",
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
      BEETLE_OPT(use_mednafen_memcard0_method),
      "Memory Card 0 Method",
      "Choose the save data format used for memory card 0. 'Libretro' is recommended. 'Mednafen' may be used for compatibility with the stand-alone version of Mednafen.",
      {
         { "libretro", "Libretro" },
         { "mednafen", "Mednafen" },
         { NULL, NULL },
      },
      "libretro"
   },
   {
      BEETLE_OPT(enable_memcard1),
      "Enable Memory Card 1",
      "Select whether to emulate a second memory card in slot 1. When disabled, games can only access the memory card in slot 0. Note: Some games require this option to be enabled for correct operation (e.g. Codename Tenka).",
      {
         { "enabled",  NULL },
         { "disabled", NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(shared_memory_cards),
      "Shared Memcards (Restart)",
      "When enabled, all games will save to and load from the same memory card data files. When disabled, separate memory card files will be generated for each item of loaded content. Note: If shared memory cards are enabled, the 'Memory Card 0 Method' option *must* be set to 'Mednafen' for correct operation.",
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(cd_fastload),
      "Increase CD Loading Speed",
      "Select disk access speed multiplier. Setting this higher than '2x (Native)' can greatly reduce in-game loading times, but may introduce errors/timing glitches. Some games break entirely if the loading speed is increased above a certain value.",
      {
         { "2x (native)", "2x (Native)" },
         { "4x",          NULL },
         { "6x",          NULL },
         { "8x",          NULL },
         { "10x",         NULL },
         { "12x",         NULL },
         { "14x",         NULL },
         { NULL, NULL },
      },
      "2x (native)"
   },
   {
      BEETLE_OPT(initial_scanline),
      "Initial Scanline - NTSC (Restart)",
      "Select the first displayed scanline when running NTSC content. Setting a value greater than zero will reduce the height of output images by cropping pixels from the topmost edge. May be used to counteract letterboxing. Note: This can have unintended consequences. While the absolute height is reduced, the resultant video is still scaled to the currently set aspect ratio. Non-zero values may therefore cause vertical stretching.",
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
      "Last Scanline - NTSC (Restart)",
      "Select the last displayed scanline when running NTSC content. Setting a value less than 239 will reduce the height of output images by cropping pixels from the bottommost edge. May be used to counteract letterboxing. Note: This can have unintended consequences. While the absolute height is reduced, the resultant video is still scaled to the currently set aspect ratio. Values less than 239 may therefore cause vertical stretching.",
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
         { "239", NULL },
         { NULL, NULL },
      },
      "239"
   },
   {
      BEETLE_OPT(initial_scanline_pal),
      "Initial Scanline - PAL (Restart)",
      "Select the first displayed scanline when running PAL content. Setting a value greater than zero will reduce the height of output images by cropping pixels from the topmost edge. May be used to counteract letterboxing. Note: This can have unintended consequences. While the absolute height is reduced, the resultant video is still scaled to the currently set aspect ratio. Non-zero values may therefore cause vertical stretching.",
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
      "Last Scanline - PAL (Restart)",
      "Select the last displayed scanline when running PAL content. Setting a value less than 287 will reduce the height of output images by cropping pixels from the bottommost edge. May be used to counteract letterboxing. Note: This can have unintended consequences. While the absolute height is reduced, the resultant video is still scaled to the currently set aspect ratio. Values less than 287 may therefore cause vertical stretching.",
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
         { "287", NULL },
         { NULL, NULL },
      },
      "287"
   },
   { NULL, NULL, NULL, {{0}}, NULL },
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_option_definition *option_defs_intl[RETRO_LANGUAGE_LAST] = {
   option_defs_us, /* RETRO_LANGUAGE_ENGLISH */
   NULL,           /* RETRO_LANGUAGE_JAPANESE */
   NULL,           /* RETRO_LANGUAGE_FRENCH */
   NULL,           /* RETRO_LANGUAGE_SPANISH */
   NULL,           /* RETRO_LANGUAGE_GERMAN */
   NULL,           /* RETRO_LANGUAGE_ITALIAN */
   NULL,           /* RETRO_LANGUAGE_DUTCH */
   NULL,           /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   NULL,           /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   NULL,           /* RETRO_LANGUAGE_RUSSIAN */
   NULL,           /* RETRO_LANGUAGE_KOREAN */
   NULL,           /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   NULL,           /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   NULL,           /* RETRO_LANGUAGE_ESPERANTO */
   NULL,           /* RETRO_LANGUAGE_POLISH */
   NULL,           /* RETRO_LANGUAGE_VIETNAMESE */
   NULL,           /* RETRO_LANGUAGE_ARABIC */
   NULL,           /* RETRO_LANGUAGE_GREEK */
   NULL,           /* RETRO_LANGUAGE_TURKISH */
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

static INLINE void libretro_set_core_options(retro_environment_t environ_cb)
{
   unsigned version = 0;

   if (!environ_cb)
      return;

   if (environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && (version >= 1))
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_intl core_options_intl;
      unsigned language = 0;

      core_options_intl.us    = option_defs_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = option_defs_intl[language];

      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_intl);
#else
      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, &option_defs_us);
#endif
   }
   else
   {
      size_t i;
      size_t num_options               = 0;
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      /* Allocate arrays */
      variables  = (struct retro_variable *)calloc(num_options + 1, sizeof(struct retro_variable));
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
               size_t j;

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

         variables[i].key   = key;
         variables[i].value = values_buf[i];
      }

      /* Set variables */
      environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);

error:

      /* Clean up */
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
