/* A disc image read through its mapping never waits on read-ahead
 * (Windows).
 *
 *    offthread_test
 *
 * PrefetchVirtualMemory is not documented to return before its reads
 * complete. This build points it at a stand-in that sleeps the way a
 * cold disk would, and reads an image in order through cdstream, as
 * the disc readers do:
 *
 *    - no read waits on the stand-in, and it never runs on the reader;
 *    - each hint starts past the read that made it (filestream_prefetch
 *      is wrapped to see them).
 *
 * Elsewhere there is nothing to check, and the test passes. C89.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../mednafen/cdstream.h"

#ifdef _WIN32
#include <windows.h>

#include "prefetch_hook.h"

#define FIXTURE_LEN   (16u * 1024u * 1024u)
#define STEP          2352u
#define SLOW_MS       200
/* far under SLOW_MS, far over a sector's copy */
#define READ_LIMIT_MS 100

/* the hints as the readers give them (run.sh links with --wrap) */
static unsigned          hints;
static uint64_t          hint_bad;
static uint64_t          read_end;

void __real_filestream_prefetch(RFILE *stream, uint64_t offset, uint64_t len);

void __wrap_filestream_prefetch(RFILE *stream, uint64_t offset, uint64_t len)
{
   hints++;
   if (offset < read_end)
      hint_bad++;
   __real_filestream_prefetch(stream, offset, len);
}

static volatile LONG     hook_calls;
static volatile LONG     hook_on_reader;
static DWORD             reader_thread;
static int               fails;

int readahead_test_hook(void *addr, size_t len)
{
   (void)addr;
   (void)len;
   InterlockedIncrement(&hook_calls);
   if (GetCurrentThreadId() == reader_thread)
      InterlockedIncrement(&hook_on_reader);
   Sleep(SLOW_MS);
   return 1;
}

#define CHECK(cond, msg) \
   do { \
      if (cond) \
         printf("  ok: %s\n", msg); \
      else \
      { \
         printf("  FAIL: %s\n", msg); \
         fails++; \
      } \
   } while (0)

int main(void)
{
   const char   *path = "offthread_fixture.bin";
   uint8_t      *data = (uint8_t*)malloc(FIXTURE_LEN);
   uint8_t       buf[STEP];
   cdstream      s;
   FILE         *f;
   LARGE_INTEGER freq, t0, t1;
   double        worst = 0.0;
   uint32_t      i;
   int           same  = 1;

   printf("mapped read-ahead off the reader's thread\n");
   if (!data)
      return 1;
   for (i = 0; i < FIXTURE_LEN; i++)
      data[i] = (uint8_t)((i * 2654435761u) >> 13);
   f = fopen(path, "wb");
   if (!f || fwrite(data, 1, FIXTURE_LEN, f) != FIXTURE_LEN)
   {
      printf("  FAIL: cannot write %s\n", path);
      return 1;
   }
   fclose(f);

   reader_thread = GetCurrentThreadId();
   QueryPerformanceFrequency(&freq);
   CHECK(cdstream_open(&s, path) && s.buf, "open, mapped");
   if (s.buf)
   {
      for (i = 0; i + STEP <= FIXTURE_LEN; i += STEP)
      {
         double ms;
         QueryPerformanceCounter(&t0);
         read_end = i + STEP;
         if (cdstream_read(&s, buf, STEP) != STEP || memcmp(buf, data + i, STEP))
            same = 0;
         QueryPerformanceCounter(&t1);
         ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart;
         if (ms > worst)
            worst = ms;
      }
      /* the thread runs on its own: let it go quiet */
      {
         LONG seen = -1;
         int  n;
         for (n = 0; n < 40 && seen != hook_calls; n++)
         {
            seen = hook_calls;
            Sleep(SLOW_MS * 3);
         }
      }
      printf("  hints: %ld, slowest read %.2f ms\n", (long)hook_calls, worst);
      CHECK(same, "reads return the file");
      CHECK(hook_calls > 1, "a reader in order is read ahead of");
      CHECK(hook_on_reader == 0, "the read-ahead never runs on the reader's thread");
      CHECK(worst < READ_LIMIT_MS, "no read waits on the read-ahead");
      CHECK(hints > 1 && !hint_bad, "each hint starts past the read that made it");
   }
   cdstream_close(&s);
   remove(path);
   free(data);

   printf(fails ? "offthread: FAILED\n" : "offthread: PASS\n");
   return fails ? 1 : 0;
}
#else
int main(void)
{
   printf("offthread: Windows only, nothing to check\noffthread: PASS\n");
   return 0;
}
#endif
