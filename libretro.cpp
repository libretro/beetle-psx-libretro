#include "mednafen/mednafen-types.h"
#include "mednafen/mednafen.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include <iostream>
#include "libretro.h"

static MDFNGI *game;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static MDFN_Surface *surf;

static uint16_t conv_buf[680 * 512] __attribute__((aligned(16)));
static uint32_t mednafen_buf[680 * 512] __attribute__((aligned(16)));

void retro_init()
{
   MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 16, 8, 0, 24);
   surf = new MDFN_Surface(mednafen_buf, 680, 512, 680, pix_fmt);

   std::vector<MDFNGI*> ext;
   MDFNI_InitializeModules(ext);

   std::vector<MDFNSetting> settings;
   std::string home = getenv("HOME");
   home += "/.mednafen";
   MDFNI_Initialize(home.c_str(), settings);
}

void retro_deinit()
{
   //MDFNI_Kill();
   delete surf;
   surf = NULL;
}

void retro_reset()
{
   MDFNI_Reset();
}

bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t)
{
   return false;
}

bool retro_load_game(const struct retro_game_info *info)
{
   game = MDFNI_LoadGame("psx", info->path);
   return game;
}

void retro_unload_game()
{
   MDFNI_CloseGame();
}

static inline uint16_t conv_pixel(uint32_t pix)
{
   uint16_t r = (pix >> 19) & 0x1f;
   uint16_t g = (pix >> 11) & 0x1f;
   uint16_t b = (pix >>  3) & 0x1f;
   return (r << 10) | (g << 5) | (b << 0);
}

void retro_run()
{
   input_poll_cb();

   static int16_t sound_buf[0x10000];
   static MDFN_Rect rects[512];

   EmulateSpecStruct spec = {0}; 
   spec.surface = surf;
   spec.SoundRate = 44100;
   spec.SoundBuf = sound_buf;
   spec.LineWidths = rects;
   spec.SoundBufMaxSize = sizeof(sound_buf) / 2;
   spec.SoundVolume = 1.0;
   spec.soundmultiplier = 1.0;

   MDFNI_Emulate(&spec);

   unsigned width = rects[0].w;
   unsigned height = spec.DisplayRect.h;

   for (unsigned i = 0; i < 680 * 512; i++)
      conv_buf[i] = conv_pixel(surf->pixels[i]);

   video_cb(conv_buf, width, height, 680 << 1);

   audio_batch_cb(spec.SoundBuf, spec.SoundBufSize);
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "Mednafen PSX";
   info->library_version  = "0.9.22";
   info->need_fullpath    = true;
   info->valid_extensions = "iso|ISO";
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = 59.97;
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = game->nominal_width;
   info->geometry.base_height  = game->nominal_height;
   info->geometry.max_width    = 680;
   info->geometry.max_height   = 480;
   info->geometry.aspect_ratio = 4.0 / 3.0;
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
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

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *, size_t)
{
   return false;
}

bool retro_unserialize(const void *, size_t)
{
   return false;
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

