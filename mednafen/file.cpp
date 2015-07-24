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
#include <errno.h>

#include "file.h"
#include "general.h"

// This function should ALWAYS close the system file "descriptor"(gzip library, zip library, or FILE *) it's given,
// even if it errors out.
bool MDFNFILE::MakeMemWrapAndClose(void *fp)
{
   bool ret = FALSE;

   location = 0;

   ::fseek((FILE *)fp, 0, SEEK_END);
   f_size = ::ftell((FILE *)fp);
   ::fseek((FILE *)fp, 0, SEEK_SET);

   if (!(f_data = (uint8*)malloc((size_t)f_size)))
      goto fail;
   ::fread(f_data, 1, (size_t)f_size, (FILE *)fp);

   ret = TRUE;
fail:
   fclose((FILE*)fp);
   return ret;
}

MDFNFILE::MDFNFILE()
{
   f_data = NULL;
   f_size = 0;
   f_ext = NULL;

   location = 0;
}

MDFNFILE::MDFNFILE(const char *path, const void *known_ext, const char *purpose)
{
   (void)known_ext;
   if (!Open(path, known_ext, purpose, false))
      throw(MDFN_Error(0, "TODO ERROR"));
}


MDFNFILE::~MDFNFILE()
{
   Close();
}


bool MDFNFILE::Open(const char *path, const void *known_ext, const char *purpose, const bool suppress_notfound_pe)
{
   FILE *fp;
   (void)known_ext;

   if (!(fp = fopen(path, "rb")))
      return FALSE;

   ::fseek(fp, 0, SEEK_SET);

   if (!MakeMemWrapAndClose(fp))
      return FALSE;

   const char *ld = (const char*)strrchr(path, '.');
   f_ext = strdup(ld ? ld + 1 : "");

   return(TRUE);
}

bool MDFNFILE::Close(void)
{
   if (f_ext)
      free(f_ext);
   f_ext = 0;

   if (f_data)
      free(f_data);
   f_data = 0;

   return(1);
}

uint64 MDFNFILE::fread(void *ptr, size_t element_size, size_t nmemb)
{
   uint32 total = element_size * nmemb;

   if (location >= f_size)
      return 0;

   if ((location + total) > f_size)
   {
      size_t ak = f_size - location;

      memcpy((uint8*)ptr, f_data + location, ak);

      location = f_size;

      return(ak / element_size);
   }

   memcpy((uint8*)ptr, f_data + location, total);

   location += total;

   return nmemb;
}

int MDFNFILE::fseek(int64 offset, int whence)
{
   switch(whence)
   {
      case SEEK_SET:
         if (offset >= f_size)
            return -1;

         location = offset;
         break;
      case SEEK_CUR:
         if ((offset + location) > f_size)
            return -1;

         location += offset;
         break;
   }    

   return 0;
}

int MDFNFILE::read16le(uint16 *val)
{
   if ((location + 2) > f_size)
      return 0;

   *val = MDFN_de16lsb(f_data + location);

   location += 2;

   return 1;
}

int MDFNFILE::read32le(uint32 *val)
{
   if ((location + 4) > f_size)
      return 0;

   *val = MDFN_de32lsb(f_data + location);

   location += 4;

   return 1;
}

char *MDFNFILE::fgets(char *s, int buffer_size)
{
   int pos = 0;

   if (!buffer_size)
      return(NULL);

   if (location >= buffer_size)
      return(NULL);

   while(pos < (buffer_size - 1) && location < buffer_size)
   {
      int v = f_data[location];
      s[pos] = v;
      location++;
      pos++;
      if (v == '\n')
         break;
   }

   if (buffer_size)
      s[pos] = 0;

   return s;
}

static INLINE bool MDFN_DumpToFileReal(const char *filename, int compress, const std::vector<PtrLengthPair> &pearpairs)
{
   FILE *fp = fopen(filename, "wb");

   if (!fp)
      return 0;

   for(unsigned int i = 0; i < pearpairs.size(); i++)
   {
      const void *data = pearpairs[i].GetData();
      const uint64 length = pearpairs[i].GetLength();

      if (fwrite(data, 1, length, fp) != length)
      {
         fclose(fp);
         return 0;
      }
   }

   if (fclose(fp) == EOF)
      return 0;

   return 1;
}

bool MDFN_DumpToFile(const char *filename, int compress, const std::vector<PtrLengthPair> &pearpairs)
{
   return (MDFN_DumpToFileReal(filename, compress, pearpairs));
}

bool MDFN_DumpToFile(const char *filename, int compress, const void *data, uint64 length)
{
   std::vector<PtrLengthPair> tmp_pairs;
   tmp_pairs.push_back(PtrLengthPair(data, length));
   return (MDFN_DumpToFileReal(filename, compress, tmp_pairs));
}
