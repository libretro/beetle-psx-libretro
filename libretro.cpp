#include "mednafen/mednafen.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#ifdef NEED_DEINTERLACER
#include	"mednafen/video/Deinterlacer.h"
#endif
#include <iostream>
#include "libretro.h"

static MDFNGI *game;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static double last_sound_rate;
static MDFN_PixelFormat last_pixel_format;

static MDFN_Surface *surf;

static bool failed_init;

std::string retro_base_directory;
std::string retro_base_name;

#ifdef NEED_DEINTERLACER
static bool PrevInterlaced;
static Deinterlacer deint;
#endif

#if defined(WANT_PSX_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "psx"
#define MEDNAFEN_CORE_NAME "Mednafen PSX"
#define MEDNAFEN_CORE_VERSION "v0.9.26"
#define MEDNAFEN_CORE_EXTENSIONS "cue|CUE|toc|TOC"
#define MEDNAFEN_CORE_TIMING_FPS 59.85398
#define MEDNAFEN_CORE_GEOMETRY_BASE_W 320
#define MEDNAFEN_CORE_GEOMETRY_BASE_H 240
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 640
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 480
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 680
#define FB_HEIGHT 576

#elif defined(WANT_PCE_FAST_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "pce_fast"
#define MEDNAFEN_CORE_NAME "Mednafen PCE Fast"
#define MEDNAFEN_CORE_VERSION "v0.9.26"
#define MEDNAFEN_CORE_EXTENSIONS "pce|PCE|cue|CUE|zip|ZIP"
#define MEDNAFEN_CORE_TIMING_FPS 59.82
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 512
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 242
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 512
#define FB_HEIGHT 242

#elif defined(WANT_WSWAN_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "wswan"
#define MEDNAFEN_CORE_NAME "Mednafen WonderSwan"
#define MEDNAFEN_CORE_VERSION "v0.9.22"
#define MEDNAFEN_CORE_EXTENSIONS "ws|WS|wsc|WSC|zip|ZIP"
#define MEDNAFEN_CORE_TIMING_FPS 75.47
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 224
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 144
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 224
#define FB_HEIGHT 144

#elif defined(WANT_NGP_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "ngp"
#define MEDNAFEN_CORE_NAME "Mednafen Neopop"
#define MEDNAFEN_CORE_VERSION "v0.9.26"
#define MEDNAFEN_CORE_EXTENSIONS "ngp|NGP|ngc|NGC|zip|ZIP"
#define MEDNAFEN_CORE_TIMING_FPS 60.25
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 160
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 152
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 160
#define FB_HEIGHT 152

#elif defined(WANT_GBA_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "gba"
#define MEDNAFEN_CORE_NAME "Mednafen VBA-M"
#define MEDNAFEN_CORE_VERSION "v0.9.26"
#define MEDNAFEN_CORE_EXTENSIONS "gba|GBA|zip|ZIP"
#define MEDNAFEN_CORE_TIMING_FPS 59.73
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 240
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 160
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 240
#define FB_HEIGHT 160

#elif defined(WANT_SNES_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "snes"
#define MEDNAFEN_CORE_NAME "Mednafen bSNES"
#define MEDNAFEN_CORE_VERSION "v0.9.26"
#define MEDNAFEN_CORE_EXTENSIONS "sfc|SFC|zip|ZIP"
#define MEDNAFEN_CORE_TIMING_FPS 60.10
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 512
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 512
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 512
#define FB_HEIGHT 512

#endif

#ifdef WANT_16BPP
static uint16_t mednafen_buf[FB_WIDTH * FB_HEIGHT];
#else
static uint32_t mednafen_buf[FB_WIDTH * FB_HEIGHT];
#endif
const char *mednafen_core_str = MEDNAFEN_CORE_NAME;

static void check_system_specs(void)
{
   unsigned level = 0;
#if defined(WANT_PSX_EMU)
   // Hints that we need a fairly powerful system to run this.
   level = 3;
#endif
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_init()
{
   std::vector<MDFNGI*> ext;
   MDFNI_InitializeModules(ext);

   const char *dir = NULL;

   std::vector<MDFNSetting> settings;

   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
   {
      retro_base_directory = dir;
      // Make sure that we don't have any lingering slashes, etc, as they break Windows.
      size_t last = retro_base_directory.find_last_not_of("/\\");
      if (last != std::string::npos)
         last++;

      retro_base_directory = retro_base_directory.substr(0, last);

      MDFNI_Initialize(retro_base_directory.c_str());
   }
   else
   {
	/* TODO: Add proper fallback */
      fprintf(stderr, "System directory is not defined. Fallback on using same dir as ROM for system directory later ...\n");
      failed_init = true;
   }

#if defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
   enum retro_pixel_format rgb565 = RETRO_PIXEL_FORMAT_RGB565;
   if(environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &rgb565))
      fprintf(stderr, "Frontend supports RGB565 - will use that instead of XRGB1555.\n");
#endif

   check_system_specs();
}

void retro_reset()
{
   game->DoSimpleCommand(MDFN_MSC_RESET);
}

bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t)
{
   return false;
}

bool retro_load_game(const struct retro_game_info *info)
{
   if (failed_init)
      return false;

#ifdef WANT_32BPP
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      fprintf(stderr, "Pixel format XRGB8888 not supported by platform, cannot use %s.\n", MEDNAFEN_CORE_NAME);
      return false;
   }
#endif

   const char *base = strrchr(info->path, '/');
   if (!base)
      base = strrchr(info->path, '\\');

   if (base)
      retro_base_name = base + 1;
   else
      retro_base_name = info->path;

   retro_base_name = retro_base_name.substr(0, retro_base_name.find_last_of('.'));
 
   game = MDFNI_LoadGame(MEDNAFEN_CORE_NAME_MODULE, info->path);
   if (!game)
      return false;

   MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 16, 8, 0, 24);
   memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));

   surf = new MDFN_Surface(mednafen_buf, FB_WIDTH, FB_HEIGHT, FB_WIDTH, pix_fmt);

#ifdef NEED_DEINTERLACER
	PrevInterlaced = false;
	deint.ClearState();
#endif

   return game;
}

void retro_unload_game()
{
   MDFNI_CloseGame();
}

static unsigned retro_devices[2];

// Hardcoded for PSX. No reason to parse lots of structures ...
// See mednafen/psx/input/gamepad.cpp
static void update_input(void)
{
#if defined(WANT_PSX_EMU)
   union
   {
      uint32_t u32[2][1 + 8];
      uint8_t u8[2][2 * sizeof(uint16_t) + 8 * sizeof(uint32_t)];
   } static buf;

   uint16_t input_buf[2] = {0};
   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_L3,
      RETRO_DEVICE_ID_JOYPAD_R3,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_L2,
      RETRO_DEVICE_ID_JOYPAD_R2,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_X,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_Y,
   };

   for (unsigned j = 0; j < 2; j++)
   {
      for (unsigned i = 0; i < 16; i++)
         input_buf[j] |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;
   }

   // Buttons.
   buf.u8[0][0] = (input_buf[0] >> 0) & 0xff;
   buf.u8[0][1] = (input_buf[0] >> 8) & 0xff;
   buf.u8[1][0] = (input_buf[1] >> 0) & 0xff;
   buf.u8[1][1] = (input_buf[1] >> 8) & 0xff;

   // Analogs
   for (unsigned j = 0; j < 2; j++)
   {
      int analog_left_x = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
            RETRO_DEVICE_ID_ANALOG_X);

      int analog_left_y = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT,
            RETRO_DEVICE_ID_ANALOG_Y);

      int analog_right_x = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
            RETRO_DEVICE_ID_ANALOG_X);

      int analog_right_y = input_state_cb(j, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT,
            RETRO_DEVICE_ID_ANALOG_Y);

      uint32_t r_right = analog_right_x > 0 ?  analog_right_x : 0;
      uint32_t r_left  = analog_right_x < 0 ? -analog_right_x : 0;
      uint32_t r_down  = analog_right_y > 0 ?  analog_right_y : 0;
      uint32_t r_up    = analog_right_y < 0 ? -analog_right_y : 0;

      uint32_t l_right = analog_left_x > 0 ?  analog_left_x : 0;
      uint32_t l_left  = analog_left_x < 0 ? -analog_left_x : 0;
      uint32_t l_down  = analog_left_y > 0 ?  analog_left_y : 0;
      uint32_t l_up    = analog_left_y < 0 ? -analog_left_y : 0;

      buf.u32[j][1] = r_right;
      buf.u32[j][2] = r_left;
      buf.u32[j][3] = r_down;
      buf.u32[j][4] = r_up;

      buf.u32[j][5] = l_right;
      buf.u32[j][6] = l_left;
      buf.u32[j][7] = l_down;
      buf.u32[j][8] = l_up;
   }

   for (int j = 0; j < 2; j++)
   {
      switch (retro_devices[j])
      {
         case RETRO_DEVICE_ANALOG:
            game->SetInput(j, "dualanalog", &buf.u8[j]);
            break;
         default:
            game->SetInput(j, "gamepad", &buf.u8[j]);
            break;
      }
   }
#elif defined(WANT_PCE_FAST_EMU)
   static uint8_t input_buf[5][2];

   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_Y,
      RETRO_DEVICE_ID_JOYPAD_X,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_L2
   };

   if (input_state_cb)
   {
      for (unsigned j = 0; j < 5; j++)
      {
         uint16_t input_state = 0;
         for (unsigned i = 0; i < 13; i++)
            input_state |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

         // Input data must be little endian.
         input_buf[j][0] = (input_state >> 0) & 0xff;
         input_buf[j][1] = (input_state >> 8) & 0xff;
      }
   }

   // Possible endian bug ...
   for (unsigned i = 0; i < 5; i++)
      game->SetInput(i, "gamepad", &input_buf[i][0]);
#elif defined(WANT_WSWAN_EMU)
   static uint16_t input_buf;
   input_buf = 0;

   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_UP, //X Cursor horizontal-layout games
      RETRO_DEVICE_ID_JOYPAD_RIGHT, //X Cursor horizontal-layout games
      RETRO_DEVICE_ID_JOYPAD_DOWN, //X Cursor horizontal-layout games
      RETRO_DEVICE_ID_JOYPAD_LEFT, //X Cursor horizontal-layout games
      RETRO_DEVICE_ID_JOYPAD_R2, //Y Cursor UP vertical-layout games
      RETRO_DEVICE_ID_JOYPAD_R, //Y Cursor RIGHT vertical-layout games
      RETRO_DEVICE_ID_JOYPAD_L2, //Y Cursor DOWN vertical-layout games
      RETRO_DEVICE_ID_JOYPAD_L, //Y Cursor LEFT vertical-layout games
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
   };

   for (unsigned i = 0; i < 11; i++)
      input_buf |= map[i] != -1u &&
         input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

#ifdef MSB_FIRST
   union {
      uint8_t b[2];
      uint16_t s;
   } u;
   u.s = input_buf;
   input_buf = u.b[0] | u.b[1] << 8;
#endif

   game->SetInput(0, "gamepad", &input_buf);
#elif defined(WANT_GBA_EMU)
   static uint16_t input_buf;
   input_buf = 0;
   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_A, //A button
      RETRO_DEVICE_ID_JOYPAD_B, //B button
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_L,
   };

   for (unsigned i = 0; i < 11; i++)
      input_buf |= map[i] != -1u &&
         input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

   // Possible endian bug ...
   game->SetInput(0, "gamepad", &input_buf);
#elif defined(WANT_SNES_EMU)
   static uint8_t input_buf[5][2];

   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_Y,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_X,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
   };

   if (input_state_cb)
   {
      for (unsigned j = 0; j < 5; j++)
      {
         uint16_t input_state = 0;
         for (unsigned i = 0; i < 13; i++)
            input_state |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

         // Input data must be little endian.
         input_buf[j][0] = (input_state >> 0) & 0xff;
         input_buf[j][1] = (input_state >> 8) & 0xff;
      }
   }

   // Possible endian bug ...
   for (unsigned i = 0; i < 5; i++)
      game->SetInput(i, "gamepad", &input_buf[i][0]);
#else
   static uint16_t input_buf[1];
   input_buf[0] = 0;
   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_A, //A button
      RETRO_DEVICE_ID_JOYPAD_B, //B button
      RETRO_DEVICE_ID_JOYPAD_START, //Option button
   };

   for (unsigned j = 0; j < 1; j++)
   {
      for (unsigned i = 0; i < 7; i++)
         input_buf[j] |= map[i] != -1u &&
            input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;
   }

   // Possible endian bug ...
   game->SetInput(0, "gamepad", &input_buf[0]);
#endif
}

static uint64_t video_frames, audio_frames;

void retro_run()
{
   input_poll_cb();

   update_input();

   static int16_t sound_buf[0x10000];
   static MDFN_Rect rects[FB_HEIGHT];
   rects[0].w = ~0;

   EmulateSpecStruct spec = {0};
   spec.surface = surf;
   spec.SoundRate = 44100;
   spec.SoundBuf = sound_buf;
   spec.LineWidths = rects;
   spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
   spec.SoundVolume = 1.0;
   spec.soundmultiplier = 1.0;
   spec.SoundBufSize = 0;
   spec.VideoFormatChanged = false;
   spec.SoundFormatChanged = false;

   if(memcmp(&last_pixel_format, &spec.surface->format, sizeof(MDFN_PixelFormat)))
   {
      spec.VideoFormatChanged = TRUE;

      last_pixel_format = spec.surface->format;
   }

   if(spec.SoundRate != last_sound_rate)
   {
      spec.SoundFormatChanged = true;
      last_sound_rate = spec.SoundRate;
   }

   MDFNGameInfo->Emulate(&spec);

#ifdef NEED_DEINTERLACER
   if(spec.InterlaceOn)
   {
      if(!PrevInterlaced)
         deint.ClearState();

      deint.Process(spec.surface, spec.DisplayRect, spec.LineWidths, spec.InterlaceField);

      PrevInterlaced = true;

      spec.InterlaceOn = false;
      spec.InterlaceField = 0;
   }
   else
      PrevInterlaced = false;
#endif

   int16 *const SoundBuf = spec.SoundBuf + spec.SoundBufSizeALMS * MDFNGameInfo->soundchan;
   int32 SoundBufSize = spec.SoundBufSize - spec.SoundBufSizeALMS;
   const int32 SoundBufMaxSize = spec.SoundBufMaxSize - spec.SoundBufSizeALMS;

   spec.SoundBufSize = spec.SoundBufSizeALMS + SoundBufSize;

#if defined(WANT_PSX_EMU)
   unsigned width = rects[0].w;
   unsigned height = spec.DisplayRect.h;
   unsigned int ptrDiff = 0;
#else
   unsigned width  = spec.DisplayRect.w;
   unsigned height = spec.DisplayRect.h;
#endif

#if defined(WANT_PSX_EMU)
   // This is for PAL, the core implements PAL over NTSC TV so you get the
   // infamous PAL borders. This removes them. The PS1 supports only two horizontal
   // resolutions so it's OK to use constants and not percentage.
   bool isPal = false;
   if (height == FB_HEIGHT)
   {
       ptrDiff += width * 47;
       height = 480;
       isPal = true;
   }
   else if (height == 288)
   {
       // TODO: This seems to be OK as is, but I might be wrong.
       isPal = true;
   }

   if (isPal && width == FB_WIDTH)
       ptrDiff += 7;

   // The core handles vertical overscan for NTSC pretty well, but it ignores
   // horizontal overscan. This is a tough estimation of what the horizontal
   // overscan should be, tested with all major NTSC resolutions. Mayeb make it
   // configurable?
   float hoverscan = 0.941176471;

   width = width * hoverscan;
   ptrDiff += ((rects[0].w - width) / 2);

   const uint32_t *ptr = surf->pixels;
   ptr += ptrDiff;

   video_cb(ptr, width, height, FB_WIDTH << 2);
#elif defined(WANT_32BPP)
   const uint32_t *pix = surf->pixels;
   video_cb(pix, width, height, FB_WIDTH << 2);
#elif defined(WANT_16BPP)
   const uint16_t *pix = surf->pixels16;
   video_cb(pix, width, height, FB_WIDTH << 1);
#endif

   video_frames++;
   audio_frames += spec.SoundBufSize;

   audio_batch_cb(spec.SoundBuf, spec.SoundBufSize);
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = MEDNAFEN_CORE_NAME;
   info->library_version  = MEDNAFEN_CORE_VERSION;
   info->need_fullpath    = true;
   info->valid_extensions = MEDNAFEN_CORE_EXTENSIONS;
   info->block_extract    = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = MEDNAFEN_CORE_TIMING_FPS; // Determined from empirical testing.
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = MEDNAFEN_CORE_GEOMETRY_BASE_W;
   info->geometry.base_height  = MEDNAFEN_CORE_GEOMETRY_BASE_H;
   info->geometry.max_width    = MEDNAFEN_CORE_GEOMETRY_MAX_W;
   info->geometry.max_height   = MEDNAFEN_CORE_GEOMETRY_MAX_H;
   info->geometry.aspect_ratio = MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO;
}

void retro_deinit()
{
   delete surf;
   surf = NULL;

   fprintf(stderr, "[%s]: Samples / Frame: %.5f\n",
         mednafen_core_str, (double)audio_frames / video_frames);

   fprintf(stderr, "[%s]: Estimated FPS: %.5f\n",
         mednafen_core_str, (double)video_frames * 44100 / audio_frames);
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned in_port, unsigned device)
{
#ifdef WANT_PSX_EMU
   if (in_port > 1)
   {
      fprintf(stderr,
            "[%s]: Only the 2 main ports are supported at the moment", mednafen_core_str);
      return;
   }

   switch (device)
   {
      // TODO: Add support for other input types
      case RETRO_DEVICE_JOYPAD:
      case RETRO_DEVICE_ANALOG:
         fprintf(stderr, "[%s]: Selected controller type %u", mednafen_core_str, device);
         retro_devices[in_port] = device;
         break;
      default:
         retro_devices[in_port] = RETRO_DEVICE_JOYPAD;
         fprintf(stderr,
               "[%s]: Unsupported controller device, falling back to gamepad", mednafen_core_str);
   }
#endif
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

static size_t serialize_size;

size_t retro_serialize_size(void)
{
   //if (serialize_size)
   //   return serialize_size;

   if (!game->StateAction)
   {
      fprintf(stderr, "[mednafen]: Module %s doesn't support save states.\n", game->shortname);
      return 0;
   }

   StateMem st;
   memset(&st, 0, sizeof(st));

   if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL))
   {
      fprintf(stderr, "[mednafen]: Module %s doesn't support save states.\n", game->shortname);
      return 0;
   }

   free(st.data);
   return serialize_size = st.len;
}

bool retro_serialize(void *data, size_t size)
{
   StateMem st;
   memset(&st, 0, sizeof(st));
   st.data     = (uint8_t*)data;
   st.malloced = size;

   return MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL);
}

bool retro_unserialize(const void *data, size_t size)
{
   StateMem st;
   memset(&st, 0, sizeof(st));
   st.data = (uint8_t*)data;
   st.len  = size;

   return MDFNSS_LoadSM(&st, 0, 0);
}

void *retro_get_memory_data(unsigned)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned)
{
   return 0;
}

void retro_cheat_reset(void)
{}

void retro_cheat_set(unsigned, bool, const char *)
{}

#ifdef __cplusplus
extern "C" {
#endif

static const unsigned long crc_table[256] = {
  0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
  0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
  0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
  0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
  0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
  0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
  0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
  0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
  0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
  0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
  0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
  0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
  0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
  0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
  0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
  0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
  0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
  0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
  0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
  0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
  0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
  0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
  0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
  0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
  0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
  0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
  0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
  0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
  0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
  0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
  0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
  0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
  0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
  0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
  0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
  0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
  0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
  0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
  0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
  0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
  0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
  0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
  0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
  0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
  0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
  0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
  0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
  0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
  0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
  0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
  0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
  0x2d02ef8dL
};

#define DO1_CRC32(buf) crc = crc_table[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
#define DO2_CRC32(buf) DO1_CRC32(buf); DO1_CRC32(buf);
#define DO4_CRC32(buf) DO2_CRC32(buf); DO2_CRC32(buf);
#define DO8_CRC32(buf) DO4_CRC32(buf); DO4_CRC32(buf);

unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len)
{
	if (buf == 0) return 0L;
	crc = crc ^ 0xffffffffL;
	while (len >= 8)
	{
	DO8_CRC32(buf);
	len -= 8;
	}
	if (len) do {
	DO1_CRC32(buf);
	} while (--len);
	return crc ^ 0xffffffffL;
}

#ifdef __cplusplus
}
#endif
