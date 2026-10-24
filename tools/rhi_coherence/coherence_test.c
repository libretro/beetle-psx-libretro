/* Regression test for the VRAM readback coherence map (rhi/rhi_coherence.h)
 * that rhi_intf_read_vram uses to skip or shrink blocking GPU->CPU reads.
 *
 * A model GPU VRAM and the CPU copy (g->vram) are driven by random op
 * streams shaped like the emulator's: primitives inside a draw area, draw
 * area changes, fills and copies, masked and unmasked CPU uploads (with
 * VRAM wrap), and readbacks of random rects, whole-VRAM savestate reads
 * included. A readback transfers only the rects tt_coh_read_rects returns.
 * After every readback the CPU copy must equal the GPU over the requested
 * rect - what an always-read-everything baseline guarantees.
 *
 * Also reports how many pixels the bounded reads moved against reading
 * each requested rect whole.
 *
 * Build and run: tools/rhi_coherence/run.sh */

#include <stdio.h>
#include <stdlib.h>

#include "rhi/rhi_coherence.h"

#define W TT_COH_VRAM_W
#define H TT_COH_VRAM_H

static uint16_t gpu[W * H];
static uint16_t cpu[W * H];
static unsigned rng_state = 1u;
static unsigned long moved, requested;
static unsigned failures;

static unsigned rnd(unsigned n)
{
   rng_state = rng_state * 1103515245u + 12345u;
   return (rng_state >> 8) % n;
}

static uint16_t *px(uint16_t *v, unsigned x, unsigned y)
{
   return &v[(y & (H - 1)) * W + (x & (W - 1))];
}

static void gpu_write(unsigned x, unsigned y, unsigned w, unsigned h)
{
   unsigned i, j;
   for (j = 0; j < h; j++)
      for (i = 0; i < w; i++)
         *px(gpu, x + i, y + j) = (uint16_t)rnd(65536);
}

static void op_draw(void)
{
   /* Primitives land inside the draw area (scissor). */
   unsigned w, h, x, y;
   if (!tt_coh_daw || !tt_coh_dah)
      return;
   w = 1 + rnd(tt_coh_daw < 64 ? tt_coh_daw : 64);
   h = 1 + rnd(tt_coh_dah < 64 ? tt_coh_dah : 64);
   x = tt_coh_dax + rnd(tt_coh_daw - w + 1);
   y = tt_coh_day + rnd(tt_coh_dah - h + 1);
   tt_coh_mark_draw_area();
   gpu_write(x, y, w, h);
}

static void op_set_draw_area(void)
{
   unsigned x0 = rnd(W), y0 = rnd(H);
   unsigned x1 = x0 + rnd(W - x0), y1 = y0 + rnd(H - y0);
   tt_coh_dax = (uint16_t)x0;
   tt_coh_day = (uint16_t)y0;
   tt_coh_daw = (uint16_t)(x1 - x0 + 1);
   tt_coh_dah = (uint16_t)(y1 - y0 + 1);
   tt_coh_da_pending = true;
}

static void op_fill_or_copy(void)
{
   unsigned x = rnd(W), y = rnd(H), w = 1 + rnd(256), h = 1 + rnd(128);
   tt_coh_dirty(x, y, w, h);
   gpu_write(x, y, w, h);
}

static void op_upload(bool masked)
{
   unsigned x = rnd(W), y = rnd(H), w = 1 + rnd(128), h = 1 + rnd(128);
   unsigned i, j;
   for (j = 0; j < h; j++)
      for (i = 0; i < w; i++)
      {
         uint16_t v = (uint16_t)rnd(65536);
         *px(cpu, x + i, y + j) = v;
         /* A masked upload skips mask-blocked pixels on the GPU. */
         if (!masked || rnd(4))
            *px(gpu, x + i, y + j) = v;
      }
   if (masked)
      tt_coh_dirty(x, y, w, h);
   else
   {
      tt_coh_clean_rect(x, y, w, h);
      tt_coh_da_pending = true;
   }
}

static void op_read(unsigned x, unsigned y, unsigned w, unsigned h)
{
   tt_coh_rect r[TT_COH_MAX_RECTS];
   unsigned    n, k, m, i, j;
   requested += (unsigned long)w * h;
   n = tt_coh_read_rects(x, y, w, h, r);
   if (n > TT_COH_MAX_RECTS)
   {
      fprintf(stderr, "FAIL read (%u,%u %ux%u): %u rects\n", x, y, w, h, n);
      failures++;
      return;
   }
   for (k = 0; k < n; k++)
   {
      /* Inside the request (a wrapping request comes back whole), and
       * no pixel fetched twice. */
      if (n > 1 && (r[k].x < x || r[k].y < y || r[k].x + r[k].w > x + w
               || r[k].y + r[k].h > y + h || !r[k].w || !r[k].h))
      {
         fprintf(stderr, "FAIL read (%u,%u %ux%u): rect %u (%u,%u %ux%u) outside\n",
               x, y, w, h, k, (unsigned)r[k].x, (unsigned)r[k].y,
               (unsigned)r[k].w, (unsigned)r[k].h);
         failures++;
         return;
      }
      for (m = k + 1; m < n; m++)
         if (tt_coh_overlap(&r[k], &r[m]))
         {
            fprintf(stderr, "FAIL read (%u,%u %ux%u): rects %u and %u overlap\n",
                  x, y, w, h, k, m);
            failures++;
            return;
         }
      moved += (unsigned long)r[k].w * r[k].h;
      for (j = 0; j < r[k].h; j++)
         for (i = 0; i < r[k].w; i++)
            *px(cpu, r[k].x + i, r[k].y + j) = *px(gpu, r[k].x + i, r[k].y + j);
   }
   if (n)
   {
      tt_coh_clean_rect(x, y, w, h);
      tt_coh_da_pending = true;
   }
   for (j = 0; j < h; j++)
      for (i = 0; i < w; i++)
         if (*px(cpu, x + i, y + j) != *px(gpu, x + i, y + j))
         {
            if (failures < 8)
               fprintf(stderr, "FAIL read (%u,%u %ux%u): pixel (%u,%u) stale"
                     " (%u rects)\n", x, y, w, h,
                     (x + i) & (W - 1), (y + j) & (H - 1), n);
            failures++;
            return;
         }
}

static void run_stream(unsigned seed, unsigned ops)
{
   unsigned n;
   rng_state = seed;
   memset(gpu, 0, sizeof(gpu));
   memset(cpu, 0, sizeof(cpu));
   tt_coh_reset();
   op_set_draw_area();
   for (n = 0; n < ops; n++)
   {
      unsigned k = rnd(100);
      if (k < 55)
         op_draw();
      else if (k < 60)
         op_set_draw_area();
      else if (k < 70)
         op_fill_or_copy();
      else if (k < 78)
         op_upload(false);
      else if (k < 81)
         op_upload(true);
      else if (k < 97)
         op_read(rnd(W), rnd(H), 1 + rnd(256), 1 + rnd(256));
      else
         op_read(0, 0, W, H);   /* savestate */
   }
}

/* A double-buffered game's frame: the drawing area is the back buffer, a
 * fill and primitives land in it, a texture is uploaded and a block copied
 * elsewhere in VRAM, then runahead takes a savestate (a whole-VRAM read).
 * Returns the share of VRAM those savestate reads moved. */
static double run_game(unsigned frames)
{
   unsigned long before_moved, before_req, f, p;
   rng_state = 7u;
   memset(gpu, 0, sizeof(gpu));
   memset(cpu, 0, sizeof(cpu));
   tt_coh_reset();
   op_read(0, 0, W, H);
   before_moved = moved;
   before_req   = requested;
   for (f = 0; f < frames; f++)
   {
      unsigned by = (f & 1) ? 256 : 0;
      tt_coh_dax = 0;
      tt_coh_day = (uint16_t)by;
      tt_coh_daw = 320;
      tt_coh_dah = 240;
      tt_coh_da_pending = true;
      tt_coh_dirty(0, by, 320, 240);
      gpu_write(0, by, 320, 240);
      for (p = 0; p < 20; p++)
         op_draw();
      {
         unsigned i, j;
         for (j = 0; j < 64; j++)
            for (i = 0; i < 64; i++)
            {
               uint16_t v = (uint16_t)rnd(65536);
               *px(cpu, 512 + i, j) = v;
               *px(gpu, 512 + i, j) = v;
            }
         tt_coh_clean_rect(512, 0, 64, 64);
         tt_coh_da_pending = true;
      }
      tt_coh_dirty(768, 0, 64, 64);
      gpu_write(768, 0, 64, 64);
      op_read(0, 0, W, H);
   }
   return (double)(moved - before_moved) / (double)(requested - before_req);
}

int main(void)
{
   double game;
   unsigned seed;
   for (seed = 1; seed <= 100 && !failures; seed++)
      run_stream(seed, 400);
   game = failures ? 0.0 : run_game(120);
   /* One frame dirties a 320x240 buffer and a 64x64 block: under 16% of
    * VRAM. A single bounding box around both would move about 80%. */
   if (!failures && game > 0.20)
   {
      fprintf(stderr, "FAIL game-shaped savestate reads moved %.1f%% of VRAM\n",
            100.0 * game);
      failures++;
   }
   if (failures)
   {
      fprintf(stderr, "rhi_coherence: %u failure(s)\n", failures);
      return EXIT_FAILURE;
   }
   printf("rhi_coherence: all checks passed; game-shaped savestate reads "
         "moved %.1f%% of VRAM\n", 100.0 * game);
   return EXIT_SUCCESS;
}
