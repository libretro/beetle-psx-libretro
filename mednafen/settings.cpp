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
uint32_t setting_pce_keepaspect = 1;
#elif defined(WANT_PSX_EMU)
uint32_t setting_psx_multitap_port_1 = 0;
uint32_t setting_psx_multitap_port_2 = 0;
uint32_t setting_psx_fastboot = 1;
#elif defined(WANT_NGP_EMU)
uint32_t setting_ngp_language = 0;
#elif defined(WANT_GBA_EMU)
uint32_t setting_gba_hle = 1;
#elif defined(WANT_VB_EMU)
uint32_t setting_vb_lcolor=0xFF0000;
uint32_t setting_vb_rcolor=0x000000;
uint32_t setting_vb_anaglyph_preset=0;
#endif

bool MDFN_SaveSettings(const char *path)
{
   return(1);
}

uint64 MDFN_GetSettingUI(const char *name)
{
#if defined(WANT_VB_EMU)
   if (!strcmp("vb.anaglyph.lcolor", name))
   {
	  fprintf(stderr, "Setting UI: %s=%x\n", name, setting_vb_lcolor);
      return setting_vb_lcolor;
   }
   if (!strcmp("vb.anaglyph.rcolor", name))
      return setting_vb_rcolor;
#elif defined(WANT_PCE_FAST_EMU)
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
#elif defined(WANT_WSWAN_EMU)
   if (!strcmp("wswan.ocmultiplier", name))
      return 1;
   if (!strcmp("wswan.bday", name))
      return 23;
   if (!strcmp("wswan.bmonth", name))
      return 6;
   if (!strcmp("wswan.byear", name))
      return 1989;
   if (!strcmp("wswan.slstart", name))
      return 4;
   if (!strcmp("wswan.slend", name))
      return 235;
#elif defined(WANT_PSX_EMU)
   if (!strcmp("psx.spu.resamp_quality", name)) /* make configurable */
      return 4;
#endif

   fprintf(stderr, "unhandled setting UI: %s\n", name);
   return 0;
}

int64 MDFN_GetSettingI(const char *name)
{
#if defined(WANT_VB_EMU)
   if (!strcmp("vb.anaglyph.preset", name))
      return setting_vb_anaglyph_preset;
#elif defined(WANT_PSX_EMU)
   if (!strcmp("psx.region_default", name)) /* make configurable */
      return 1; /* REGION_JP = 0, REGION_NA = 1, REGION_EU = 2 */
   if (!strcmp("psx.slstart", name))
      return 0;
   if (!strcmp("psx.slstartp", name))
      return 0;
   if (!strcmp("psx.slend", name))
      return 239;
   if (!strcmp("psx.slendp", name))
      return 287;
#elif defined(WANT_WSWAN_EMU)
   if (!strcmp("wswan.sex", name))
      return 0;
   if (!strcmp("wswan.blood", name))
      return 0;
#endif
   fprintf(stderr, "unhandled setting I: %s\n", name);
   return 0;
}

double MDFN_GetSettingF(const char *name)
{
#if defined(WANT_PSX_EMU)
   if (!strcmp("psx.input.mouse_sensitivity", name)) /* make configurable */
      return 1.00;
#elif defined(WANT_WSWAN_EMU)
   if (!strcmp("wswan.mouse_sensitivity", name))
      return 0.50;
#endif

   fprintf(stderr, "unhandled setting F: %s\n", name);
   return 0;
}

bool MDFN_GetSettingB(const char *name)
{
   if (!strcmp("cheats", name))
      return 0;
   /* LIBRETRO */
   if (!strcmp("libretro.cd_load_into_ram", name))
      return 0;
#if defined(WANT_VB_EMU)
   if (!strcmp("vb.instant_display_hack", name))
      return 1;
   if (!strcmp("vb.allow_draw_skip", name))
      return 1;
#elif defined(WANT_SNES_EMU)
   if (!strcmp("snes.correct_aspect", name))
      return 0;
   if (!strcmp("snes.input.port1.multitap", name))
      return 0;
   if (!strcmp("snes.input.port2.multitap", name))
      return 0;
#elif defined(WANT_PCE_FAST_EMU)
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
      return setting_pce_keepaspect;
#elif defined(WANT_PSX_EMU)
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
      return setting_psx_fastboot;
#elif defined(WANT_NGP_EMU)
   if (!strcmp("ngp.language", name))
      return setting_ngp_language;
#elif defined(WANT_WSWAN_EMU)
   if (!strcmp("wswan.forcemono", name))
      return 0;
   if (!strcmp("wswan.language", name))
      return 1;
   if (!strcmp("wswan.correct_aspect", name))
      return 1;
#endif
   /* CDROM */
   if (!strcmp("cdrom.lec_eval", name))
      return 1;
   /* FILESYS */
   if (!strcmp("filesys.untrusted_fip_check", name))
      return 0;
   if (!strcmp("filesys.disablesavegz", name))
      return 1;
   fprintf(stderr, "unhandled setting B: %s\n", name);
   return 0;
}

extern std::string retro_base_directory;
extern std::string retro_base_name;

std::string MDFN_GetSettingS(const char *name)
{
#if defined(WANT_GBA_EMU)
   if (!strcmp("gba.bios", name))
      return setting_gba_hle ? std::string("") : std::string("gba_bios.bin");
#elif defined(WANT_PCE_FAST_EMU)
   if (!strcmp("pce_fast.cdbios", name))
      return std::string("syscard3.pce");
#elif defined(WANT_PSX_EMU)
   if (!strcmp("psx.bios_eu", name))
      return std::string("scph5502.bin");
   if (!strcmp("psx.bios_jp", name))
      return std::string("scph5500.bin");
   if (!strcmp("psx.bios_na", name))
      return std::string("scph5501.bin");
   if (!strcmp("psx.region_default", name)) /* make configurable */
      return "na";
#elif defined(WANT_WSWAN_EMU)
   if (!strcmp("wswan.name", name))
      return std::string("Mednafen");
#endif
   /* FILESYS */
   if (!strcmp("filesys.path_firmware", name))
      return retro_base_directory;
   if (!strcmp("filesys.path_palette", name))
      return retro_base_directory;
   if (!strcmp("filesys.path_sav", name))
      return retro_base_directory;
   if (!strcmp("filesys.path_state", name))
      return retro_base_directory;
   if (!strcmp("filesys.path_cheat", name))
      return retro_base_directory;
   if (!strcmp("filesys.fname_state", name))
      return retro_base_name + std::string(".sav");
   if (!strcmp("filesys.fname_sav", name))
      return retro_base_name + std::string(".bsv");
   fprintf(stderr, "unhandled setting S: %s\n", name);
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
