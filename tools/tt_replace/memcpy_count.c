/* LD_PRELOAD shim for tools/tt_replace/hd_upload.sh: counts the bytes
 * memcpy moves on the thread that loaded it (the host's main thread, which
 * runs retro_run and so the renderer), per call site, and at exit prints
 * the total and writes one line per site to $MEMCPY_COUNT_SITES:
 * "<object> <offset in object> <bytes> <calls>". The copy itself is done by
 * memmove, which is not interposed. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SITES 8192

static __thread int counted_thread;
static unsigned long long bytes, calls;
static struct { void *ra; unsigned long long bytes, calls; } sites[SITES];

__attribute__((constructor)) static void memcpy_count_init(void)
{
   counted_thread = 1;
}

__attribute__((destructor)) static void memcpy_count_fini(void)
{
   const char *path = getenv("MEMCPY_COUNT_SITES");
   FILE *f;
   int i;
   counted_thread = 0;
   fprintf(stderr, "[memcpy_count] main thread: %llu bytes in %llu calls\n",
         bytes, calls);
   if (!path || !(f = fopen(path, "w")))
      return;
   for (i = 0; i < SITES; i++)
   {
      Dl_info info;
      if (!sites[i].ra)
         continue;
      if (dladdr(sites[i].ra, &info) && info.dli_fname)
         fprintf(f, "%s %lx %llu %llu\n", info.dli_fname,
               (unsigned long)((char *)sites[i].ra - (char *)info.dli_fbase),
               sites[i].bytes, sites[i].calls);
   }
   fclose(f);
}

void *memcpy(void *dst, const void *src, size_t n)
{
   if (counted_thread)
   {
      void *ra = __builtin_return_address(0);
      unsigned h = (unsigned)(((size_t)ra >> 2) * 2654435761u) % SITES;
      unsigned probes = 0;
      while (sites[h].ra && sites[h].ra != ra && ++probes < SITES)
         h = (h + 1) % SITES;
      if (probes < SITES)
      {
         sites[h].ra = ra;
         sites[h].bytes += n;
         sites[h].calls++;
      }
      bytes += n;
      calls++;
   }
   return memmove(dst, src, n);
}
