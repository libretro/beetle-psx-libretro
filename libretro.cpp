#include "mednafen/mednafen.h"
#include "mednafen/mempatcher.h"
#include "mednafen/git.h"
#include "mednafen/general.h"
#include "mednafen/md5.h"
#ifdef NEED_DEINTERLACER
#include	"mednafen/video/Deinterlacer.h"
#endif
#include "libretro.h"

static MDFNGI *game;

//forward decls
extern void Emulate(EmulateSpecStruct *espec);
extern void SetInput(int port, const char *type, void *ptr);

struct retro_perf_callback perf_cb;
retro_get_cpu_features_t perf_get_cpu_features_cb = NULL;
retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static retro_rumble_interface rumble;

static bool overscan;
static double last_sound_rate;
static MDFN_PixelFormat last_pixel_format;

static MDFN_Surface *surf;

static bool failed_init;

static void hookup_ports(bool force);

static bool initial_ports_hookup = false;

char *psx_analog_type;

#define RETRO_DEVICE_PS1PAD       RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0)
#define RETRO_DEVICE_DUALANALOG   RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0)
#define RETRO_DEVICE_DUALSHOCK    RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1)
#define RETRO_DEVICE_FLIGHTSTICK  RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)

std::string retro_base_directory;
std::string retro_base_name;
std::string retro_save_directory;

static void set_basename(const char *path)
{
   const char *base = strrchr(path, '/');
   if (!base)
      base = strrchr(path, '\\');

   if (base)
      retro_base_name = base + 1;
   else
      retro_base_name = path;

   retro_base_name = retro_base_name.substr(0, retro_base_name.find_last_of('.'));
}

#ifdef NEED_DEINTERLACER
static bool PrevInterlaced;
static Deinterlacer deint;
#endif

#define MEDNAFEN_CORE_NAME_MODULE "psx"
#define MEDNAFEN_CORE_NAME "Mednafen PSX"
#define MEDNAFEN_CORE_VERSION "v0.9.33.3"
#define MEDNAFEN_CORE_EXTENSIONS "cue|toc|m3u|ccd"
static double mednafen_psx_fps = 59.82704; // Hardcoded for NTSC atm.
#define MEDNAFEN_CORE_GEOMETRY_BASE_W 320
#define MEDNAFEN_CORE_GEOMETRY_BASE_H 240
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 700
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 480
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 700
static int mednafen_psx_fb_height;
static bool is_pal = false;

#define FB_MAX_HEIGHT 576

// Wastes a little space for NTSC PSX, but better than dynamically allocating.
#ifdef WANT_16BPP
static uint16_t mednafen_buf[FB_WIDTH * FB_MAX_HEIGHT];
#else
static uint32_t mednafen_buf[FB_WIDTH * FB_MAX_HEIGHT];
#endif
const char *mednafen_core_str = MEDNAFEN_CORE_NAME;

static void check_system_specs(void)
{
   // Hints that we need a fairly powerful system to run this.
   unsigned level = 15;
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

// Ugly poking. There's no public interface for this.
#include "mednafen/psx/psx.h"
namespace MDFN_IEN_PSX
{
   extern int CD_SelectedDisc;
   extern std::vector<CDIF*> *cdifs;
}

// Poke into psx.cpp
unsigned CalcDiscSCEx();
using MDFN_IEN_PSX::CD_SelectedDisc;
using MDFN_IEN_PSX::cdifs;

enum
{
 REGION_JP = 0,
 REGION_NA = 1,
 REGION_EU = 2,
};

static unsigned disk_get_num_images(void)
{
   return cdifs ? cdifs->size() : 0;
}

static bool eject_state;
static bool disk_set_eject_state(bool ejected)
{
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "[Mednafen]: Ejected: %u.\n", ejected);
   if (ejected == eject_state)
      return false;

   game->DoSimpleCommand(ejected ? MDFN_MSC_EJECT_DISK : MDFN_MSC_INSERT_DISK);
   eject_state = ejected;
   return true;
}

static bool disk_get_eject_state(void)
{
   return eject_state;
}

static unsigned disk_get_image_index(void)
{
   // PSX global. Hacky.
   return CD_SelectedDisc;
}

static bool disk_set_image_index(unsigned index)
{
   CD_SelectedDisc = index;
   if (CD_SelectedDisc > disk_get_num_images())
      CD_SelectedDisc = disk_get_num_images();

   // Very hacky. CDSelect command will increment first.
   CD_SelectedDisc--;

   game->DoSimpleCommand(MDFN_MSC_SELECT_DISK);
   return true;
}

// Mednafen PSX really doesn't support adding disk images on the fly ...
// Hack around this.
static void update_md5_checksum(CDIF *iface)
{
   uint8 LayoutMD5[16];
   md5_context layout_md5;

   layout_md5.starts();

   CD_TOC toc;

   iface->ReadTOC(&toc);

   layout_md5.update_u32_as_lsb(toc.first_track);
   layout_md5.update_u32_as_lsb(toc.last_track);
   layout_md5.update_u32_as_lsb(toc.tracks[100].lba);

   for (uint32 track = toc.first_track; track <= toc.last_track; track++)
   {
      layout_md5.update_u32_as_lsb(toc.tracks[track].lba);
      layout_md5.update_u32_as_lsb(toc.tracks[track].control & 0x4);
   }

   layout_md5.finish(LayoutMD5);
   memcpy(MDFNGameInfo->MD5, LayoutMD5, 16);
   
   std::string md5 = md5_context::asciistr(MDFNGameInfo->MD5, 0);
   if (log_cb)
      log_cb(RETRO_LOG_INFO, "[Mednafen]: Updated md5 checksum: %s.\n", md5.c_str());
}

// Untested ...
static bool disk_replace_image_index(unsigned index, const struct retro_game_info *info)
{
   if (index >= disk_get_num_images())
      return false;

   if (!eject_state)
      return false;

   if (!info)
   {
      delete cdifs->at(index);
      cdifs->erase(cdifs->begin() + index);
      if (index < CD_SelectedDisc)
         CD_SelectedDisc--;
      
      // Poke into psx.cpp
      CalcDiscSCEx();
      return true;
   }

   try
   {
      CDIF *iface = CDIF_Open(info->path, false, false);
      delete cdifs->at(index);
      cdifs->at(index) = iface;
      CalcDiscSCEx();
      set_basename(info->path); // If we replace, we want the "swap disk manually effect".
      update_md5_checksum(iface); // Ugly, but needed to get proper disk swapping effect.
      return true;
   }
   catch (const std::exception &e)
   {
      return false;
   }
}

static bool disk_add_image_index(void)
{
   cdifs->push_back(NULL);
   return true;
}
///////

static struct retro_disk_control_callback disk_interface = {
   disk_set_eject_state,
   disk_get_eject_state,
   disk_get_image_index,
   disk_set_image_index,
   disk_get_num_images,
   disk_replace_image_index,
   disk_add_image_index,
};

void retro_init(void)
{
   struct retro_log_callback log;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else 
      log_cb = NULL;

   MDFNI_InitializeModule();
   eject_state = false;

   const char *dir = NULL;

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
      if (log_cb)
         log_cb(RETRO_LOG_WARN, "System directory is not defined. Fallback on using same dir as ROM for system directory later ...\n");
      failed_init = true;
   }
   
   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
   {
	  // If save directory is defined use it, otherwise use system directory
      retro_save_directory = *dir ? dir : retro_base_directory;
      // Make sure that we don't have any lingering slashes, etc, as they break Windows.
      size_t last = retro_save_directory.find_last_not_of("/\\");
      if (last != std::string::npos)
         last++;

      retro_save_directory = retro_save_directory.substr(0, last);      
   }
   else
   {
      /* TODO: Add proper fallback */
      if (log_cb)
         log_cb(RETRO_LOG_WARN, "Save directory is not defined. Fallback on using SYSTEM directory ...\n");
	  retro_save_directory = retro_base_directory;
   }      

#if defined(WANT_16BPP) && defined(FRONTEND_SUPPORTS_RGB565)
   enum retro_pixel_format rgb565 = RETRO_PIXEL_FORMAT_RGB565;
   if (environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &rgb565) && log_cb)
      log_cb(RETRO_LOG_INFO, "Frontend supports RGB565 - will use that instead of XRGB1555.\n");
#endif

   environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface);

   if (environ_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &perf_cb))
      perf_get_cpu_features_cb = perf_cb.get_cpu_features;
   else
      perf_get_cpu_features_cb = NULL;

   check_system_specs();
}

void retro_reset(void)
{
   game->DoSimpleCommand(MDFN_MSC_RESET);
}

bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t)
{
   return false;
}

static void set_volume (uint32_t *ptr, unsigned number)
{
   switch(number)
   {
      default:
         *ptr = number;
         break;
   }
}

static void check_variables(void)
{
   struct retro_variable var = {0};

   extern void PSXDitherApply(bool);

   var.key = "psx_dithering";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      static bool old_apply_dither = false;
      bool apply_dither = true;
      if (strcmp(var.value, "enabled") == 0)
         apply_dither = true;
      else if (strcmp(var.value, "disabled") == 0)
         apply_dither = false;
      if (apply_dither != old_apply_dither)
      {
         PSXDitherApply(apply_dither);
         old_apply_dither = apply_dither;
      }
   }
   
   var.key = "psx_enable_analog_toggle";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "enabled") == 0)
         setting_psx_analog_toggle = 1;
      else if (strcmp(var.value, "disabled") == 0)
         setting_psx_analog_toggle = 0;
   }  
   
   var.key = "psx_enable_multitap_port1";
   
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "enabled") == 0)
         setting_psx_multitap_port_1 = true;
      else if (strcmp(var.value, "disabled") == 0)
         setting_psx_multitap_port_1 = false;
   }   

   var.key = "psx_enable_multitap_port2";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "enabled") == 0)
         setting_psx_multitap_port_2 = true;
      else if (strcmp(var.value, "disabled") == 0)
         setting_psx_multitap_port_2 = false;
   }
}

#define MAX_PLAYERS 2
#define MAX_BUTTONS 16

union
{
   uint32_t u32[MAX_PLAYERS][1 + 8 + 1]; // Buttons + Axes + Rumble
   uint8_t u8[MAX_PLAYERS][(1 + 8 + 1) * sizeof(uint32_t)];
} static buf;

static uint16_t input_buf[MAX_PLAYERS] = {0};


static void hookup_ports(bool force)
{
   if (initial_ports_hookup && !force)
      return;

   SetInput(0, "gamepad", &input_buf[0]);

   initial_ports_hookup = true;
}

bool retro_load_game(const struct retro_game_info *info)
{
   if (failed_init)
      return false;

#ifdef WANT_32BPP
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "Pixel format XRGB8888 not supported by platform, cannot use %s.\n", MEDNAFEN_CORE_NAME);
      return false;
   }
#endif

   overscan = false;
   environ_cb(RETRO_ENVIRONMENT_GET_OVERSCAN, &overscan);

   set_basename(info->path);

   check_variables();
   if (environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble) && log_cb)
      log_cb(RETRO_LOG_INFO, "Rumble interface supported!\n");

   game = MDFNI_LoadGame(MEDNAFEN_CORE_NAME_MODULE, info->path);
   if (!game)
      return false;

   MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 16, 8, 0, 24);
   memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));
   
   is_pal = (CalcDiscSCEx() == REGION_EU);
   mednafen_psx_fb_height = is_pal ? 576  : 480;
   mednafen_psx_fps       = is_pal ? 50.0 : 59.82704;
   surf = new MDFN_Surface(mednafen_buf, FB_WIDTH, mednafen_psx_fb_height, FB_WIDTH, pix_fmt);

#ifdef NEED_DEINTERLACER
	PrevInterlaced = false;
	deint.ClearState();
#endif

   hookup_ports(true);

   return game;
}

void retro_unload_game()
{
   if (!game)
      return;

   MDFNI_CloseGame();
}



// Hardcoded for PSX. No reason to parse lots of structures ...
// See mednafen/psx/input/gamepad.cpp
static void update_input(void)
{
   input_buf[0] = 0;
   input_buf[1] = 0;
   	
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

   for (unsigned j = 0; j < MAX_PLAYERS; j++)
   {
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
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

   //fprintf(stderr, "Rumble strong: %u, weak: %u.\n", buf.u8[0][9 * 4 + 1], buf.u8[0][9 * 4]);
   if (rumble.set_rumble_state)
   {
      // Appears to be correct.
      rumble.set_rumble_state(0, RETRO_RUMBLE_WEAK, buf.u8[0][9 * 4] * 0x101);
      rumble.set_rumble_state(0, RETRO_RUMBLE_STRONG, buf.u8[0][9 * 4 + 1] * 0x101);
      rumble.set_rumble_state(1, RETRO_RUMBLE_WEAK, buf.u8[1][9 * 4] * 0x101);
      rumble.set_rumble_state(1, RETRO_RUMBLE_STRONG, buf.u8[1][9 * 4 + 1] * 0x101);
   }
}

static uint64_t video_frames, audio_frames;


void retro_run(void)
{
   MDFNGI *curgame = game;

   input_poll_cb();

   update_input();

   static int16_t sound_buf[0x10000];
   static MDFN_Rect rects[FB_MAX_HEIGHT];
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

   if (memcmp(&last_pixel_format, &spec.surface->format, sizeof(MDFN_PixelFormat)))
   {
      spec.VideoFormatChanged = TRUE;

      last_pixel_format = spec.surface->format;
   }

   if (spec.SoundRate != last_sound_rate)
   {
      spec.SoundFormatChanged = true;
      last_sound_rate = spec.SoundRate;
   }

   Emulate(&spec);

#ifdef NEED_DEINTERLACER
   if (spec.InterlaceOn)
   {
      if (!PrevInterlaced)
         deint.ClearState();

      deint.Process(spec.surface, spec.DisplayRect, spec.LineWidths, spec.InterlaceField);

      PrevInterlaced = true;

      spec.InterlaceOn = false;
      spec.InterlaceField = 0;
   }
   else
      PrevInterlaced = false;
#endif

   int16 *const SoundBuf = spec.SoundBuf + spec.SoundBufSizeALMS * curgame->soundchan;
   int32 SoundBufSize = spec.SoundBufSize - spec.SoundBufSizeALMS;
   const int32 SoundBufMaxSize = spec.SoundBufMaxSize - spec.SoundBufSizeALMS;

   spec.SoundBufSize = spec.SoundBufSizeALMS + SoundBufSize;

   // PSX is rather special, and needs specific handling ...
   
   unsigned width = rects[0].w; // spec.DisplayRect.w is 0. Only rects[0].w seems to return something sane.
   unsigned height = spec.DisplayRect.h;
   //fprintf(stderr, "(%u x %u)\n", width, height);
   // PSX core inserts padding on left and right (overscan). Optionally crop this.

   const uint32_t *pix = surf->pixels;
   if (!overscan)
   {
      // 320 width -> 350 width.
      // 364 width -> 400 width.
      // 256 width -> 280 width.
      // 560 width -> 512 width.
      // 640 width -> 700 width.
      // Rectify this.
      switch (width)
      {
         // The shifts are not simply (padded_width - real_width) / 2.
         case 350:
            pix += 14;
            width = 320;
            break;

         case 700:
            pix += 33;
            width = 640;
            break;

         case 400:
            pix += 15;
            width = 364;
            break;

         case 280:
            pix += 10;
            width = 256;
            break;

         case 560:
            pix += 26;
            width = 512;
            break;

         default:
            // This shouldn't happen.
            break;
      }
      
      if (is_pal)
      {
         // Attempt to remove black bars.
         // These numbers are arbitrary since the bars differ some by game.
         // Changes aspect ratio in the process.
         height -= 36;
         pix += 5*(FB_WIDTH << 2);
      }
   }
   video_cb(pix, width, height, FB_WIDTH << 2);

   video_frames++;
   audio_frames += spec.SoundBufSize;

   audio_batch_cb(spec.SoundBuf, spec.SoundBufSize);

   bool updated = false;
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();
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
   info->timing.fps            = mednafen_psx_fps; // Determined for NTSC from empirical testing.
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

   if (log_cb)
   {
      log_cb(RETRO_LOG_INFO, "[%s]: Samples / Frame: %.5f\n",
            mednafen_core_str, (double)audio_frames / video_frames);
      log_cb(RETRO_LOG_INFO, "[%s]: Estimated FPS: %.5f\n",
            mednafen_core_str, (double)video_frames * 44100 / audio_frames);
   }
}

unsigned retro_get_region(void)
{
   return is_pal ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned in_port, unsigned device)
{
   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
      case RETRO_DEVICE_PS1PAD:
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "[%s]: Selected controller type standard gamepad.\n", mednafen_core_str);
         SetInput(in_port, "gamepad", &buf.u8[in_port]);    
         break;
      case RETRO_DEVICE_DUALANALOG:
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "[%s]: Selected controller type Dual Analog.\n", mednafen_core_str);
         SetInput(in_port, "dualanalog", &buf.u8[in_port]);    
         break;
      case RETRO_DEVICE_DUALSHOCK:
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "[%s]: Selected controller type DualShock.\n", mednafen_core_str);
         SetInput(in_port, "dualshock", &buf.u8[in_port]);    
         break;
      case RETRO_DEVICE_FLIGHTSTICK:
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "[%s]: Selected controller type FlightStick.\n", mednafen_core_str);
         SetInput(in_port, "flightstick", &buf.u8[in_port]);    
         break;
      default:
         if (log_cb)
            log_cb(RETRO_LOG_WARN, "[%s]: Unsupported controller device %u, falling back to gamepad.\n", mednafen_core_str,device);
   }

   if (rumble.set_rumble_state)
   {
      rumble.set_rumble_state(in_port, RETRO_RUMBLE_STRONG, 0);
      rumble.set_rumble_state(in_port, RETRO_RUMBLE_WEAK, 0);
      buf.u32[in_port][9] = 0;
   }
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

   static const struct retro_variable vars[] = {
      { "psx_dithering", "Dithering; enabled|disabled" },
      { "psx_enable_analog_toggle", "Dualshock analog button; disabled|enabled" },
      { "psx_enable_multitap_port1", "Port 1: Multitap enable; disabled|enabled" },
      { "psx_enable_multitap_port2", "Port 2: Multitap enable; disabled|enabled" },
	  

      { NULL, NULL },
   };
   static const struct retro_controller_description pads[] = {
      { "PS1 Joypad", RETRO_DEVICE_JOYPAD },
      { "DualAnalog", RETRO_DEVICE_DUALANALOG },
      { "DualShock", RETRO_DEVICE_DUALSHOCK },
      { "FlightStick", RETRO_DEVICE_FLIGHTSTICK },
   };

   static const struct retro_controller_info ports[] = {
      { pads, 4 },
      { pads, 4 },
      { 0 },
   };

   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
   environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)ports);
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
   MDFNGI *curgame = (MDFNGI*)game;
   //if (serialize_size)
   //   return serialize_size;

   if (!curgame->StateAction)
   {
      if (log_cb)
         log_cb(RETRO_LOG_WARN, "[mednafen]: Module %s doesn't support save states.\n", curgame->shortname);
      return 0;
   }

   StateMem st;
   memset(&st, 0, sizeof(st));

   if (!MDFNSS_SaveSM(&st, 0, 0, NULL, NULL, NULL))
   {
      if (log_cb)
         log_cb(RETRO_LOG_WARN, "[mednafen]: Module %s doesn't support save states.\n", curgame->shortname);
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
