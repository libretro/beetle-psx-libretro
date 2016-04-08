/* Copyright  (C) 2010-2016 The RetroArch team
 *
 * ---------------------------------------------------------------------------------------
 * The following license statement only applies to this file (msvc_compat.h).
 * ---------------------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge,
 * to any person obtaining a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __LIBRETRO_SDK_COMPAT_MSVC_H
#define __LIBRETRO_SDK_COMPAT_MSVC_H

#ifdef _MSC_VER

#ifdef __cplusplus
extern "C"  {
#endif

/* Pre-MSVC 2015 compilers don't implement snprintf in a cross-platform manner. */
#if _MSC_VER < 1900
   #include <stdio.h>
   #include <stdarg.h>
   #define snprintf c99_snprintf
   #define vsnprintf c99_vsnprintf

   _inline int c99_vsnprintf(char *outBuf, size_t size, const char *format, va_list ap)
   {
      int count = -1;

      if (size != 0)
         count = _vsnprintf_s(outBuf, size, _TRUNCATE, format, ap);
      if (count == -1)
         count = _vscprintf(format, ap);

      return count;
   }

   _inline int c99_snprintf(char *outBuf, size_t size, const char *format, ...)
   {
      int count;
      va_list ap;

      va_start(ap, format);
      count = c99_vsnprintf(outBuf, size, format, ap);
      va_end(ap);

      return count;
}
#endif

#ifdef __cplusplus
}
#endif

#undef UNICODE /* Do not bother with UNICODE at this time. */
#include <direct.h>
#include <stddef.h>
#include <math.h>

/* Python headers defines ssize_t and sets HAVE_SSIZE_T.
 * Cannot duplicate these efforts.
 */
#ifndef HAVE_SSIZE_T
#if defined(_WIN64)
typedef __int64 ssize_t;
#elif defined(_WIN32)
typedef int ssize_t;
#endif
#endif

#define mkdir(dirname, unused) _mkdir(dirname)
#define strtoull _strtoui64
#undef strcasecmp
#define strcasecmp _stricmp
#undef strncasecmp
#define strncasecmp _strnicmp

/* Disable some of the annoying warnings. */
#pragma warning(disable : 4800)
#pragma warning(disable : 4805)
#pragma warning(disable : 4244)
#pragma warning(disable : 4305)
#pragma warning(disable : 4146)
#pragma warning(disable : 4267)
#pragma warning(disable : 4723)
#pragma warning(disable : 4996)

/* roundf is available since MSVC 2013 */
#if _MSC_VER < 1800
#define roundf(in) (in >= 0.0f ? floorf(in + 0.5f) : ceilf(in - 0.5f))
#endif

#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif

#ifndef SIZE_MAX
#define SIZE_MAX _UI32_MAX
#endif

#endif
#endif

