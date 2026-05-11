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

#include <stddef.h>
#include <string.h>

#include <file/file_path.h>

#include "general_c.h"

static void copy_truncate(char *out, size_t outlen, const char *src,
      size_t srclen)
{
   if (!out || outlen == 0)
      return;
   if (srclen >= outlen)
      srclen = outlen - 1;
   memcpy(out, src, srclen);
   out[srclen] = 0;
}

void MDFN_GetFilePathComponents_c(const char *file_path,
      char *dir_out,  size_t dir_outlen,
      char *base_out, size_t base_outlen,
      char *ext_out,  size_t ext_outlen)
{
   const char *p_slash;
   const char *p_dot;
   const char *file_name;
   size_t      file_path_len = strlen(file_path);
   size_t      dir_len, base_len, ext_len;

#ifdef _WIN32
   {
      const char *p_back = strrchr(file_path, '\\');
      const char *p_fwd  = strrchr(file_path, '/');
      p_slash = (p_back && (!p_fwd || p_fwd < p_back)) ? p_back : p_fwd;
   }
#else
   p_slash = strrchr(file_path, '/');
#endif

   if (!p_slash)
   {
      copy_truncate(dir_out, dir_outlen, ".", 1);
      file_name = file_path;
      dir_len   = 1;
      (void)dir_len;
   }
   else
   {
      dir_len   = (size_t)(p_slash - file_path);
      copy_truncate(dir_out, dir_outlen, file_path, dir_len);
      file_name = p_slash + 1;
   }

   p_dot = strrchr(file_name, '.');

   if (p_dot)
   {
      base_len = (size_t)(p_dot - file_name);
      ext_len  = file_path_len - (size_t)(p_dot - file_path);
      copy_truncate(base_out, base_outlen, file_name, base_len);
      copy_truncate(ext_out,  ext_outlen,  p_dot,     ext_len);
   }
   else
   {
      base_len = strlen(file_name);
      copy_truncate(base_out, base_outlen, file_name, base_len);
      copy_truncate(ext_out,  ext_outlen,  "",        0);
   }
}

void MDFN_EvalFIP_c(const char *dir_path, const char *rel_path,
      char *out, size_t outlen)
{
#ifdef _WIN32
   const char slash = '\\';
#else
   const char slash = '/';
#endif
   size_t dlen, rlen, total;

   if (!out || outlen == 0)
      return;

   if (path_is_absolute(rel_path))
   {
      copy_truncate(out, outlen, rel_path, strlen(rel_path));
      return;
   }

   dlen  = strlen(dir_path);
   rlen  = strlen(rel_path);
   total = dlen + 1 + rlen;
   if (total >= outlen)
      total = outlen - 1;

   if (dlen >= outlen)
      dlen = outlen - 1;
   memcpy(out, dir_path, dlen);
   {
      size_t pos = dlen;
      if (pos + 1 < outlen)
         out[pos++] = slash;
      if (pos < outlen)
      {
         size_t can = outlen - 1 - pos;
         if (rlen > can)
            rlen = can;
         memcpy(out + pos, rel_path, rlen);
         pos += rlen;
      }
      if (pos >= outlen)
         pos = outlen - 1;
      out[pos] = 0;
   }
}

static int is_trim_ws(char c)
{
   return (c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == 0x0b);
}

void MDFN_trim_c(char *str)
{
   size_t len, di, si;
   int    seen_nonws;

   if (!str)
      return;

   /* rtrim */
   len = strlen(str);
   while (len > 0 && is_trim_ws(str[len - 1]))
   {
      len--;
      str[len] = 0;
   }

   /* ltrim - shift left */
   di = si = 0;
   seen_nonws = 0;
   while (str[si])
   {
      if (!seen_nonws && is_trim_ws(str[si]))
      {
         si++;
         continue;
      }
      seen_nonws = 1;
      str[di++] = str[si++];
   }
   str[di] = 0;
}

void MDFN_strtoupper_c(char *str)
{
   if (!str)
      return;
   for (; *str; str++)
   {
      if (*str >= 'a' && *str <= 'z')
         *str = (char)(*str - 'a' + 'A');
   }
}
