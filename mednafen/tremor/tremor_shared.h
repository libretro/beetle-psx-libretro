#ifndef _TREMOR_SHARED_H_
#define _TREMOR_SHARED_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int ilog(unsigned int v);

#include "os_types.h"

ogg_uint32_t bitreverse(ogg_uint32_t x);

#ifdef __cplusplus
}
#endif

#endif
