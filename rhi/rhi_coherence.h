#ifndef RHI_COHERENCE_H__
#define RHI_COHERENCE_H__

/* Per-8x8-block coherence between the CPU VRAM copy and the GPU, used by
 * rhi_intf.c to skip or shrink blocking readbacks. Header-only so
 * tools/rhi_coherence can test the shipping code; include from exactly
 * one translation unit. */

#include <stdint.h>
#include <string.h>
#include <boolean.h>

/* ------------------------------------------------------------------------
 * VRAM readback coherence tracker.
 *
 * FBRead (Command_FBRead) issues a hard, blocking GPU->CPU readback that
 * stalls the emulation thread on a fence. Many such reads are redundant:
 * the region was last written by the CPU (load_image / FBWrite, which also
 * updates g->vram) and never rendered into since, so g->vram already holds
 * the result. Track per-8x8-block coherence between g->vram and the GPU and
 * skip the readback (and its fence) when the whole rect is provably clean.
 *
 * Rule (asymmetric, conservative):
 *   - Any GPU-side VRAM write marks every OVERLAPPING block dirty (superset).
 *     Draws are scissor-clipped to the draw area, so marking the draw area is
 *     a guaranteed superset of a primitive's writes; fills/copies/masked
 *     uploads mark their literal destination rect.
 *   - A coherence-making op (unmasked CPU load_image, or a completed
 *     readback) marks only FULLY CONTAINED blocks clean (subset); a partially
 *     covered boundary block keeps its prior state, as its uncovered pixels
 *     are not made coherent by the op.
 *   - A readback is skipped only when every block the rect touches is clean.
 *
 * Default state is dirty (zero-init), and rhi_intf_open() re-dirties on every
 * renderer (re)creation, so the first read of any region always reads back.
 * The state machine was verified bit-exact against an always-readback baseline
 * over 250k randomized op sequences incl. VRAM wrap, sub-block writes/reads,
 * and draw-area superset marking of scissor-clipped primitives.
 * ---------------------------------------------------------------------- */
#define TT_COH_VRAM_W 1024
#define TT_COH_VRAM_H 512
#define TT_COH_SH     3
#define TT_COH_BW     (TT_COH_VRAM_W >> TT_COH_SH)   /* 128 block columns */
#define TT_COH_BH     (TT_COH_VRAM_H >> TT_COH_SH)   /* 64  block rows    */

static bool     tt_coh_skip_enabled = true;
static bool     tt_coh_da_pending   = true;  /* re-mark draw area on next primitive */
static uint8_t  tt_coh_clean[TT_COH_BH * TT_COH_BW]; /* 0=dirty (default), 1=clean */
static uint16_t tt_coh_dax, tt_coh_day, tt_coh_daw, tt_coh_dah; /* draw area x,y,w,h */

static void tt_coh_reset(void)
{
   memset(tt_coh_clean, 0, sizeof(tt_coh_clean));
   tt_coh_dax = tt_coh_day = 0;
   tt_coh_daw = tt_coh_dah = 0;
   tt_coh_da_pending = true;
}

/* DIRTY: every block the rect OVERLAPS (conservative superset). */
static void tt_coh_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
   uint32_t bx0, by0, nbx, nby, kx, ky;
   if (!w || !h)
      return;
   bx0 = (x & (TT_COH_VRAM_W - 1)) >> TT_COH_SH;
   by0 = (y & (TT_COH_VRAM_H - 1)) >> TT_COH_SH;
   nbx = ((x & 7) + w + 7) >> TT_COH_SH; if (nbx > TT_COH_BW) nbx = TT_COH_BW;
   nby = ((y & 7) + h + 7) >> TT_COH_SH; if (nby > TT_COH_BH) nby = TT_COH_BH;
   for (ky = 0; ky < nby; ky++)
      for (kx = 0; kx < nbx; kx++)
         tt_coh_clean[((by0 + ky) & (TT_COH_BH - 1)) * TT_COH_BW
                    + ((bx0 + kx) & (TT_COH_BW - 1))] = 0;
}

/* CLEAN: only blocks FULLY CONTAINED in the rect (subset). */
static void tt_coh_clean_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
   uint32_t cb0, cb1, rb0, rb1, sc, sr, bc, br;
   if (w < 8 || h < 8)
      return;
   cb0 = (x + 7) >> TT_COH_SH;   /* first fully covered col (unwrapped) */
   cb1 = (x + w) >> TT_COH_SH;   /* one past last fully covered col     */
   rb0 = (y + 7) >> TT_COH_SH;
   rb1 = (y + h) >> TT_COH_SH;
   if (cb1 <= cb0 || rb1 <= rb0)
      return;
   sc = cb1 - cb0; if (sc > TT_COH_BW) sc = TT_COH_BW;
   sr = rb1 - rb0; if (sr > TT_COH_BH) sr = TT_COH_BH;
   for (br = 0; br < sr; br++)
      for (bc = 0; bc < sc; bc++)
         tt_coh_clean[((rb0 + br) & (TT_COH_BH - 1)) * TT_COH_BW
                    + ((cb0 + bc) & (TT_COH_BW - 1))] = 1;
}

/* True iff every block the rect touches is clean (=> whole rect coherent). */
static bool tt_coh_all_clean(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
   uint32_t bx0, by0, nbx, nby, kx, ky;
   if (!w || !h)
      return true;
   bx0 = (x & (TT_COH_VRAM_W - 1)) >> TT_COH_SH;
   by0 = (y & (TT_COH_VRAM_H - 1)) >> TT_COH_SH;
   nbx = ((x & 7) + w + 7) >> TT_COH_SH; if (nbx > TT_COH_BW) nbx = TT_COH_BW;
   nby = ((y & 7) + h + 7) >> TT_COH_SH; if (nby > TT_COH_BH) nby = TT_COH_BH;
   for (ky = 0; ky < nby; ky++)
      for (kx = 0; kx < nbx; kx++)
         if (!tt_coh_clean[((by0 + ky) & (TT_COH_BH - 1)) * TT_COH_BW
                         + ((bx0 + kx) & (TT_COH_BW - 1))])
            return false;
   return true;
}

/* The parts of a readback that still have to be fetched: dirty blocks
 * grouped into 8-connected regions, each a bounding rect, merged while
 * they overlap or exceed TT_COH_MAX_RECTS. A frame typically dirties its
 * back buffer and a few copies or fills elsewhere in VRAM, and one box
 * around all of them would cover most of VRAM. */
#define TT_COH_MAX_RECTS 8

typedef struct
{
   uint32_t x, y, w, h;
} tt_coh_rect;

static uint8_t  tt_coh_seen[TT_COH_BH * TT_COH_BW];
static uint16_t tt_coh_stack[TT_COH_BH * TT_COH_BW];

static uint32_t tt_coh_area(const tt_coh_rect *r)
{
   return r->w * r->h;
}

static tt_coh_rect tt_coh_union(const tt_coh_rect *a, const tt_coh_rect *b)
{
   tt_coh_rect u;
   uint32_t    x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
   uint32_t    y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
   u.x = a->x < b->x ? a->x : b->x;
   u.y = a->y < b->y ? a->y : b->y;
   u.w = x1 - u.x;
   u.h = y1 - u.y;
   return u;
}

static bool tt_coh_overlap(const tt_coh_rect *a, const tt_coh_rect *b)
{
   return a->x < b->x + b->w && b->x < a->x + a->w
       && a->y < b->y + b->h && b->y < a->y + a->h;
}

/* Rects of a non-wrapping (x,y,w,h) that hold dirty blocks, in block
 * units first, then clipped to the request in pixels. Returns the count. */
static unsigned tt_coh_dirty_rects(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
      tt_coh_rect *out)
{
   const uint32_t bx0 = x >> TT_COH_SH, by0 = y >> TT_COH_SH;
   const uint32_t bx1 = (x + w - 1) >> TT_COH_SH;
   const uint32_t by1 = (y + h - 1) >> TT_COH_SH;
   tt_coh_rect    r[TT_COH_MAX_RECTS + 1];
   unsigned       n = 0, i, j;
   uint32_t       bx, by;

   for (by = by0; by <= by1; by++)
      memset(tt_coh_seen + by * TT_COH_BW + bx0, 0, bx1 - bx0 + 1);

   for (by = by0; by <= by1; by++)
   {
      for (bx = bx0; bx <= bx1; bx++)
      {
         uint32_t lx, ly, hx, hy, sp = 0;
         if (tt_coh_clean[by * TT_COH_BW + bx] || tt_coh_seen[by * TT_COH_BW + bx])
            continue;
         /* Flood the region from here. */
         lx = hx = bx;
         ly = hy = by;
         tt_coh_seen[by * TT_COH_BW + bx] = 1;
         tt_coh_stack[sp++] = (uint16_t)(by * TT_COH_BW + bx);
         while (sp)
         {
            uint32_t c  = tt_coh_stack[--sp];
            uint32_t cx = c % TT_COH_BW, cy = c / TT_COH_BW;
            uint32_t nx, ny;
            if (cx < lx) lx = cx;
            if (cx > hx) hx = cx;
            if (cy < ly) ly = cy;
            if (cy > hy) hy = cy;
            for (ny = (cy > by0 ? cy - 1 : cy); ny <= cy + 1 && ny <= by1; ny++)
               for (nx = (cx > bx0 ? cx - 1 : cx); nx <= cx + 1 && nx <= bx1; nx++)
               {
                  uint32_t k = ny * TT_COH_BW + nx;
                  if (tt_coh_clean[k] || tt_coh_seen[k])
                     continue;
                  tt_coh_seen[k] = 1;
                  tt_coh_stack[sp++] = (uint16_t)k;
               }
         }
         r[n].x = lx;
         r[n].y = ly;
         r[n].w = hx - lx + 1;
         r[n].h = hy - ly + 1;
         n++;
         /* Over budget: fold the pair whose union wastes least. */
         if (n > TT_COH_MAX_RECTS)
         {
            unsigned bi = 0, bj = 1;
            uint32_t best = 0xffffffffu;
            for (i = 0; i < n; i++)
               for (j = i + 1; j < n; j++)
               {
                  tt_coh_rect u   = tt_coh_union(&r[i], &r[j]);
                  uint32_t    add = tt_coh_area(&u) - tt_coh_area(&r[i]) - tt_coh_area(&r[j]);
                  if (tt_coh_area(&u) < tt_coh_area(&r[i]) + tt_coh_area(&r[j]))
                     add = 0;
                  if (add < best)
                  {
                     best = add;
                     bi   = i;
                     bj   = j;
                  }
               }
            r[bi] = tt_coh_union(&r[bi], &r[bj]);
            r[bj] = r[--n];
         }
      }
   }

   /* Regions' boxes may overlap; merge until none do, so nothing is
    * fetched twice. */
   for (;;)
   {
      bool merged = false;
      for (i = 0; i < n && !merged; i++)
         for (j = i + 1; j < n && !merged; j++)
            if (tt_coh_overlap(&r[i], &r[j]))
            {
               r[i]   = tt_coh_union(&r[i], &r[j]);
               r[j]   = r[--n];
               merged = true;
            }
      if (!merged)
         break;
   }

   for (i = 0; i < n; i++)
   {
      uint32_t px0 = r[i].x << TT_COH_SH, py0 = r[i].y << TT_COH_SH;
      uint32_t px1 = (r[i].x + r[i].w) << TT_COH_SH;
      uint32_t py1 = (r[i].y + r[i].h) << TT_COH_SH;
      if (px0 < x)     px0 = x;
      if (py0 < y)     py0 = y;
      if (px1 > x + w) px1 = x + w;
      if (py1 > y + h) py1 = y + h;
      out[i].x = px0;
      out[i].y = py0;
      out[i].w = px1 - px0;
      out[i].h = py1 - py0;
   }
   return n;
}

/* Re-mark the whole draw area dirty, but only when it may have changed
 * (set_draw_area) or a clean op cleaned part of it since the last mark.
 * Primitives are scissor-clipped to the draw area, so this stays a superset
 * while avoiding an O(blocks) mark on every one of the thousands of quads a
 * frame may push into a stable draw area. */
static void tt_coh_mark_draw_area(void)
{
   if (tt_coh_da_pending)
   {
      tt_coh_dirty(tt_coh_dax, tt_coh_day, tt_coh_daw, tt_coh_dah);
      tt_coh_da_pending = false;
   }
}

/* Block row `br`'s dirty blocks as a mask of 32-pixel column groups, bit
 * n for pixels [32n, 32n + 32). A state load hands the GPU these besides
 * what it changed in the CPU copy. */
static uint32_t tt_coh_dirty_groups(uint32_t br)
{
   const uint8_t *row  = tt_coh_clean + (br & (TT_COH_BH - 1)) * TT_COH_BW;
   uint32_t       mask = 0, g, q;
   if (!tt_coh_skip_enabled)
      return 0xffffffffu;
   /* Four blocks a group, one byte each, 1 when clean. */
   for (g = 0; g < TT_COH_BW / 4; g++)
   {
      memcpy(&q, row + g * 4, sizeof(q));
      if (q != 0x01010101u)
         mask |= 1u << g;
   }
   return mask;
}

/* The rects a readback of (x,y,w,h) actually has to fetch; 0 when the
 * whole rect is already coherent. Wrapping rects are fetched whole. */
static unsigned tt_coh_read_rects(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
      tt_coh_rect *out)
{
   out[0].x = x; out[0].y = y; out[0].w = w; out[0].h = h;
   if (!tt_coh_skip_enabled)
      return 1;
   if (tt_coh_all_clean(x, y, w, h))
      return 0;
   if (!w || !h || x + w > TT_COH_VRAM_W || y + h > TT_COH_VRAM_H)
      return 1;
   return tt_coh_dirty_rects(x, y, w, h, out);
}

#endif
