#ifndef __MDFN_CLAMP_H
#define __MDFN_CLAMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#ifndef _WIN32
#include <unistd.h>
#endif

static INLINE int32 clamp_to_u8(int32 i)
{
 if(i & 0xFFFFFF00)
  i = (((~i) >> 30) & 0xFF);

 return(i);
}


static INLINE int32 clamp_to_u16(int32 i)
{
 if(i & 0xFFFF0000)
  i = (((~i) >> 31) & 0xFFFF);

 return(i);
}

static INLINE void clamp(int32_t *val, ssize_t min, ssize_t max)
{
   if(*val < min)
      *val = min;
   if(*val > max)
      *val = max;
}

#define clamp_simple(val) \
   if ( (int16_t)val != val ) \
      val = (val >> 31) ^ 0x7FFF;

#ifdef __cplusplus
}
#endif

#endif
