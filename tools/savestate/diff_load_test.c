/* MDFNSTATE_DIFF savestate loads (mednafen/state.c).
 *
 * A diff load compares each chunk of the stream with the memory it loads
 * into, copies only the changed span and records it in a map; the GPU
 * uses that map to hand the hardware renderer only the VRAM a load
 * changed. Checked through the real MDFNSS_StateAction against plain loads
 * of the same data:
 *
 *   - the memory ends up byte-identical to a plain load;
 *   - every chunk with a difference is marked, every other one is not;
 *   - each mark's first and last block are exactly the first and last
 *     blocks that differ;
 *   - other entries in the section load as before, and with no map set a
 *     flagged entry is a plain load.
 *
 * Sizes cover a whole number of chunks, a short last chunk and a short
 * last block. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mednafen/state.h"

int StateAction(StateMem *sm, int load, int data_only);
int StateAction(StateMem *sm, int load, int data_only)
{
   (void)sm;
   (void)load;
   (void)data_only;
   return 1;
}

static int      fails;
static unsigned rng = 12345;

static unsigned rnd(void)
{
   rng = rng * 1103515245u + 12345u;
   return (rng >> 16) & 0x7fff;
}

static void chk(int cond, const char *what, unsigned a, unsigned b)
{
   if (!cond)
   {
      if (fails < 20)
         printf("  FAIL: %s (%u, %u)\n", what, a, b);
      fails++;
   }
}

#define MAXN   (2048 * 64 + 4096)
#define CHUNKS 1100

static uint8_t  saved[MAXN], live[MAXN], plain[MAXN];
static uint16_t map[CHUNKS];

/* Save `saved` (plus a word either side) into a stream, change `live` by
 * `mode`, then diff-load the stream over it and check the result. */
static void run(uint32_t size, uint32_t chunk, int mode)
{
   StateMem st;
   uint32_t before = 0x11111111, after = 0x22222222;
   uint32_t c, chunks = (size + chunk - 1) / chunk;
   unsigned i;
   SFORMAT  sf[4];

   for (i = 0; i < size; i++)
      saved[i] = (uint8_t)rnd();
   sf[0].v = &before; sf[0].size = 4; sf[0].flags = MDFNSTATE_RLSB; sf[0].name = "before";
   sf[1].v = saved;   sf[1].size = size; sf[1].flags = MDFNSTATE_DIFF; sf[1].name = "mem";
   sf[2].v = &after;  sf[2].size = 4; sf[2].flags = MDFNSTATE_RLSB; sf[2].name = "after";
   sf[3].v = NULL;    sf[3].size = 0; sf[3].flags = 0; sf[3].name = NULL;

   memset(&st, 0, sizeof(st));
   if (!MDFNSS_StateAction(&st, 0, 0, sf, "TEST"))
   {
      chk(0, "save", size, chunk);
      return;
   }

   memcpy(live, saved, size);
   switch (mode)
   {
      case 0: /* nothing */
         break;
      case 1: /* one byte */
         live[rnd() % size] ^= 0x5a;
         break;
      case 2: /* scattered */
         for (i = 0; i < 40; i++)
            live[(rnd() * 7919u + rnd()) % size] ^= (uint8_t)(1 + rnd() % 255);
         break;
      case 3: /* everything */
         for (i = 0; i < size; i++)
            live[i] ^= 0xff;
         break;
      case 4: /* the very first and very last byte */
         live[0] ^= 1;
         live[size - 1] ^= 1;
         break;
   }

   /* Reference: what a plain load leaves, and what differs per block. */
   memcpy(plain, saved, size);

   before = after = 0;
   sf[1].v = live;
   for (c = 0; c < CHUNKS; c++)
      map[c] = 0xBEEF;
   for (c = 0; c < chunks; c++)
      map[c] = 0;
   MDFNSS_SetDiffMap(map, chunk);
   /* The saved image is what to diff against: restore it in `saved` and
    * keep the pre-load copy for the per-block expectation. */
   memcpy(saved, live, size);
   st.len = st.loc;
   st.loc = 0;
   chk(MDFNSS_StateAction(&st, 1, 0, sf, "TEST") != 0, "load", size, chunk);
   MDFNSS_SetDiffMap(NULL, 0);

   chk(before == 0x11111111 && after == 0x22222222, "neighbours load", size, chunk);
   chk(!memcmp(live, plain, size), "memory matches a plain load", size, chunk);
   chk(map[chunks] == 0xBEEF || chunks >= CHUNKS, "no entry past the end", size, chunk);

   /* saved = memory before the load, plain = the state's image. */
   for (c = 0; c < chunks; c++)
   {
      uint32_t off = c * chunk;
      uint32_t n   = size - off < chunk ? size - off : chunk;
      uint32_t b, first = ~0u, last = 0;
      for (b = 0; b * MDFNSS_DIFF_BLOCK < n; b++)
      {
         uint32_t s = b * MDFNSS_DIFF_BLOCK;
         uint32_t e = s + MDFNSS_DIFF_BLOCK < n ? s + MDFNSS_DIFF_BLOCK : n;
         if (memcmp(saved + off + s, plain + off + s, e - s))
         {
            if (first == ~0u)
               first = b;
            last = b;
         }
      }
      if (first == ~0u)
         chk(map[c] == 0, "unchanged chunk left unmarked", c, map[c]);
      else
      {
         chk((map[c] & MDFNSS_DIFF_CHANGED) != 0, "changed chunk marked", c, mode);
         chk(MDFNSS_DIFF_FIRST(map[c]) == first, "first changed block", c, first);
         chk(MDFNSS_DIFF_LAST(map[c]) == last, "last changed block", c, last);
      }
   }

   /* Without a map the flag means nothing: a plain load. */
   memcpy(live, saved, size);
   st.loc = 0;
   chk(MDFNSS_StateAction(&st, 1, 0, sf, "TEST") != 0, "plain load", size, chunk);
   chk(!memcmp(live, plain, size), "unmapped load matches", size, chunk);

   free(st.data);
}

int main(void)
{
   static const uint32_t sizes[]  = { 2048 * 64, 2048 * 3 + 100, 4096, 63, 64 * 1024 + 37 };
   static const uint32_t chunks[] = { 2048, 2048, 1024, 64, 4096 };
   unsigned s, m, r;

   for (s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
      for (m = 0; m < 5; m++)
         for (r = 0; r < 8; r++)
            run(sizes[s], chunks[s], m);

   printf(fails ? "diff_load: %d failures\n" : "diff_load: all checks passed\n",
         fails);
   return fails ? 1 : 0;
}
