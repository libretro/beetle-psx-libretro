/* The GPU thread: software drawing off the emulation thread.
 *
 * Textually included at the end of gpu.c, after everything it calls.
 *
 * The emulation thread's PS_GPU (GPU, role FRONT) decodes every GP0
 * command as it always did: FIFO, emulated timing, GPUSTAT, the GL/Vulkan
 * renderer calls.  It does not touch VRAM.  Each command it decodes goes
 * into a single-producer single-consumer ring of words, written once from
 * the FIFO into the ring slot, and the GPU thread runs it from there on an
 * instance of its own (GPU_R, role BACK) through the same command code,
 * drawing into VRAM.  Both instances see the same stream, so the raster
 * state they derive from it (draw area, offset, texture page and window,
 * mask bits, the pending quad or polyline) stays the same on both.  What
 * the drawing reads that does not come from GP0 goes into the ring too:
 * the GP1 resets that clear raster state, the display state that line
 * skipping reads, and each command's PGXP vertices as the emulation thread
 * resolved them.  Scanout lines and the rows copied into a frontend loan
 * are records as well, so the GPU thread writes the picture in order with
 * the drawing it shows.
 *
 * VRAM belongs to the GPU thread while the GPU thread runs.  The emulation
 * thread reads it only after a drain, which waits for the ring to empty,
 * and drains only at what needs VRAM: a VRAM-to-CPU transfer, the end of a
 * software-rendered frame, a masked upload to a GPU renderer,
 * GPU_get_vram(),
 * and anything that changes the GPU's state from outside the GP0 stream
 * (savestates, power, rescale, core options), which settle: drain, take
 * the texture and CLUT caches back from the GPU thread, and start it from
 * the emulation thread's state at the next record.
 *
 * There is no lock.  The ring's positions are free-running counters, each
 * written by one side with a release store and read by the other with an
 * acquire load.  The GPU thread parks on an eventcount when the ring is
 * empty, the emulation thread on another when the ring is full or it
 * drains; both re-check between announcing a wait and committing to it.
 * The producer publishes every GPU_RING_BATCH words, at every wait and at
 * the end of a frame.  A record never straddles the ring's end: a pad
 * record skips to the start instead.
 *
 * Under a GPU renderer a CPU-to-VRAM transfer's record holds the whole
 * transfer, and the renderer reads the finished upload from there
 * (rhi_intf_load_image with a stride), so it waits for nothing. */

#ifdef GPU_HAVE_THREAD
#include <rthreads/rthreads.h>
#include <rthreads/retro_eventcount.h>
#endif

/* 2 MiB: room for a whole-VRAM CPU-to-VRAM transfer in one record */
#define GPU_RING_WORDS   (1u << 19)
#define GPU_RING_MASK    (GPU_RING_WORDS - 1)
/* The producer publishes once this much is unpublished. */
#define GPU_RING_BATCH   256u
/* The consumer hands space back this often while it runs. */
#define GPU_RING_RETIRE  (GPU_RING_WORDS / 8)
/* Words a CPU-to-VRAM transfer record carries at most. */
#define GPU_FBW_CHUNK    2048u

/* Record header: type in the low byte, payload words above it. */
enum
{
   GPU_REC_PAD = 0,
   GPU_REC_CMD,      /* [cc | cont << 8 | len << 16] [len words] [PGXP] */
   GPU_REC_FBWRITE,  /* CPU-to-VRAM transfer words                      */
   GPU_REC_GP1,      /* [GP1 word]                                       */
   GPU_REC_DISP,     /* [DisplayMode] [DisplayFB_YStart] [field_ram_readout] */
   GPU_REC_SCANOUT,  /* [surface] [line] [y] [rgb24 | skip_margin << 1]
                      * [udx_start] [udx_end] [udmw] [ufb_x]           */
   GPU_REC_COPY,     /* [dst] [src] [bytes], pointers as two words each */
   GPU_REC_CLEAR     /* [surface] [rows] [cols]                         */
};
#define GPU_REC(type, len) ((uint32_t)(type) | ((uint32_t)(len) << 8))

/* A command record's boundaries for the band lanes, in its second word
 * above cc, cont and len: set by the emulation thread (gpu_thread_cmd_end) */
#define GPU_CMD_BARRIER  (1u << 24)  /* the lanes finish what came before  */
#define GPU_CMD_SERIAL   (1u << 25)  /* ...merge their caches, and lane 0
                                      * draws alone from here              */
#define GPU_CMD_WHOLE    (1u << 26)  /* lane 0 draws this one alone        */
#define GPU_CMD_BANDED   (1u << 27)  /* the lanes draw bands again after it */

#define GPU_LANES_MAX    8

#ifdef GPU_HAVE_THREAD

/* Lane 0 is the GPU thread's; lanes 1 and up are band workers'. */
static MDFN_ALIGN(64) PS_GPU GPU_L[GPU_LANES_MAX];
#define GPU_R GPU_L[0]

static uint32_t           *gpu_ring;
static retro_atomic_size_t gpu_ring_write;   /* published by the producer */
static retro_atomic_size_t gpu_ring_read;    /* published by the consumer */
static retro_eventcount_t  gpu_ec_work;      /* the consumer sleeps here  */
static retro_eventcount_t  gpu_ec_idle;      /* the producer sleeps here  */
static retro_atomic_int_t  gpu_thread_quit;
static sthread_t          *gpu_thread;

/* Band lanes.  Set while every thread is idle (gpu_back_push). */
static unsigned            gpu_lanes = 1;    /* lanes drawing bands       */
static unsigned            gpu_workers_n;    /* worker threads running    */
static sthread_t          *gpu_workers[GPU_LANES_MAX];
/* Lane 0 hands a batch of records to the workers: the batch, then a
 * release increment of its generation; each worker counts itself out of
 * gpu_batch_left when done. */
static size_t              gpu_batch_start, gpu_batch_end;
static bool                gpu_batch_whole;
static retro_atomic_int_t  gpu_batch_gen;
static retro_atomic_int_t  gpu_batch_left;
static retro_eventcount_t  gpu_ec_batch;     /* workers sleep here        */
static retro_eventcount_t  gpu_ec_done;      /* lane 0 sleeps here        */
/* GPU thread only: lane 0 draws alone until the cache is invalidated */
static bool                gpu_serial;

/* Producer only. */
static size_t   gpu_w_pos;        /* next word to write          */
static size_t   gpu_w_pub;        /* last published gpu_w_pos    */
static size_t   gpu_r_seen;       /* last gpu_ring_read seen     */
static bool     gpu_back_stale;   /* GPU_R restarts from GPU     */
static uint32_t *gpu_fbw_hdr;     /* open transfer record, or NULL */
static uint32_t gpu_fbw_len;
/* The open record holds the whole transfer, for a GPU renderer to read */
static bool     gpu_fbw_whole;
static uint32_t gpu_disp_sent[3];
static bool     gpu_disp_valid;
/* What the lanes may have written and read since their last boundary, in
 * tiles of 32x8 native pixels; whether lane 0 draws alone; whether the
 * texture cache has been read since it was last invalidated. */
static uint32_t gpu_tiles[64];
static uint32_t gpu_rtiles[64];
static bool     gpu_f_serial;
static bool     gpu_f_nonempty;

static INLINE void gpu_put_ptr(uint32_t *w, const void *p)
{
   uint64_t v = (uint64_t)(uintptr_t)p;
   w[0] = (uint32_t)v;
   w[1] = (uint32_t)(v >> 32);
}

static INLINE void *gpu_get_ptr(const uint32_t *w)
{
   return (void*)(uintptr_t)((uint64_t)w[0] | ((uint64_t)w[1] << 32));
}

/* ------------------------------------------------------------------ */
/* Consumer                                                            */

static void gpu_back_disp(PS_GPU *g, const uint32_t *p)
{
   g->DisplayMode       = p[0];
   g->DisplayFB_YStart  = p[1];
   g->field_ram_readout = p[2] != 0;
}

static void gpu_back_gp1(PS_GPU *g, uint32_t v)
{
   switch (v >> 24)
   {
      case 0x00:
         SoftResetRaster(g);
         break;
      case 0x01:
         g->InCmd = INCMD_NONE;
         break;
      case 0x09:
         g->TexDisableAllowChange = v & 1;
         break;
   }
}

static void gpu_back_cmd(PS_GPU *g, uint32_t *p)
{
   const uint32_t cc   = p[0] & 0xFF;
   const bool     cont = (p[0] >> 8) & 1;
   const unsigned len  = (p[0] >> 16) & 0xFF;

   g->pgxp_side   = p + 1 + len;
   g->pgxp_side_n = 0;
   gpu_run_command(g, cc, cont, p + 1);
}

/* One record on lane g: every lane follows every record's state, and
 * draws what its bands hold (GPU_ROW_MINE).  A scanout line belongs to
 * the lane that draws its VRAM row; the frame surface's clears and the
 * loan's copies to one drawing every row. */
static void gpu_back_run(PS_GPU *g, uint32_t *rec)
{
   const uint32_t len = rec[0] >> 8;
   uint32_t      *p   = rec + 1;
   uint32_t       i;

   switch (rec[0] & 0xFF)
   {
      case GPU_REC_CMD:
         gpu_back_cmd(g, p);
         break;
      case GPU_REC_FBWRITE:
         for (i = 0; i < len; i++)
            if (gpu_fbwrite_word(g, p[i]))
               break;
         break;
      case GPU_REC_GP1:
         gpu_back_gp1(g, p[0]);
         break;
      case GPU_REC_DISP:
         gpu_back_disp(g, p);
         break;
      case GPU_REC_SCANOUT:
         if (GPU_ROW_MINE(g, p[3] >> g->upscale_shift))
            gpu_scanout_line((const MDFN_Surface*)gpu_get_ptr(p),
                  (int32_t)p[2], p[3], (p[4] & 1) != 0, (p[4] & 2) != 0,
                  (int32_t)p[5], (int32_t)p[6], p[7], (int32_t)p[8],
                  g->vram, g->upscale_shift);
         break;
      case GPU_REC_COPY:
         if (!g->band_lanes)
            memcpy(gpu_get_ptr(p), gpu_get_ptr(p + 2), p[4]);
         break;
      case GPU_REC_CLEAR:
         if (!g->band_lanes)
            gpu_surface_clear_now((MDFN_Surface*)gpu_get_ptr(p),
                  (int32_t)p[2], (int32_t)p[3]);
         break;
      default:
         break;
   }
}

static void gpu_back_range(PS_GPU *g, size_t r, size_t e)
{
   while (r != e)
   {
      uint32_t *rec = &gpu_ring[r & GPU_RING_MASK];
      gpu_back_run(g, rec);
      r += 1 + (rec[0] >> 8);
   }
}

/* Which rows lane k draws in a batch: its bands, or for a batch lane 0
 * draws alone, all rows for lane 0 and none for the others. */
static INLINE void gpu_lane_bands(PS_GPU *g, unsigned k, bool alone)
{
   if (alone)
      g->band_lanes = k ? GPU_BAND_NONE : 0;
   else
   {
      g->band_lanes = (uint8_t)gpu_lanes;
      g->band_lane  = (uint8_t)k;
   }
}

static void gpu_worker_main(void *arg)
{
   const unsigned k    = (unsigned)(uintptr_t)arg;
   PS_GPU        *g    = &GPU_L[k];
   /* The generation the workers were started at, not the one this thread
    * first sees: lane 0 may publish a batch before it is scheduled. */
   int            seen = 0;

   sthread_setname("beetle-gpu-band");

   for (;;)
   {
      int key, gen = retro_atomic_load_acquire_int(&gpu_batch_gen);
      if (gen == seen)
      {
         key = retro_eventcount_prepare_wait(&gpu_ec_batch);
         gen = retro_atomic_load_acquire_int(&gpu_batch_gen);
         if (gen == seen)
         {
            if (retro_atomic_load_acquire_int(&gpu_thread_quit))
            {
               retro_eventcount_cancel_wait(&gpu_ec_batch);
               return;
            }
            retro_eventcount_commit_wait(&gpu_ec_batch, key);
            continue;
         }
         retro_eventcount_cancel_wait(&gpu_ec_batch);
      }
      seen = gen;
      gpu_lane_bands(g, k, gpu_batch_whole);
      gpu_back_range(g, gpu_batch_start, gpu_batch_end);
      if (retro_atomic_fetch_sub_int(&gpu_batch_left, 1) == 1)
         retro_eventcount_notify(&gpu_ec_done);
   }
}

/* Lane 0 runs [r, e) with the workers and waits for them. */
static void gpu_lanes_batch(size_t r, size_t e, bool alone)
{
   alone = alone || gpu_serial;
   gpu_batch_start = r;
   gpu_batch_end   = e;
   gpu_batch_whole = alone;
   retro_atomic_store_release_int(&gpu_batch_left, (int)gpu_lanes - 1);
   retro_atomic_fetch_add_int(&gpu_batch_gen, 1);
   retro_eventcount_notify(&gpu_ec_batch);

   gpu_lane_bands(&GPU_R, 0, alone);
   gpu_back_range(&GPU_R, r, e);

   for (;;)
   {
      int key;
      if (!retro_atomic_load_acquire_int(&gpu_batch_left))
         break;
      key = retro_eventcount_prepare_wait(&gpu_ec_done);
      if (!retro_atomic_load_acquire_int(&gpu_batch_left))
      {
         retro_eventcount_cancel_wait(&gpu_ec_done);
         break;
      }
      retro_eventcount_commit_wait(&gpu_ec_done, key);
   }
}

/* The texture cache drawing in order would hold, from the lanes' caches,
 * into lane 0's: for each index, the line of the lane that read it last
 * in drawing order (tc_pos).  While no draw has written under a cached
 * line, every lane's lines hold what VRAM holds, so the data comes along.
 * All lanes idle. */
static void gpu_lanes_merge(void)
{
   unsigned i, k;
   for (i = 0; i < 256; i++)
   {
      unsigned best = 0;
      for (k = 1; k < gpu_lanes; k++)
         if (GPU_L[k].tc_pos[i] > GPU_L[best].tc_pos[i])
            best = k;
      if (best)
      {
         GPU_R.TexCache[i]  = GPU_L[best].TexCache[i];
         GPU_R.tc_pos[i]    = GPU_L[best].tc_pos[i];
      }
   }
   GPU_R.last_tex_line = ~0U;
   GPU_R.last_tex_c    = NULL;
}

static INLINE uint32_t gpu_rec_flags(const uint32_t *rec)
{
   switch (rec[0] & 0xFF)
   {
      case GPU_REC_CMD:
         return rec[1] & (GPU_CMD_BARRIER | GPU_CMD_SERIAL | GPU_CMD_WHOLE
               | GPU_CMD_BANDED);
      case GPU_REC_COPY:
      case GPU_REC_CLEAR:
         return GPU_CMD_WHOLE;
   }
   return 0;
}

/* The records in [r, w) through the lanes: batches between boundaries. */
static void gpu_lanes_run(size_t r, size_t w)
{
   size_t retired = r;
   while (r != w)
   {
      uint32_t *rec = &gpu_ring[r & GPU_RING_MASK];
      uint32_t  f   = gpu_rec_flags(rec);
      size_t    e   = r + 1 + (rec[0] >> 8);

      if ((f & GPU_CMD_SERIAL) && !gpu_serial)
      {
         gpu_lanes_merge();
         gpu_serial = true;
      }
      if (!(f & (GPU_CMD_WHOLE | GPU_CMD_BANDED)))
         while (e != w)
         {
            uint32_t *next = &gpu_ring[e & GPU_RING_MASK];
            uint32_t  nf   = gpu_rec_flags(next);
            if (nf & (GPU_CMD_BARRIER | GPU_CMD_SERIAL | GPU_CMD_WHOLE))
               break;
            e += 1 + (next[0] >> 8);
            if (nf & GPU_CMD_BANDED)
            {
               f |= GPU_CMD_BANDED;
               break;
            }
         }
      gpu_lanes_batch(r, e, (f & GPU_CMD_WHOLE) != 0);
      if (f & GPU_CMD_BANDED)
      {
         /* The CLUT lane 0 loaded drawing alone, which the others did not
          * read VRAM for */
         unsigned k;
         for (k = 1; k < gpu_lanes; k++)
         {
            memcpy(GPU_L[k].CLUT_Cache, GPU_R.CLUT_Cache,
                  sizeof(GPU_R.CLUT_Cache));
            GPU_L[k].CLUT_Cache_VB = GPU_R.CLUT_Cache_VB;
         }
         gpu_serial = false;
      }
      r = e;
      if (r - retired >= GPU_RING_RETIRE)
      {
         retro_atomic_store_release_size(&gpu_ring_read, r);
         retro_eventcount_notify(&gpu_ec_idle);
         retired = r;
      }
   }
}

static void gpu_thread_main(void *unused)
{
   size_t r = retro_atomic_load_acquire_size(&gpu_ring_read);
   (void)unused;

   sthread_setname("beetle-gpu");

   for (;;)
   {
      size_t w = retro_atomic_load_acquire_size(&gpu_ring_write);
      size_t retired;

      if (r == w)
      {
         int key = retro_eventcount_prepare_wait(&gpu_ec_work);
         if (     retro_atomic_load_acquire_size(&gpu_ring_write) != r
               || retro_atomic_load_acquire_int(&gpu_thread_quit))
         {
            retro_eventcount_cancel_wait(&gpu_ec_work);
            if (     retro_atomic_load_acquire_int(&gpu_thread_quit)
                  && retro_atomic_load_acquire_size(&gpu_ring_write) == r)
               return;
            continue;
         }
         retro_eventcount_commit_wait(&gpu_ec_work, key);
         continue;
      }

      if (gpu_lanes > 1)
      {
         gpu_lanes_run(r, w);
         r = w;
      }
      else
      {
         retired = r;
         while (r != w)
         {
            uint32_t *rec = &gpu_ring[r & GPU_RING_MASK];
            gpu_back_run(&GPU_R, rec);
            r += 1 + (rec[0] >> 8);
            if (r - retired >= GPU_RING_RETIRE)
            {
               retro_atomic_store_release_size(&gpu_ring_read, r);
               retro_eventcount_notify(&gpu_ec_idle);
               retired = r;
            }
         }
      }
      retro_atomic_store_release_size(&gpu_ring_read, r);
      retro_eventcount_notify(&gpu_ec_idle);
   }
}

/* ------------------------------------------------------------------ */
/* Producer                                                            */

static INLINE void gpu_ring_publish(void)
{
   if (gpu_w_pub == gpu_w_pos)
      return;
   retro_atomic_store_release_size(&gpu_ring_write, gpu_w_pos);
   gpu_w_pub = gpu_w_pos;
   retro_eventcount_notify(&gpu_ec_work);
}

/* Park until the consumer's position satisfies room for need words, or,
 * with need 0, until it has caught up. */
static void gpu_ring_wait(size_t need)
{
   gpu_ring_publish();
   for (;;)
   {
      int key;
      gpu_r_seen = retro_atomic_load_acquire_size(&gpu_ring_read);
      if (need ? (gpu_w_pos + need - gpu_r_seen <= GPU_RING_WORDS)
               : (gpu_r_seen == gpu_w_pos))
         return;
      key = retro_eventcount_prepare_wait(&gpu_ec_idle);
      gpu_r_seen = retro_atomic_load_acquire_size(&gpu_ring_read);
      if (need ? (gpu_w_pos + need - gpu_r_seen <= GPU_RING_WORDS)
               : (gpu_r_seen == gpu_w_pos))
      {
         retro_eventcount_cancel_wait(&gpu_ec_idle);
         return;
      }
      retro_eventcount_commit_wait(&gpu_ec_idle, key);
   }
}

static void gpu_back_push(void);

static INLINE void gpu_fbw_close(void)
{
   if (!gpu_fbw_hdr)
      return;
   *gpu_fbw_hdr  = GPU_REC(GPU_REC_FBWRITE, gpu_fbw_len);
   gpu_w_pos    += 1 + gpu_fbw_len;
   gpu_fbw_hdr   = NULL;
   gpu_fbw_whole = false;
   if (gpu_w_pos - gpu_w_pub >= GPU_RING_BATCH)
      gpu_ring_publish();
}

/* Room for n contiguous words, a record's header included; the record is
 * written in place and handed over by gpu_ring_commit. */
static uint32_t *gpu_ring_reserve(uint32_t n)
{
   uint32_t at;

   gpu_fbw_close();
   if (gpu_back_stale)
      gpu_back_push();

   at = (uint32_t)(gpu_w_pos & GPU_RING_MASK);
   if (at + n > GPU_RING_WORDS)
   {
      uint32_t pad = GPU_RING_WORDS - at;
      if (gpu_w_pos + pad - gpu_r_seen > GPU_RING_WORDS)
         gpu_ring_wait(pad);
      gpu_ring[at] = GPU_REC(GPU_REC_PAD, pad - 1);
      gpu_w_pos   += pad;
   }
   if (gpu_w_pos + n - gpu_r_seen > GPU_RING_WORDS)
      gpu_ring_wait(n);
   return &gpu_ring[gpu_w_pos & GPU_RING_MASK];
}

static INLINE void gpu_ring_commit(uint32_t n)
{
   gpu_w_pos += n;
   if (gpu_w_pos - gpu_w_pub >= GPU_RING_BATCH)
      gpu_ring_publish();
}

/* The display state line skipping reads, when it has changed. */
static INLINE void gpu_thread_disp(void)
{
   uint32_t *rec;
   if (     gpu_disp_valid
         && gpu_disp_sent[0] == GPU.DisplayMode
         && gpu_disp_sent[1] == GPU.DisplayFB_YStart
         && gpu_disp_sent[2] == (uint32_t)GPU.field_ram_readout)
      return;
   rec    = gpu_ring_reserve(4);
   rec[0] = GPU_REC(GPU_REC_DISP, 3);
   rec[1] = gpu_disp_sent[0] = GPU.DisplayMode;
   rec[2] = gpu_disp_sent[1] = GPU.DisplayFB_YStart;
   rec[3] = gpu_disp_sent[2] = GPU.field_ram_readout;
   gpu_disp_valid = true;
   gpu_ring_commit(4);
}

/* Starts a command record; its words go where the return points. */
static uint32_t *gpu_thread_cmd_begin(unsigned len)
{
   uint32_t *rec;
   gpu_thread_disp();
   rec             = gpu_ring_reserve(2 + len + 12);
   GPU.pgxp_side   = rec + 2 + len;
   GPU.pgxp_side_n = 0;
   return rec;
}

/* Tile rows (bits of a 64-bit mask) and columns (of a 32-bit one) a
 * native rectangle covers, wrapping at 1024x512 as VRAM does. */
static INLINE uint32_t gpu_tile_cols(uint32_t rect0, uint32_t rect1)
{
   const uint32_t x = rect0 & 1023, w = rect1 & 0xFFFF;
   const uint32_t c0 = x >> 5, n = ((x + w - 1) >> 5) - c0 + 1;
   uint32_t m;
   if (n >= 32)
      return ~0u;
   m = (1u << n) - 1;
   return c0 ? (m << c0) | (m >> (32 - c0)) : m;
}

static INLINE uint64_t gpu_tile_rows(uint32_t rect0, uint32_t rect1)
{
   const uint32_t y = (rect0 >> 16) & 511, h = rect1 >> 16;
   const uint32_t r0 = y >> 3, n = ((y + h - 1) >> 3) - r0 + 1;
   uint64_t m;
   if (n >= 64)
      return ~(uint64_t)0;
   m = ((uint64_t)1 << n) - 1;
   return r0 ? (m << r0) | (m >> (64 - r0)) : m;
}

static void gpu_tiles_mark(uint32_t *t, uint32_t rect0, uint32_t rect1)
{
   const uint32_t cols = gpu_tile_cols(rect0, rect1);
   const uint64_t rows = gpu_tile_rows(rect0, rect1);
   unsigned r;
   for (r = 0; r < 64; r++)
      if (rows & ((uint64_t)1 << r))
         t[r] |= cols;
}

static bool gpu_tiles_hit(const uint32_t *t, uint32_t rect0, uint32_t rect1)
{
   const uint32_t cols = gpu_tile_cols(rect0, rect1);
   const uint64_t rows = gpu_tile_rows(rect0, rect1);
   unsigned r;
   for (r = 0; r < 64; r++)
      if ((rows & ((uint64_t)1 << r)) && (t[r] & cols))
         return true;
   return false;
}

static INLINE bool gpu_rects_meet(uint32_t a0, uint32_t a1,
      uint32_t b0, uint32_t b1)
{
   return (gpu_tile_cols(a0, a1) & gpu_tile_cols(b0, b1))
      && (gpu_tile_rows(a0, a1) & gpu_tile_rows(b0, b1));
}

/* The lanes' boundary before and after a command, from what it wrote and
 * read (gpu.h, fp_*):
 *  - a command that reads, from its texture page or CLUT, tiles the lanes
 *    may have written since their last boundary, or writes tiles they may
 *    have read, waits for them;
 *  - a draw under the texture page while the cache may hold lines of it,
 *    the command's own reads included, leaves cached lines that VRAM no
 *    longer matches: the lanes merge their caches into the one drawing in
 *    order would hold, and lane 0 draws alone until the cache is next
 *    invalidated;
 *  - a copy reads anywhere, and runs alone.
 * Rectangles are worked at the granularity of tiles, which only ever
 * makes a boundary more likely. */
static uint32_t gpu_lanes_flags(uint32_t cc, bool cont)
{
   const bool     inval    = GPU.tc_invalidated;
   const bool     write    = GPU.fp_write[1] != 0;
   /* The 256 halfwords the texture cache can hold lines of, across the
    * texture modes, at the texture page */
   const uint32_t page0    = GPU.TexPageX | (GPU.TexPageY << 16);
   const uint32_t page1    = 256u | (256u << 16);
   /* The halfwords this command's texels come from: the page at its mode */
   const uint32_t read1    = (64u << (GPU.TexMode ? (GPU.TexMode > 1 ? 2 : 1)
            : 0)) | (256u << 16);
   const bool     nonempty = (!inval && gpu_f_nonempty) || GPU.fp_textured;
   const bool     dirty    = write && nonempty
      && gpu_rects_meet(GPU.fp_write[0], GPU.fp_write[1], page0, page1);
   uint32_t       f        = 0;

   if (!cont && cc >= 0x80 && cc <= 0x9F)
      f |= GPU_CMD_WHOLE;
   if (!gpu_f_serial)
   {
      if (dirty)
         f |= GPU_CMD_SERIAL;
      else if ((GPU.fp_textured
               && (   gpu_tiles_hit(gpu_tiles, page0, read1)
                   || (GPU.fp_clut[1]
                      && gpu_tiles_hit(gpu_tiles,
                         GPU.fp_clut[0], GPU.fp_clut[1]))))
            || (write && gpu_tiles_hit(gpu_rtiles,
                  GPU.fp_write[0], GPU.fp_write[1])))
         f |= GPU_CMD_BARRIER;
   }
   else if (inval && !dirty)
      f |= GPU_CMD_BANDED;

   if (f)
   {
      memset(gpu_tiles,  0, sizeof(gpu_tiles));
      memset(gpu_rtiles, 0, sizeof(gpu_rtiles));
   }
   if (f & GPU_CMD_SERIAL)
      gpu_f_serial = true;
   if (f & GPU_CMD_BANDED)
      gpu_f_serial = false;
   else if (!gpu_f_serial && !(f & GPU_CMD_WHOLE))
   {
      if (write)
         gpu_tiles_mark(gpu_tiles, GPU.fp_write[0], GPU.fp_write[1]);
      if (GPU.fp_textured)
      {
         gpu_tiles_mark(gpu_rtiles, page0, read1);
         if (GPU.fp_clut[1])
            gpu_tiles_mark(gpu_rtiles, GPU.fp_clut[0], GPU.fp_clut[1]);
      }
   }

   gpu_f_nonempty     = nonempty;
   GPU.tc_invalidated = false;
   return f;
}

static void gpu_thread_cmd_end(uint32_t *rec, uint32_t cc, bool cont,
      unsigned len)
{
   const uint32_t plen = 1 + len + 4 * GPU.pgxp_side_n;
   rec[0] = GPU_REC(GPU_REC_CMD, plen);
   rec[1] = cc | ((uint32_t)cont << 8) | ((uint32_t)len << 16);
   if (GPU.tc_track)
      rec[1] |= gpu_lanes_flags(cc, cont);
   gpu_ring_commit(1 + plen);
}

/* A CPU-to-VRAM transfer of words words starts.  Under a GPU renderer
 * its record holds all of it, contiguous, so that the renderer can read
 * the finished transfer out of the ring (gpu_thread_fbwrite_lend)
 * instead of waiting for it to reach VRAM.  A masked transfer needs VRAM
 * itself, and on a big-endian host the words' halves are not the pixels'
 * order, so those go in chunks and the upload waits. */
static void gpu_thread_fbwrite_begin(uint32_t words)
{
#ifndef MSB_FIRST
   if (     rhi_intf_is_type() != RHI_SOFTWARE
         && !GPU.MaskEvalAND
         && words < GPU_RING_WORDS / 2)
   {
      gpu_fbw_hdr   = gpu_ring_reserve(1 + words);
      gpu_fbw_len   = 0;
      gpu_fbw_whole = true;
   }
#endif
}

static void gpu_thread_fbwrite(uint32_t word)
{
   if (!gpu_fbw_hdr)
   {
      gpu_fbw_hdr = gpu_ring_reserve(1 + GPU_FBW_CHUNK);
      gpu_fbw_len = 0;
   }
   gpu_fbw_hdr[1 + gpu_fbw_len++] = word;
   if (!gpu_fbw_whole && gpu_fbw_len == GPU_FBW_CHUNK)
      gpu_fbw_close();
}

/* The finished transfer's pixels, in place in its record, rows the
 * transfer's width apart; NULL if it went in chunks.  With the mask bit
 * forced on, it goes into the words here, as the GPU thread puts it into
 * VRAM.  Valid until the record is handed over (gpu_thread_fbwrite_end). */
static const uint16_t *gpu_thread_fbwrite_lend(void)
{
   uint32_t i;
   if (!gpu_fbw_whole || !gpu_fbw_hdr)
      return NULL;
   if (GPU.MaskSetOR)
      for (i = 0; i < gpu_fbw_len; i++)
         gpu_fbw_hdr[1 + i] |= 0x80008000u;
   return (const uint16_t*)(gpu_fbw_hdr + 1);
}

static void gpu_thread_fbwrite_end(void)
{
   gpu_fbw_close();
}

static void gpu_thread_gp1(uint32_t v)
{
   uint32_t *rec = gpu_ring_reserve(2);
   rec[0] = GPU_REC(GPU_REC_GP1, 1);
   rec[1] = v;
   gpu_ring_commit(2);
}

static void gpu_thread_scanout(const MDFN_Surface *surface,
      int32_t dest_line, uint32_t y, bool rgb24, bool skip_margin,
      int32_t udx_start, int32_t udx_end, uint32_t udmw, int32_t ufb_x)
{
   uint32_t *rec = gpu_ring_reserve(10);
   rec[0] = GPU_REC(GPU_REC_SCANOUT, 9);
   gpu_put_ptr(rec + 1, surface);
   rec[3] = (uint32_t)dest_line;
   rec[4] = y;
   rec[5] = (rgb24 ? 1u : 0u) | (skip_margin ? 2u : 0u);
   rec[6] = (uint32_t)udx_start;
   rec[7] = (uint32_t)udx_end;
   rec[8] = udmw;
   rec[9] = (uint32_t)ufb_x;
   gpu_ring_commit(10);
}

static void gpu_thread_copy(void *dst, const void *src, size_t bytes)
{
   uint32_t *rec = gpu_ring_reserve(6);
   rec[0] = GPU_REC(GPU_REC_COPY, 5);
   gpu_put_ptr(rec + 1, dst);
   gpu_put_ptr(rec + 3, src);
   rec[5] = (uint32_t)bytes;
   gpu_ring_commit(6);
}

static void gpu_thread_clear(MDFN_Surface *surface, int32_t rows,
      int32_t cols)
{
   uint32_t *rec = gpu_ring_reserve(5);
   rec[0] = GPU_REC(GPU_REC_CLEAR, 4);
   gpu_put_ptr(rec + 1, surface);
   rec[3] = (uint32_t)rows;
   rec[4] = (uint32_t)cols;
   gpu_ring_commit(5);
}

/* Wait for the GPU thread to finish everything handed to it. */
static void gpu_thread_drain(void)
{
   if (GPU.role != GPU_ROLE_FRONT)
      return;
   gpu_fbw_close();
   gpu_ring_wait(0);
}

/* The lanes take the emulation thread's state.  Called with every thread
 * idle, before the first record after a settle.  The lanes draw bands
 * when there are more than one and either they were asked for or the
 * picture is upscaled. */
static unsigned gpu_lane_count = 1;   /* lanes the threads were started for */
static bool     gpu_lanes_forced;

static void gpu_back_push(void)
{
   unsigned k;

   gpu_lanes = (gpu_lane_count > 1 && (gpu_lanes_forced || GPU.upscale_shift))
      ? gpu_lane_count : 1;
   for (k = 0; k < gpu_lane_count; k++)
   {
      PS_GPU *g = &GPU_L[k];
      memcpy(g, &GPU, sizeof(*g));
      gpu_set_role(g, GPU_ROLE_BACK);
      g->band_lanes    = 0;
      g->band_lane     = (uint8_t)k;
      g->tc_track      = gpu_lanes > 1;
      g->tc_draw       = 0;
      g->tc_stamp      = 0;
      memset(g->tc_pos, 0, sizeof(g->tc_pos));
      g->last_tex_line = ~0U;
      g->last_tex_c    = NULL;
      g->pgxp_side     = NULL;
   }
   gpu_serial          = false;
   /* The emulation thread works out the lanes' boundaries when they draw
    * bands (GPU_FP) */
   GPU.tc_track        = gpu_lanes > 1;
   memset(gpu_tiles,  0, sizeof(gpu_tiles));
   memset(gpu_rtiles, 0, sizeof(gpu_rtiles));
   gpu_f_serial        = false;
   gpu_f_nonempty      = true;
   gpu_disp_sent[0]    = GPU.DisplayMode;
   gpu_disp_sent[1]    = GPU.DisplayFB_YStart;
   gpu_disp_sent[2]    = GPU.field_ram_readout;
   gpu_disp_valid      = true;
   gpu_back_stale      = false;
}

/* Before the GPU's state is read or changed from outside the GP0 stream:
 * the GPU thread finishes, the emulation thread takes back the texture
 * cache drawing in order would hold and the CLUT cache, and the lanes
 * restart from the emulation thread's state at the next record. */
static void gpu_thread_settle(void)
{
   if (GPU.role != GPU_ROLE_FRONT)
      return;
   gpu_thread_drain();
   if (gpu_back_stale)
      return;
   if (gpu_lanes > 1 && !gpu_serial)
      gpu_lanes_merge();
   memcpy(GPU.TexCache, GPU_R.TexCache, sizeof(GPU.TexCache));
   memcpy(GPU.CLUT_Cache, GPU_R.CLUT_Cache, sizeof(GPU.CLUT_Cache));
   GPU.last_tex_line = ~0U;
   GPU.last_tex_c    = NULL;
   gpu_back_stale    = true;
}

static void gpu_thread_stop(void);

static void gpu_thread_start(unsigned lanes)
{
   unsigned k;
   if (gpu_thread)
      return;
   gpu_ring = (uint32_t*)malloc(GPU_RING_WORDS * sizeof(uint32_t));
   if (!gpu_ring)
      return;
   retro_atomic_size_init(&gpu_ring_write, 0);
   retro_atomic_size_init(&gpu_ring_read, 0);
   retro_atomic_int_init(&gpu_thread_quit, 0);
   retro_atomic_int_init(&gpu_batch_gen, 0);
   retro_atomic_int_init(&gpu_batch_left, 0);
   gpu_w_pos = gpu_w_pub = gpu_r_seen = 0;
   gpu_fbw_hdr = NULL;
   gpu_lane_count = 1;
   gpu_workers_n  = 0;
   if (!retro_eventcount_init(&gpu_ec_work))
      goto fail_ring;
   if (!retro_eventcount_init(&gpu_ec_idle))
      goto fail_work;
   if (!retro_eventcount_init(&gpu_ec_batch))
      goto fail_idle;
   if (!retro_eventcount_init(&gpu_ec_done))
      goto fail_batch;
   gpu_thread = sthread_create(gpu_thread_main, NULL);
   if (!gpu_thread)
      goto fail_done;
   /* Lanes the workers could not be had for are simply not used */
   for (k = 1; k < lanes; k++)
   {
      gpu_workers[k] = sthread_create(gpu_worker_main, (void*)(uintptr_t)k);
      if (!gpu_workers[k])
         break;
      gpu_workers_n++;
   }
   gpu_lane_count = 1 + gpu_workers_n;
   return;
fail_done:
   retro_eventcount_free(&gpu_ec_done);
fail_batch:
   retro_eventcount_free(&gpu_ec_batch);
fail_idle:
   retro_eventcount_free(&gpu_ec_idle);
fail_work:
   retro_eventcount_free(&gpu_ec_work);
fail_ring:
   free(gpu_ring);
   gpu_ring = NULL;
}

static void gpu_thread_stop(void)
{
   unsigned k;
   if (!gpu_thread)
      return;
   gpu_thread_settle();
   gpu_set_role(&GPU, GPU_ROLE_INLINE);
   GPU.tc_track = 0;
   retro_atomic_store_release_int(&gpu_thread_quit, 1);
   retro_eventcount_notify(&gpu_ec_work);
   retro_eventcount_notify(&gpu_ec_batch);
   sthread_join(gpu_thread);
   gpu_thread = NULL;
   for (k = 1; k <= gpu_workers_n; k++)
   {
      sthread_join(gpu_workers[k]);
      gpu_workers[k] = NULL;
   }
   gpu_workers_n  = 0;
   gpu_lane_count = 1;
   retro_eventcount_free(&gpu_ec_done);
   retro_eventcount_free(&gpu_ec_batch);
   retro_eventcount_free(&gpu_ec_idle);
   retro_eventcount_free(&gpu_ec_work);
   free(gpu_ring);
   gpu_ring = NULL;
}

void GPU_ThreadSelect(int lanes)
{
   static int      cores = -1;
   static int      asked;
   unsigned        want_lanes;

   /* Without a software renderer or framebuffer there is nothing to draw.
    * Fast physical cores: one for each thread; without the topology (the
    * consoles) everything stays inline unless a lane count is chosen. */
   if (cores < 0)
   {
      unsigned fast = 0, slow = 0;
      cores = sthread_get_core_topology(&fast, &slow) ? (int)fast : 1;
   }
   if (!rhi_intf_has_software_renderer() || (cores < 2 && lanes < 0))
      lanes = 0;

   /* Automatic: a lane for each fast core the emulation thread leaves */
   want_lanes = lanes < 0 ? (unsigned)(cores - 1) : (unsigned)lanes;
   if (want_lanes > GPU_LANES_MAX)
      want_lanes = GPU_LANES_MAX;

   /* Other lanes than the threads were started for: start them again */
   if (gpu_thread && lanes != asked)
   {
      gpu_thread_stop();
      gpu_back_stale = true;
   }
   asked = lanes;

   if (lanes && GPU.role == GPU_ROLE_INLINE)
   {
      gpu_thread_start(want_lanes ? want_lanes : 1);
      if (!gpu_thread)
         return;
      gpu_lanes_forced = lanes > 0;
      gpu_set_role(&GPU, GPU_ROLE_FRONT);
      gpu_back_stale       = true;
   }
   else if (!lanes && GPU.role == GPU_ROLE_FRONT)
   {
      gpu_thread_settle();
      gpu_set_role(&GPU, GPU_ROLE_INLINE);
      GPU.tc_track = 0;
   }
}

void GPU_ThreadFrameEnd(void)
{
   if (GPU.role != GPU_ROLE_FRONT)
      return;
   /* The software renderer presents what the GPU thread scanned out;
    * the GPU renderers present their own picture and keep going. */
   if (rhi_intf_is_type() == RHI_SOFTWARE)
      gpu_thread_drain();
   else
   {
      gpu_fbw_close();
      gpu_ring_publish();
   }
}

void GPU_ThreadSettle(void)
{
   gpu_thread_settle();
}

#else /* !GPU_HAVE_THREAD: everything runs inline */

static INLINE uint32_t *gpu_thread_cmd_begin(unsigned len) { (void)len; return NULL; }
static INLINE void gpu_thread_cmd_end(uint32_t *rec, uint32_t cc, bool cont, unsigned len) { }
static INLINE void gpu_thread_fbwrite(uint32_t word) { }
static INLINE void gpu_thread_fbwrite_begin(uint32_t words) { }
static INLINE const uint16_t *gpu_thread_fbwrite_lend(void) { return NULL; }
static INLINE void gpu_thread_fbwrite_end(void) { }
static INLINE void gpu_thread_gp1(uint32_t v) { }
static INLINE void gpu_thread_scanout(const MDFN_Surface *surface,
      int32_t dest_line, uint32_t y, bool rgb24, bool skip_margin,
      int32_t udx_start, int32_t udx_end, uint32_t udmw, int32_t ufb_x) { }
static INLINE void gpu_thread_copy(void *dst, const void *src, size_t bytes) { }
static INLINE void gpu_thread_clear(MDFN_Surface *surface, int32_t rows,
      int32_t cols) { }
static INLINE void gpu_thread_drain(void) { }
static INLINE void gpu_thread_settle(void) { }
static INLINE void gpu_thread_stop(void) { }

void GPU_ThreadSelect(int lanes) { (void)lanes; }
void GPU_ThreadFrameEnd(void) { }
void GPU_ThreadSettle(void) { }

#endif
