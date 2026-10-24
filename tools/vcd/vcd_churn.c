/* Savestate loads must not rebuild Video CD decoders nothing has used.
 *
 * Every savestate load runs VCD_StateAction, and runahead and rollback
 * netplay load a state on every frame of every game. The load drops the MPEG
 * demuxer, the video decoder and the MP2 stream so no reference picture
 * survives from before it, but rebuilding a set that has never been fed only
 * replaces it with an identical one: over 2 MiB of allocation and clearing,
 * every frame, for nothing.
 *
 * Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc, this counts
 * every heap allocation made inside a load and checks:
 *
 *   - loads with untouched decoders allocate nothing, repeatedly;
 *   - once a sector has been fed the next load does rebuild, so stale
 *     decoder state still cannot cross a load;
 *   - after that rebuild, loads are free again;
 *   - running a frame (which touches the video decoder) also forces the
 *     next load to rebuild.
 *
 * Needs no MPEG data: zero-filled Form 2 sectors are enough to mark the
 * demuxer as used, and that is the property under test. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../mednafen/psx/vcd.h"
#include "../../mednafen/state.h"

struct CDIF;
int CDIF_ReadSector(struct CDIF *c, uint8_t *b, uint32_t lba, uint32_t n);
int CDIF_ReadSector(struct CDIF *c, uint8_t *b, uint32_t lba, uint32_t n)
{ (void)c; (void)b; (void)lba; (void)n; return 0; }

void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);
void *__wrap_malloc(size_t n);
void *__wrap_calloc(size_t n, size_t m);
void *__wrap_realloc(void *p, size_t n);

static unsigned long allocs;
static unsigned long bytes;

void *__wrap_malloc(size_t n)
{
   allocs++;
   bytes += (unsigned long)n;
   return __real_malloc(n);
}

void *__wrap_calloc(size_t n, size_t m)
{
   allocs++;
   bytes += (unsigned long)(n * m);
   return __real_calloc(n, m);
}

void *__wrap_realloc(void *p, size_t n)
{
   allocs++;
   bytes += (unsigned long)n;
   return __real_realloc(p, n);
}

static int      fails;
static StateMem sm;

static void chk(const char *what, int cond)
{
   printf("  %-52s %s\n", what, cond ? "ok" : "FAIL");
   if (!cond)
      fails++;
}

/* Load the saved state; returns the bytes allocated inside the load. */
static unsigned long load(void)
{
   unsigned long before = bytes;
   sm.loc = 0;
   if (!VCD_StateAction(&sm, 1, 1))
   {
      printf("  load failed\n");
      fails++;
   }
   return bytes - before;
}

static void feed(void)
{
   uint8_t sec[2352];
   memset(sec, 0, sizeof(sec));
   sec[16 + 2] = 0x20 | 0x02 | 0x04;
   sec[16 + 6] = sec[16 + 2];
   VCD_FeedSector(sec, 5000);
}

int main(void)
{
   int           i;
   unsigned long a0, b;

   VCD_Init();
   VCD_SetMode(VCD_MODE_HLE);
   VCD_SetAVSwitch(true);

   memset(&sm, 0, sizeof(sm));
   sm.data     = (uint8_t *)__real_malloc(1 << 16);
   sm.malloced = 1 << 16;
   if (!VCD_StateAction(&sm, 0, 1))
   {
      printf("save failed\n");
      return 1;
   }
   sm.len = sm.loc;

   /* The first load may build the set if VCD_Init left none. */
   load();

   a0 = allocs;
   b  = 0;
   for (i = 0; i < 100; i++)
      b += load();
   printf("100 loads, decoders untouched: %lu allocations, %lu bytes\n",
          allocs - a0, b);
   chk("loads with untouched decoders allocate nothing", allocs == a0);

   feed();
   b = load();
   printf("load after a fed sector: %lu bytes\n", b);
   chk("a fed demuxer is rebuilt on load", b > 0);

   a0 = allocs;
   for (i = 0; i < 10; i++)
      load();
   chk("loads after the rebuild allocate nothing", allocs == a0);

   VCD_RunFrame();
   b = load();
   printf("load after a frame ran: %lu bytes\n", b);
   chk("a run decoder is rebuilt on load", b > 0);

   a0 = allocs;
   load();
   chk("and the next load is free again", allocs == a0);

   VCD_Kill();
   printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
   return fails ? 1 : 0;
}
