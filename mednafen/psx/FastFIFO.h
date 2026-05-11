#ifndef __MDFN_FASTFIFO_H
#define __MDFN_FASTFIFO_H

#include <string.h>

#include "../mednafen-types.h"

/*
 * Fixed-size circular FIFO of uint32 entries with a 0x20-element
 * capacity. Used by gpu.cpp (GPU_BlitterFIFO) and mdec.cpp (InFIFO,
 * OutFIFO); all three callers historically instantiated the same
 * FastFIFO<uint32, 0x20> template, so this is now a concrete struct
 * with no parameterization. The 0x20 size is hard-wired - it has to
 * be a power of 2 because the read/write index advances mask with
 * (size - 1).
 *
 * The struct is plain C and works from both .c and .cpp callers.
 * All mutator functions are static inline so the optimizer folds
 * them away just like the template did.
 *
 * Field layout is preserved from the previous template body so any
 * out-of-tree consumer poking at fifo->in_count / read_pos /
 * write_pos / data continues to work.
 */

#define FASTFIFO_SIZE      0x20
#define FASTFIFO_SIZE_MASK (FASTFIFO_SIZE - 1)

typedef struct
{
   uint32 data[FASTFIFO_SIZE];
   uint32 read_pos;
   uint32 write_pos;
   uint32 in_count;
} FastFIFO;

static INLINE void FastFIFO_Init(FastFIFO *f)
{
   memset(f->data, 0, sizeof(f->data));
   f->read_pos  = 0;
   f->write_pos = 0;
   f->in_count  = 0;
}

static INLINE void FastFIFO_Flush(FastFIFO *f)
{
   f->read_pos  = 0;
   f->write_pos = 0;
   f->in_count  = 0;
}

static INLINE void FastFIFO_SaveStatePostLoad(FastFIFO *f)
{
   f->read_pos  %= FASTFIFO_SIZE;
   f->write_pos %= FASTFIFO_SIZE;
   f->in_count  %= (FASTFIFO_SIZE + 1);
}

static INLINE uint32 FastFIFO_CanWrite(const FastFIFO *f)
{
   return FASTFIFO_SIZE - f->in_count;
}

static INLINE uint32 FastFIFO_Peek(const FastFIFO *f)
{
   return f->data[f->read_pos];
}

static INLINE uint32 FastFIFO_Read(FastFIFO *f)
{
   uint32 ret = f->data[f->read_pos];
   f->read_pos = (f->read_pos + 1) & FASTFIFO_SIZE_MASK;
   f->in_count--;
   return ret;
}

static INLINE void FastFIFO_Write(FastFIFO *f, uint32 v)
{
   f->data[f->write_pos] = v;
   f->write_pos = (f->write_pos + 1) & FASTFIFO_SIZE_MASK;
   f->in_count++;
}

#endif
