/* SimpleFIFO: the CDC's byte FIFO, a power-of-two ring, used by the DMA
 * path; the only place it lives is inside struct PS_CDC. Self-contained
 * so tools/cdc_fifo can test it. */
#ifndef __MDFN_PSX_CDC_FIFO_H
#define __MDFN_PSX_CDC_FIFO_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <retro_inline.h>

typedef struct SimpleFIFO
{
   uint8_t  *data;
   uint32_t size;
   uint32_t read_pos;
   uint32_t write_pos;
   uint32_t in_count;
} SimpleFIFO;

static INLINE bool SimpleFIFO_Init(SimpleFIFO *f, uint32_t the_size)
{
   f->data = (uint8_t *)malloc(the_size * sizeof(uint8_t));
   if (!f->data)
   {
      f->size      = 0;
      f->read_pos  = 0;
      f->write_pos = 0;
      f->in_count  = 0;
      return false;
   }
   f->size      = the_size;
   f->read_pos  = 0;
   f->write_pos = 0;
   f->in_count  = 0;
   return true;
}

static INLINE void SimpleFIFO_Destroy(SimpleFIFO *f)
{
   if (f->data)
   {
      free(f->data);
      f->data = NULL;
   }
}

static INLINE uint32_t SimpleFIFO_CanWrite(const SimpleFIFO *f)
{
   return f->size - f->in_count;
}

static INLINE uint8_t SimpleFIFO_ReadByte(SimpleFIFO *f)
{
   uint8_t ret  = f->data[f->read_pos];
   f->read_pos  = (f->read_pos + 1) & (f->size - 1);
   f->in_count--;
   return ret;
}

/* Writes cnt bytes from src, or cnt zeros when src is NULL; at most two
 * runs, split at the ring's wrap. cnt must fit (SimpleFIFO_CanWrite). */
static INLINE void SimpleFIFO_Write(SimpleFIFO *f,
      const uint8_t *src, uint32_t cnt)
{
   uint32_t first = f->size - f->write_pos;

   if (first > cnt)
      first = cnt;
   if (src)
   {
      memcpy(f->data + f->write_pos, src, first);
      memcpy(f->data, src + first, cnt - first);
   }
   else
   {
      memset(f->data + f->write_pos, 0, first);
      memset(f->data, 0, cnt - first);
   }
   f->write_pos  = (f->write_pos + cnt) & (f->size - 1);
   f->in_count  += cnt;
}

static INLINE void SimpleFIFO_Flush(SimpleFIFO *f)
{
   f->read_pos  = 0;
   f->write_pos = 0;
   f->in_count  = 0;
}

static INLINE void SimpleFIFO_SaveStatePostLoad(SimpleFIFO *f)
{
   f->read_pos  %= f->size;
   f->write_pos %= f->size;
   f->in_count  %= (f->size + 1);
}

/* Four bytes as a little-endian word, as the DMA reads them; bytes past
 * an emptied FIFO read as zero */
static INLINE uint32_t SimpleFIFO_ReadWordLE(SimpleFIFO *f)
{
   unsigned i;
   uint32_t data = 0;

   /* A whole word short of the wrap, as nearly every word is: one load */
   if (f->in_count >= 4 && f->read_pos <= f->size - 4)
   {
      const uint8_t *p = f->data + f->read_pos;

      data = (uint32_t)p[0]         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
      f->read_pos  = (f->read_pos + 4) & (f->size - 1);
      f->in_count -= 4;
      return data;
   }

   for (i = 0; i < 4; i++)
      if (f->in_count)
         data |= (uint32_t)SimpleFIFO_ReadByte(f) << (i * 8);

   return data;
}

#endif
