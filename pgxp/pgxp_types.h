#ifndef _PGXP_TYPES_H_
#define _PGXP_TYPES_H_

#include <stdint.h>

/* Build switch for all PGXP instrumentation: transform Z-range counters,
 * vertex cache counters, colour counters, and the BEETLE_PGXP_DIAG dump.
 *
 * Off, and compiled out entirely, unless built with -DPGXP_DIAG=1. None
 * of it may cost anything in a shipping build: the counters sit on
 * per-vertex paths, and a runtime flag would still be a load, a test and
 * a branch each time. It lives here because every PGXP header includes
 * this one, so no translation unit can pick up an instrumented
 * declaration and a non-instrumented definition, or vice versa. */
#ifndef PGXP_DIAG
#define PGXP_DIAG 0
#endif

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
