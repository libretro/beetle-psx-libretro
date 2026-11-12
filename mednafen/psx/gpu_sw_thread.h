/* C99 generation-based raster batches. The publication/completion pattern
 * follows parallel-n64's mupen64plus-video-angrylion/parallel_al.c:
 * https://github.com/libretro/parallel-n64/blob/master/mupen64plus-video-angrylion/parallel_al.c
 * Worker 0 is the caller. Eventcounts only park/wake; acquire/release atomic
 * counters publish immutable batches and make completed VRAM writes visible.
 */
#ifndef PSX_GPU_SW_THREAD_H
#define PSX_GPU_SW_THREAD_H

#if defined(HAVE_THREADS) && !defined(GPU_DISABLE_THREADS) && !defined(GPU_SW_REFERENCE)
#include <retro_atomic.h>
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#endif

#if defined(HAVE_THREADS) && defined(RETRO_ATOMIC_LOCK_FREE) && !defined(GPU_DISABLE_THREADS) && !defined(GPU_SW_REFERENCE)
#define GPU_SW_QUEUE 1024
#define GPU_SW_MAX_WORKERS 4
#ifndef GPU_SW_WORKERS
#define GPU_SW_WORKERS 0
#endif
static struct
{
   struct
   {
      sthread_t *thread;
      unsigned id;
      uint64_t jobs;
      char pad[64];
   } workers[GPU_SW_MAX_WORKERS];
   retro_eventcount_t work_ec, done_ec;
   GPU_SW_Packet packets[GPU_SW_QUEUE];
   unsigned worker_count, count, pixels, lanes;
   uint64_t generations;
   int accept_work, ec_live;
   char pad0[64];
   retro_atomic_int_t generation;
   char pad1[64];
   retro_atomic_int_t remaining;
   char pad2[64];
} gpu_sw;

static void GPU_SW_RunLane(unsigned id)
{
   unsigned i;
   for (i = 0; i < gpu_sw.count; i++)
      if (gpu_sw.packets[i].native_row % gpu_sw.worker_count == id)
      {
         GPU_SW_Execute(&gpu_sw.packets[i]);
         gpu_sw.workers[id].jobs++;
      }
}

static int GPU_SW_AwaitGeneration(int seen)
{
   for (;;)
   {
      int gen = retro_atomic_load_acquire_int(&gpu_sw.generation);
      int key;
      if (gen != seen) return gen;
      key = retro_eventcount_prepare_wait(&gpu_sw.work_ec);
      gen = retro_atomic_load_acquire_int(&gpu_sw.generation);
      if (gen != seen)
      {
         retro_eventcount_cancel_wait(&gpu_sw.work_ec);
         return gen;
      }
      retro_eventcount_commit_wait(&gpu_sw.work_ec, key);
   }
}

static void GPU_SW_Worker(void *userdata)
{
   unsigned id = *(const unsigned *)userdata;
   int seen = 0;
   for (;;)
   {
      seen = GPU_SW_AwaitGeneration(seen);
      if (!gpu_sw.accept_work) break;
      GPU_SW_RunLane(id);
      if (retro_atomic_fetch_sub_int(&gpu_sw.remaining, 1) == 1)
         retro_eventcount_notify(&gpu_sw.done_ec);
   }
}

static void GPU_SW_BumpGeneration(void)
{
   /* The conversion keeps the counter's wrap defined in C99. */
   unsigned gen = (unsigned)retro_atomic_load_acquire_int(&gpu_sw.generation);
   retro_atomic_store_release_int(&gpu_sw.generation, (int)(gen + 1u));
   retro_eventcount_notify(&gpu_sw.work_ec);
}

static void GPU_SW_Flush(void)
{
   unsigned i;
   if (!gpu_sw.count) return;
   /* Short batches or batches touching only one lane cost less inline.
    * Larger independent rows are worthwhile parallel raster work. */
   if (gpu_sw.worker_count <= 1 || gpu_sw.pixels < 8192 ||
         !(gpu_sw.lanes & (gpu_sw.lanes - 1)))
   {
      for (i = 0; i < gpu_sw.count; i++)
      {
         GPU_SW_Execute(&gpu_sw.packets[i]);
         gpu_sw.workers[0].jobs++;
      }
   }
   else
   {
      gpu_sw.generations++;
      retro_atomic_store_release_int(&gpu_sw.remaining, (int)gpu_sw.worker_count - 1);
      GPU_SW_BumpGeneration();
      GPU_SW_RunLane(0);
      while (retro_atomic_load_acquire_int(&gpu_sw.remaining))
      {
         int key = retro_eventcount_prepare_wait(&gpu_sw.done_ec);
         if (!retro_atomic_load_acquire_int(&gpu_sw.remaining))
            retro_eventcount_cancel_wait(&gpu_sw.done_ec);
         else
            retro_eventcount_commit_wait(&gpu_sw.done_ec, key);
      }
   }
   gpu_sw.count = gpu_sw.pixels = gpu_sw.lanes = 0;
   memset(gpu_sw_dirty_tiles, 0, sizeof(gpu_sw_dirty_tiles));
}

static void GPU_SW_Sync(void)
{
   GPU_SW_Flush();

   memset(gpu_sw_dirty_tiles, 0, sizeof(gpu_sw_dirty_tiles));
}

static void GPU_SW_Destroy(void)
{
   unsigned i;
   GPU_SW_Sync();
   if (gpu_sw.worker_count > 1)
   {
      gpu_sw.accept_work = 0;
      GPU_SW_BumpGeneration();
      for (i = 1; i < gpu_sw.worker_count; i++)
         sthread_join(gpu_sw.workers[i].thread);
   }
   if (gpu_sw.ec_live)
   {
      retro_eventcount_free(&gpu_sw.work_ec);
      retro_eventcount_free(&gpu_sw.done_ec);
   }
   memset(&gpu_sw, 0, sizeof(gpu_sw));
}

static void GPU_SW_Init(void)
{
   unsigned i, fast = 0, slow = 0, count = GPU_SW_WORKERS;
   GPU_SW_Destroy();
   if (!count)
   {
      if (sthread_get_core_topology(&fast, &slow)) count = fast;
      if (!count) count = 2;
   }
   if (count > GPU_SW_MAX_WORKERS) count = GPU_SW_MAX_WORKERS;
   retro_atomic_int_init(&gpu_sw.generation, 0);
   retro_atomic_int_init(&gpu_sw.remaining, 0);
   gpu_sw.worker_count = 1;
   if (count <= 1) return;
   gpu_sw.ec_live = 1;
   if (!retro_eventcount_init(&gpu_sw.work_ec) || !retro_eventcount_init(&gpu_sw.done_ec))
   {
      GPU_SW_Destroy();
      gpu_sw.worker_count = 1;
      return;
   }
   gpu_sw.accept_work = 1;
   for (i = 1; i < count; i++)
   {
      gpu_sw.workers[i].id = i;
      gpu_sw.workers[i].thread = sthread_create(GPU_SW_Worker, &gpu_sw.workers[i].id);
      if (!gpu_sw.workers[i].thread) break;
      gpu_sw.worker_count = i + 1;
   }
}

static INLINE int GPU_SW_Threaded(void) { return gpu_sw.worker_count > 1; }

static void GPU_SW_Submit(const GPU_SW_Packet *p)
{
   if (!p->count) return;
   if (!GPU_SW_Threaded()) { GPU_SW_Execute(p); return; }
   GPU_SW_MarkPacket(p);
   gpu_sw.packets[gpu_sw.count++] = *p;
   gpu_sw.pixels += p->count << (p->native_shift * 2);
   gpu_sw.lanes |= 1u << (p->native_row % gpu_sw.worker_count);
   if (gpu_sw.count == GPU_SW_QUEUE) GPU_SW_Flush();
}
#else
static void GPU_SW_Sync(void)
{

   memset(gpu_sw_dirty_tiles, 0, sizeof(gpu_sw_dirty_tiles));
}
static void GPU_SW_Init(void) { }
static void GPU_SW_Destroy(void) { GPU_SW_Sync(); }
static INLINE int GPU_SW_Threaded(void) { return 0; }
static void GPU_SW_Submit(const GPU_SW_Packet *p) { GPU_SW_Execute(p); }
#endif
#endif
