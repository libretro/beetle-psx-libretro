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

#include <retro_atomic.h>

#if defined(HAVE_THREADS) && defined(RETRO_ATOMIC_LOCK_FREE)
#define GPU_HAVE_THREAD 1
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

#ifdef GPU_HAVE_THREAD

static MDFN_ALIGN(64) PS_GPU GPU_R;

static uint32_t           *gpu_ring;
static retro_atomic_size_t gpu_ring_write;   /* published by the producer */
static retro_atomic_size_t gpu_ring_read;    /* published by the consumer */
static retro_eventcount_t  gpu_ec_work;      /* the consumer sleeps here  */
static retro_eventcount_t  gpu_ec_idle;      /* the producer sleeps here  */
static retro_atomic_int_t  gpu_thread_quit;
static sthread_t          *gpu_thread;

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

static void gpu_back_disp(const uint32_t *p)
{
   GPU_R.DisplayMode       = p[0];
   GPU_R.DisplayFB_YStart  = p[1];
   GPU_R.field_ram_readout = p[2] != 0;
}

static void gpu_back_gp1(uint32_t v)
{
   switch (v >> 24)
   {
      case 0x00:
         SoftResetRaster(&GPU_R);
         break;
      case 0x01:
         GPU_R.InCmd = INCMD_NONE;
         break;
      case 0x09:
         GPU_R.TexDisableAllowChange = v & 1;
         break;
   }
}

static void gpu_back_cmd(uint32_t *p)
{
   const uint32_t cc   = p[0] & 0xFF;
   const bool     cont = (p[0] >> 8) & 1;
   const unsigned len  = (p[0] >> 16) & 0xFF;

   GPU_R.pgxp_side   = p + 1 + len;
   GPU_R.pgxp_side_n = 0;
   gpu_run_command(&GPU_R, cc, cont, p + 1);
}

static void gpu_back_run(uint32_t *rec)
{
   const uint32_t len = rec[0] >> 8;
   uint32_t      *p   = rec + 1;
   uint32_t       i;

   switch (rec[0] & 0xFF)
   {
      case GPU_REC_CMD:
         gpu_back_cmd(p);
         break;
      case GPU_REC_FBWRITE:
         for (i = 0; i < len; i++)
            if (gpu_fbwrite_word(&GPU_R, p[i]))
               break;
         break;
      case GPU_REC_GP1:
         gpu_back_gp1(p[0]);
         break;
      case GPU_REC_DISP:
         gpu_back_disp(p);
         break;
      case GPU_REC_SCANOUT:
         gpu_scanout_line((const MDFN_Surface*)gpu_get_ptr(p),
               (int32_t)p[2], p[3], (p[4] & 1) != 0, (p[4] & 2) != 0,
               (int32_t)p[5], (int32_t)p[6], p[7], (int32_t)p[8],
               GPU_R.vram, GPU_R.upscale_shift);
         break;
      case GPU_REC_COPY:
         memcpy(gpu_get_ptr(p), gpu_get_ptr(p + 2), p[4]);
         break;
      case GPU_REC_CLEAR:
         gpu_surface_clear_now((MDFN_Surface*)gpu_get_ptr(p),
               (int32_t)p[2], (int32_t)p[3]);
         break;
      default:
         break;
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

      retired = r;
      while (r != w)
      {
         uint32_t *rec = &gpu_ring[r & GPU_RING_MASK];
         gpu_back_run(rec);
         r += 1 + (rec[0] >> 8);
         if (r - retired >= GPU_RING_RETIRE)
         {
            retro_atomic_store_release_size(&gpu_ring_read, r);
            retro_eventcount_notify(&gpu_ec_idle);
            retired = r;
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

static void gpu_thread_cmd_end(uint32_t *rec, uint32_t cc, bool cont,
      unsigned len)
{
   const uint32_t plen = 1 + len + 4 * GPU.pgxp_side_n;
   rec[0] = GPU_REC(GPU_REC_CMD, plen);
   rec[1] = cc | ((uint32_t)cont << 8) | ((uint32_t)len << 16);
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

/* GPU_R takes the emulation thread's state.  Called with the GPU thread
 * idle, before the first record after a settle. */
static void gpu_back_push(void)
{
   memcpy(&GPU_R, &GPU, sizeof(GPU_R));
   gpu_set_role(&GPU_R, GPU_ROLE_BACK);
   GPU_R.last_tex_line = ~0U;
   GPU_R.last_tex_c    = NULL;
   GPU_R.pgxp_side     = NULL;
   gpu_disp_sent[0]    = GPU.DisplayMode;
   gpu_disp_sent[1]    = GPU.DisplayFB_YStart;
   gpu_disp_sent[2]    = GPU.field_ram_readout;
   gpu_disp_valid      = true;
   gpu_back_stale      = false;
}

/* Before the GPU's state is read or changed from outside the GP0 stream:
 * the GPU thread finishes, the emulation thread takes its texture and CLUT
 * caches back, and the GPU thread restarts from the emulation thread's
 * state at the next record. */
static void gpu_thread_settle(void)
{
   if (GPU.role != GPU_ROLE_FRONT)
      return;
   gpu_thread_drain();
   if (gpu_back_stale)
      return;
   memcpy(GPU.TexCache, GPU_R.TexCache, sizeof(GPU.TexCache));
   memcpy(GPU.CLUT_Cache, GPU_R.CLUT_Cache, sizeof(GPU.CLUT_Cache));
   GPU.last_tex_line = ~0U;
   GPU.last_tex_c    = NULL;
   gpu_back_stale    = true;
}

static void gpu_thread_start(void)
{
   if (gpu_thread)
      return;
   gpu_ring = (uint32_t*)malloc(GPU_RING_WORDS * sizeof(uint32_t));
   if (!gpu_ring)
      return;
   retro_atomic_size_init(&gpu_ring_write, 0);
   retro_atomic_size_init(&gpu_ring_read, 0);
   retro_atomic_int_init(&gpu_thread_quit, 0);
   gpu_w_pos = gpu_w_pub = gpu_r_seen = 0;
   gpu_fbw_hdr = NULL;
   if (!retro_eventcount_init(&gpu_ec_work))
      goto fail_ring;
   if (!retro_eventcount_init(&gpu_ec_idle))
      goto fail_work;
   gpu_thread = sthread_create(gpu_thread_main, NULL);
   if (gpu_thread)
      return;
   retro_eventcount_free(&gpu_ec_idle);
fail_work:
   retro_eventcount_free(&gpu_ec_work);
fail_ring:
   free(gpu_ring);
   gpu_ring = NULL;
}

static void gpu_thread_stop(void)
{
   if (!gpu_thread)
      return;
   gpu_thread_settle();
   gpu_set_role(&GPU, GPU_ROLE_INLINE);
   retro_atomic_store_release_int(&gpu_thread_quit, 1);
   retro_eventcount_notify(&gpu_ec_work);
   sthread_join(gpu_thread);
   gpu_thread = NULL;
   retro_eventcount_free(&gpu_ec_idle);
   retro_eventcount_free(&gpu_ec_work);
   free(gpu_ring);
   gpu_ring = NULL;
}

void GPU_ThreadSelect(bool want)
{
   unsigned i;

   /* Without a software renderer or framebuffer there is nothing to draw */
   want = want && rhi_intf_has_software_renderer();

   if (want && GPU.role == GPU_ROLE_INLINE)
   {
      /* Fast physical cores: one for each thread.  Without the topology
       * (the consoles) everything stays inline. */
      static int cores = -1;
      if (cores < 0)
      {
         unsigned fast = 0, slow = 0;
         cores = sthread_get_core_topology(&fast, &slow) ? (int)fast : 1;
      }
      if (cores < 2)
         return;
      gpu_thread_start();
      if (!gpu_thread)
         return;
      /* Inline at 1x the texture cache's own tags were the timing tags */
      if (!GPU.upscale_shift)
         for (i = 0; i < 256; i++)
            GPU.TexCacheTimingTag[i] = GPU.TexCache[i].Tag;
      GPU.last_timing_line = ~0U;
      gpu_set_role(&GPU, GPU_ROLE_FRONT);
      gpu_back_stale       = true;
   }
   else if (!want && GPU.role == GPU_ROLE_FRONT)
   {
      gpu_thread_settle();
      gpu_set_role(&GPU, GPU_ROLE_INLINE);
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

void GPU_ThreadSelect(bool want) { (void)want; }
void GPU_ThreadFrameEnd(void) { }
void GPU_ThreadSettle(void) { }

#endif
