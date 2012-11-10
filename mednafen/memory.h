#ifndef _MDFN_MEMORY_H
#define _MDFN_MEMORY_H

// These functions can be used from driver code or from internal Mednafen code.
//

#include <stdint.h>

#define MDFN_malloc(size, purpose) malloc(size)
#define MDFN_calloc(nmemb, size, purpose) calloc(nmemb, size)
#define MDFN_realloc(ptr, size, purpose) realloc(ptr, size)

#define MDFN_malloc_real(size, purpose) malloc(size)
#define MDFN_calloc_real(nmemb, size, purpose) calloc(nmemb, size)
#define MDFN_realloc_real(ptr, size, purpose) realloc(ptr, size)
#define MDFN_free(ptr) free(ptr)

static inline void MDFN_FastU32MemsetM8(uint32_t *array, uint32_t value_32, unsigned int u32len)
{
 for(uint32_t *ai = array; ai < array + u32len; ai += 2)
 {
  ai[0] = value_32;
  ai[1] = value_32;
 }
}

#endif
