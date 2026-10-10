/* mc_async_test: the memory card writer, with its real writer thread.
 *
 * The writer's one promise is that each card file ends up holding the last
 * save the game made, whatever else was going on. The file write is
 * replaced by a recorder here: every save carries its path's running
 * sequence number, and the recorder checks the number never goes backwards
 * for a path and remembers the last one it saw. What a run has to show:
 *
 *   order      a path's writes land in the order the saves were made
 *   no loss    after flush_and_stop the last write of every path is the
 *              last save made to it (a save may be superseded by a newer
 *              one before it is written; it may not outlive it)
 *   overflow   more distinct paths than the writer has slots: the extra
 *              ones are written all the same, in order
 *   no memory  saves that cannot be snapshotted are written inline, and an
 *              older save the writer already held never lands after them
 *   restart    init / flush_and_stop can be repeated, as a core does per
 *              content
 *   stop       a save made right before flush_and_stop is not lost
 *   no thread  with no writer every save is written before enqueue returns
 *
 * The recorder sleeps now and then so the producer gets ahead of the
 * writer and saves pile up behind it. Built with ThreadSanitizer and with
 * AddressSanitizer (leaks on) by run.sh; a snapshot freed twice, leaked,
 * or read while the other side writes it fails there.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <retro_atomic.h>
#include <retro_timers.h>

#include "../../mc_async.h"

#define PATHS 24            /* more than the writer's 16 slots */
#define SIZE  4096

static retro_atomic_int_t last_written[PATHS];   /* sequence of the last write */
static retro_atomic_int_t writes;
static retro_atomic_int_t failures;
static int                last_saved[PATHS];     /* producer only */
static int                slow;

#define FAIL(...) do { \
   fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); \
   retro_atomic_fetch_add_int(&failures, 1); } while (0)

static void record(const char *path, const uint8_t *data, uint32_t size)
{
   int      idx = atoi(path + 5);           /* "card/NN" */
   int      seq;
   int      prev;
   uint32_t i;

   /* Dawdle before looking at anything, so a save the writer has already
    * taken is still unwritten while the producer carries on. */
   if (slow && (retro_atomic_fetch_add_int(&writes, 1) % 7) == 0)
      retro_sleep(1);

   if (idx < 0 || idx >= PATHS || size != SIZE)
   {
      FAIL("write with a bad path or size: %s, %u", path, (unsigned)size);
      return;
   }
   memcpy(&seq, data, sizeof(seq));
   for (i = sizeof(seq); i < size; i++)
      if (data[i] != (uint8_t)(seq + idx))
      {
         FAIL("%s: save %d arrived corrupted at byte %u", path, seq, (unsigned)i);
         break;
      }
   prev = retro_atomic_load_acquire_int(&last_written[idx]);
   if (seq <= prev)
      FAIL("%s: save %d written after save %d", path, seq, prev);
   retro_atomic_store_release_int(&last_written[idx], seq);
}

static void linger(void)
{
   retro_sleep(2);
}

static void save(int idx)
{
   static uint8_t card[SIZE];
   char path[32];
   int  seq = ++last_saved[idx];

   snprintf(path, sizeof(path), "card/%d", idx);
   memcpy(card, &seq, sizeof(seq));
   memset(card + sizeof(seq), (uint8_t)(seq + idx), SIZE - sizeof(seq));
   mc_async_enqueue(path, card, SIZE);
   /* The caller may change its buffer the moment enqueue returns. */
   memset(card, 0xEE, SIZE);
}

static void check_all_written(const char *what)
{
   int i;
   for (i = 0; i < PATHS; i++)
   {
      int got = retro_atomic_load_acquire_int(&last_written[i]);
      if (got != last_saved[i])
         FAIL("%s: card/%d holds save %d, the game's last was %d",
               what, i, got, last_saved[i]);
   }
}

static void reset(void)
{
   int i;
   for (i = 0; i < PATHS; i++)
   {
      retro_atomic_store_release_int(&last_written[i], 0);
      last_saved[i] = 0;
   }
}

int main(int argc, char **argv)
{
   int rounds = (argc > 1) ? atoi(argv[1]) : 20000;
   int r, i;
   unsigned rng = 12345;

   for (i = 0; i < PATHS; i++)
      retro_atomic_int_init(&last_written[i], 0);
   retro_atomic_int_init(&writes, 0);
   retro_atomic_int_init(&failures, 0);

   /* no thread: every save is on disk by the time enqueue returns */
   mc_async_test_no_thread = 1;
   mc_async_init(record);
   mc_async_test_no_thread = 0;
   for (i = 0; i < 200; i++)
   {
      save(i % PATHS);
      check_all_written("no thread");
   }
   mc_async_flush_and_stop();
   reset();

   /* order, no loss, overflow - twice over, for the restart */
   for (r = 0; r < 2; r++)
   {
      slow = 1;
      mc_async_init(record);
      for (i = 0; i < rounds; i++)
      {
         rng = rng * 1103515245u + 12345u;
         /* mostly the first few cards, so saves pile up and supersede */
         save((rng >> 16) % ((i % 5) ? 3 : PATHS));
      }
      mc_async_flush_and_stop();
      check_all_written(r ? "after restart" : "order / no loss / overflow");
      reset();
   }

   /* stop: a save made just before the stop is written all the same,
    * however the stop lands against the writer's scan. The writer is held
    * for a moment after each scan that found nothing, which is where a
    * save and the stop behind it would slip past a writer that looked at
    * the stop flag only afterwards. */
   slow = 0;
   mc_async_test_after_empty_scan = linger;
   for (r = 0; r < 300; r++)
   {
      mc_async_init(record);
      retro_sleep(1);        /* let the writer reach its first empty scan */
      save(r % 3);
      if (r & 1)
         save((r + 1) % 3);
      mc_async_flush_and_stop();
      check_all_written("stop");
   }
   mc_async_test_after_empty_scan = NULL;
   reset();

   /* no memory: every third save cannot be snapshotted */
   slow = 1;
   mc_async_init(record);
   for (i = 0; i < rounds; i++)
   {
      rng = rng * 1103515245u + 12345u;
      mc_async_test_no_memory = ((i % 3) == 0);
      save((rng >> 16) % 3);
   }
   mc_async_test_no_memory = 0;
   mc_async_flush_and_stop();
   check_all_written("no memory");

   if (retro_atomic_load_acquire_int(&failures))
   {
      fprintf(stderr, "mc_async_test: FAILED\n");
      return 1;
   }
   printf("mc_async_test: ok (%d rounds per stage, %d writes paced)\n",
         rounds, retro_atomic_load_acquire_int(&writes));
   return 0;
}
