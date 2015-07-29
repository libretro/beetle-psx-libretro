#ifndef _TREMOR_SHARED_H_
#define _TREMOR_SHARED_H_

#include <stdint.h>
#include <stddef.h>
#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

static INLINE int ilog(unsigned int v)
{
   int ret=0;
   while(v)
   {
      ret++;
      v>>=1;
   }
   return(ret);
}

#include "os_types.h"

static INLINE ogg_uint32_t bitreverse(ogg_uint32_t x)
{
  x=    ((x>>16)&0x0000ffffUL) | ((x<<16)&0xffff0000UL);
  x=    ((x>> 8)&0x00ff00ffUL) | ((x<< 8)&0xff00ff00UL);
  x=    ((x>> 4)&0x0f0f0f0fUL) | ((x<< 4)&0xf0f0f0f0UL);
  x=    ((x>> 2)&0x33333333UL) | ((x<< 2)&0xccccccccUL);
  return((x>> 1)&0x55555555UL) | ((x<< 1)&0xaaaaaaaaUL);
}

#ifdef __cplusplus
}
#endif

#endif
