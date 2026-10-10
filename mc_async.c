/* Asynchronous memory-card file writer.
 *
 * The periodic memcard flush in retro_run used to do a synchronous 128 KiB
 * file write on the emulation thread, stalling a frame each time a game
 * saved (file-managed cards: all secondary cards, and card 0 under the
 * mednafen method). The file write happens on a background thread; the
 * emulation thread only snapshots the 128 KiB and hands it over.
 *
 * There is no lock. One thread produces (the emulation thread) and one
 * writes, and what passes between them is a pointer per card:
 *
 *   - Each card path gets a slot, assigned by the producer the first time
 *     the path is seen and kept until the writer stops. The path is
 *     written before the slot's first snapshot is published and never
 *     again, so the writer reads it without any coordination.
 *
 *   - A slot holds at most one pending snapshot, in an atomic pointer. The
 *     producer exchanges its new snapshot in and frees whatever comes out:
 *     a newer snapshot supersedes a pending older one (full-buffer saves
 *     are idempotent, latest wins). The writer exchanges NULL in and
 *     writes whatever comes out. Whoever takes a snapshot out of the
 *     pointer owns it; nobody else can reach it.
 *
 *   - Only the writer thread writes the file of a path that has a slot, so
 *     the writes for one path land in the order they were handed over.
 *
 *   - The writer sleeps on an eventcount and is notified on every
 *     hand-over. It looks again at every slot between announcing the wait
 *     and committing to it, so a snapshot published in that window is not
 *     slept through.
 *
 *   - More distinct paths than slots (there are eight card ports and
 *     sixteen slots; it takes paths changing mid-session): a path with no
 *     slot is written inline. The writer never writes such a path, so the
 *     producer is its only writer and the order holds. Nothing waits for
 *     space.
 *
 *   - No memory for a snapshot: the save is written inline instead, but
 *     only once the writer can no longer put an older one on top of it.
 *     The producer takes back whatever is pending for the slot, then waits
 *     until the writer is not in the middle of that slot. The writer
 *     marks a slot as being written before it takes the slot's snapshot,
 *     and the producer reads the mark after it has emptied the pointer,
 *     with a full barrier between the store and the load on each side -
 *     so either the producer sees the mark and waits, or the writer finds
 *     the pointer already empty.
 *
 *   - flush_and_stop raises a flag and joins. The writer reads the flag
 *     before it scans, and leaves only when a scan that started after the
 *     flag was up found nothing - so no save handed over before the stop
 *     is lost.
 *
 *   - If the writer thread cannot be created, every save is written
 *     inline; with no second writer the order is trivially preserved.
 *
 * tools/mcasync/mc_async_test.c holds it to all of the above with a real
 * writer thread, under ThreadSanitizer and AddressSanitizer.
 */

#include <stdlib.h>
#include <string.h>

#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>

#include "mc_async.h"

#define MC_ASYNC_SLOTS 16

typedef struct
{
   uint32_t size;
   /* The card image follows. */
} mc_async_job;

typedef struct
{
   retro_atomic_ptr_t pending;   /* mc_async_job *, or NULL        */
   retro_atomic_int_t writing;   /* the writer is on this slot     */
   char               path[4096];
} mc_async_slot;

static mc_async_slot      mc_async_slots[MC_ASYNC_SLOTS];
static unsigned           mc_async_slots_used;   /* producer only */
static retro_eventcount_t mc_async_work;         /* writer sleeps: a snapshot was handed over */
static retro_eventcount_t mc_async_idle;         /* producer sleeps: the writer left a slot   */
static retro_atomic_int_t mc_async_quit;
static sthread_t         *mc_async_thread;
static bool               mc_async_running;
static mc_async_write_t   mc_async_write;

#ifdef MC_ASYNC_TEST
int mc_async_test_no_memory;
int mc_async_test_no_thread;
void (*mc_async_test_after_empty_scan)(void);
#endif

static void mc_async_writer(void *unused)
{
   (void)unused;
   for (;;)
   {
      unsigned i;
      bool wrote = false;
      int  key   = retro_eventcount_prepare_wait(&mc_async_work);
      int  quit  = retro_atomic_load_acquire_int(&mc_async_quit);

      for (i = 0; i < MC_ASYNC_SLOTS; i++)
      {
         mc_async_slot *slot = &mc_async_slots[i];
         mc_async_job  *job;

         if (!retro_atomic_load_acquire_ptr(&slot->pending))
            continue;

         /* Mark, then take: see the no-memory case at the top. */
         retro_atomic_store_release_int(&slot->writing, 1);
         retro_atomic_thread_fence_seq_cst();
         job = (mc_async_job*)retro_atomic_exchange_ptr(&slot->pending, NULL);
         if (job)
         {
            mc_async_write(slot->path, (const uint8_t*)(job + 1), job->size);
            free(job);
            wrote = true;
         }
         retro_atomic_store_release_int(&slot->writing, 0);
         retro_eventcount_notify(&mc_async_idle);
      }

      if (wrote)
      {
         retro_eventcount_cancel_wait(&mc_async_work);
         continue;
      }
#ifdef MC_ASYNC_TEST
      if (mc_async_test_after_empty_scan)
         mc_async_test_after_empty_scan();
#endif
      if (quit)
      {
         retro_eventcount_cancel_wait(&mc_async_work);
         return;
      }
      retro_eventcount_commit_wait(&mc_async_work, key);
   }
}

/* Producer only. The slot for a path, a new one if there is room, or NULL. */
static mc_async_slot *mc_async_slot_for(const char *path)
{
   unsigned i;
   mc_async_slot *slot;

   for (i = 0; i < mc_async_slots_used; i++)
      if (!strcmp(mc_async_slots[i].path, path))
         return &mc_async_slots[i];
   if (     mc_async_slots_used == MC_ASYNC_SLOTS
         || strlen(path) >= sizeof(mc_async_slots[0].path))
      return NULL;
   slot = &mc_async_slots[mc_async_slots_used++];
   /* The writer reads this only after a snapshot has been published in
    * the slot, and the exchange that publishes it carries this write. */
   strcpy(slot->path, path);
   return slot;
}

void mc_async_enqueue(const char *path, const uint8_t *data, uint32_t size)
{
   mc_async_slot *slot;
   mc_async_job  *job;

   if (!mc_async_write)
      return;
   if (!mc_async_running)   /* no writer thread: inline is the sole writer */
   {
      mc_async_write(path, data, size);
      return;
   }

   if (!(slot = mc_async_slot_for(path)))
   {
      /* No slot, so the writer never writes this path. */
      mc_async_write(path, data, size);
      return;
   }

   job = (mc_async_job*)malloc(sizeof(*job) + size);
#ifdef MC_ASYNC_TEST
   if (mc_async_test_no_memory)
   {
      free(job);
      job = NULL;
   }
#endif
   if (!job)
   {
      /* Take back what is pending - this save supersedes it - and wait
       * for the writer to be out of the slot before writing inline. */
      free(retro_atomic_exchange_ptr(&slot->pending, NULL));
      retro_atomic_thread_fence_seq_cst();
      for (;;)
      {
         int key = retro_eventcount_prepare_wait(&mc_async_idle);
         if (!retro_atomic_load_acquire_int(&slot->writing))
         {
            retro_eventcount_cancel_wait(&mc_async_idle);
            break;
         }
         retro_eventcount_commit_wait(&mc_async_idle, key);
      }
      mc_async_write(path, data, size);
      return;
   }

   job->size = size;
   memcpy(job + 1, data, size);
   /* A snapshot still pending is superseded: latest wins. */
   free(retro_atomic_exchange_ptr(&slot->pending, job));
   retro_eventcount_notify(&mc_async_work);
}

void mc_async_init(mc_async_write_t write)
{
   unsigned i;

   if (mc_async_running)
      return;
   mc_async_write      = write;
   mc_async_slots_used = 0;
   for (i = 0; i < MC_ASYNC_SLOTS; i++)
   {
      retro_atomic_ptr_init(&mc_async_slots[i].pending, NULL);
      retro_atomic_int_init(&mc_async_slots[i].writing, 0);
      mc_async_slots[i].path[0] = '\0';
   }
   retro_atomic_int_init(&mc_async_quit, 0);

   if (!write || !retro_eventcount_init(&mc_async_work))
      return;
   if (!retro_eventcount_init(&mc_async_idle))
   {
      retro_eventcount_free(&mc_async_work);
      return;
   }
   mc_async_thread  = sthread_create(mc_async_writer, NULL);
#ifdef MC_ASYNC_TEST
   if (mc_async_test_no_thread && mc_async_thread)
   {
      retro_atomic_store_release_int(&mc_async_quit, 1);
      retro_eventcount_notify(&mc_async_work);
      sthread_join(mc_async_thread);
      retro_atomic_store_release_int(&mc_async_quit, 0);
      mc_async_thread = NULL;
   }
#endif
   mc_async_running = (mc_async_thread != NULL);
   if (!mc_async_running)   /* fall back to inline writes */
   {
      retro_eventcount_free(&mc_async_idle);
      retro_eventcount_free(&mc_async_work);
   }
}

void mc_async_flush_and_stop(void)
{
   if (!mc_async_running)
      return;
   retro_atomic_store_release_int(&mc_async_quit, 1);
   retro_eventcount_notify(&mc_async_work);
   sthread_join(mc_async_thread);   /* returns only once everything pending is written */
   mc_async_thread  = NULL;
   mc_async_running = false;
   retro_eventcount_free(&mc_async_idle);
   retro_eventcount_free(&mc_async_work);
}
