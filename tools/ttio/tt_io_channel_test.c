/* tt_io_channel_test: the HD texture IO channel, with real workers.
 *
 * The render thread's side is this file's main thread; the pool is four
 * detached threads doing what the core's workers do - take a request,
 * spend some time on it, deliver a response. What a run has to show:
 *
 *   once        every request queued while the channel runs is taken by
 *               exactly one worker and answered exactly once
 *   priority    a high priority request queued behind a thousand low
 *               ones is served next, not after them
 *   promote     a low priority request promoted out of the backlog is
 *               served next as well; one the workers can already see, or
 *               that was never queued, reports false
 *   wakeup      a request queued just as the last worker goes to sleep
 *               is still taken
 *   wait        the bounded wait for a response returns with nothing
 *               delivered
 *   stop        stopping with work outstanding leaves no worker behind
 *               and frees every node (the leak check is AddressSanitizer)
 *   restart     channels come and go, as they do per content
 *
 * The channel is built with TT_IO_CHANNEL_TEST, which makes it give up the
 * CPU between the steps of each hand-over, so that the workers really do
 * get in each other's way even on one core. Built with ThreadSanitizer
 * and with AddressSanitizer by run.sh.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <sched.h>

#include <retro_atomic.h>
#include <retro_timers.h>
#include <rthreads/rthreads.h>

#include "../../rhi/tt_io_channel.h"

#define WORKERS 4
#define MAX_IDS 40000

typedef struct { tt_io_node node; int id; } item;

static retro_atomic_int_t taken[MAX_IDS];      /* times a worker took id   */
static retro_atomic_int_t served_at[MAX_IDS];  /* order it was taken in    */
static int                answered[MAX_IDS];   /* main thread only         */
static retro_atomic_int_t served;              /* requests taken so far    */
static retro_atomic_int_t exits;               /* workers gone             */
static retro_atomic_int_t live_nodes;          /* allocated, not yet freed */
static retro_atomic_int_t slow;                /* workers dawdle           */
static int                failures;

#define FAIL(...) do { \
   fprintf(stderr, "FAIL: " __VA_ARGS__); fputc('\n', stderr); failures++; } while (0)

/* Called by the channel (built with TT_IO_CHANNEL_TEST) wherever another
 * thread getting in is what it has to survive. */
static retro_atomic_int_t test_points;
void tt_io_channel_test_point(void)
{
   if ((retro_atomic_fetch_add_int(&test_points, 1) % 3) == 0)
      sched_yield();
}

static item *item_new(int id)
{
   item *it = (item*)malloc(sizeof(*it));
   it->node.next = NULL;
   it->id        = id;
   retro_atomic_fetch_add_int(&live_nodes, 1);
   return it;
}

static void item_free(tt_io_node *n)
{
   retro_atomic_fetch_sub_int(&live_nodes, 1);
   free(n);
}

static void worker(void *ud)
{
   tt_io_channel *c = (tt_io_channel*)ud;
   tt_io_node    *n;

   while ((n = tt_io_channel_pop(c)))
   {
      int id = ((item*)n)->id;
      retro_atomic_store_release_int(&served_at[id],
            retro_atomic_fetch_add_int(&served, 1) + 1);
      retro_atomic_fetch_add_int(&taken[id], 1);
      if (retro_atomic_load_acquire_int(&slow))
         retro_sleep(1);
      item_free(n);
      tt_io_channel_push_response(c, &item_new(id)->node);
   }
   tt_io_channel_release(c);
   retro_atomic_fetch_add_int(&exits, 1);
}

static int workers = WORKERS;

static tt_io_channel *start(void)
{
   int i;
   tt_io_channel *c = tt_io_channel_new(item_free, item_free);
   if (!c)
   {
      FAIL("channel could not be created");
      exit(1);
   }
   retro_atomic_store_release_int(&exits, 0);
   for (i = 0; i < workers; i++)
   {
      sthread_t *t;
      tt_io_channel_acquire(c);
      t = sthread_create(worker, c);
      if (t)
         sthread_detach(t);
      else
      {
         FAIL("worker could not be created");
         exit(1);
      }
   }
   return c;
}

/* Stops the channel and waits for the workers to be gone. */
static void stop(tt_io_channel *c, const char *what)
{
   int waited = 0;
   tt_io_channel_stop(c);
   while (retro_atomic_load_acquire_int(&exits) != workers && waited++ < 10000)
      retro_sleep(1);
   if (retro_atomic_load_acquire_int(&exits) != workers)
      FAIL("%s: %d of %d workers gone after stop", what,
            retro_atomic_load_acquire_int(&exits), workers);
}

static int collect(tt_io_channel *c)
{
   int got = 0;
   tt_io_node *n = tt_io_channel_take_responses(c);
   while (n)
   {
      tt_io_node *next = n->next;
      answered[((item*)n)->id]++;
      item_free(n);
      n = next;
      got++;
   }
   return got;
}

/* Collects until `want` responses are in, or gives up after ~20 s. */
static void collect_until(tt_io_channel *c, int *have, int want, const char *what)
{
   int idle = 0;
   while (*have < want && idle < 10000)
   {
      int got = collect(c);
      *have  += got;
      if (!got)
      {
         tt_io_channel_wait_response(c, 2000);
         idle++;
      }
      else
         idle = 0;
   }
   if (*have < want)
      FAIL("%s: %d of %d responses arrived", what, *have, want);
}

static void reset_ids(void)
{
   int i;
   for (i = 0; i < MAX_IDS; i++)
   {
      retro_atomic_store_release_int(&taken[i], 0);
      retro_atomic_store_release_int(&served_at[i], 0);
      answered[i] = 0;
   }
   retro_atomic_store_release_int(&served, 0);
}

static bool match_id(const tt_io_node *n, void *ctx)
{
   return ((const item*)n)->id == *(int*)ctx;
}

int main(int argc, char **argv)
{
   int rounds = (argc > 1) ? atoi(argv[1]) : 20000;
   tt_io_channel *c;
   unsigned rng = 99;
   int i, have;

   if (rounds > MAX_IDS)
      rounds = MAX_IDS;
   for (i = 0; i < MAX_IDS; i++)
   {
      retro_atomic_int_init(&taken[i], 0);
      retro_atomic_int_init(&served_at[i], 0);
   }
   retro_atomic_int_init(&served, 0);
   retro_atomic_int_init(&exits, 0);
   retro_atomic_int_init(&live_nodes, 0);
   retro_atomic_int_init(&slow, 0);
   retro_atomic_int_init(&test_points, 0);

   /* once */
   c    = start();
   have = 0;
   for (i = 0; i < rounds; i++)
   {
      rng = rng * 1103515245u + 12345u;
      tt_io_channel_push(c, &item_new(i)->node, ((rng >> 16) % 8) == 0);
      if ((i % 97) == 0)
         have += collect(c);
   }
   collect_until(c, &have, rounds, "once");
   for (i = 0; i < rounds; i++)
   {
      int t = retro_atomic_load_acquire_int(&taken[i]);
      if (t != 1 || answered[i] != 1)
      {
         FAIL("once: request %d taken %d times, answered %d times", i, t, answered[i]);
         break;
      }
   }
   stop(c, "once");
   reset_ids();

   /* priority */
   {
      int before, after;
      retro_atomic_store_release_int(&slow, 1);
      c    = start();
      have = 0;
      for (i = 0; i < 1000; i++)
         tt_io_channel_push(c, &item_new(i)->node, false);
      before = retro_atomic_load_acquire_int(&served);
      tt_io_channel_push(c, &item_new(1000)->node, true);
      while (!answered[1000] && have < 1001)
         collect_until(c, &have, have + 1, "priority");
      after = retro_atomic_load_acquire_int(&served_at[1000]);
      if (after - before > 2 * WORKERS)
         FAIL("priority: %d requests were taken between queueing the high"
               " priority one and serving it", after - before);
      stop(c, "priority");
      have = 0;
      reset_ids();
   }

   /* promote */
   {
      int before, after, id;
      c = start();
      for (i = 0; i < 1000; i++)
         tt_io_channel_push(c, &item_new(i)->node, false);
      id = 5000;
      if (tt_io_channel_promote(c, match_id, &id))
         FAIL("promote: a request that was never queued was promoted");
      id = 999;      /* the newest: still in the backlog, a full window and more behind */
      before = retro_atomic_load_acquire_int(&served);
      if (!tt_io_channel_promote(c, match_id, &id))
         FAIL("promote: a request in the backlog was not promoted");
      while (!answered[999] && have < 1000)
         collect_until(c, &have, have + 1, "promote");
      after = retro_atomic_load_acquire_int(&served_at[999]);
      if (after - before > 2 * WORKERS)
         FAIL("promote: %d requests were taken between the promotion and"
               " serving it", after - before);
      id = 999;      /* answered by now */
      if (tt_io_channel_promote(c, match_id, &id))
         FAIL("promote: a request already served was promoted");
      stop(c, "promote");
      have = 0;
      reset_ids();
   }

   /* wakeup: one worker, one request at a time, the next queued the
    * moment the last is answered - which is exactly when the worker is
    * deciding to sleep. A request it sleeps through is never answered. */
   retro_atomic_store_release_int(&slow, 0);
   workers = 1;
   c       = start();
   for (i = 0; i < 3000 && !failures; i++)
   {
      have = 0;
      tt_io_channel_push(c, &item_new(i)->node, (i & 1) != 0);
      collect_until(c, &have, 1, "wakeup");
   }
   stop(c, "wakeup");
   workers = WORKERS;
   have    = 0;
   reset_ids();

   /* wait: nothing queued, nothing delivered - must come back */
   retro_atomic_store_release_int(&slow, 0);
   c = start();
   tt_io_channel_wait_response(c, 2000);
   stop(c, "wait");

   /* stop with work outstanding, and restart */
   retro_atomic_store_release_int(&slow, 1);
   for (i = 0; i < 200; i++)
   {
      int j;
      c = start();
      for (j = 0; j < 300; j++)
         tt_io_channel_push(c, &item_new(j)->node, (j % 5) == 0);
      if (i & 1)
         collect(c);
      stop(c, "stop");
      reset_ids();
   }

   /* Every worker has released by now, so the channels are gone and
    * every node with them. */
   {
      int waited = 0;
      while (retro_atomic_load_acquire_int(&live_nodes) && waited++ < 2000)
         retro_sleep(1);
      if (retro_atomic_load_acquire_int(&live_nodes))
         FAIL("%d nodes were never freed", retro_atomic_load_acquire_int(&live_nodes));
   }

   if (failures)
   {
      fprintf(stderr, "tt_io_channel_test: FAILED\n");
      return 1;
   }
   printf("tt_io_channel_test: ok (%d requests, %d workers)\n", rounds, WORKERS);
   return 0;
}
