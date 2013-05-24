/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "mednafen.h"
#include <errno.h>
#include <string.h>
#include <string>
#include "settings.h"

#if defined(WANT_PCE_FAST_EMU)
int setting_pce_fast_nospritelimit = 0;
int setting_pce_fast_cddavolume = 100;
int setting_pce_fast_adpcmvolume = 100;
int setting_pce_fast_cdpsgvolume = 100;
uint32_t setting_pce_fast_cdspeed = 1;
#elif defined(WANT_PSX_EMU)
uint32_t setting_psx_multitap_port_1 = 0;
uint32_t setting_psx_multitap_port_2 = 0;
#endif

bool MDFN_SaveSettings(const char *path)
{
   return(1);
}

uint64 MDFN_GetSettingUI(const char *name)
{
   /* VB */
   if (!strcmp("vb.anaglyph.lcolor", name))
      return 0xFF0000;
   if (!strcmp("vb.anaglyph.rcolor", name))
      return 0x000000;
   /* PCE FAST */
   if (!strcmp("pce_fast.cddavolume", name))
      return setting_pce_fast_cddavolume;
   if (!strcmp("pce_fast.adpcmvolume", name))
      return setting_pce_fast_adpcmvolume;
   if (!strcmp("pce_fast.cdpsgvolume", name))
      return setting_pce_fast_cdpsgvolume;
   if (!strcmp("pce_fast.cdspeed", name))
      return setting_pce_fast_cdspeed;
   if (!strcmp("pce_fast.ocmultiplier", name)) /* make configurable */
      return 1;
   if (!strcmp("pce_fast.slstart", name))
      return 4;
   if (!strcmp("pce_fast.slend", name))
      return 235;
   /* WSWAN */
   if (!strcmp("wswan.ocmultiplier", name))
      return 1;
   if (!strcmp("wswan.bday", name))
      return 23;
   if (!strcmp("wswan.bmonth", name))
      return 6;
   if (!strcmp("wswan.byear", name))
      return 1989;
   if (!strcmp("wswan.cdspeed", name))
      return 2;
   if (!strcmp("wswan.cdpsgvolume", name))
      return 100;
   if (!strcmp("wswan.cddavolume", name))
      return 100;
   if (!strcmp("wswan.adpcmvolume", name))
      return 100;
   if (!strcmp("wswan.slstart", name))
      return 4;
   if (!strcmp("wswan.slend", name))
      return 235;
   /* PSX */
   if (!strcmp("psx.spu.resamp_quality", name)) /* make configurable */
      return 4;

   fprintf(stderr, "unhandled setting UI: %s\n", name);
   assert(0);
   return 0;
}

int64 MDFN_GetSettingI(const char *name)
{
   /* VB */
   if (!strcmp("vb.anaglyph.preset", name))
      return 0;
   /* PSX */
   if (!strcmp("psx.region_default", name)) /* make configurable */
      return 1; /* REGION_JP = 0, REGION_NA = 1, REGION_EU = 2 */
   /* WSWAN */
   if (!strcmp("wswan.sex", name))
      return 0;
   if (!strcmp("wswan.blood", name))
      return 0;
   fprintf(stderr, "unhandled setting I: %s\n", name);
   assert(0);
   return 0;
}

double MDFN_GetSettingF(const char *name)
{
   /* PSX */
   if (!strcmp("psx.input.mouse_sensitivity", name)) /* make configurable */
      return 1.00;
   /* WSWAN */
   if (!strcmp("wswan.mouse_sensitivity", name))
      return 0.50;

   fprintf(stderr, "unhandled setting F: %s\n", name);
   assert(0);
   return 0;
}

bool MDFN_GetSettingB(const char *name)
{
   if (!strcmp("cheats", name))
      return 0;
   /* LIBRETRO */
   if (!strcmp("libretro.cd_load_into_ram", name))
      return 0;
   /* VB */
   if (!strcmp("vb.instant_display_hack", name))
      return 1;
   if (!strcmp("vb.allow_draw_skip", name))
      return 1;
   /* SNES */
   if (!strcmp("snes.correct_aspect", name))
      return 0;
   if (!strcmp("snes.input.port1.multitap", name))
      return 0;
   if (!strcmp("snes.input.port2.multitap", name))
      return 0;
   /* PCE_FAST */
#if defined(WANT_PCE_FAST_EMU)
   if (!strcmp("pce_fast.input.multitap", name))
      return 1;
   if (!strcmp("pce_fast.arcadecard", name))
      return 1;
   if (!strcmp("pce_fast.forcesgx", name))
      return 0;
   if (!strcmp("pce_fast.nospritelimit", name))
      return setting_pce_fast_nospritelimit;
   if (!strcmp("pce_fast.forcemono", name))
      return 0;
   if (!strcmp("pce_fast.disable_softreset", name))
      return 0;
   if (!strcmp("pce_fast.adpcmlp", name))
      return 0;
   if (!strcmp("pce_fast.correct_aspect", name))
      return 1;
#elif defined(WANT_PSX_EMU)
   /* PSX */
   if (!strcmp("psx.input.port1.memcard", name))
      return 1;
   if (!strcmp("psx.input.port2.memcard", name))
      return 1;
   if (!strcmp("psx.input.port3.memcard", name))
      return 1;
   if (!strcmp("psx.input.port4.memcard", name))
      return 1;
   if (!strcmp("psx.input.port5.memcard", name))
      return 1;
   if (!strcmp("psx.input.port6.memcard", name))
      return 1;
   if (!strcmp("psx.input.port7.memcard", name))
      return 1;
   if (!strcmp("psx.input.port8.memcard", name))
      return 1;
   if (!strcmp("psx.input.port1.multitap", name)) /* make configurable */
      return setting_psx_multitap_port_1;
   if (!strcmp("psx.input.port2.multitap", name)) /* make configurable */
      return setting_psx_multitap_port_2;
   if (!strcmp("psx.region_autodetect", name)) /* make configurable */
      return 1;
   if (!strcmp("psx.input.analog_mode_ct", name)) /* make configurable */
      return 1;
   if (!strcmp("psx.fastboot", name))
      return 1;
#endif
   /* WSWAN */
   if (!strcmp("wswan.forcemono", name))
      return 0;
   if (!strcmp("wswan.language", name))
      return 1;
   if (!strcmp("wswan.correct_aspect", name))
      return 1;
   /* CDROM */
   if (!strcmp("cdrom.lec_eval", name))
      return 1;
   /* FILESYS */
   if (!strcmp("filesys.untrusted_fip_check", name))
      return 0;
   if (!strcmp("filesys.disablesavegz", name))
      return 1;
   fprintf(stderr, "unhandled setting B: %s\n", name);
   assert(0);
   return 0;
}

extern std::string retro_base_directory;
extern std::string retro_base_name;

std::string MDFN_GetSettingS(const char *name)
{
   char slash;
#ifdef _WIN32
   slash = '\\';
#else
   slash = '/';
#endif
   /* GBA */
   if (!strcmp("gba.bios", name))
   {
      fprintf(stderr, "gba.bios: %s\n", std::string("gba_bios.bin").c_str());
      return std::string(retro_base_directory) + slash + std::string("gba_bios.bin");
   }
   /* PCE_FAST */
   if (!strcmp("pce_fast.cdbios", name))
   {
      fprintf(stderr, "pce_fast.cdbios: %s\n", std::string("syscard3.pce").c_str());
      return std::string(retro_base_directory) + slash + std::string("syscard3.pce");
   }
   /* PSX */
   if (!strcmp("psx.bios_eu", name))
   {
      std::string ret = std::string(retro_base_directory) + slash + std::string("scph5502.bin");
      fprintf(stderr, "psx.bios_eu: %s\n", ret.c_str());
      return ret;
   }
   if (!strcmp("psx.bios_jp", name))
   {
      std::string ret = std::string(retro_base_directory) + slash + std::string("scph5500.bin");
      fprintf(stderr, "psx.bios_jp: %s\n", ret.c_str());
      return ret;
   }
   if (!strcmp("psx.bios_na", name))
   {
      std::string ret = std::string(retro_base_directory) + slash + std::string("scph5501.bin");
      fprintf(stderr, "psx.bios_na: %s\n", ret.c_str());
      return ret;
   }
   if (!strcmp("psx.region_default", name)) /* make configurable */
      return "na";
   /* WSWAN */
   if (!strcmp("wswan.name", name))
   {
      return std::string("Mednafen");
   }
   /* FILESYS */
   if (!strcmp("filesys.path_firmware", name))
   {
      fprintf(stderr, "filesys.path_firmware: %s\n", retro_base_directory.c_str());
      return retro_base_directory;
   }
   if (!strcmp("filesys.path_palette", name))
   {
      fprintf(stderr, "filesys.path_palette: %s\n", retro_base_directory.c_str());
      return retro_base_directory;
   }
   if (!strcmp("filesys.path_sav", name))
   {
      fprintf(stderr, "filesys.path_sav: %s\n", retro_base_directory.c_str());
      return retro_base_directory;
   }
   if (!strcmp("filesys.path_state", name))
   {
      fprintf(stderr, "filesys.path_state: %s\n", retro_base_directory.c_str());
      return retro_base_directory;
   }
   if (!strcmp("filesys.path_cheat", name))
   {
      fprintf(stderr, "filesys.path_cheat: %s\n", retro_base_directory.c_str());
      return retro_base_directory;
   }
   if (!strcmp("filesys.fname_state", name))
   {
      fprintf(stderr, "filesys.fname_state: %s%s\n", retro_base_name.c_str(), std::string(".sav").c_str());
      return retro_base_name + std::string(".sav");
   }
   if (!strcmp("filesys.fname_sav", name))
   {
      fprintf(stderr, "filesys.fname_sav: %s%s\n", retro_base_name.c_str(), std::string(".bsv").c_str());
      return retro_base_name + std::string(".bsv");
   }
   fprintf(stderr, "unhandled setting S: %s\n", name);
   assert(0);
   return 0;
}

bool MDFNI_SetSetting(const char *name, const char *value, bool NetplayOverride)
{
   return false;
}

bool MDFNI_SetSettingB(const char *name, bool value)
{
   return false;
}

bool MDFNI_SetSettingUI(const char *name, uint64 value)
{
   return false;
}

void MDFNI_DumpSettingsDef(const char *path)
{
}
