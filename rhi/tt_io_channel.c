/* The channel between the render thread and the HD texture IO workers.
 *
 * One thread produces requests (the render thread) and a small pool of
 * workers takes them; the workers produce responses and the render thread
 * takes those. It used to be two linked lists of requests and one of
 * responses under a mutex, with a condition variable each way. There is
 * no lock now.
 *
 * Requests. The render thread keeps the requests the workers have not
 * been shown yet in two lists of its own, one per priority, which nobody
 * else touches. From those it fills two small rings, high priority first,
 * and the workers take from the rings, high priority first. A ring has
 * one writer, so publishing is a store of the cell and then of the tail;
 * the workers race each other for the head with a compare-and-swap. The
 * head and tail only ever count up, so a worker that wins the swap for
 * position h is the only one to take the cell it read there.
 *
 *   Promoting a request - a draw now needs a texture that was queued as
 *   a prediction - is a move between the render thread's own two lists,
 *   and costs no coordination at all. A request already in the workers'
 *   window cannot be moved, and does not need to be: at most
 *   TT_IO_WINDOW low priority requests are ever ahead of anything, and
 *   every high priority one is served before them.
 *
 *   The rings are topped up whenever the render thread comes through
 *   here: on every push, and on every take of or wait for responses,
 *   which it does at least once a frame.
 *
 * Responses. The workers push onto a list whose head is one atomic
 * pointer; the render thread takes the whole list with an exchange and
 * turns it round. Nothing ever pops a single node, so the list cannot
 * suffer the reuse problem a lock-free stack otherwise has.
 *
 * Sleeping. Workers with nothing to take sleep on an eventcount that the
 * render thread notifies when it has put something in a ring; a worker
 * looks at both rings again between announcing the wait and committing to
 * it. The render thread's bounded wait for a response is the same thing
 * the other way round.
 *
 * Lifetime. The workers are detached, so the channel is reference
 * counted: one for the render thread and one per worker, and the last to
 * let go frees it. The count is an atomic. Stopping raises a flag and
 * wakes everyone; a worker that sees it leaves without taking anything
 * more, and what is left in the rings is freed with the channel.
 *
 * tools/ttio/tt_io_channel_test.c runs it with real workers under
 * ThreadSanitizer and AddressSanitizer.
 */

#include <stdlib.h>

#include <retro_atomic.h>
#include <rthreads/retro_eventcount.h>

#include "tt_io_channel.h"

#define TT_IO_MASK (TT_IO_WINDOW - 1)

/* The test build gives up the CPU at the points where another thread
 * getting in is what the code has to survive, so that on one core the
 * interleavings actually happen. Nothing in a normal build. */
#ifdef TT_IO_CHANNEL_TEST
void tt_io_channel_test_point(void);
#define TT_IO_TEST_POINT() tt_io_channel_test_point()
#else
#define TT_IO_TEST_POINT() ((void)0)
#endif

typedef struct
{
   retro_atomic_ptr_t cell[TT_IO_WINDOW];
   retro_atomic_int_t head;   /* workers */
   retro_atomic_int_t tail;   /* render thread */
} tt_io_ring;

typedef struct
{
   tt_io_node *head;
   tt_io_node *tail;
} tt_io_list;

struct tt_io_channel
{
   tt_io_ring         ring[2];       /* [1] high priority, [0] low        */
   tt_io_list         backlog[2];    /* render thread only, same indices  */
   retro_atomic_ptr_t responses;     /* newest first                      */
   retro_eventcount_t work;          /* workers sleep: a ring was filled  */
   retro_eventcount_t delivered;     /* render thread sleeps: a response  */
   retro_atomic_int_t done;
   retro_atomic_int_t refcount;
   tt_io_free_t       free_request;
   tt_io_free_t       free_response;
};

static bool tt_io_ring_push(tt_io_ring *r, tt_io_node *n)
{
   int t = retro_atomic_load_relaxed_int(&r->tail);
   int h = retro_atomic_load_acquire_int(&r->head);
   if ((unsigned)t - (unsigned)h >= TT_IO_WINDOW)
      return false;
   retro_atomic_store_release_ptr(&r->cell[(unsigned)t & TT_IO_MASK], n);
   TT_IO_TEST_POINT();
   retro_atomic_store_release_int(&r->tail, (int)((unsigned)t + 1u));
   return true;
}

static tt_io_node *tt_io_ring_pop(tt_io_ring *r)
{
   for (;;)
   {
      tt_io_node *n;
      int h = retro_atomic_load_acquire_int(&r->head);
      int t = retro_atomic_load_acquire_int(&r->tail);
      if (h == t)
         return NULL;
      /* Published before the tail moved past h, and not written again
       * until the head has: if the swap below wins, this is ours. */
      n = (tt_io_node*)retro_atomic_load_acquire_ptr(&r->cell[(unsigned)h & TT_IO_MASK]);
      TT_IO_TEST_POINT();
      if (retro_atomic_cas_int(&r->head, h, (int)((unsigned)h + 1u)))
         return n;
   }
}

static bool tt_io_ring_empty(tt_io_ring *r)
{
   return retro_atomic_load_acquire_int(&r->head)
       == retro_atomic_load_acquire_int(&r->tail);
}

static void tt_io_list_append(tt_io_list *l, tt_io_node *n)
{
   n->next = NULL;
   if (l->tail)
      l->tail->next = n;
   else
      l->head = n;
   l->tail = n;
}

/* Render thread. Shows the workers as much of the backlog as fits. */
static void tt_io_channel_feed(tt_io_channel *c)
{
   int  prio;
   bool fed = false;
   for (prio = 1; prio >= 0; prio--)
   {
      tt_io_list *l = &c->backlog[prio];
      while (l->head)
      {
         tt_io_node *n    = l->head;
         tt_io_node *next = n->next;
         if (!tt_io_ring_push(&c->ring[prio], n))
            break;
         /* n belongs to whichever worker takes it from here on. */
         l->head = next;
         if (!next)
            l->tail = NULL;
         fed = true;
      }
   }
   if (fed)
      retro_eventcount_notify(&c->work);
}

tt_io_channel *tt_io_channel_new(tt_io_free_t free_request, tt_io_free_t free_response)
{
   unsigned i, j;
   tt_io_channel *c = (tt_io_channel*)calloc(1, sizeof(*c));
   if (!c)
      return NULL;
   for (i = 0; i < 2; i++)
   {
      for (j = 0; j < TT_IO_WINDOW; j++)
         retro_atomic_ptr_init(&c->ring[i].cell[j], NULL);
      retro_atomic_int_init(&c->ring[i].head, 0);
      retro_atomic_int_init(&c->ring[i].tail, 0);
   }
   retro_atomic_ptr_init(&c->responses, NULL);
   retro_atomic_int_init(&c->done, 0);
   retro_atomic_int_init(&c->refcount, 1);
   c->free_request  = free_request;
   c->free_response = free_response;
   if (!retro_eventcount_init(&c->work))
   {
      free(c);
      return NULL;
   }
   if (!retro_eventcount_init(&c->delivered))
   {
      retro_eventcount_free(&c->work);
      free(c);
      return NULL;
   }
   return c;
}

void tt_io_channel_acquire(tt_io_channel *c)
{
   if (c)
      retro_atomic_fetch_add_int(&c->refcount, 1);
}

void tt_io_channel_release(tt_io_channel *c)
{
   unsigned    i;
   tt_io_node *n;

   if (!c || retro_atomic_fetch_sub_int(&c->refcount, 1) != 1)
      return;

   /* The last reference: nobody else is in here any more. */
   for (i = 0; i < 2; i++)
   {
      while ((n = tt_io_ring_pop(&c->ring[i])))
         c->free_request(n);
      while ((n = c->backlog[i].head))
      {
         c->backlog[i].head = n->next;
         c->free_request(n);
      }
   }
   n = (tt_io_node*)retro_atomic_exchange_ptr(&c->responses, NULL);
   while (n)
   {
      tt_io_node *next = n->next;
      c->free_response(n);
      n = next;
   }
   retro_eventcount_free(&c->delivered);
   retro_eventcount_free(&c->work);
   free(c);
}

bool tt_io_channel_push(tt_io_channel *c, tt_io_node *request, bool high)
{
   if (!c)
      return false;
   tt_io_list_append(&c->backlog[high ? 1 : 0], request);
   tt_io_channel_feed(c);
   return true;
}

bool tt_io_channel_promote(tt_io_channel *c, tt_io_match_t match, void *ctx)
{
   tt_io_node *prev = NULL;
   tt_io_node *n;

   if (!c)
      return false;
   for (n = c->backlog[0].head; n; prev = n, n = n->next)
   {
      if (!match(n, ctx))
         continue;
      if (prev)
         prev->next = n->next;
      else
         c->backlog[0].head = n->next;
      if (c->backlog[0].tail == n)
         c->backlog[0].tail = prev;
      tt_io_list_append(&c->backlog[1], n);
      tt_io_channel_feed(c);
      return true;
   }
   return false;
}

tt_io_node *tt_io_channel_take_responses(tt_io_channel *c)
{
   tt_io_node *n;
   tt_io_node *oldest_first = NULL;

   if (!c)
      return NULL;
   tt_io_channel_feed(c);
   n = (tt_io_node*)retro_atomic_exchange_ptr(&c->responses, NULL);
   while (n)
   {
      tt_io_node *next = n->next;
      n->next          = oldest_first;
      oldest_first     = n;
      n                = next;
   }
   return oldest_first;
}

void tt_io_channel_wait_response(tt_io_channel *c, int64_t timeout_us)
{
   int key;

   if (!c)
      return;
   tt_io_channel_feed(c);
   key = retro_eventcount_prepare_wait(&c->delivered);
   if (retro_atomic_load_acquire_ptr(&c->responses))
   {
      retro_eventcount_cancel_wait(&c->delivered);
      return;
   }
   retro_eventcount_commit_wait_timeout(&c->delivered, key, timeout_us);
}

void tt_io_channel_stop(tt_io_channel *c)
{
   unsigned    i;
   tt_io_node *n;

   if (!c)
      return;
   /* What the workers were never shown is the render thread's to drop,
    * and it will not be back. */
   for (i = 0; i < 2; i++)
   {
      while ((n = c->backlog[i].head))
      {
         c->backlog[i].head = n->next;
         c->free_request(n);
      }
      c->backlog[i].tail = NULL;
   }
   retro_atomic_store_release_int(&c->done, 1);
   retro_eventcount_notify(&c->work);
   tt_io_channel_release(c);
}

tt_io_node *tt_io_channel_pop(tt_io_channel *c)
{
   for (;;)
   {
      tt_io_node *n;
      int key;

      if (retro_atomic_load_acquire_int(&c->done))
         return NULL;
      if ((n = tt_io_ring_pop(&c->ring[1])) || (n = tt_io_ring_pop(&c->ring[0])))
         return n;

      key = retro_eventcount_prepare_wait(&c->work);
      TT_IO_TEST_POINT();
      if (     retro_atomic_load_acquire_int(&c->done)
            || !tt_io_ring_empty(&c->ring[1])
            || !tt_io_ring_empty(&c->ring[0]))
      {
         retro_eventcount_cancel_wait(&c->work);
         continue;
      }
      retro_eventcount_commit_wait(&c->work, key);
   }
}

void tt_io_channel_push_response(tt_io_channel *c, tt_io_node *response)
{
   tt_io_node *head;
   do
   {
      head           = (tt_io_node*)retro_atomic_load_acquire_ptr(&c->responses);
      response->next = head;
      TT_IO_TEST_POINT();
   } while (!retro_atomic_cas_ptr(&c->responses, head, response));
   retro_eventcount_notify(&c->delivered);
}
