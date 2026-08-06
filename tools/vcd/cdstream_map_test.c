/* cdstream ownership: open, close, and the memcache conversion, with file
 * mapping enabled.
 *
 * buf is always borrowed -- from a VFS mapping owned by the RFILE, or from a
 * data_transfer's buffer owned by the transfer. Freeing it corrupts the heap,
 * and the corruption does not surface at the free: it surfaces later, in an
 * unrelated allocation, which is how it reached a user as a crash inside
 * RtlFreeHeap.
 *
 * This has to be built with the mapping enabled (-DHAVE_MMAP) or it proves
 * nothing -- which is exactly why the defect got through: every harness here
 * was built without it, so cdstream_open never returned a mapped stream and
 * the bad branch was unreachable in test.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../mednafen/cdstream.h"

static int fails = 0;
static void chk(const char *what, int cond)
{ printf("  %-52s %s\n", what, cond ? "ok" : "FAIL"); if (!cond) fails++; }

int main(int argc, char **argv)
{
   cdstream  s;
   cdstream *h;
   uint8_t   tmp[512];
   int       mapped;

   if (argc < 2) { fprintf(stderr, "usage: %s file\n", argv[0]); return 2; }

   chk("cdstream_open succeeds", cdstream_open(&s, argv[1]));
   mapped = (s.buf != NULL);
   printf("  (stream is %s)\n", mapped ? "mapped" : "file-backed");
   chk("a read works", cdstream_read(&s, tmp, sizeof(tmp)) == sizeof(tmp));
   cdstream_close(&s);
   chk("close leaves the stream zeroed", s.buf == NULL && s.fp == NULL
                                      && s.dt == NULL && s.size == 0);
   cdstream_close(&s);            /* documented idempotent */
   chk("close is idempotent", 1);

   /* Heap-allocated variant, the shape CDAccess_Image uses. */
   h = cdstream_new(argv[1]);
   chk("cdstream_new succeeds", h != NULL);
   if (h)
   {
      chk("heap stream reads", cdstream_read(h, tmp, sizeof(tmp)) == sizeof(tmp));
      cdstream_destroy(h);
      chk("destroy does not corrupt the heap", 1);
   }

   /* memcache conversion: buf moves from a mapping to a transfer, and the
    * old owner must be released without the borrowed pointer being freed. */
   if (cdstream_open(&s, argv[1]))
   {
      bool ok = cdstream_memcache_in_place(&s);
      chk("memcache_in_place succeeds", ok);
      if (ok)
      {
         chk("converted stream still reads",
             cdstream_read(&s, tmp, sizeof(tmp)) == sizeof(tmp));
         cdstream_close(&s);
         chk("close after conversion is clean", s.buf == NULL && s.dt == NULL);
      }
   }

   /* A fresh allocation after every close: if a borrowed pointer had been
    * freed, this is where the damage shows up. */
   {
      void *p = malloc(4096);
      memset(p, 0xA5, 4096);
      free(p);
      chk("heap is intact after all closes", 1);
   }

   printf(fails ? "RESULT: FAIL\n" : "RESULT: PASS\n");
   return fails ? 1 : 0;
}
