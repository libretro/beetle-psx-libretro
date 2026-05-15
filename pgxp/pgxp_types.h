#ifndef _PGXP_TYPES_H_
#define _PGXP_TYPES_H_

#include <stdint.h>

typedef struct PGXP_value_Tag
{
   float       x;
   float       y;
   float       z;
   union
   {
      uint32_t flags;
      uint8_t  compFlags[4];
      uint16_t halfFlags[2];
   };
   uint32_t    count;
   uint32_t    value;

   uint16_t    gFlags;
   uint8_t     lFlags;
   uint8_t     hFlags;
} PGXP_value;

#endif/*_PGXP_TYPES_H_*/
