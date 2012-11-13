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

#include <string.h>
#include	<stdarg.h>
#include	<errno.h>
#include	<trio/trio.h>
#include	<list>
#include	<algorithm>

#include	"general.h"

#include	"state.h"
#include "video.h"
#ifdef NEED_DEINTERLACER
#include	"video/Deinterlacer.h"
#endif
#include	"file.h"
#include	"FileWrapper.h"

#ifdef NEED_CD
#include	"cdrom/cdromif.h"
#include	"cdrom/CDUtility.h"
#endif

#include	"mempatcher.h"
#include	"md5.h"
#include	"clamp.h"

MDFNGI *MDFNGameInfo = NULL;

static MDFN_PixelFormat last_pixel_format;
static double last_sound_rate;

#ifdef NEED_DEINTERLACER
static bool PrevInterlaced;
static Deinterlacer deint;
#endif

#ifdef NEED_CD
static std::vector<CDIF *> CDInterfaces;	// FIXME: Cleanup on error out.
#endif

void MDFNI_CloseGame(void)
{
 if(MDFNGameInfo)
 {
   MDFN_FlushGameCheats(0);

  MDFNGameInfo->CloseGame();
  if(MDFNGameInfo->name)
  {
   free(MDFNGameInfo->name);
   MDFNGameInfo->name=0;
  }
  MDFNMP_Kill();

  MDFNGameInfo = NULL;

#ifdef NEED_CD
  for(unsigned i = 0; i < CDInterfaces.size(); i++)
   delete CDInterfaces[i];
  CDInterfaces.clear();
#endif
 }

 #ifdef WANT_DEBUGGER
 MDFNDBG_Kill();
 #endif
}


std::vector<MDFNGI *> MDFNSystems;
static std::list<MDFNGI *> MDFNSystemsPrio;

bool MDFNSystemsPrio_CompareFunc(MDFNGI *first, MDFNGI *second)
{
 if(first->ModulePriority > second->ModulePriority)
  return(true);

 return(false);
}

static void AddSystem(MDFNGI *system)
{
 MDFNSystems.push_back(system);
}


#ifdef NEED_CD
bool CDIF_DumpCD(const char *fn);

void MDFNI_DumpModulesDef(const char *fn)
{
 FILE *fp = fopen(fn, "wb");

 for(unsigned int i = 0; i < MDFNSystems.size(); i++)
 {
  fprintf(fp, "%s\n", MDFNSystems[i]->shortname);
  fprintf(fp, "%s\n", MDFNSystems[i]->fullname);
  fprintf(fp, "%d\n", MDFNSystems[i]->nominal_width);
  fprintf(fp, "%d\n", MDFNSystems[i]->nominal_height);
 }


 fclose(fp);
}

static void ReadM3U(std::vector<std::string> &file_list, std::string path, unsigned depth = 0)
{
 std::vector<std::string> ret;
 FileWrapper m3u_file(path.c_str(), FileWrapper::MODE_READ, _("M3U CD Set"));
 std::string dir_path;
 char linebuf[2048];

 MDFN_GetFilePathComponents(path, &dir_path);

 while(m3u_file.get_line(linebuf, sizeof(linebuf)))
 {
  std::string efp;

  if(linebuf[0] == '#') continue;
  MDFN_rtrim(linebuf);
  if(linebuf[0] == 0) continue;

  efp = MDFN_EvalFIP(dir_path, std::string(linebuf));

  if(efp.size() >= 4 && efp.substr(efp.size() - 4) == ".m3u")
  {
   if(efp == path)
    throw(MDFN_Error(0, _("M3U at \"%s\" references self."), efp.c_str()));

   if(depth == 99)
    throw(MDFN_Error(0, _("M3U load recursion too deep!")));

   ReadM3U(file_list, efp, depth++);
  }
  else
   file_list.push_back(efp);
 }
}

// TODO: LoadCommon()

MDFNGI *MDFNI_LoadCD(const char *force_module, const char *devicename)
{
 uint8 LayoutMD5[16];

 MDFNI_CloseGame();

 MDFN_printf(_("Loading %s...\n\n"), devicename ? devicename : _("PHYSICAL CD"));

 try
 {
  if(devicename && strlen(devicename) > 4 && !strcasecmp(devicename + strlen(devicename) - 4, ".m3u"))
  {
   std::vector<std::string> file_list;

   ReadM3U(file_list, devicename);

   for(unsigned i = 0; i < file_list.size(); i++)
   {
    CDInterfaces.push_back(new CDIF_MT(file_list[i].c_str()));
   }
  }
  else
  {
   CDInterfaces.push_back(new CDIF_MT(devicename));
  }
 }
 catch(std::exception &e)
 {
  MDFND_PrintError(e.what());
  MDFN_PrintError(_("Error opening CD."));
  return(0);
 }

 //
 // Print out a track list for all discs.
 //
 MDFN_indent(1);
 for(unsigned i = 0; i < CDInterfaces.size(); i++)
 {
  CDUtility::TOC toc;

  CDInterfaces[i]->ReadTOC(&toc);

  MDFN_printf(_("CD %d Layout:\n"), i + 1);
  MDFN_indent(1);

  for(int32 track = toc.first_track; track <= toc.last_track; track++)
  {
   MDFN_printf(_("Track %2d, LBA: %6d  %s\n"), track, toc.tracks[track].lba, (toc.tracks[track].control & 0x4) ? "DATA" : "AUDIO");
  }

  MDFN_printf("Leadout: %6d\n", toc.tracks[100].lba);
  MDFN_indent(-1);
  MDFN_printf("\n");
 }
 MDFN_indent(-1);

 // Calculate layout MD5.  The system emulation LoadCD() code is free to ignore this value and calculate
 // its own, or to use it to look up a game in its database.
 {
  md5_context layout_md5;

  layout_md5.starts();

  for(unsigned i = 0; i < CDInterfaces.size(); i++)
  {
   CD_TOC toc;

   CDInterfaces[i]->ReadTOC(&toc);

   layout_md5.update_u32_as_lsb(toc.first_track);
   layout_md5.update_u32_as_lsb(toc.last_track);
   layout_md5.update_u32_as_lsb(toc.tracks[100].lba);

   for(uint32 track = toc.first_track; track <= toc.last_track; track++)
   {
    layout_md5.update_u32_as_lsb(toc.tracks[track].lba);
    layout_md5.update_u32_as_lsb(toc.tracks[track].control & 0x4);
   }
  }

  layout_md5.finish(LayoutMD5);
 }

	MDFNGameInfo = NULL;

        for(std::list<MDFNGI *>::iterator it = MDFNSystemsPrio.begin(); it != MDFNSystemsPrio.end(); it++)
        {
         char tmpstr[256];
         trio_snprintf(tmpstr, 256, "%s.enable", (*it)->shortname);

         if(force_module)
         {
          if(!strcmp(force_module, (*it)->shortname))
          {
           MDFNGameInfo = *it;
           break;
          }
         }
         else
         {
          // Is module enabled?
          if(!MDFN_GetSettingB(tmpstr))
           continue; 

          if(!(*it)->LoadCD || !(*it)->TestMagicCD)
           continue;

          if((*it)->TestMagicCD(&CDInterfaces))
          {
           MDFNGameInfo = *it;
           break;
          }
         }
        }

        if(!MDFNGameInfo)
        {
	 if(force_module)
	 {
	  MDFN_PrintError(_("Unrecognized system \"%s\"!"), force_module);
	  return(0);
	 }

	 // This code path should never be taken, thanks to "cdplay"
 	 MDFN_PrintError(_("Could not find a system that supports this CD."));
	 return(0);
        }

	// This if statement will be true if force_module references a system without CDROM support.
        if(!MDFNGameInfo->LoadCD)
	{
         MDFN_PrintError(_("Specified system \"%s\" doesn't support CDs!"), force_module);
	 return(0);
	}

        MDFN_printf(_("Using module: %s(%s)\n\n"), MDFNGameInfo->shortname, MDFNGameInfo->fullname);


 // TODO: include module name in hash
 memcpy(MDFNGameInfo->MD5, LayoutMD5, 16);

 if(!(MDFNGameInfo->LoadCD(&CDInterfaces)))
 {
  for(unsigned i = 0; i < CDInterfaces.size(); i++)
   delete CDInterfaces[i];
  CDInterfaces.clear();

  MDFNGameInfo = NULL;
  return(0);
 }

 MDFNI_SetLayerEnableMask(~0ULL);

 #ifdef WANT_DEBUGGER
 MDFNDBG_PostGameLoad(); 
 #endif

 MDFN_ResetMessages();   // Save state, status messages, etc.

 MDFN_LoadGameCheats(NULL);
 MDFNMP_InstallReadPatches();

  last_sound_rate = -1;
  memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));

 return(MDFNGameInfo);
}
#endif

// Return FALSE on fatal error(IPS file found but couldn't be applied),
// or TRUE on success(IPS patching succeeded, or IPS file not found).
static bool LoadIPS(MDFNFILE &GameFile, const char *path)
{
 return(1);
}

MDFNGI *MDFNI_LoadGame(const char *force_module, const char *name)
{
   MDFNFILE GameFile;
	std::vector<FileExtensionSpecStruct> valid_iae;

#ifdef NEED_CD
	if(strlen(name) > 4 && (!strcasecmp(name + strlen(name) - 4, ".cue") || !strcasecmp(name + strlen(name) - 4, ".toc") || !strcasecmp(name + strlen(name) - 4, ".m3u")))
	 return(MDFNI_LoadCD(force_module, name));
#endif

	MDFNI_CloseGame();

	MDFNGameInfo = NULL;

	MDFN_printf(_("Loading %s...\n"),name);

	MDFN_indent(1);

	// Construct a NULL-delimited list of known file extensions for MDFN_fopen()
	for(unsigned int i = 0; i < MDFNSystems.size(); i++)
	{
	 const FileExtensionSpecStruct *curexts = MDFNSystems[i]->FileExtensions;

	 // If we're forcing a module, only look for extensions corresponding to that module
	 if(force_module && strcmp(MDFNSystems[i]->shortname, force_module))
	  continue;

	 if(curexts)	
 	  while(curexts->extension && curexts->description)
	  {
	   valid_iae.push_back(*curexts);
           curexts++;
 	  }
	}
	{
	 FileExtensionSpecStruct tmpext = { NULL, NULL };
	 valid_iae.push_back(tmpext);
	}

	if(!GameFile.Open(name, &valid_iae[0], _("game")))
        {
	 MDFNGameInfo = NULL;
	 return 0;
	}

	if(!LoadIPS(GameFile, MDFN_MakeFName(MDFNMKF_IPS, 0, 0).c_str()))
	{
	 MDFNGameInfo = NULL;
         GameFile.Close();
         return(0);
	}

	MDFNGameInfo = NULL;

	for(std::list<MDFNGI *>::iterator it = MDFNSystemsPrio.begin(); it != MDFNSystemsPrio.end(); it++)  //_unsigned int x = 0; x < MDFNSystems.size(); x++)
	{
	 char tmpstr[256];
	 trio_snprintf(tmpstr, 256, "%s.enable", (*it)->shortname);

	 if(force_module)
	 {
          if(!strcmp(force_module, (*it)->shortname))
          {
	   if(!(*it)->Load)
	   {
            GameFile.Close();
#ifdef NEED_CD
	    if((*it)->LoadCD)
             MDFN_PrintError(_("Specified system only supports CD(physical, or image files, such as *.cue and *.toc) loading."));
	    else
#endif
             MDFN_PrintError(_("Specified system does not support normal file loading."));
            MDFN_indent(-1);
            MDFNGameInfo = NULL;
            return 0;
	   }
           MDFNGameInfo = *it;
           break;
          }
	 }
	 else
	 {
	  // Is module enabled?
	  if(!MDFN_GetSettingB(tmpstr))
	   continue; 

	  if(!(*it)->Load || !(*it)->TestMagic)
	   continue;

	  if((*it)->TestMagic(name, &GameFile))
	  {
	   MDFNGameInfo = *it;
	   break;
	  }
	 }
	}

        if(!MDFNGameInfo)
        {
	 GameFile.Close();

	 if(force_module)
          MDFN_PrintError(_("Unrecognized system \"%s\"!"), force_module);
	 else
          MDFN_PrintError(_("Unrecognized file format.  Sorry."));

         MDFN_indent(-1);
         MDFNGameInfo = NULL;
         return 0;
        }

	MDFN_printf(_("Using module: %s(%s)\n\n"), MDFNGameInfo->shortname, MDFNGameInfo->fullname);
	MDFN_indent(1);

	assert(MDFNGameInfo->soundchan != 0);

        MDFNGameInfo->soundrate = 0;
        MDFNGameInfo->name = NULL;
        MDFNGameInfo->rotated = 0;


	//
	// Load per-game settings
	//
	// Maybe we should make a "pgcfg" subdir, and automatically load all files in it?
	// End load per-game settings
	//

        if(MDFNGameInfo->Load(name, &GameFile) <= 0)
	{
         GameFile.Close();
         MDFN_indent(-2);
         MDFNGameInfo = NULL;
         return(0);
        }

	MDFN_LoadGameCheats(NULL);
	MDFNMP_InstallReadPatches();

	MDFNI_SetLayerEnableMask(~0ULL);

	#ifdef WANT_DEBUGGER
	MDFNDBG_PostGameLoad();
	#endif

	MDFN_ResetMessages();	// Save state, status messages, etc.

	MDFN_indent(-2);

	if(!MDFNGameInfo->name)
        {
         unsigned int x;
         char *tmp;

         MDFNGameInfo->name = (UTF8 *)strdup(GetFNComponent(name));

         for(x=0;x<strlen((char *)MDFNGameInfo->name);x++)
         {
          if(MDFNGameInfo->name[x] == '_')
           MDFNGameInfo->name[x] = ' ';
         }
         if((tmp = strrchr((char *)MDFNGameInfo->name, '.')))
          *tmp = 0;
        }

#ifdef NEED_DEINTERLACER
	PrevInterlaced = false;
	deint.ClearState();
#endif

        last_sound_rate = -1;
        memset(&last_pixel_format, 0, sizeof(MDFN_PixelFormat));

        return(MDFNGameInfo);
}

#if defined(WANT_PSX_EMU)
extern MDFNGI EmulatedPSX;
#elif defined(WANT_PCE_EMU)
extern MDFNGI EmulatedPCE;
#elif defined(WANT_PCE_FAST_EMU)
extern MDFNGI EmulatedPCE_Fast;
#elif defined(WANT_WSWAN_EMU)
extern MDFNGI EmulatedWSwan;
#endif

bool MDFNI_InitializeModules(const std::vector<MDFNGI *> &ExternalSystems)
{
 static MDFNGI *InternalSystems[] =
 {
  #ifdef WANT_NES_EMU
  &EmulatedNES,
  #endif

  #ifdef WANT_SNES_EMU
  &EmulatedSNES,
  #endif

  #ifdef WANT_GB_EMU
  &EmulatedGB,
  #endif

  #ifdef WANT_GBA_EMU
  &EmulatedGBA,
  #endif

  #ifdef WANT_PCE_EMU
  &EmulatedPCE,
  #endif

  #ifdef WANT_PCE_FAST_EMU
  &EmulatedPCE_Fast,
  #endif

  #ifdef WANT_LYNX_EMU
  &EmulatedLynx,
  #endif

  #ifdef WANT_MD_EMU
  &EmulatedMD,
  #endif

  #ifdef WANT_PCFX_EMU
  &EmulatedPCFX,
  #endif

  #ifdef WANT_NGP_EMU
  &EmulatedNGP,
  #endif

  #ifdef WANT_PSX_EMU
  &EmulatedPSX,
  #endif

  #ifdef WANT_VB_EMU
  &EmulatedVB,
  #endif

  #ifdef WANT_WSWAN_EMU
  &EmulatedWSwan,
  #endif

  #ifdef WANT_SMS_EMU
  &EmulatedSMS,
  &EmulatedGG,
  #endif
 };
 std::string i_modules_string, e_modules_string;

 for(unsigned int i = 0; i < sizeof(InternalSystems) / sizeof(MDFNGI *); i++)
 {
  AddSystem(InternalSystems[i]);
  if(i)
   i_modules_string += " ";
  i_modules_string += std::string(InternalSystems[i]->shortname);
 }

 MDFNI_printf(_("Internal emulation modules: %s\n"), i_modules_string.c_str());

 for(unsigned int i = 0; i < MDFNSystems.size(); i++)
  MDFNSystemsPrio.push_back(MDFNSystems[i]);

 MDFNSystemsPrio.sort(MDFNSystemsPrio_CompareFunc);

#ifdef NEED_CD
 CDUtility::CDUtility_Init();
#endif

 return(1);
}

static std::string settings_file_path;
int MDFNI_Initialize(const char *basedir)
{
#ifdef WANT_DEBUGGER
	MDFNDBG_Init();
#endif

	return(1);
}

void MDFNI_Kill(void)
{
 /* save settings */
}

static inline void ProcessAudio(EmulateSpecStruct *espec)
{
   int16 *const SoundBuf = espec->SoundBuf + espec->SoundBufSizeALMS * MDFNGameInfo->soundchan;
   int32 SoundBufSize = espec->SoundBufSize - espec->SoundBufSizeALMS;
   const int32 SoundBufMaxSize = espec->SoundBufMaxSize - espec->SoundBufSizeALMS;

   espec->SoundBufSize = espec->SoundBufSizeALMS + SoundBufSize;
}

void MDFN_MidSync(EmulateSpecStruct *espec)
{
 ProcessAudio(espec);

 MDFND_MidSync(espec);

 espec->SoundBufSizeALMS = espec->SoundBufSize;
 espec->MasterCyclesALMS = espec->MasterCycles;
}

void MDFNI_Emulate(EmulateSpecStruct *espec)
{
 // Initialize some espec member data to zero, to catch some types of bugs.
 espec->DisplayRect.x = 0;
 espec->DisplayRect.w = 0;
 espec->DisplayRect.y = 0;
 espec->DisplayRect.h = 0;

 assert((bool)(espec->SoundBuf != NULL) == (bool)espec->SoundRate && (bool)espec->SoundRate == (bool)espec->SoundBufMaxSize);

 espec->SoundBufSize = 0;

 espec->VideoFormatChanged = false;
 espec->SoundFormatChanged = false;

 if(memcmp(&last_pixel_format, &espec->surface->format, sizeof(MDFN_PixelFormat)))
 {
  espec->VideoFormatChanged = TRUE;

  last_pixel_format = espec->surface->format;
 }

 if(espec->SoundRate != last_sound_rate)
 {
  espec->SoundFormatChanged = true;
  last_sound_rate = espec->SoundRate;
 }

 espec->NeedSoundReverse = false;

 MDFNGameInfo->Emulate(espec);

#ifdef NEED_DEINTERLACER
 if(espec->InterlaceOn)
 {
  if(!PrevInterlaced)
   deint.ClearState();

  deint.Process(espec->surface, espec->DisplayRect, espec->LineWidths, espec->InterlaceField);

  PrevInterlaced = true;

  espec->InterlaceOn = false;
  espec->InterlaceField = 0;
 }
 else
  PrevInterlaced = false;
#endif

 ProcessAudio(espec);
}

// This function should only be called for state rewinding.
// FIXME:  Add a macro for SFORMAT structure access instead of direct access
int MDFN_RawInputStateAction(StateMem *sm, int load, int data_only)
{
 static const char *stringies[16] = { "RI00", "RI01", "RI02", "RI03", "RI04", "RI05", "RI06", "RI07", "RI08", "RI09", "RI0a", "RI0b", "RI0c", "RI0d", "RI0e", "RI0f" };
 SFORMAT StateRegs[17];
 int x;

 for(x = 0; x < 16; x++)
 {
  StateRegs[x].name = stringies[x];
  StateRegs[x].flags = 0;

  StateRegs[x].v = NULL;
  StateRegs[x].size = 0;
 }

 StateRegs[x].v = NULL;
 StateRegs[x].size = 0;
 StateRegs[x].name = NULL;

 int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "rinp");

 return(ret);
}

static int curindent = 0;

void MDFN_indent(int indent)
{
 curindent += indent;
}

static uint8 lastchar = 0;
void MDFN_printf(const char *format, ...) throw()
{
 char *format_temp;
 char *temp;
 unsigned int x, newlen;

 va_list ap;
 va_start(ap,format);


 // First, determine how large our format_temp buffer needs to be.
 uint8 lastchar_backup = lastchar; // Save lastchar!
 for(newlen=x=0;x<strlen(format);x++)
 {
  if(lastchar == '\n' && format[x] != '\n')
  {
   int y;
   for(y=0;y<curindent;y++)
    newlen++;
  }
  newlen++;
  lastchar = format[x];
 }

 format_temp = (char *)malloc(newlen + 1); // Length + NULL character, duh
 
 // Now, construct our format_temp string
 lastchar = lastchar_backup; // Restore lastchar
 for(newlen=x=0;x<strlen(format);x++)
 {
  if(lastchar == '\n' && format[x] != '\n')
  {
   int y;
   for(y=0;y<curindent;y++)
    format_temp[newlen++] = ' ';
  }
  format_temp[newlen++] = format[x];
  lastchar = format[x];
 }

 format_temp[newlen] = 0;

 temp = trio_vaprintf(format_temp, ap);
 free(format_temp);

 MDFND_Message(temp);
 free(temp);

 va_end(ap);
}

void MDFN_PrintError(const char *format, ...) throw()
{
 char *temp;

 va_list ap;

 va_start(ap, format);

 temp = trio_vaprintf(format, ap);
 MDFND_PrintError(temp);
 free(temp);

 va_end(ap);
}

void MDFN_DebugPrintReal(const char *file, const int line, const char *format, ...)
{
 char *temp;

 va_list ap;

 va_start(ap, format);

 temp = trio_vaprintf(format, ap);
 printf("%s:%d  %s\n", file, line, temp);
 free(temp);

 va_end(ap);
}

void MDFN_DoSimpleCommand(int cmd)
{
 MDFNGameInfo->DoSimpleCommand(cmd);
}

void MDFN_QSimpleCommand(int cmd)
{
}

void MDFNI_Power(void)
{
 assert(MDFNGameInfo);

 MDFN_QSimpleCommand(MDFN_MSC_POWER);
}

void MDFNI_Reset(void)
{
 assert(MDFNGameInfo);

 MDFN_QSimpleCommand(MDFN_MSC_RESET);
}

// Arcade-support functions
void MDFNI_ToggleDIPView(void)
{

}

void MDFNI_ToggleDIP(int which)
{
 assert(MDFNGameInfo);
 assert(which >= 0);

 MDFN_QSimpleCommand(MDFN_MSC_TOGGLE_DIP0 + which);
}

void MDFNI_InsertCoin(void)
{
 assert(MDFNGameInfo);
 
 MDFN_QSimpleCommand(MDFN_MSC_INSERT_COIN);
}

// Disk/Disc-based system support functions
void MDFNI_DiskInsert(int which)
{
 assert(MDFNGameInfo);

 MDFN_QSimpleCommand(MDFN_MSC_INSERT_DISK0 + which);
}

void MDFNI_DiskSelect()
{
 assert(MDFNGameInfo);

 MDFN_QSimpleCommand(MDFN_MSC_SELECT_DISK);
}

void MDFNI_DiskInsert()
{
 assert(MDFNGameInfo);

 MDFN_QSimpleCommand(MDFN_MSC_INSERT_DISK);
}

void MDFNI_DiskEject()
{
 assert(MDFNGameInfo);

 MDFN_QSimpleCommand(MDFN_MSC_EJECT_DISK);
}

void MDFNI_SetLayerEnableMask(uint64 mask)
{
 if(MDFNGameInfo && MDFNGameInfo->SetLayerEnableMask)
 {
  MDFNGameInfo->SetLayerEnableMask(mask);
 }
}

void MDFNI_SetInput(int port, const char *type, void *ptr, uint32 ptr_len_thingy)
{
  MDFNGameInfo->SetInput(port, type, ptr);
}
