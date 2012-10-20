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
#include <vector>
#include <string>
#include <trio/trio.h>
#include <map>
#include <list>
#include "settings.h"
#include "md5.h"

bool MDFN_SaveSettings(const char *path)
{
 return(1);
}

uint64 MDFN_GetSettingUI(const char *name)
{
	/* PSX */
	if(!strcmp("psx.spu.resamp_quality", name)) /* make configurable */
		return 4;

	fprintf(stderr, "unhandled setting UI: %s\n", name);
	assert(0);
	return 0;
}

int64 MDFN_GetSettingI(const char *name)
{
	/* PSX */
	if(!strcmp("psx.region_default", name)) /* make configurable */
		return 1; /* REGION_JP = 0, REGION_NA = 1, REGION_EU = 2 */

	fprintf(stderr, "unhandled setting I: %s\n", name);
	assert(0);
	return 0;
}

double MDFN_GetSettingF(const char *name)
{
	/* PSX */
	if(!strcmp("psx.input.mouse_sensitivity", name)) /* make configurable */
		return 1.00;

	fprintf(stderr, "unhandled setting F: %s\n", name);
	assert(0);
	return 0;
}

bool MDFN_GetSettingB(const char *name)
{
	if(!strcmp("cheats", name))
		return 0;
	/* PCE_FAST */
        if(!strcmp("pce_fast.input.multitap", name))
                return 1;
	if(!strcmp("pce_fast.arcadecard", name))
		return 1;
	if(!strcmp("pce_fast.forcesgx", name))
		return 0;
	if(!strcmp("pce_fast.nospritelimit", name))
		return 0;
	if(!strcmp("pce_fast.forcemono", name))
		return 0;
	if(!strcmp("pce_fast.disable_softreset", name))
		return 0;
	if(!strcmp("pce_fast.adpcmlp", name))
		return 0;
	if(!strcmp("pce_fast.correct_aspect", name))
		return 1;
	/* PSX */
	if(!strcmp("psx.input.port1.memcard", name))
		return 1;
	if(!strcmp("psx.input.port2.memcard", name))
		return 1;
	if(!strcmp("psx.input.port3.memcard", name))
		return 1;
	if(!strcmp("psx.input.port4.memcard", name))
		return 1;
	if(!strcmp("psx.input.port5.memcard", name))
		return 1;
	if(!strcmp("psx.input.port6.memcard", name))
		return 1;
	if(!strcmp("psx.input.port7.memcard", name))
		return 1;
	if(!strcmp("psx.input.port8.memcard", name))
		return 1;
	if(!strcmp("psx.input.port1.multitap", name)) /* make configurable */
		return 1;
	if(!strcmp("psx.input.port2.multitap", name)) /* make configurable */
		return 1;
	if(!strcmp("psx.region_autodetect", name)) /* make configurable */
		return 1;
	if(!strcmp("psx.input.analog_mode_ct", name)) /* make configurable */
		return 1;
	/* CDROM */
	if(!strcmp("cdrom.lec_eval", name))
		return 1;
	/* FILESYS */
	if(!strcmp("filesys.untrusted_fip_check", name))
		return 0;
	if(!strcmp("filesys.disablesavegz", name))
		return 1;
	fprintf(stderr, "unhandled setting B: %s\n", name);
	assert(0);
	return 0;
}

extern std::string retro_base_directory;
extern std::string retro_base_name;

std::string MDFN_GetSettingS(const char *name)
{
	/* PCE_FAST */
	if(!strcmp("pce_fast.cdbios", name))
        {
                fprintf(stderr, "pce_fast.cdbios: %s\n", std::string("syscard3.pce").c_str());
		return std::string("syscard3.pce");
        }
	/* PSX */
	if(!strcmp("psx.bios_eu", name))
        {
                fprintf(stderr, "psx.bios_eu: %s%s\n", retro_base_directory.c_str(), name);
		assert(0);
		return std::string(retro_base_directory) + std::string("scph5502.bin");
        }
	if(!strcmp("psx.bios_jp", name))
        {
                fprintf(stderr, "psx.bios_jp: %s%s\n", retro_base_directory.c_str(), name);
		assert(0);
		return std::string(retro_base_directory) + std::string("scph5500.bin");
        }
	if(!strcmp("psx.bios_na", name))
        {
                fprintf(stderr, "psx.bios_na: %s%s\n", retro_base_directory.c_str(), name);
		assert(0);
		return std::string(retro_base_directory) + std::string("scph5501.bin");
        }
	if(!strcmp("psx.region_default", name)) /* make configurable */
		return "na";
	/* FILESYS */
	if(!strcmp("filesys.path_firmware", name))
        {
                fprintf(stderr, "filesys.path_firmware: %s\n", retro_base_directory.c_str());
		return retro_base_directory;
        }
	if(!strcmp("filesys.path_palette", name))
        {
                fprintf(stderr, "filesys.path_palette: %s\n", retro_base_directory.c_str());
		return retro_base_directory;
        }
	if(!strcmp("filesys.path_sav", name))
        {
                fprintf(stderr, "filesys.path_sav: %s\n", retro_base_directory.c_str());
		return retro_base_directory;
        }
	if(!strcmp("filesys.path_state", name))
        {
                fprintf(stderr, "filesys.path_state: %s\n", retro_base_directory.c_str());
		return retro_base_directory;
        }
	if(!strcmp("filesys.path_cheat", name))
        {
                fprintf(stderr, "filesys.path_cheat: %s\n", retro_base_directory.c_str());
		return retro_base_directory;
        }
	if(!strcmp("filesys.fname_state", name))
        {
                fprintf(stderr, "filesys.fname_state: %s%s\n", retro_base_name.c_str(), std::string(".sav").c_str());
		return retro_base_name + std::string(".sav");
        }
	if(!strcmp("filesys.fname_sav", name))
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
