#ifndef _STATE_H
#define _STATE_H

#include <stdint.h>
#include <boolean.h>
#include <retro_inline.h>

typedef struct
{
   uint8_t *data;
   uint32_t loc;
   uint32_t len;
   uint32_t malloced;
   uint32_t initial_malloc; /* A setting! */
   bool     fixed;          /* data is the caller's, malloced bytes long; never grown */
   bool     sizing;         /* writes only count bytes; data is never touched */
} StateMem;

typedef struct
{
   void *v;		      /* Pointer to the variable/array */
   uint32_t size;		/* Length, in bytes, of the data to be saved EXCEPT:
                      *  In the case of MDFNSTATE_BOOL, 
                      *  it is the number of bool elements to save 
                      *  (bool is not always 1-byte).
                      * If 0, the subchunk isn't saved. */
   uint32_t flags;	/* Flags */
   const char *name;	/* Name */
} SFORMAT;

/* State-Section Descriptor */
struct SSDescriptor
{
   SFORMAT *sf;
   const char *name;
   bool optional;
};

/* Flag for a single, >= 1 byte native-endian variable */
#define MDFNSTATE_RLSB            0x80000000
/* 32-bit native-endian elements */
#define MDFNSTATE_RLSB32          0x40000000
/* 16-bit native-endian elements */
#define MDFNSTATE_RLSB16          0x20000000
/* 64-bit native-endian elements */
#define MDFNSTATE_RLSB64          0x10000000

#define MDFNSTATE_BOOL		  0x08000000

/* Load by comparing before copying, recording what changed in the map set
 * with MDFNSS_SetDiffMap. Unchanged memory is left unwritten. */
#define MDFNSTATE_DIFF            0x04000000

/* MDFNSS_DIFF_CHANGED is set in a map entry whose chunk changed; bits 0-6
 * and 7-13 hold its first and last changed MDFNSS_DIFF_BLOCK-byte block. */
#define MDFNSS_DIFF_BLOCK         64
#define MDFNSS_DIFF_CHANGED       0x8000
#define MDFNSS_DIFF_FIRST(e)      ((unsigned)(e) & 0x7F)
#define MDFNSS_DIFF_LAST(e)       (((unsigned)(e) >> 7) & 0x7F)

#ifdef __cplusplus
extern "C" {
#endif

int MDFNSS_SaveSM(void *st, int a, int b, const void *c, const void *d, const void *e);
int MDFNSS_LoadSM(void *st, int a, int b);

int MDFNSS_StateAction(void *st, int load, int data_only,
      SFORMAT *sf, const char *name);

/* Map for MDFNSTATE_DIFF loads: one entry per chunk_bytes of the entry
 * (at most 127 blocks per chunk). Entries of loaded chunks are written,
 * others left as they are. NULL turns diff loads back into plain ones. */
void MDFNSS_SetDiffMap(uint16_t *map, uint32_t chunk_bytes);

#ifdef __cplusplus
}
#endif

#endif
