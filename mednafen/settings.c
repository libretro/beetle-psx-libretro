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

#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <libretro.h>

#include "mednafen-driver.h"
#include "settings.h"

int setting_initial_scanline = 0;
int setting_initial_scanline_pal = 0;
int setting_last_scanline = 239;
int setting_last_scanline_pal = 287;

uint32_t setting_psx_multitap_port_1 = 0;
uint32_t setting_psx_multitap_port_2 = 0;
uint32_t setting_psx_analog_toggle = 0;
uint32_t setting_psx_fastboot = 1;

uint64_t MDFN_GetSettingUI(const char *name)
{
   if (!strcmp("psx.spu.resamp_quality", name)) /* make configurable */
      return 4;

   MDFN_DispMessage(3, RETRO_LOG_WARN,
         RETRO_MESSAGE_TARGET_LOG, RETRO_MESSAGE_TYPE_NOTIFICATION,
         "unhandled setting UI: %s\n", name);
   return 0;
}

int64_t MDFN_GetSettingI(const char *name)
{
   if (!strcmp("psx.region_default", name)) /* make configurable */
      return 1; /* REGION_JP = 0, REGION_NA = 1, REGION_EU = 2 */
   if (!strcmp("psx.slstart", name))
      return setting_initial_scanline;
   if (!strcmp("psx.slstartp", name))
      return setting_initial_scanline_pal;
   if (!strcmp("psx.slend", name))
      return setting_last_scanline;
   if (!strcmp("psx.slendp", name))
      return setting_last_scanline_pal;
   MDFN_DispMessage(3, RETRO_LOG_WARN,
         RETRO_MESSAGE_TARGET_LOG, RETRO_MESSAGE_TYPE_NOTIFICATION,
         "unhandled setting I: %s\n", name);
   return 0;
}

bool MDFN_GetSettingB(const char *name)
{
   if (!strcmp("cheats", name))
      return 1;
   /* LIBRETRO */
   if (!strcmp("libretro.cd_load_into_ram", name))
      return 0;
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
   if (!strcmp("psx.input.pport1.multitap", name)) /* make configurable */
      return setting_psx_multitap_port_1;
   if (!strcmp("psx.input.pport2.multitap", name)) /* make configurable */
      return setting_psx_multitap_port_2;
   if (!strcmp("psx.region_autodetect", name)) /* make configurable */
      return 1;
   if (!strcmp("psx.input.analog_mode_ct", name)) /* make configurable */
      return setting_psx_analog_toggle;
   if (!strcmp("psx.fastboot", name))
      return setting_psx_fastboot;
   /* CDROM */
   if (!strcmp("cdrom.lec_eval", name))
      return 1;
   /* FILESYS */
   if (!strcmp("filesys.untrusted_fip_check", name))
      return 0;
   MDFN_DispMessage(3, RETRO_LOG_WARN,
         RETRO_MESSAGE_TARGET_LOG, RETRO_MESSAGE_TYPE_NOTIFICATION,
         "unhandled setting B: %s\n", name);
   return 0;
}

const char *MDFN_GetSettingS(const char *name)
{
   if (!strcmp("psx.bios_eu", name))
      return "scph5502.bin";
   if (!strcmp("psx.bios_jp", name))
      return "scph5500.bin";
   if (!strcmp("psx.bios_na", name))
      return "scph5501.bin";
   if (!strcmp("psx.region_default", name)) /* make configurable */
      return "na";
   MDFN_DispMessage(3, RETRO_LOG_WARN,
         RETRO_MESSAGE_TARGET_LOG, RETRO_MESSAGE_TYPE_NOTIFICATION,
         "unhandled setting S: %s\n", name);
   return 0;
}
