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

#if defined(WANT_PSX_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "psx"
#define MEDNAFEN_CORE_NAME "Mednafen PSX"
#define MEDNAFEN_CORE_VERSION "v0.9.32"
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

#elif defined(WANT_LYNX_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "lynx"
#define MEDNAFEN_CORE_NAME "Mednafen Lynx"
#define MEDNAFEN_CORE_VERSION "v0.9.32"
#define MEDNAFEN_CORE_EXTENSIONS "lnx"
#define MEDNAFEN_CORE_TIMING_FPS 75
#define MEDNAFEN_CORE_GEOMETRY_BASE_W 160
#define MEDNAFEN_CORE_GEOMETRY_BASE_H 102
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 160
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 102
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 160
#define FB_HEIGHT 102
static bool is_pal = false;

#elif defined(WANT_PCE_FAST_EMU)
#include "mednafen/cdrom/pcecd.h"
#define MEDNAFEN_CORE_NAME_MODULE "pce_fast"
#define MEDNAFEN_CORE_NAME "Mednafen PCE Fast"
#define MEDNAFEN_CORE_VERSION "v0.9.28"
#define MEDNAFEN_CORE_EXTENSIONS "pce|sgx|cue"
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
#define MEDNAFEN_CORE_VERSION "v0.9.28"
#define MEDNAFEN_CORE_EXTENSIONS "ws|wsc"
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
#define MEDNAFEN_CORE_EXTENSIONS "ngp|ngc"
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
#define MEDNAFEN_CORE_EXTENSIONS "gba"
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
#define MEDNAFEN_CORE_EXTENSIONS "smc|fig|bs|st|sfc"
#define MEDNAFEN_CORE_TIMING_FPS 60.10
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 512
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 512
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 512
#define FB_HEIGHT 512

#elif defined(WANT_VB_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "vb"
#define MEDNAFEN_CORE_NAME "Mednafen VB"
#define MEDNAFEN_CORE_VERSION "v0.9.26"
#define MEDNAFEN_CORE_EXTENSIONS "vb|vboy|bin"
#define MEDNAFEN_CORE_TIMING_FPS 50.27
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 384
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 224
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 384
#define FB_HEIGHT 224

#elif defined(WANT_PCFX_EMU)
#define MEDNAFEN_CORE_NAME_MODULE "pcfx"
#define MEDNAFEN_CORE_NAME "Mednafen PCFX"
#define MEDNAFEN_CORE_VERSION "v0.9.26"
#define MEDNAFEN_CORE_EXTENSIONS "cue"
#define MEDNAFEN_CORE_TIMING_FPS 59.94
#define MEDNAFEN_CORE_GEOMETRY_BASE_W (game->nominal_width)
#define MEDNAFEN_CORE_GEOMETRY_BASE_H (game->nominal_height)
#define MEDNAFEN_CORE_GEOMETRY_MAX_W 341
#define MEDNAFEN_CORE_GEOMETRY_MAX_H 480
#define MEDNAFEN_CORE_GEOMETRY_ASPECT_RATIO (4.0 / 3.0)
#define FB_WIDTH 344
#define FB_HEIGHT 480
#endif


#if defined(WANT_PSX_EMU)
#define FB_MAX_HEIGHT 576
#else
#define FB_MAX_HEIGHT FB_HEIGHT
#endif

// Wastes a little space for NTSC PSX, but better than dynamically allocating.
#ifdef WANT_16BPP
static uint16_t mednafen_buf[FB_WIDTH * FB_MAX_HEIGHT];
#else
static uint32_t mednafen_buf[FB_WIDTH * FB_MAX_HEIGHT];
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

#if defined(WANT_PSX_EMU)

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
#endif

void retro_init()
{
   struct retro_log_callback log;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else 
      log_cb = NULL;

   MDFNI_InitializeModule();
#if defined(WANT_PSX_EMU)
   eject_state = false;
#endif

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

#if defined(WANT_PSX_EMU)
   environ_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE, &disk_interface);
#endif

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

#if defined(WANT_PCE_FAST_EMU)
   var.key = "pce_nospritelimit";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "disabled") == 0)
         setting_pce_fast_nospritelimit = 0;
      else if (strcmp(var.value, "enabled") == 0)
         setting_pce_fast_nospritelimit = 1;
   }

   var.key = "pce_nospritelimit";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "disabled") == 0)
         setting_pce_fast_nospritelimit = 0;
      else if (strcmp(var.value, "enabled") == 0)
         setting_pce_fast_nospritelimit = 1;
   }

   var.key = "pce_keepaspect";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "disabled") == 0)
      {
         setting_pce_keepaspect = 0;
         game->fb_width = 512;
         game->nominal_width = 341;
         game->lcm_width = 341;
      }
      else if (strcmp(var.value, "enabled") == 0)
      {
         setting_pce_keepaspect = 1;
         game->fb_width = 682;
         game->nominal_width = 288;
         game->lcm_width = 1024;
      }
   }

   bool do_cdsettings = false;
   var.key = "pce_cddavolume";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      do_cdsettings = true;
      setting_pce_fast_cddavolume = atoi(var.value);
   }

   var.key = "pce_adpcmvolume";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      do_cdsettings = true;
      setting_pce_fast_adpcmvolume = atoi(var.value);
   }

   var.key = "pce_cdpsgvolume";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      do_cdsettings = true;
      setting_pce_fast_cdpsgvolume = atoi(var.value);
   }

   var.key = "pce_cdspeed";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      do_cdsettings = true;
      setting_pce_fast_cdspeed = atoi(var.value);
   }

   if (do_cdsettings)
   {
      PCECD_Settings settings = {0};
      settings.CDDA_Volume = (double)setting_pce_fast_adpcmvolume / 100;
      settings.CD_Speed = setting_pce_fast_cdspeed;
      settings.ADPCM_Volume = (double)setting_pce_fast_cddavolume / 100;

      if (PCECD_SetSettings(&settings) && log_cb)
         log_cb(RETRO_LOG_INFO, "PCE CD Audio settings changed.\n");
   }
#elif defined(WANT_PSX_EMU)

#if 0
   
   var.key = "psx_dithering";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      bool apply_dither = true;
      if (strcmp(var.value, "enabled") == 0)
         apply_dither = true;
      else if (strcmp(var.value, "disabled") == 0)
         apply_dither = false;

      PSXDitherApply(apply_dither);
   }
    
#endif


   var.key = "psx_enable_dual_analog_type";
   
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {	
		if((strcmp(var.value, "disabled") == 0))
			psx_analog_type="gamepad";
		else if((strcmp(var.value, "dualshock") == 0))
			psx_analog_type="dualshock";
		else if((strcmp(var.value, "dualanalog") == 0))
			psx_analog_type="dualanalog";
		else if((strcmp(var.value, "flightstick") == 0))
			psx_analog_type="analogjoy";
		else
			psx_analog_type="gamepad";	
		if(initial_ports_hookup==true)
			hookup_ports(true);
   }	
   
       
   var.key = "psx_fastboot";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "enabled") == 0)
         setting_psx_fastboot = 1;
      else if (strcmp(var.value, "disabled") == 0)
         setting_psx_fastboot = 0;
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
#elif defined(WANT_NGP_EMU)
   var.key = "ngp_language";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "japanese") == 0)
         setting_ngp_language = 0;
      else if (strcmp(var.value, "english") == 0)
         setting_ngp_language = 1;    
      retro_reset();
   }
#elif defined (WANT_GBA_EMU)
   var.key = "gba_hle";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "enabled") == 0)
         setting_gba_hle = 1;
      else if (strcmp(var.value, "disabled") == 0)
         setting_gba_hle = 0;
   }
#elif defined (WANT_VB_EMU)   
    var.key = "vb_color_mode";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
      if (strcmp(var.value, "black & red") == 0)
	  {
         setting_vb_lcolor = 0xFF0000;
		 setting_vb_rcolor = 0x000000;
	  }
      else if (strcmp(var.value, "black & white") == 0)
	  {
         setting_vb_lcolor = 0xFFFFFF;      
		 setting_vb_rcolor = 0x000000;
	  }
      log_cb(RETRO_LOG_INFO, "[%s]: Palette changed: %s .\n", mednafen_core_str, var.value);  
   }   
   
    var.key = "vb_anaglyph_preset";

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var))
   {
   
   
      if (strcmp(var.value, "disabled") == 0)
		setting_vb_anaglyph_preset = 0; 
      else if (strcmp(var.value, "red & blue") == 0)
         setting_vb_anaglyph_preset = 1;      
      else if (strcmp(var.value, "red & cyan") == 0)
         setting_vb_anaglyph_preset = 2;      
	  else if (strcmp(var.value, "red & electric cyan") == 0)	 
         setting_vb_anaglyph_preset = 3;      
      else if (strcmp(var.value, "red & green") == 0)
         setting_vb_anaglyph_preset = 4;      
      else if (strcmp(var.value, "green & magenta") == 0)
         setting_vb_anaglyph_preset = 5;      
      else if (strcmp(var.value, "yellow & blue") == 0)
         setting_vb_anaglyph_preset = 6;      

      log_cb(RETRO_LOG_INFO, "[%s]: Palette changed: %s .\n", mednafen_core_str, var.value);  
   }      
#endif
}

#if defined(WANT_PSX_EMU)
#define MAX_PLAYERS 2
#define MAX_BUTTONS 16
union
{
   uint32_t u32[MAX_PLAYERS][1 + 8 + 1]; // Buttons + Axes + Rumble
   uint8_t u8[MAX_PLAYERS][(1 + 8 + 1) * sizeof(uint32_t)];
} static buf;
static uint16_t input_buf[MAX_PLAYERS] = {0};

#elif defined(WANT_PCE_FAST_EMU)

#define MAX_PLAYERS 5
#define MAX_BUTTONS 13
static uint8_t input_buf[MAX_PLAYERS][2] = {0};

#elif defined(WANT_LYNX_EMU)

#define MAX_PLAYERS 1
#define MAX_BUTTONS 9
static uint8_t input_buf[MAX_PLAYERS][2] = {0};

#elif defined(WANT_WSWAN_EMU)

#define MAX_PLAYERS 1
#define MAX_BUTTONS 11
static uint16_t input_buf;

#elif defined(WANT_NGP_EMU)

#define MAX_PLAYERS 1
#define MAX_BUTTONS 7
static uint8_t input_buf;

#elif defined(WANT_GBA_EMU)

#define MAX_PLAYERS 1
#define MAX_BUTTONS 11
static uint16_t input_buf;

#elif defined(WANT_SNES_EMU)

#define MAX_PLAYERS 5
#define MAX_BUTTONS 14
static uint8_t input_buf[MAX_PLAYERS][2];

#elif defined(WANT_VB_EMU)

#define MAX_PLAYERS 1
#define MAX_BUTTONS 14
static uint16_t input_buf[MAX_PLAYERS];

#elif defined(WANT_PCFX_EMU)

#define MAX_PLAYERS 2
#define MAX_BUTTONS 12
static uint16_t input_buf[MAX_PLAYERS];

#else

#define MAX_PLAYERS 1
#define MAX_BUTTONS 7

static uint16_t input_buf[1];
#endif


static unsigned retro_devices[2];

static void hookup_ports(bool force)
{
   MDFNGI *currgame = game;

   if (initial_ports_hookup && !force)
      return;

#if defined(WANT_PSX_EMU)
   for (int j = 0; j < MAX_PLAYERS; j++)
   {
      switch (retro_devices[j])
      { 
        case RETRO_DEVICE_ANALOG:
		{
            log_cb(RETRO_LOG_INFO, "[%s]: Selected analog controller type %s.\n", mednafen_core_str, psx_analog_type);
            currgame->SetInput(j, psx_analog_type, &buf.u8[j]);            
			break;          
		}
        default:
			currgame->SetInput(j, "gamepad", &buf.u8[j]);
			break;
      }

      if (rumble.set_rumble_state)
      {
         rumble.set_rumble_state(j, RETRO_RUMBLE_STRONG, 0);
         rumble.set_rumble_state(j, RETRO_RUMBLE_WEAK, 0);
         buf.u32[j][9] = 0;
      }
   }
#elif defined(WANT_PCE_FAST_EMU)
   // Possible endian bug ...
   for (unsigned i = 0; i < MAX_PLAYERS; i++)
      currgame->SetInput(i, "gamepad", &input_buf[i][0]);
#elif defined(WANT_LYNX_EMU)
   currgame->SetInput(0, "gamepad", &input_buf);
#elif defined(WANT_WSWAN_EMU)
   currgame->SetInput(0, "gamepad", &input_buf);
#elif defined(WANT_NGP_EMU)
   currgame->SetInput(0, "gamepad", &input_buf);
#elif defined(WANT_GBA_EMU)
   // Possible endian bug ...
   currgame->SetInput(0, "gamepad", &input_buf);
#elif defined(WANT_SNES_EMU)
   // Possible endian bug ...
   for (unsigned i = 0; i < MAX_PLAYERS; i++)
      currgame->SetInput(i, "gamepad", &input_buf[i][0]);
#elif defined(WANT_VB_EMU)
   // Possible endian bug ...
   currgame->SetInput(0, "gamepad", &input_buf[0]);
#elif defined(WANT_PCFX_EMU)
   for (unsigned i = 0; i < MAX_PLAYERS; i++)
      currgame->SetInput(i, "gamepad", &input_buf[i]);
#else
   // Possible endian bug ...
   currgame->SetInput(0, "gamepad", &input_buf[0]);
#endif

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

#if defined(WANT_GBA_EMU)
   check_variables();
#endif

#if defined(WANT_PSX_EMU)
   check_variables();
   if (environ_cb(RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE, &rumble) && log_cb)
      log_cb(RETRO_LOG_INFO, "Rumble interface supported!\n");
#endif

#if defined(WANT_VB_EMU)
   check_variables();
#endif

   game = MDFNI_LoadGame(MEDNAFEN_CORE_NAME_MODULE, info->path);
   if (!game)
      return false;

   MDFN_PixelFormat pix_fmt(MDFN_COLORSPACE_RGB, 16, 8, 0, 24);
   memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));
   
#if defined(WANT_PSX_EMU)
   is_pal = (CalcDiscSCEx() == REGION_EU);
   mednafen_psx_fb_height = is_pal ? 576  : 480;
   mednafen_psx_fps       = is_pal ? 50.0 : 59.82704;
   surf = new MDFN_Surface(mednafen_buf, FB_WIDTH, mednafen_psx_fb_height, FB_WIDTH, pix_fmt);
#else
   surf = new MDFN_Surface(mednafen_buf, FB_WIDTH, FB_HEIGHT, FB_WIDTH, pix_fmt);
#endif

#ifdef NEED_DEINTERLACER
	PrevInterlaced = false;
	deint.ClearState();
#endif

   hookup_ports(true);

#if !defined(WANT_PSX_EMU)
   check_variables();
#endif

   return game;
}

void retro_unload_game()
{
   if (!game)
      return;

   MDFN_FlushGameCheats(0);

   game->CloseGame();

   if (game->name)
      free(game->name);
   game->name = 0;

   MDFNMP_Kill();

   game = NULL;
}



// Hardcoded for PSX. No reason to parse lots of structures ...
// See mednafen/psx/input/gamepad.cpp
static void update_input(void)
{
   MDFNGI *currgame = game;
#if defined(WANT_PSX_EMU)
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
#elif defined(WANT_LYNX_EMU)
   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_START,
   };

   for (unsigned j = 0; j < MAX_PLAYERS; j++)
   {
      uint16_t input_state = 0;
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_state |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

      // Input data must be little endian.
      input_buf[j][0] = (input_state >> 0) & 0xff;
      input_buf[j][1] = (input_state >> 8) & 0xff;
   }
#elif defined(WANT_PCE_FAST_EMU)

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

   for (unsigned j = 0; j < MAX_PLAYERS; j++)
   {
      uint16_t input_state = 0;
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_state |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

      // Input data must be little endian.
      input_buf[j][0] = (input_state >> 0) & 0xff;
      input_buf[j][1] = (input_state >> 8) & 0xff;
   }
#elif defined(WANT_WSWAN_EMU)
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

   for (unsigned i = 0; i < MAX_BUTTONS; i++)
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

#elif defined(WANT_NGP_EMU)
   input_buf = 0;

   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_UP, //X Cursor horizontal-layout games
      RETRO_DEVICE_ID_JOYPAD_DOWN, //X Cursor horizontal-layout games
      RETRO_DEVICE_ID_JOYPAD_LEFT, //X Cursor horizontal-layout games
      RETRO_DEVICE_ID_JOYPAD_RIGHT, //X Cursor horizontal-layout games
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_START,
   };

   for (unsigned i = 0; i < MAX_BUTTONS; i++)
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

#elif defined(WANT_GBA_EMU)
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

   for (unsigned i = 0; i < MAX_BUTTONS; i++)
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

#elif defined(WANT_SNES_EMU)

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

   for (unsigned j = 0; j < MAX_PLAYERS; j++)
   {
      uint16_t input_state = 0;
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_state |= input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

#ifdef MSB_FIRST
      union {
         uint8_t b[2];
         uint16_t s;
      } u;
      u.s = input_buf[j];
      input_buf[j] = u.b[0] | u.b[1] << 8;
#else
      input_buf[j][0] = (input_state >> 0) & 0xff;
      input_buf[j][1] = (input_state >> 8) & 0xff;
#endif
   }

#elif defined(WANT_VB_EMU)
   input_buf[0] = 0;
   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_L2, //right d-pad UP
      RETRO_DEVICE_ID_JOYPAD_R3, //right d-pad RIGHT
      RETRO_DEVICE_ID_JOYPAD_RIGHT, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_LEFT, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_DOWN, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_UP, //left d-pad
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_R2, //right d-pad LEFT
      RETRO_DEVICE_ID_JOYPAD_L3, //right d-pad DOWN
   };

   for (unsigned j = 0; j < MAX_PLAYERS; j++)
   {
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_buf[j] |= map[i] != -1u &&
            input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

#ifdef MSB_FIRST
      union {
         uint8_t b[2];
         uint16_t s;
      } u;
      u.s = input_buf[j];
      input_buf[j] = u.b[0] | u.b[1] << 8;
#endif
   }

#elif defined(WANT_PCFX_EMU)
   input_buf[0] = input_buf[1] = 0;
   static unsigned map[] = {
      RETRO_DEVICE_ID_JOYPAD_A,
      RETRO_DEVICE_ID_JOYPAD_B,
      RETRO_DEVICE_ID_JOYPAD_X,
      RETRO_DEVICE_ID_JOYPAD_Y,
      RETRO_DEVICE_ID_JOYPAD_L,
      RETRO_DEVICE_ID_JOYPAD_R,
      RETRO_DEVICE_ID_JOYPAD_SELECT,
      RETRO_DEVICE_ID_JOYPAD_START,
      RETRO_DEVICE_ID_JOYPAD_UP,
      RETRO_DEVICE_ID_JOYPAD_RIGHT,
      RETRO_DEVICE_ID_JOYPAD_DOWN,
      RETRO_DEVICE_ID_JOYPAD_LEFT,
   };

   for (unsigned j = 0; j < MAX_PLAYERS; j++)
   {
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_buf[j] |= map[i] != -1u &&
            input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;

#ifdef MSB_FIRST
      union {
         uint8_t b[2];
         uint16_t s;
      } u;
      u.s = input_buf[j];
      input_buf[j] = u.b[0] | u.b[1] << 8;
#endif

   }
#else
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

   for (unsigned j = 0; j < MAX_PLAYERS; j++)
   {
      for (unsigned i = 0; i < MAX_BUTTONS; i++)
         input_buf[j] |= map[i] != -1u &&
            input_state_cb(j, RETRO_DEVICE_JOYPAD, 0, map[i]) ? (1 << i) : 0;
   }

#endif
}

static uint64_t video_frames, audio_frames;


void retro_run()
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

   curgame->Emulate(&spec);

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
#if defined(WANT_PSX_EMU)
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
#else
   unsigned width  = spec.DisplayRect.w;
   unsigned height = spec.DisplayRect.h;

#if defined(WANT_32BPP)
   const uint32_t *pix = surf->pixels;
   video_cb(pix, width, height, FB_WIDTH << 2);
#elif defined(WANT_16BPP)
   const uint16_t *pix = surf->pixels16;
   video_cb(pix, width, height, FB_WIDTH << 1);
#endif
#endif

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
#ifdef WANT_PSX_EMU
   info->timing.fps            = mednafen_psx_fps; // Determined for NTSC from empirical testing.
#else
   info->timing.fps            = MEDNAFEN_CORE_TIMING_FPS;
#endif
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
#ifdef WANT_PSX_EMU
   return is_pal ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
#else
   return RETRO_REGION_NTSC; // FIXME: Regions for other cores.
#endif
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
      if (log_cb)
         log_cb(RETRO_LOG_WARN, "[%s]: Only the 2 main ports are supported at the moment", mednafen_core_str);
      return;
   }

   switch (device)
   {
      // TODO: Add support for other input types
      case RETRO_DEVICE_JOYPAD:
      case RETRO_DEVICE_ANALOG:
         if (log_cb)
            log_cb(RETRO_LOG_INFO, "[%s]: Selected controller type %u.\n", mednafen_core_str, device);
         retro_devices[in_port] = device;
         break;
      default:
         retro_devices[in_port] = RETRO_DEVICE_JOYPAD;
         if (log_cb)
            log_cb(RETRO_LOG_WARN, "[%s]: Unsupported controller device %u, falling back to gamepad.\n", mednafen_core_str,device);
   }
   check_variables();
   hookup_ports(true);
#endif
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;

#if defined(WANT_PCE_FAST_EMU)
   static const struct retro_variable vars[] = {
      { "pce_nospritelimit", "No Sprite Limit; disabled|enabled" },
      { "pce_keepaspect", "Keep Aspect; enabled|disabled" },
      { "pce_cddavolume", "(CD) CDDA Volume; 100|90|80|70|60|50|40|30|20|10|0" },
      { "pce_adpcmvolume", "(CD) ADPCM Volume; 100|90|80|70|60|50|40|30|20|10|0" },
      { "pce_cdpsgvolume", "(CD) CD PSG Volume; 100|90|80|70|60|50|40|30|20|10|0" },
      { "pce_cdspeed", "(CD) CD Speed; 1|2|4|8" },
      { NULL, NULL },
   };
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
#elif defined(WANT_PSX_EMU)
   static const struct retro_variable vars[] = {
      { "psx_fastboot", "Skip BIOS sequence; disabled|enabled" },
      { "psx_dithering", "Dithering; enabled|disabled" },
      { "psx_enable_dual_analog_type", "Analog controller mode; disabled|dualshock|dualanalog|flightstick" },	  
      { "psx_enable_multitap_port1", "Port 1: Multitap enable; disabled|enabled" },
      { "psx_enable_multitap_port2", "Port 2: Multitap enable; disabled|enabled" },

      { NULL, NULL },
   };
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
#elif defined(WANT_GBA_EMU)
   static const struct retro_variable vars[] = {
      { "gba_hle", "HLE bios emulation; enabled|disabled" },
      { NULL, NULL },
   };
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
#elif defined(WANT_NGP_EMU)
   static const struct retro_variable vars[] = {
      { "ngp_language", "Language; japanese|english" },
      { NULL, NULL },
   };
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
#elif defined(WANT_VB_EMU)
    static const struct retro_variable vars[] = {
	  { "vb_anaglyph_preset", "Anaglyph preset (restart); disabled|red & blue|red & cyan|red & electric cyan|red & green|green & magenta|yellow & blue" },
      { "vb_color_mode", "Palette (restart); black & red|black & white" },
      { NULL, NULL },
   };
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
   
#endif
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
   MDFNGI *curgame = game;
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
