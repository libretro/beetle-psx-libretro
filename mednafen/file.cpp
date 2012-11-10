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
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <trio/trio.h>

#include "file.h"
#include "general.h"

#ifndef __GNUC__
 #define strcasecmp strcmp
#endif

static const int64 MaxROMImageSize = (int64)1 << 26; // 2 ^ 26 = 64MiB

enum
{
 MDFN_FILETYPE_PLAIN = 0,
};

bool MDFNFILE::ApplyIPS(FILE *ips)
{
 uint8 header[5];
 uint32 count = 0;
 
 //MDFN_printf(_("Applying IPS file \"%s\"...\n"), path);

 MDFN_indent(1);
 if(::fread(header, 1, 5, ips) != 5)
 {
  ErrnoHolder ene(errno);

  MDFN_PrintError(_("Error reading IPS file header: %s"), ene.StrError());
  MDFN_indent(-1);
  return(0);
 }

 if(memcmp(header, "PATCH", 5))
 {
  MDFN_PrintError(_("IPS file header is invalid."));
  MDFN_indent(-1);
  return(0);
 }

 while(::fread(header, 1, 3, ips) == 3)
 {
  uint32 offset = (header[0] << 16) | (header[1] << 8) | header[2];
  uint8 patch_size_raw[2];
  uint32 patch_size;
  bool rle = false;

  if(!memcmp(header, "EOF", 3))
  {
   MDFN_printf(_("IPS EOF:  Did %d patches\n\n"), count);
   MDFN_indent(-1);
   return(1);
  }

  if(::fread(patch_size_raw, 1, 2, ips) != 2)
  {
   ErrnoHolder ene(errno);
   MDFN_PrintError(_("Error reading IPS patch length: %s"), ene.StrError());
   return(0);
  }

  patch_size = MDFN_de16msb(patch_size_raw);

  if(!patch_size)	/* RLE */
  {
   if(::fread(patch_size_raw, 1, 2, ips) != 2)
   {
    ErrnoHolder ene(errno);
    MDFN_PrintError(_("Error reading IPS RLE patch length: %s"), ene.StrError());
    return(0);
   }

   patch_size = MDFN_de16msb(patch_size_raw);

   // Is this right?
   if(!patch_size)
    patch_size = 65536;

   rle = true;
   //MDFN_printf("  Offset: %8d  Size: %5d RLE\n",offset, patch_size);
  }

  if((offset + patch_size) > f_size)
  {
   uint8 *tmp;

   //printf("%d\n", offset + patch_size, f_size);

   if((offset + patch_size) > MaxROMImageSize)
   {
    MDFN_PrintError(_("ROM image will be too large after IPS patch; maximum size allowed is %llu bytes."), (unsigned long long)MaxROMImageSize);
    return(0);
   }

   if(!(tmp = (uint8 *)MDFN_realloc(f_data, offset + patch_size, _("file read buffer"))))
    return(0);

   // Zero newly-allocated memory
   memset(tmp + f_size, 0, (offset + patch_size) - f_size);

   f_size = offset + patch_size;
   f_data = tmp;
  }


  if(rle)
  {
   const int b = ::fgetc(ips);
   uint8 *start = f_data + offset;

   if(EOF == b)
   {
    ErrnoHolder ene(errno);

    MDFN_PrintError(_("Error reading IPS RLE patch byte: %s"), ene.StrError());

    return(0);
   }

   while(patch_size--)
   {
    *start=b;
    start++;
   }

  }
  else		/* Normal patch */
  {
   //MDFN_printf("  Offset: %8d  Size: %5d\n", offset, patch_size);
   if(::fread(f_data + offset, 1, patch_size, ips) != patch_size)
   {
    ErrnoHolder ene(errno);

    MDFN_PrintError(_("Error reading IPS patch: %s"), ene.StrError());
    return(0);
   }
  }
  count++;
 }
 ErrnoHolder ene(errno);

 //MDFN_printf(_("Warning:  IPS ended without an EOF chunk.\n"));
 //MDFN_printf(_("IPS EOF:  Did %d patches\n\n"), count);
 MDFN_indent(-1);

 MDFN_PrintError(_("Error reading IPS patch header: %s"), ene.StrError());
 return(0);

 //return(1);
}

// This function should ALWAYS close the system file "descriptor"(gzip library, zip library, or FILE *) it's given,
// even if it errors out.
bool MDFNFILE::MakeMemWrapAndClose(void *tz, int type)
{
 bool ret = FALSE;

 location = 0;

  ::fseek((FILE *)tz, 0, SEEK_END);
  f_size = ::ftell((FILE *)tz);
  ::fseek((FILE *)tz, 0, SEEK_SET);

  if(size > MaxROMImageSize)
  {
   MDFN_PrintError(_("ROM image is too large; maximum size allowed is %llu bytes."), (unsigned long long)MaxROMImageSize);
   goto doret;
  }

   if(!(f_data = (uint8*)MDFN_malloc(size, _("file read buffer"))))
   {
    goto doret;
   }
   if((int64)::fread(f_data, 1, size, (FILE *)tz) != size)
   {
    ErrnoHolder ene(errno);
    MDFN_PrintError(_("Error reading file: %s"), ene.StrError());

    free(f_data);
    goto doret;
   }

 ret = TRUE;

 doret:
  fclose((FILE *)tz);

 return(ret);
}

MDFNFILE::MDFNFILE() : size(f_size), data((const uint8* const &)f_data), ext((const char * const &)f_ext)
{
 f_data = NULL;
 f_size = 0;
 f_ext = NULL;

 location = 0;
}

MDFNFILE::MDFNFILE(const char *path, const FileExtensionSpecStruct *known_ext, const char *purpose) : size(f_size), data((const uint8* const &)f_data), ext((const char * const &)f_ext)
{
 if(!Open(path, known_ext, purpose, false))
 {
  throw(MDFN_Error(0, "TODO ERROR"));
 }
}


MDFNFILE::~MDFNFILE()
{
 Close();
}


bool MDFNFILE::Open(const char *path, const FileExtensionSpecStruct *known_ext, const char *purpose, const bool suppress_notfound_pe)
{
 local_errno = 0;
 error_code = MDFNFILE_EC_OTHER;	// Set to 0 at the end if the function succeeds.

 //f_data = (uint8 *)0xDEADBEEF;

 {
  FILE *fp;

  if(!(fp = fopen(path, "rb")))
  {
   ErrnoHolder ene(errno);
   local_errno = ene.Errno();

   if(ene.Errno() == ENOENT)
   {
    local_errno = ene.Errno();
    error_code = MDFNFILE_EC_NOTFOUND;
   }

   if(ene.Errno() != ENOENT || !suppress_notfound_pe)
    MDFN_PrintError(_("Error opening \"%s\": %s"), path, ene.StrError());

   return(0);
  }

   ::fseek(fp, 0, SEEK_SET);

   if(!MakeMemWrapAndClose(fp, MDFN_FILETYPE_PLAIN))
    return(0);

   const char *ld = strrchr(path, '.');
   f_ext = strdup(ld ? ld + 1 : "");
 } // End normal and gzip file handling else to zip

 // FIXME:  Handle extension fixing for cases where loaded filename is like "moo.moo/lalala"

 error_code = 0;

 return(TRUE);
}

bool MDFNFILE::Close(void)
{
 if(f_ext)
 {
  free(f_ext);
  f_ext = NULL;
 }

 if(f_data)
 {
   free(f_data);
  f_data = NULL;
 }

 return(1);
}

uint64 MDFNFILE::fread(void *ptr, size_t element_size, size_t nmemb)
{
 uint32 total = element_size * nmemb;

 if(location >= f_size)
  return 0;

 if((location + total) > f_size)
 {
  int64 ak = f_size - location;

  memcpy((uint8*)ptr, f_data + location, ak);

  location = f_size;

  return(ak / element_size);
 }
 else
 {
  memcpy((uint8*)ptr, f_data + location, total);

  location += total;

  return nmemb;
 }
}

int MDFNFILE::fseek(int64 offset, int whence)
{
  switch(whence)
  {
   case SEEK_SET:if(offset >= f_size)
                  return(-1);
                 location = offset;
		 break;

   case SEEK_CUR:if((offset + location) > f_size)
                  return(-1);

                 location += offset;
                 break;
  }    
  return 0;
}

int MDFNFILE::read16le(uint16 *val)
{
 if((location + 2) > size)
  return 0;

 *val = MDFN_de16lsb(data + location);

 location += 2;

 return(1);
}

int MDFNFILE::read32le(uint32 *val)
{
 if((location + 4) > size)
  return 0;

 *val = MDFN_de32lsb(data + location);

 location += 4;

 return(1);
}

char *MDFNFILE::fgets(char *s, int buffer_size)
{
 int pos = 0;

 if(!buffer_size)
  return(NULL);

 if(location >= buffer_size)
  return(NULL);

 while(pos < (buffer_size - 1) && location < buffer_size)
 {
  int v = data[location];
  s[pos] = v;
  location++;
  pos++;
  if(v == '\n') break;
 }

 if(buffer_size)
  s[pos] = 0;

 return(s);
}

static INLINE bool MDFN_DumpToFileReal(const char *filename, int compress, const std::vector<PtrLengthPair> &pearpairs)
{
  FILE *fp = fopen(filename, "wb");
  if(!fp)
  {
   ErrnoHolder ene(errno);

   MDFN_PrintError(_("Error opening \"%s\": %s"), filename, ene.StrError());
   return(0);
  }

  for(unsigned int i = 0; i < pearpairs.size(); i++)
  {
   const void *data = pearpairs[i].GetData();
   const uint64 length = pearpairs[i].GetLength();

   if(fwrite(data, 1, length, fp) != length)
   {
    ErrnoHolder ene(errno);

    MDFN_PrintError(_("Error writing to \"%s\": %s"), filename, ene.StrError());
    fclose(fp);
    return(0);
   }
  }

  if(fclose(fp) == EOF)
  {
   ErrnoHolder ene(errno);

   MDFN_PrintError(_("Error closing \"%s\": %s"), filename, ene.StrError());
   return(0);
  }

 return(1);
}

bool MDFN_DumpToFile(const char *filename, int compress, const std::vector<PtrLengthPair> &pearpairs)
{
 return(MDFN_DumpToFileReal(filename, compress, pearpairs));
}

bool MDFN_DumpToFile(const char *filename, int compress, const void *data, uint64 length)
{
 std::vector<PtrLengthPair> tmp_pairs;
 tmp_pairs.push_back(PtrLengthPair(data, length));
 return(MDFN_DumpToFileReal(filename, compress, tmp_pairs));
}
