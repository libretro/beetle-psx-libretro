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

#include <sys/stat.h>
#include "mednafen.h"
#include "Stream.h"
#include "FileStream.h"

#include <trio/trio.h>
#include <stdarg.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define fseeko fseek
#define ftello ftell

FileStream::FileStream(const char *path, const int mode): OpenedMode(mode)
{
 if(mode == MODE_WRITE)
  fp = fopen(path, "wb");
 else
  fp = fopen(path, "rb");

 if(!fp)
 {
  ErrnoHolder ene(errno);

  throw(MDFN_Error(ene.Errno(), _("Error opening file %s"), ene.StrError()));
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

   throw(MDFN_Error(ene.Errno(), _("Error closing opened file %s"), ene.StrError()));
  }
 }
}
