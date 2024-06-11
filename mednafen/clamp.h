#ifndef __MDFN_CLAMP_H
#define __MDFN_CLAMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef _MSC_VER
#include <compat/msvc.h>
#endif

static INLINE void clamp(int32_t *val, ssize_t min, ssize_t max)
{
   if(*val < min)
      *val = min;
   if(*val > max)
      *val = max;
}

#ifdef __cplusplus
}
#endif

#endif
