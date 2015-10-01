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

#include <sys/stat.h>
#include "mednafen.h"
#include "FileWrapper.h"

#include "include/trio/trio.h"
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

// Some really bad preprocessor abuse follows to handle platforms that don't have fseeko and ftello...and of course
// for largefile support on Windows:

#define fseeko fseek
#define ftello ftell

// For special uses, IE in classes that take a path or a FileWrapper & in the constructor, and the FileWrapper non-pointer member
// is in the initialization list for the path constructor but not the constructor with FileWrapper&

FileWrapper::FileWrapper(const char *path, const int mode, const char *purpose) : OpenedMode(mode)
{
   if(!(fp = fopen(path, (mode == MODE_WRITE) ? "wb" : "rb")))
   {
      ErrnoHolder ene(errno);

      printf("%s\n", path);

      throw(MDFN_Error(ene.Errno(), _("Error opening file %s"), ene.StrError()));
   }
}

FileWrapper::~FileWrapper()
{
   close();
}

void FileWrapper::close(void)
{
   if(!fp)
      return;

   FILE *tmp = fp;
   fp = NULL;
   fclose(tmp);
}

uint64_t FileWrapper::read(void *data, uint64_t count, bool error_on_eof)
{
   return fread(data, 1, count, fp);
}

void FileWrapper::write(const void *data, uint64_t count)
{
   fwrite(data, 1, count, fp);
}

char *FileWrapper::get_line(char *buf_s, int buf_size)
{
   return ::fgets(buf_s, buf_size, fp);
}


void FileWrapper::seek(int64_t offset, int whence)
{
   fseeko(fp, offset, whence);
}

int64_t FileWrapper::size(void)
{
   struct stat buf;

   fstat(fileno(fp), &buf);

   return(buf.st_size);
}

int64_t FileWrapper::tell(void)
{
   return ftello(fp);
}
