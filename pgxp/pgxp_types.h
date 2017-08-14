#ifndef _PGXP_TYPES_H_
#define _PGXP_TYPES_H_

#include "stdint.h"

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef intptr_t sptr;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uintptr_t uptr;

typedef struct PGXP_value_Tag
{
   float			x;
   float			y;
   float			z;
   union
   {
      unsigned int	flags;
      unsigned char	compFlags[4];
      unsigned short	halfFlags[2];
   };
   unsigned int	count;
   unsigned int	value;

   unsigned short	gFlags;
   unsigned char	lFlags;
   unsigned char	hFlags;
} PGXP_value;

#endif//_PGXP_TYPES_H_
