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
#include	"file.h"
#include	"FileWrapper.h"

#ifdef NEED_CD
#include	"cdrom/cdromif.h"
#include	"cdrom/CDUtility.h"
#endif

#include	"mempatcher.h"
#include	"md5.h"
#include	"clamp.h"

#ifdef _MSC_VER
#include "msvc_compat.h"
#endif

MDFNGI *MDFNGameInfo = NULL;

#if defined(WANT_NES_EMU)
extern MDFNGI EmulatedNES;
#define MDFNGI_CORE &EmulatedNES
#elif defined WANT_SNES_EMU
extern MDFNGI EmulatedSNES;
#define MDFNGI_CORE &EmulatedSNES
extern MDFNGI EmulatedGB;
#elif defined WANT_GB_EMU
#define MDFNGI_CORE &EmulatedGB
#elif defined WANT_GBA_EMU
extern MDFNGI EmulatedGBA;
#define MDFNGI_CORE &EmulatedGBA
#elif defined WANT_PCE_EMU
extern MDFNGI EmulatedPCE;
#define MDFNGI_CORE &EmulatedPCE
#elif defined WANT_PCE_FAST_EMU
extern MDFNGI EmulatedPCE_Fast;
#define MDFNGI_CORE &EmulatedPCE_Fast
#elif defined WANT_LYNX_EMU
extern MDFNGI EmulatedLynx;
#define MDFNGI_CORE &EmulatedLynx
#elif defined WANT_MD_EMU
extern MDFNGI EmulatedMD;
#define MDFNGI_CORE &EmulatedMD
#elif defined WANT_PCFX_EMU
extern MDFNGI EmulatedPCFX;
#define MDFNGI_CORE &EmulatedPCFX
#elif defined WANT_NGP_EMU
extern MDFNGI EmulatedNGP;
#define MDFNGI_CORE &EmulatedNGP
#elif defined WANT_PSX_EMU
extern MDFNGI EmulatedPSX;
#define MDFNGI_CORE &EmulatedPSX
#elif defined WANT_VB_EMU
extern MDFNGI EmulatedVB;
#define MDFNGI_CORE &EmulatedVB
#elif defined WANT_WSWAN_EMU
extern MDFNGI EmulatedWSwan;
#define MDFNGI_CORE &EmulatedWSwan
#elif defined WANT_SMS_EMU
extern MDFNGI EmulatedSMS;
#define MDFNGI_CORE &EmulatedSMS
#elif defined(WANT_SMS_EMU) && defined(WANT_GG_EMU)
extern MDFNGI EmulatedGG;
#define MDFNGI_CORE &EmulatedGG
#endif


/* forward declarations */
extern void MDFND_DispMessage(unsigned char *str);

void MDFN_DispMessage(const char *format, ...)
{
 va_list ap;
 va_start(ap,format);
 char *msg = NULL;

 trio_vasprintf(&msg, format,ap);
 va_end(ap);

 MDFND_DispMessage((UTF8*)msg);
}

void MDFN_ResetMessages(void)
{
 MDFND_DispMessage(NULL);
}


#ifdef NEED_CD
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
#ifdef NEED_CD
 static std::vector<CDIF *> CDInterfaces;	// FIXME: Cleanup on error out.
#endif

 MDFN_printf(_("Loading %s...\n\n"), devicename ? devicename : _("PHYSICAL CD"));

 try
 {
  if(devicename && strlen(devicename) > 4 && !strcasecmp(devicename + strlen(devicename) - 4, ".m3u"))
  {
   std::vector<std::string> file_list;

   ReadM3U(file_list, devicename);

   for(unsigned i = 0; i < file_list.size(); i++)
   {
    CDInterfaces.push_back(CDIF_Open(file_list[i].c_str(), false, false /* cdimage_memcache */));
   }
  }
  else
  {
   CDInterfaces.push_back(CDIF_Open(devicename, false, false /* cdimage_memcache */));
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

 //MDFNI_SetLayerEnableMask(~0ULL);

 #ifdef WANT_DEBUGGER
 MDFNDBG_PostGameLoad(); 
 #endif

 MDFN_ResetMessages();   // Save state, status messages, etc.

 MDFN_LoadGameCheats(NULL);
 MDFNMP_InstallReadPatches();

 return(MDFNGameInfo);
}
#endif

MDFNGI *MDFNI_LoadGame(const char *force_module, const char *name)
{
   MDFNFILE GameFile;
	std::vector<FileExtensionSpecStruct> valid_iae;
   MDFNGameInfo = MDFNGI_CORE;

#ifdef NEED_CD
	if(strlen(name) > 4 && (!strcasecmp(name + strlen(name) - 4, ".cue") || !strcasecmp(name + strlen(name) - 4, ".toc") || !strcasecmp(name + strlen(name) - 4, ".m3u")))
	 return(MDFNI_LoadCD(force_module, name));
#endif

	MDFN_printf(_("Loading %s...\n"),name);

	MDFN_indent(1);

	// Construct a NULL-delimited list of known file extensions for MDFN_fopen()
   const FileExtensionSpecStruct *curexts = MDFNGameInfo->FileExtensions;

   while(curexts->extension && curexts->description)
   {
      valid_iae.push_back(*curexts);
      curexts++;
   }

	if(!GameFile.Open(name, &valid_iae[0], _("game")))
   {
      MDFNGameInfo = NULL;
      return 0;
   }

	MDFN_printf(_("Using module: %s(%s)\n\n"), MDFNGameInfo->shortname, MDFNGameInfo->fullname);
	MDFN_indent(1);

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

   return(MDFNGameInfo);
}


bool MDFNI_InitializeModule(void)
{

#ifdef NEED_CD
 CDUtility::CDUtility_Init();
#endif

 return(1);
}

int MDFNI_Initialize(const char *basedir)
{
#ifdef WANT_DEBUGGER
	MDFNDBG_Init();
#endif

	return(1);
}

static int curindent = 0;

void MDFN_indent(int indent)
{
 curindent += indent;
}

static uint8 lastchar = 0;

void MDFN_printf(const char *format, ...)
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

void MDFN_PrintError(const char *format, ...)
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
 fprintf(stderr, "%s:%d  %s\n", file, line, temp);
 free(temp);

 va_end(ap);
}
