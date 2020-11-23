#ifndef __MDFN_CLAMP_H
#define __MDFN_CLAMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
// ptrdiff_t (adopted by C99/C++) is generally a more portable version of ssize_t (POSIX only).
static INLINE void clamp(int32_t *val, ptrdiff_t min, ptrdiff_t max)
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
