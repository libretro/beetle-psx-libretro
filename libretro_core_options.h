#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include <retro_inline.h>

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
      "Renderer (restart)",
      "",
      {
         { "hardware",   "Hardware" },
         { "software",      "Software" },
         { NULL, NULL },
      },
      "hardware"
   },
   {
      BEETLE_OPT(renderer_software_fb),
      "Software framebuffer",
      "",
      {
         { "enabled",   NULL },
         { "disabled",  NULL },
         { NULL, NULL },
      },
      "enabled"
   },
#else
   {
      BEETLE_OPT(renderer),
      "Renderer (restart)",
      "",
      {
         { "software",      "Software" },
         { NULL, NULL },
      },
      "software"
   },
   {
      BEETLE_OPT(renderer_software_fb),
      "Software framebuffer",
      "",
      {
         { "enabled",   NULL },
         { NULL, NULL },
      },
      "enabled"
   },
#endif
#ifdef HAVE_VULKAN
   {
      BEETLE_OPT(adaptive_smoothing),
      "Adaptive smoothing",
      "",
      {
         { "enabled",       NULL },
         { "disabled",       NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(super_sampling),
      "Super sampling (downsample from internal upscale)",
      "",
      {
         { "disabled",       NULL },
         { "enabled",       NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(msaa),
      "Multi-Sampled Anti Aliasing",
      "",
      {
         { "1x",       NULL },
         { "2x",       NULL },
         { "4x",       NULL },
         { "8x",       NULL },
         { "16x",       NULL },
         { NULL, NULL },
      },
      "1x"
   },
   {
      BEETLE_OPT(mdec_yuv),
      "MDEC YUV Chroma filter",
      "",
      {
         { "disabled",       NULL },
         { "enabled",       NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(internal_resolution),
      "Internal GPU resolution",
      "",
      {
         { "1x(native)", "1x (Native)" },
         { "2x",       NULL },
         { "4x",       NULL },
         { "8x",       NULL },
         { "16x",       NULL },
         { NULL, NULL },
      },
      "1x(native)"
   },
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES)
   {
      BEETLE_OPT(depth),
      "Internal color depth",
      "",
      {
         { "dithered 16bpp (native)", "Dithered 16bpp (Native)" },
         { "32bpp",       NULL },
         { NULL, NULL },
      },
      "dithered 16bpp (native)"
   },
   {
      BEETLE_OPT(wireframe),
      "Wireframe mode",
      "",
      {
         { "disabled", NULL},
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(display_vram),
      "Display full VRAM",
      "",
      {
         { "disabled", NULL},
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
#endif
#if defined(HAVE_OPENGL) || defined(HAVE_OPENGLES) || defined(HAVE_VULKAN)
   {
      BEETLE_OPT(filter),
      "Texture filtering",
      "",
      {
         { "nearest", NULL},
         { "SABR",  NULL },
         { "xBR",  NULL },
         { "bilinear",  NULL },
         { "3-point",  NULL },
         { "JINC2",  NULL },
         { NULL, NULL },
      },
      "nearest"
   },
   {
      BEETLE_OPT(pgxp_mode),
      "PGXP operation mode",
      "",
      {
         { "disabled", NULL},
         { "memory only",  "Memory Only" },
         { "memory + CPU", "Memory + CPU (Buggy)" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_vertex),
      "PGXP vertex cache",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(pgxp_texture),
      "PGXP perspective correct texturing",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
#endif
   {
      BEETLE_OPT(lineRender),
      "Line-to-quad hack",
      "",
      {
         { "default", NULL},
         { "aggressive", NULL},
         { "disabled", NULL},
         { NULL, NULL },
      },
      "default"
   },
   {
      BEETLE_OPT(widescreen_hack),
      "Widescreen mode hack",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(frame_duping),
      "Frame duping (speedup)",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(cpu_freq_scale),
      "CPU frequency scaling (overclock)",
      "",
      {
         { "50%", NULL},
         { "60%", NULL},
         { "70%", NULL},
         { "80%", NULL},
         { "90%", NULL},
         { "100% (native)", NULL},
         { "110%", NULL},
         { "120%", NULL},
         { "130%", NULL},
         { "140%", NULL},
         { "150%", NULL},
         { "160%", NULL},
         { "170%", NULL},
         { "180%", NULL},
         { "190%", NULL},
         { "200%", NULL},
         { "210%", NULL},
         { "220%", NULL},
         { "230%", NULL},
         { "240%", NULL},
         { "250%", NULL},
         { "260%", NULL},
         { "270%", NULL},
         { "280%", NULL},
         { "290%", NULL},
         { "300%", NULL},
         { "310%", NULL},
         { "320%", NULL},
         { "330%", NULL},
         { "340%", NULL},
         { "350%", NULL},
         { "360%", NULL},
         { "370%", NULL},
         { "380%", NULL},
         { "390%", NULL},
         { "400%", NULL},
         { "410%", NULL},
         { "420%", NULL},
         { "430%", NULL},
         { "440%", NULL},
         { "450%", NULL},
         { "460%", NULL},
         { "470%", NULL},
         { "480%", NULL},
         { "490%", NULL},
         { "500%", NULL},
         { NULL, NULL },
      },
      "100% (native)"
   },
   {
      BEETLE_OPT(gte_overclock),
      "GTE Overclock",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(gpu_overclock),
      "GPU rasterizer Overclock",
      "",
      {
         { "1x(native)", NULL},
         { "2x", NULL},
         { "4x", NULL},
         { "8x", NULL},
         { "16x", NULL},
         { "32x", NULL},
         { NULL, NULL },
      },
      "1x(native)"
   },
   {
      BEETLE_OPT(skip_bios),
      "Skip BIOS",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(dither_mode),
      "Dithering pattern",
      "",
      {
         { "1x(native)", NULL},
         { "internal resolution", NULL},
         { "disabled", NULL},
         { NULL, NULL },
      },
      "1x(native)"
   },
   {
      BEETLE_OPT(display_internal_fps),
      "Display internal FPS",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(crop_overscan),
      "Crop Overscan",
      "",
      {
         { "enabled", NULL},
         { "disabled", NULL},
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(image_crop),
      "Additional Cropping",
      "",
      {
         { "disabled", NULL},
         { "1 px", NULL},
         { "2 px", NULL},
         { "3 px", NULL},
         { "4 px", NULL},
         { "5 px", NULL},
         { "6 px", NULL},
         { "7 px", NULL},
         { "8 px", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(image_offset),
      "Offset Cropped Image",
      "",
      {
         { "disabled", NULL},
         { "-4 px", NULL},
         { "-3 px", NULL},
         { "-2 px", NULL},
         { "-1 px", NULL},
         { "1 px", NULL},
         { "2 px", NULL},
         { "3 px", NULL},
         { "4 px", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(analog_calibration),
      "Analog self-calibration",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(analog_toggle),
      "DualShock Analog button toggle",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(enable_multitap_port1),
      "Port 1: Multitap enable",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(enable_multitap_port2),
      "Port 2: Multitap enable",
      "",
      {
         { "disabled", NULL},
         { "enabled", NULL},
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(gun_cursor),
      "Gun Cursor",
      "",
      {
         { "Cross", NULL},
         { "Dot", NULL},
         { "Off", NULL},
         { NULL, NULL },
      },
      "Cross"
   },
   {
      BEETLE_OPT(gun_input_mode),
      "Gun Input Mode",
      "",
      {
         { "Lightgun", NULL},
         { "Touchscreen", NULL},
         { NULL, NULL },
      },
      "Lightgun"
   },
   {
      BEETLE_OPT(mouse_sensitivity),
      "Mouse Sensitivity",
      "",
      {
         { "5%", NULL},
         { "10%", NULL},
         { "15%", NULL},
         { "20%", NULL},
         { "25%", NULL},
         { "30%", NULL},
         { "35%", NULL},
         { "40%", NULL},
         { "45%", NULL},
         { "50%", NULL},
         { "55%", NULL},
         { "60%", NULL},
         { "65%", NULL},
         { "70%", NULL},
         { "75%", NULL},
         { "80%", NULL},
         { "85%", NULL},
         { "90%", NULL},
         { "95%", NULL},
         { "100%", NULL},
         { "105%", NULL},
         { "110%", NULL},
         { "115%", NULL},
         { "120%", NULL},
         { "125%", NULL},
         { "130%", NULL},
         { "135%", NULL},
         { "140%", NULL},
         { "145%", NULL},
         { "150%", NULL},
         { "155%", NULL},
         { "160%", NULL},
         { "165%", NULL},
         { "170%", NULL},
         { "175%", NULL},
         { "180%", NULL},
         { "185%", NULL},
         { "190%", NULL},
         { "195%", NULL},
         { "200%", NULL},
         { NULL, NULL },
      },
      "100%"
   },
   {
      BEETLE_OPT(negcon_deadzone),
      "NegCon Twist Deadzone (percent)",
      "",
      {
         { "0", NULL},
         { "5", NULL},
         { "10", NULL},
         { "15", NULL},
         { "20", NULL},
         { "25", NULL},
         { "30", NULL},
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(negcon_response),
      "NegCon Twist Response",
      "",
      {
         { "linear",    "Linear"},
         { "quadratic", "Quadratic"},
         { "cubic",     "Cubic"},
         { NULL, NULL },
      },
      "linear"
   },
#ifndef EMSCRIPTEN
   {
      BEETLE_OPT(cd_access_method),
      "CD Access Method (Restart)",
      "",
      {
         { "sync",       NULL },
         { "async",       NULL },
         { "precache",       NULL },
         { NULL, NULL },
      },
      "sync"
   },
#endif
   {
      BEETLE_OPT(use_mednafen_memcard0_method),
      "Memory Card 0 Method",
      "",
      {
         { "libretro",       NULL },
         { "mednafen",       NULL },
         { NULL, NULL },
      },
      "libretro"
   },
   {
      BEETLE_OPT(enable_memcard1),
      "Enable memory card 1",
      "",
      {
         { "enabled",       NULL },
         { "disabled",       NULL },
         { NULL, NULL },
      },
      "enabled"
   },
   {
      BEETLE_OPT(shared_memory_cards),
      "Shared memcards (restart)",
      "",
      {
         { "disabled",       NULL },
         { "enabled",       NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      BEETLE_OPT(cd_fastload),
      "Increase CD loading speed",
      "",
      {
         { "2x (native)",       NULL },
         { "4x",       NULL },
         { "6x",       NULL },
         { "8x",       NULL },
         { "10x",       NULL },
         { "12x",       NULL },
         { "14x",       NULL },
         { NULL, NULL },
      },
      "2x (native)"
   },
   {
      BEETLE_OPT(initial_scanline),
      "Initial scanline",
      "Adjust the first displayed scanline in NTSC mode.",
      {
         { "0",       NULL },
         { "1",       NULL },
         { "2",       NULL },
         { "3",       NULL },
         { "4",       NULL },
         { "5",       NULL },
         { "6",       NULL },
         { "7",       NULL },
         { "8",       NULL },
         { "9",       NULL },
         { "10",       NULL },
         { "11",       NULL },
         { "12",       NULL },
         { "13",       NULL },
         { "14",       NULL },
         { "15",       NULL },
         { "16",       NULL },
         { "17",       NULL },
         { "18",       NULL },
         { "19",       NULL },
         { "20",       NULL },
         { "21",       NULL },
         { "22",       NULL },
         { "23",       NULL },
         { "24",       NULL },
         { "25",       NULL },
         { "26",       NULL },
         { "27",       NULL },
         { "28",       NULL },
         { "29",       NULL },
         { "30",       NULL },
         { "31",       NULL },
         { "32",       NULL },
         { "33",       NULL },
         { "34",       NULL },
         { "35",       NULL },
         { "36",       NULL },
         { "37",       NULL },
         { "38",       NULL },
         { "39",       NULL },
         { "40",       NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(last_scanline),
      "Last scanline",
      "Adjust the last displayed scanline in NTSC mode.",
      {
         { "210",       NULL },
         { "211",       NULL },
         { "212",       NULL },
         { "213",       NULL },
         { "214",       NULL },
         { "215",       NULL },
         { "216",       NULL },
         { "217",       NULL },
         { "218",       NULL },
         { "219",       NULL },
         { "220",       NULL },
         { "221",       NULL },
         { "222",       NULL },
         { "223",       NULL },
         { "224",       NULL },
         { "225",       NULL },
         { "226",       NULL },
         { "227",       NULL },
         { "228",       NULL },
         { "229",       NULL },
         { "230",       NULL },
         { "231",       NULL },
         { "232",       NULL },
         { "233",       NULL },
         { "234",       NULL },
         { "235",       NULL },
         { "236",       NULL },
         { "237",       NULL },
         { "238",       NULL },
         { "239",       NULL },
         { NULL, NULL },
      },
      "239"
   },
   {
      BEETLE_OPT(initial_scanline_pal),
      "Initial scanline (PAL)",
      "Adjust the first displayed scanline in PAL mode.",
      {
         { "0",       NULL },
         { "1",       NULL },
         { "2",       NULL },
         { "3",       NULL },
         { "4",       NULL },
         { "5",       NULL },
         { "6",       NULL },
         { "7",       NULL },
         { "8",       NULL },
         { "9",       NULL },
         { "10",       NULL },
         { "11",       NULL },
         { "12",       NULL },
         { "13",       NULL },
         { "14",       NULL },
         { "15",       NULL },
         { "16",       NULL },
         { "17",       NULL },
         { "18",       NULL },
         { "19",       NULL },
         { "20",       NULL },
         { "21",       NULL },
         { "22",       NULL },
         { "23",       NULL },
         { "24",       NULL },
         { "25",       NULL },
         { "26",       NULL },
         { "27",       NULL },
         { "28",       NULL },
         { "29",       NULL },
         { "30",       NULL },
         { "31",       NULL },
         { "32",       NULL },
         { "33",       NULL },
         { "34",       NULL },
         { "35",       NULL },
         { "36",       NULL },
         { "37",       NULL },
         { "38",       NULL },
         { "39",       NULL },
         { "40",       NULL },
         { NULL, NULL },
      },
      "0"
   },
   {
      BEETLE_OPT(last_scanline_pal),
      "Last scanline (PAL)",
      "Adjust the last displayed scanline in PAL mode.",
      {
         { "230",       NULL },
         { "231",       NULL },
         { "232",       NULL },
         { "233",       NULL },
         { "234",       NULL },
         { "235",       NULL },
         { "236",       NULL },
         { "237",       NULL },
         { "238",       NULL },
         { "239",       NULL },
         { "240",       NULL },
         { "241",       NULL },
         { "242",       NULL },
         { "243",       NULL },
         { "244",       NULL },
         { "245",       NULL },
         { "246",       NULL },
         { "247",       NULL },
         { "248",       NULL },
         { "249",       NULL },
         { "250",       NULL },
         { "251",       NULL },
         { "252",       NULL },
         { "253",       NULL },
         { "254",       NULL },
         { "255",       NULL },
         { "256",       NULL },
         { "257",       NULL },
         { "258",       NULL },
         { "259",       NULL },
         { "260",       NULL },
         { "261",       NULL },
         { "262",       NULL },
         { "263",       NULL },
         { "264",       NULL },
         { "265",       NULL },
         { "266",       NULL },
         { "267",       NULL },
         { "268",       NULL },
         { "269",       NULL },
         { "270",       NULL },
         { "271",       NULL },
         { "272",       NULL },
         { "273",       NULL },
         { "274",       NULL },
         { "275",       NULL },
         { "276",       NULL },
         { "277",       NULL },
         { "278",       NULL },
         { "279",       NULL },
         { "280",       NULL },
         { "281",       NULL },
         { "282",       NULL },
         { "283",       NULL },
         { "284",       NULL },
         { "285",       NULL },
         { "286",       NULL },
         { "287",       NULL },
         { NULL, NULL },
      },
      "287"
   },
   { NULL, NULL, NULL, {{0}}, NULL },
};

/* RETRO_LANGUAGE_JAPANESE */

/* RETRO_LANGUAGE_FRENCH */

/* RETRO_LANGUAGE_SPANISH */

/* RETRO_LANGUAGE_GERMAN */

/* RETRO_LANGUAGE_ITALIAN */

/* RETRO_LANGUAGE_DUTCH */

/* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */

/* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */

/* RETRO_LANGUAGE_RUSSIAN */

/* RETRO_LANGUAGE_KOREAN */

/* RETRO_LANGUAGE_CHINESE_TRADITIONAL */

/* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */

/* RETRO_LANGUAGE_ESPERANTO */

/* RETRO_LANGUAGE_POLISH */

/* RETRO_LANGUAGE_VIETNAMESE */

/* RETRO_LANGUAGE_ARABIC */

/* RETRO_LANGUAGE_GREEK */

/* RETRO_LANGUAGE_TURKISH */

/*
 ********************************
 * Language Mapping
 ********************************
*/

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

   if (environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version) && (version == 1))
   {
      struct retro_core_options_intl core_options_intl;
      unsigned language = 0;

      core_options_intl.us    = option_defs_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = option_defs_intl[language];

      environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_intl);
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
            if (num_values > 1)
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
