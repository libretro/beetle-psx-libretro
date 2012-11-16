// TODO/WIP

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
#include "Stream.h"
#include "FileStream.h"

#include <trio/trio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#ifdef __CELLOS_LV2__
#include <unistd.h>
#endif

#define fseeko fseek
#define ftello ftell

#if SIZEOF_OFF_T == 4

 #ifdef HAVE_FOPEN64
  #define fopen fopen64
 #endif

 #ifdef HAVE_FTELLO64
  #undef ftello
  #define ftello ftello64
 #endif

 #ifdef HAVE_FSEEKO64
  #undef fseeko
  #define fseeko fseeko64
 #endif

 #ifdef HAVE_FSTAT64
  #define fstat fstat64
  #define stat stat64
 #endif

#endif


FileStream::FileStream(const char *path, const int mode): OpenedMode(mode)
{
 path_save = std::string(path);

 if(mode == MODE_WRITE)
  fp = fopen(path, "wb");
 else
  fp = fopen(path, "rb");

 if(!fp)
 {
  ErrnoHolder ene(errno);

  throw(MDFN_Error(ene.Errno(), _("Error opening file \"%s\": %s"), path_save.c_str(), ene.StrError()));
 }
}

FileStream::~FileStream()
{
}

uint64 FileStream::attributes(void)
{
 uint64 ret = ATTRIBUTE_SEEKABLE;

 switch(OpenedMode)
 {
  case MODE_READ:
	ret |= ATTRIBUTE_READABLE;
	break;
  case MODE_WRITE_SAFE:
  case MODE_WRITE:
	ret |= ATTRIBUTE_WRITEABLE;
	break;
 }

 return ret;
}

uint64 FileStream::read(void *data, uint64 count, bool error_on_eos)
{
 uint64 read_count = fread(data, 1, count, fp);

 return(read_count);
}

void FileStream::write(const void *data, uint64 count)
{
 fwrite(data, 1, count, fp);
}

void FileStream::seek(int64 offset, int whence)
{
 fseeko(fp, offset, whence);
}

int64 FileStream::tell(void)
{
 return ftello(fp);
}

int64 FileStream::size(void)
{
 struct stat buf;

 fstat(fileno(fp), &buf);

 return(buf.st_size);
}

void FileStream::close(void)
{
 if(fp)
 {
  FILE *tmp = fp;

  fp = NULL;

  if(fclose(tmp) == EOF)
  {
   ErrnoHolder ene(errno);

   throw(MDFN_Error(ene.Errno(), _("Error closing opened file \"%s\": %s"), path_save.c_str(), ene.StrError()));
  }
 }
}
