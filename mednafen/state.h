#ifndef _STATE_H
#define _STATE_H

#include <stdint.h>
#include <retro_inline.h>

typedef struct
{
   uint8_t *data;
   uint32_t loc;
   uint32_t len;
   uint32_t malloced;
   uint32_t initial_malloc; /* A setting! */
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

#ifdef __cplusplus
extern "C" {
#endif

/* Eh, we abuse the smem_* in-memory stream code
 * in a few other places. :) */
int32_t smem_read(StateMem *st, void *buffer, uint32_t len);
int32_t smem_write(StateMem *st, void *buffer, uint32_t len);
int32_t smem_putc(StateMem *st, int value);
int32_t smem_seek(StateMem *st, uint32_t offset, int whence);
int smem_write32le(StateMem *st, uint32_t b);
int smem_read32le(StateMem *st, uint32_t *b);

int MDFNSS_SaveSM(void *st, int a, int b, const void *c, const void *d, const void *e);
int MDFNSS_LoadSM(void *st, int a, int b);

int MDFNSS_StateAction(void *st, int load, int data_only,
      SFORMAT *sf, const char *name);

#ifdef __cplusplus
}
#endif

#endif
