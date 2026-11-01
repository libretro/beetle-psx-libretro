/* cdc_fifo_test: the CDC's DMA FIFO against a byte-at-a-time model.
 *
 * Drives mednafen/psx/cdc_fifo.h through the sequences the CDC produces -
 * a sector (2340 bytes) and its zero pad to the full 4096, partial
 * sectors, byte reads through the data port, word reads by the DMA,
 * flushes - at every alignment and across the ring's wrap, and checks
 * each byte, each word and the ring's counters against a plain model.
 * Words whose top byte is 0x80 or more are read on both the one-load path
 * and the byte path; built with -fsanitize=undefined, a byte shifted as
 * an int into bit 31 fails the run.
 *
 * Usage: cdc_fifo_test [iterations]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../mednafen/psx/cdc_fifo.h"

#define FIFO_SIZE 4096

/* The model: a flat queue, read and written a byte at a time */
static uint8_t  model[FIFO_SIZE];
static uint32_t model_rd, model_count;

static uint32_t rng = 0x12345678u;

static uint32_t next_rand(void)
{
   rng ^= rng << 13;
   rng ^= rng >> 17;
   rng ^= rng << 5;
   return rng;
}

static void model_write(const uint8_t *src, uint32_t cnt)
{
   uint32_t i;
   for (i = 0; i < cnt; i++)
      model[(model_rd + model_count + i) % FIFO_SIZE] = src ? src[i] : 0;
   model_count += cnt;
}

static int model_byte(uint8_t *b)
{
   if (!model_count)
      return 0;
   *b       = model[model_rd];
   model_rd = (model_rd + 1) % FIFO_SIZE;
   model_count--;
   return 1;
}

static uint32_t model_word(void)
{
   uint32_t w = 0, i;
   uint8_t  b;
   for (i = 0; i < 4; i++)
      if (model_byte(&b))
         w |= (uint32_t)b << (i * 8);
   return w;
}

static int fail(const char *what, unsigned long it)
{
   printf("[cdc_fifo] FAIL: %s at step %lu\n", what, it);
   return 1;
}

int main(int argc, char **argv)
{
   SimpleFIFO    f;
   uint8_t       sector[2340];
   unsigned long it, iterations = argc > 1 ? strtoul(argv[1], NULL, 10) : 400000;
   unsigned long words = 0, slow_high = 0;
   uint32_t      i;

   if (!SimpleFIFO_Init(&f, FIFO_SIZE))
      return fail("init", 0);

   /* Directed: a sector of 0xFF bytes written three bytes short of the
    * wrap, read back as words across it */
   memset(sector, 0xFF, sizeof(sector));
   SimpleFIFO_Write(&f, sector, FIFO_SIZE / 2);
   SimpleFIFO_Write(&f, sector, FIFO_SIZE / 2 - 3);
   model_write(sector, FIFO_SIZE / 2);
   model_write(sector, FIFO_SIZE / 2 - 3);
   while (f.in_count)
   {
      uint8_t want;
      model_byte(&want);
      if (SimpleFIFO_ReadByte(&f) != want)
         return fail("directed drain", 0);
   }
   SimpleFIFO_Write(&f, sector, sizeof(sector));
   model_write(sector, sizeof(sector));
   while (f.in_count)
   {
      uint32_t slow = !(f.in_count >= 4 && f.read_pos <= f.size - 4);
      uint32_t want = model_word();
      if (SimpleFIFO_ReadWordLE(&f) != want)
         return fail("directed word", 0);
      if (slow && (want >> 31))
         slow_high++;
   }

   for (it = 0; it < iterations; it++)
   {
      uint32_t op = next_rand() % 10;

      if (op < 2 && !f.in_count)
      {
         /* A sector the host re-requests: all of it, then zeros to the
          * end, as cdc.c writes it on an empty FIFO */
         for (i = 0; i < sizeof(sector); i++)
            sector[i] = (uint8_t)next_rand();
         SimpleFIFO_Write(&f, sector, sizeof(sector));
         model_write(sector, sizeof(sector));
         if (op == 0)
         {
            uint32_t pad = SimpleFIFO_CanWrite(&f);
            SimpleFIFO_Write(&f, NULL, pad);
            model_write(NULL, pad);
         }
      }
      else if (op == 2 && !f.in_count)
      {
         /* A partial sector, any length */
         uint32_t n = next_rand() % (sizeof(sector) + 1);
         for (i = 0; i < n; i++)
            sector[i] = (uint8_t)next_rand();
         SimpleFIFO_Write(&f, sector, n);
         model_write(sector, n);
      }
      else if (op < 8)
      {
         uint32_t slow = !(f.in_count >= 4 && f.read_pos <= f.size - 4);
         uint32_t want = model_word();
         uint32_t got  = SimpleFIFO_ReadWordLE(&f);
         if (got != want)
            return fail("DMA word", it);
         if (slow && (want >> 31))
            slow_high++;
         words++;
      }
      else if (op == 8)
      {
         uint8_t want;
         if (model_byte(&want) && SimpleFIFO_ReadByte(&f) != want)
            return fail("data port byte", it);
      }
      else
      {
         /* A flush or a reset, which leave the ring at any alignment
          * only by the reads before them */
         if (next_rand() & 63)
            continue;
         SimpleFIFO_Flush(&f);
         model_rd = model_count = 0;
      }

      if (f.in_count != model_count || f.read_pos != model_rd)
         return fail("counters", it);
   }

   SimpleFIFO_Destroy(&f);

   printf("[cdc_fifo] %lu steps, %lu DMA words, %lu with bit 31 set on the "
         "byte path\n", iterations, words, slow_high);
   if (!slow_high)
      return fail("no high word crossed the wrap", it);
   printf("[cdc_fifo] OK\n");
   return 0;
}
