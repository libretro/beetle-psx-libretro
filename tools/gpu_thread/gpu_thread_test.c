/* GPU thread lane: the software renderer draws the same picture, at the
 * same emulated timing, on the GPU thread as inline.
 *
 * The probe EXE sends a fixed GP0 command list through DMA channel 2 in
 * linked-list mode every frame, generated here from a fixed seed: every
 * primitive type (polygons flat, Gouraud and textured in all three texture
 * modes, lines and polylines, sprites of every size with both flips,
 * fills, VRAM copies), CPU-to-VRAM transfers with and without the mask
 * test, a GPU interrupt, and changes of draw area, offset, texture window
 * and mask bits between them.  Some textured triangles have vertices the
 * GTE projects every frame and stores into the list, so with PGXP on they
 * carry sub-pixel positions and depths, drawn perspective-correct.
 * Around the list the probe changes the
 * display every frame (interlaced and progressive, with line skipping, and
 * the display start), resets the command buffer, fully resets the GPU
 * every eighth frame, and reads a rectangle of VRAM back through GPUREAD,
 * folding it into a checksum in RAM.  It counts its waits for the GPU's
 * DMA into RAM too, which follows the GPU's emulated busy time.
 *
 * The host runs FRAMES frames, saving a state part way and loading it back
 * later, then hashes native VRAM (the savestate's GPURAM entry), the last
 * video frame and main RAM.  With "gun", a GunCon and a Justifier aim at
 * the middle of the picture: they sample the lines around it as they are
 * scanned out and draw their crosshairs on them, which the video frame
 * holds; the host hashes the whole state too.  run.sh runs it with the GPU thread off, on,
 * switched on and off every few frames, and with 2, 3 and 4 lanes drawing
 * bands, and checks they all agree.
 *
 * usage: gpu_thread_test <core> <1x|2x|4x> <off|on|toggle|lanes> <pgxp|nopgxp> [deint] [gun]
 *                        [noline|aggline]
 * Runs in the current directory: it writes the probe EXE and an all-zero
 * BIOS image there.  "gpu_thread_test probe" only writes the probe EXE and
 * keeps it, for the hardware renderers' hosts (tools/glhost/timing_lanes.sh). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "libretro.h"

#define PROBE_PATH  "gpu_thread_probe.exe"
/* An all-zero kernel: the CPU runs straight to the EXE loader's patch */
#define BIOS_PATH   "scph5501.bin"
#define FRAMES      24
#define SAVE_AT     9
#define LOAD_AT     15
#define LIST_ADDR   0x80020000u
#define LIST_MAX    (64 * 1024)        /* words */
#define PRIMS       420
/* Triangles whose vertices the GTE projects each frame, written into the
 * list with SWC2, so PGXP has sub-pixel positions and depths for them */
#define GTE_TRIS    24
#define TABLE_ADDR  0x80018000u

#ifdef _WIN32
typedef HMODULE core_handle;
typedef FARPROC core_proc;
#define CORE_OPEN(p)    LoadLibraryA(p)
#define CORE_SYM(h, n)  GetProcAddress((h), (n))
#define CORE_CLOSE(h)   FreeLibrary(h)
#else
typedef void *core_handle;
typedef void *core_proc;
#define CORE_OPEN(p)    dlopen((p), RTLD_NOW | RTLD_LOCAL)
#define CORE_SYM(h, n)  dlsym((h), (n))
#define CORE_CLOSE(h)   dlclose(h)
#endif

/* Copied, not cast: ISO C has no conversion between object and
 * function pointers */
static int core_sym(core_handle core, const char *name, void *fn, size_t size)
{
   core_proc p = CORE_SYM(core, name);
   if (!p || size != sizeof(p))
   {
      fprintf(stderr, "missing %s\n", name);
      return 0;
   }
   memcpy(fn, &p, size);
   return 1;
}

static const char *resolution = "1x";
static const char *thread_mode = "off";
static const char *pgxp_mode = "disabled";
static const char *deint_mode = "weave";
static const char *line_mode  = "default";
static unsigned    frame_no;
static uint32_t    video_hash;

/* --- probe EXE ------------------------------------------------------ */

static uint32_t rng_state = 0x7EAD5EEDu;
static uint32_t rnd(uint32_t n)
{
   rng_state ^= rng_state << 13;
   rng_state ^= rng_state >> 17;
   rng_state ^= rng_state << 5;
   return n ? rng_state % n : rng_state;
}

static uint32_t list[LIST_MAX];
static size_t   list_len;
static size_t   last_node;
/* GTE vertices: VXY0, VZ0, and the list address its projection goes to */
static uint32_t table[GTE_TRIS * 9];

/* One linked-list node per command; the header is patched to point to
 * the next node when that one starts. */
static void node(const uint32_t *w, unsigned n)
{
   unsigned i;
   if (list_len + 1 + n > LIST_MAX)
      return;
   if (list_len)
      list[last_node] = (list[last_node] & 0xFF000000u)
         | ((LIST_ADDR + (uint32_t)list_len * 4) & 0xFFFFFFu);
   last_node = list_len;
   list[list_len++] = ((uint32_t)n << 24) | 0xFFFFFFu;
   for (i = 0; i < n; i++)
      list[list_len++] = w[i];
}

/* Every rnd() call is a statement of its own: C leaves the order of calls
 * within one expression unspecified. */
static uint32_t xy(int x, int y) { return ((uint32_t)(y & 0x7FF) << 16) | (uint32_t)(x & 0x7FF); }
static uint32_t rcol(void) { return rnd(0x1000000u); }
static uint32_t pt(int x, int y)
{
   int dx = (int)rnd(181) - 90;
   int dy = (int)rnd(181) - 90;
   return xy(x + dx, y + dy);
}
static uint32_t uv(uint32_t hi)
{
   uint32_t v = rnd(256);
   uint32_t u = rnd(256);
   return (hi << 16) | (v << 8) | u;
}

static void build_list(void)
{
   static const unsigned page_x[4] = { 640, 768, 512, 0 };
   uint32_t w[256];
   unsigned i, n;

   list_len = 0;
   w[0] = 0xE3000000u;                    node(w, 1);   /* draw area 0,0 */
   w[0] = 0xE4000000u | (511u << 10) | 1023u; node(w, 1);
   w[0] = 0xE5000000u;                    node(w, 1);
   w[0] = 0xE2000000u;                    node(w, 1);
   w[0] = 0xE6000000u;                    node(w, 1);
   /* CLUT rows, drawn so they are dirty when first sampled */
   for (i = 0; i < 16; i++)
   {
      w[0] = 0x60000000u | rcol(); w[1] = xy((int)i * 16, 500); w[2] = (1u << 16) | 16u;
      node(w, 3);
   }
   w[0] = 0x02000000u | rcol(); w[1] = xy(0, 501); w[2] = (1u << 16) | 256u; node(w, 3);
   /* texture pages, drawn */
   for (i = 0; i < 24; i++)
   {
      int x0, y0, v;
      x0 = 640 + (int)rnd(380);
      y0 = (int)rnd(250);
      w[0] = 0x38000000u | rcol(); w[1] = xy(x0, y0);
      for (v = 0; v < 3; v++)
      {
         int dx, dy;
         w[2 + v * 2] = rcol();
         dx = (int)rnd(120) - 60;
         dy = (int)rnd(60);
         w[3 + v * 2] = xy(x0 + dx, y0 + dy);
      }
      node(w, 8);
   }

   /* GTE triangles: x, y in [-160, 160) and [-120, 120), z in [256, 1280),
    * which the probe projects with H = 200 around (160, 120) */
   for (i = 0; i < GTE_TRIS; i++)
   {
      unsigned v;
      uint32_t page = (640u >> 6) | (rnd(3) << 7) | (1u << 9);
      w[0] = 0x24000000u | rcol();
      w[1] = 0; w[2] = uv(500u << 6); w[3] = 0; w[4] = uv(page);
      w[5] = 0; w[6] = uv(0);
      node(w, 7);
      for (v = 0; v < 3; v++)
      {
         uint32_t vx = rnd(320);
         uint32_t vy = rnd(240);
         uint32_t vz = 256 + rnd(1024);
         table[i * 9 + v * 3 + 0] = (((vy - 120) & 0xFFFFu) << 16) | ((vx - 160) & 0xFFFFu);
         table[i * 9 + v * 3 + 1] = vz;
         table[i * 9 + v * 3 + 2] = LIST_ADDR + (uint32_t)(last_node + 2 + v * 2) * 4;
      }
   }

   for (i = 0; i < PRIMS; i++)
   {
      int      x, y;
      uint32_t semi, raw, mode, page, clut, k;
      x    = (int)rnd(1024);
      y    = (int)rnd(512);
      semi = rnd(2);
      raw  = rnd(2);
      mode = rnd(3);
      page = page_x[rnd(4)] >> 6;            /* texture page x */
      page |= rnd(2) << 4;                   /* texture page y: 0 or 256 */
      page |= rnd(4) << 5;                   /* semi-transparency mode */
      page |= mode << 7;
      page |= rnd(2) << 9;                   /* dither */
      clut = mode == 1 ? (501u << 6) : (500u << 6);
      k    = rnd(20);
      switch (k)
      {
         case 0: /* textured triangle */
            w[0] = 0x24000000u | (semi << 25) | (raw << 24) | rcol();
            w[1] = pt(x, y); w[2] = uv(clut); w[3] = pt(x, y); w[4] = uv(page);
            w[5] = pt(x, y); w[6] = uv(0);
            n = 7;
            break;
         case 1: /* textured quad */
            w[0] = 0x2C000000u | (semi << 25) | (raw << 24) | rcol();
            w[1] = pt(x, y); w[2] = uv(clut); w[3] = pt(x, y); w[4] = uv(page);
            w[5] = pt(x, y); w[6] = uv(0); w[7] = pt(x, y); w[8] = uv(0);
            n = 9;
            break;
         case 2: /* Gouraud textured quad */
            w[0] = 0x3C000000u | (semi << 25) | rcol();
            w[1] = pt(x, y); w[2] = uv(clut); w[3] = rcol(); w[4] = pt(x, y); w[5] = uv(page);
            w[6] = rcol(); w[7] = pt(x, y); w[8] = uv(0);
            w[9] = rcol(); w[10] = pt(x, y); w[11] = uv(0);
            n = 12;
            break;
         case 3: /* Gouraud quad */
            w[0] = 0x38000000u | (semi << 25) | rcol();
            w[1] = pt(x, y); w[2] = rcol(); w[3] = pt(x, y); w[4] = rcol();
            w[5] = pt(x, y); w[6] = rcol(); w[7] = pt(x, y);
            n = 8;
            break;
         case 4: /* flat triangle */
            w[0] = 0x20000000u | (semi << 25) | rcol();
            w[1] = pt(x, y); w[2] = pt(x, y); w[3] = pt(x, y);
            n = 4;
            break;
         case 5: /* flat rectangle, variable size */
         {
            uint32_t rh, rw;
            rh = 1 + rnd(120);
            rw = 1 + rnd(200);
            w[0] = 0x60000000u | (semi << 25) | rcol(); w[1] = xy(x, y);
            w[2] = (rh << 16) | rw;
            n = 3;
         }
            break;
         case 6: /* textured sprites of every size, with the flips */
         {
            static const uint32_t sizes[4] = { 0x64, 0x6C, 0x74, 0x7C };
            uint32_t s = sizes[rnd(4)];
            w[0] = 0xE1000000u | page | (rnd(4) << 12); node(w, 1);
            w[0] = ((s | (semi << 1) | raw) << 24) | rcol();
            w[1] = xy(x, y); w[2] = uv(clut);
            n = 3;
            if (s == 0x64)
            {
               uint32_t rh = 1 + rnd(90);
               uint32_t rw = 1 + rnd(90);
               w[3] = (rh << 16) | rw;
               n = 4;
            }
            break;
         }
         case 7: /* mask set / test */
            w[0] = 0xE6000000u | rnd(4);
            n = 1;
            break;
         case 8: /* a polygon drawn into its own texture page */
         {
            int px;
            uint32_t own;
            px  = (int)page_x[rnd(2)];
            px += (int)rnd(200);
            own = (uint32_t)(px >> 6) | (mode << 7);
            own |= rnd(4) << 5;
            w[0] = 0x2C000000u | (semi << 25) | (raw << 24) | rcol();
            w[1] = pt(px, y & 255); w[2] = uv(clut); w[3] = pt(px, y & 255);
            w[4] = uv(own);
            w[5] = pt(px, y & 255); w[6] = uv(0); w[7] = pt(px, y & 255); w[8] = uv(0);
            n = 9;
            break;
         }
         case 9: /* copy */
         {
            int sx, sy;
            uint32_t cw, ch;
            sx = (int)rnd(1024);
            sy = (int)rnd(512);
            ch = 1 + rnd(64);
            cw = 1 + rnd(64);
            w[0] = 0x80000000u; w[1] = xy(sx, sy); w[2] = xy(x, y);
            w[3] = (ch << 16) | cw;
            n = 4;
            break;
         }
         case 10: /* lines, flat and Gouraud */
            w[0] = (rnd(2) ? 0x40000000u : 0x42000000u) | rcol();
            w[1] = pt(x, y); w[2] = pt(x, y);
            n = 3;
            if (rnd(2))
            {
               w[0] |= 0x10000000u;
               w[3] = w[2]; w[2] = rcol();
               n = 4;
            }
            break;
         case 11: /* polylines, flat and Gouraud, ended by the marker */
         {
            /* Short: a node's words all go into the 16-word FIFO, and
             * the GPU drops what does not fit while it is busy */
            unsigned v, verts = 2 + rnd(2);
            uint32_t g = rnd(2);
            w[0] = (g ? 0x58000000u : 0x48000000u) | (semi << 25) | rcol();
            n = 1;
            for (v = 0; v < verts; v++)
            {
               if (g && v)
                  w[n++] = rcol();
               w[n++] = pt(x, y);
            }
            w[n++] = 0x55555555u;
            break;
         }
         case 12: /* CPU-to-VRAM transfer, sometimes under the mask test;
                   * small, for the FIFO as above */
         {
            unsigned tw = 1 + rnd(8), th = 1 + rnd(2), words, j;
            words = (tw * th + 1) / 2;
            w[0] = 0xE6000000u | rnd(4); node(w, 1);
            w[0] = 0xA0000000u; w[1] = xy(x, y);
            w[2] = (th << 16) | tw;
            for (j = 0; j < words; j++)
               w[3 + j] = rnd(0);
            n = 3 + words;
            break;
         }
         case 13: /* fill */
            w[0] = 0x02000000u | rcol(); w[1] = xy(x & 0x3F0, y);
            w[2] = ((1 + rnd(64)) << 16) | (1 + rnd(200));
            n = 3;
            break;
         case 14: /* draw area, offset and texture window */
            w[0] = 0xE3000000u | (rnd(256) << 10) | rnd(512); node(w, 1);
            w[0] = 0xE4000000u | ((256 + rnd(256)) << 10) | (512 + rnd(512)); node(w, 1);
            w[0] = 0xE5000000u | (((rnd(64) - 32) & 0x7FF) << 11) | ((rnd(64) - 32) & 0x7FF);
            node(w, 1);
            w[0] = 0xE2000000u | rnd(1u << 20);
            n = 1;
            break;
         case 15: /* back to the whole of VRAM */
            w[0] = 0xE3000000u; node(w, 1);
            w[0] = 0xE4000000u | (511u << 10) | 1023u; node(w, 1);
            w[0] = 0xE5000000u; node(w, 1);
            w[0] = 0xE2000000u;
            n = 1;
            break;
         case 16: /* interrupt, and a texture cache flush */
            w[0] = 0x1F000000u; node(w, 1);
            w[0] = 0x01000000u;
            n = 1;
            break;
         case 17: /* draw mode, dithering and field drawing included */
            w[0] = 0xE1000000u | page | (rnd(2) << 10) | (rnd(4) << 12);
            n = 1;
            break;
         default: /* long horizontal span */
            w[0] = 0x2C000000u | (semi << 25) | (raw << 24) | rcol();
            w[1] = xy(x, y); w[2] = uv(clut); w[3] = xy(x + 400, y); w[4] = uv(page);
            w[5] = xy(x, y + 6); w[6] = uv(0); w[7] = xy(x + 400, y + 6); w[8] = uv(0);
            n = 9;
            break;
      }
      node(w, n);
   }

   /* A 15-bit page read whole, by every band, then drawn over with no
    * cache flush, then the 32x32 texels the cache was left holding read
    * again: from the lines the first read left, whichever band fetched
    * them, not from VRAM */
   w[0] = 0xE3000000u; node(w, 1);
   w[0] = 0xE4000000u | (511u << 10) | 1023u; node(w, 1);
   w[0] = 0xE5000000u; node(w, 1);
   w[0] = 0xE2000000u; node(w, 1);
   w[0] = 0xE6000000u; node(w, 1);
   w[0] = 0xE1000000u | (640u >> 6) | (2u << 7); node(w, 1);
   w[0] = 0x01000000u; node(w, 1);
   /* A page read by every band, then, the cache emptied by a page change,
    * drawn over: no band may draw over texels another has still to read.
    * One band down, so each texel row is read by another band than the
    * one that draws it over, whatever the number of bands. */
   w[0] = 0xE1000000u | (768u >> 6) | (2u << 7); node(w, 1);
   w[0] = 0x65000000u | 0x808080u; w[1] = xy(0, 264);
   w[2] = 0; w[3] = (240u << 16) | 256u;
   node(w, 4);
   w[0] = 0xE1000000u | (640u >> 6) | (2u << 7); node(w, 1);
   w[0] = 0x60000000u | rcol(); w[1] = xy(768, 0);
   w[2] = (256u << 16) | 256u;
   node(w, 3);
   w[0] = 0x01000000u; node(w, 1);
   for (i = 0; i < 2; i++)
   {
      w[0] = 0x65000000u | 0x808080u; w[1] = xy((int)i * 256, 256);
      w[2] = i ? (224u << 8) | 224u : 0;
      w[3] = i ? (32u << 16) | 32u : (256u << 16) | 256u;
      node(w, 4);
      if (!i)
      {
         w[0] = 0x60000000u | rcol(); w[1] = xy(640, 0);
         w[2] = (256u << 16) | 256u;
         node(w, 3);
      }
   }
   /* Still drawn by one lane: 8-bit (no cache flush), a CLUT not loaded
    * before; then a flush, after which the bands draw again, the first
    * with the CLUT that was loaded while they were not drawing */
   w[0] = 0xE1000000u | (640u >> 6) | (1u << 7); node(w, 1);
   for (i = 0; i < 2; i++)
   {
      w[0] = 0x65000000u | 0x808080u; w[1] = xy(512, 256 + (int)i * 64);
      w[2] = ((500u << 6) | 1u) << 16; w[3] = (64u << 16) | 64u;
      node(w, 4);
      if (!i)
      {
         w[0] = 0x01000000u; node(w, 1);
      }
   }

   /* One-pixel-wide axis-aligned textured triangles, as games draw lines
    * with: Line Render adds the triangle that completes each line */
   w[0] = 0xE1000000u | (640u >> 6) | (2u << 7); node(w, 1);
   for (i = 0; i < 64; i++)
   {
      const int      lx  = 40 + (int)(i & 7) * 60, ly = 290 + (int)(i >> 3) * 24;
      const uint32_t uv0 = (uint32_t)((i & 7) * 16) | ((uint32_t)(i >> 3) * 8 << 8);
      const uint32_t pg  = ((640u >> 6) | (2u << 7)) << 16;
      w[0] = 0x24808080u; w[1] = xy(lx, ly);       w[2] = uv0;
      w[3] = xy(lx, ly + 1);                      w[4] = pg | uv0;
      w[5] = xy(lx + 100, ly);                    w[6] = uv0 + 100;
      node(w, 7);
      w[0] = 0x24808080u; w[1] = xy(lx, ly + 40);  w[2] = uv0;
      w[3] = xy(lx + 1, ly + 40);                 w[4] = pg | uv0;
      w[5] = xy(lx, ly + 120);                    w[6] = uv0 + (80u << 8);
      node(w, 7);
   }

   w[0] = 0xE6000000u; node(w, 1);
   /* Dithering on at the end, so what a GP1 reset clears shows next frame */
   w[0] = 0xE1000000u | (1u << 9); node(w, 1);
}

/* MIPS I encodings */
#define T0 8
#define T1 9
#define T2 10
#define T3 11
#define T4 12
#define T5 13
#define T6 14
#define S0 16
#define S1 17
#define S2 18
static uint32_t op_i(unsigned op, unsigned rs, unsigned rt, uint32_t imm)
{
   return ((uint32_t)op << 26) | ((uint32_t)rs << 21) | ((uint32_t)rt << 16) | (imm & 0xFFFFu);
}
static uint32_t op_r(unsigned rs, unsigned rt, unsigned rd, unsigned sa, unsigned fn)
{
   return ((uint32_t)rs << 21) | ((uint32_t)rt << 16) | ((uint32_t)rd << 11)
      | ((uint32_t)sa << 6) | fn;
}
#define LUI(rt, imm)      op_i(0x0F, 0, rt, imm)
#define ORI(rt, rs, imm)  op_i(0x0D, rs, rt, imm)
#define ANDI(rt, rs, imm) op_i(0x0C, rs, rt, imm)
#define ADDIU(rt, rs, imm) op_i(0x09, rs, rt, imm)
#define SW(rt, off, rs)   op_i(0x2B, rs, rt, off)
#define LW(rt, off, rs)   op_i(0x23, rs, rt, off)
#define SLL(rd, rt, sa)   op_r(0, rt, rd, sa, 0x00)
#define ADDU(rd, rs, rt)  op_r(rs, rt, rd, 0, 0x21)
#define AND(rd, rs, rt)   op_r(rs, rt, rd, 0, 0x24)
#define OR(rd, rs, rt)    op_r(rs, rt, rd, 0, 0x25)
#define XOR(rd, rs, rt)   op_r(rs, rt, rd, 0, 0x26)
#define NOP               0u
#define MFC0(rt, rd)      (0x40000000u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define MTC0(rt, rd)      (0x40800000u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define MTC2(rt, rd)      (0x48800000u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define CTC2(rt, rd)      (0x48C00000u | ((uint32_t)(rt) << 16) | ((uint32_t)(rd) << 11))
#define RTPS              0x4A180001u
#define SWC2(rt, off, rs) op_i(0x3A, rs, rt, off)

static uint32_t code[512];
static size_t   ncode;

static void emit(uint32_t word) { code[ncode++] = word; }
static void emit_li(unsigned rt, uint32_t value)
{
   emit(LUI(rt, value >> 16));
   emit(ORI(rt, rt, value & 0xFFFFu));
}
/* A branch to target, from the next emitted word */
static void emit_branch(unsigned op, unsigned rs, unsigned rt, size_t target)
{
   emit(op_i(op, rs, rt, (uint32_t)(target - ncode - 1)));
   emit(NOP);
}
#define BEQ 0x04
#define BNE 0x05

static int write_probe(void)
{
   /* Display set-up, sent every frame after the optional reset */
   static const uint32_t gp1[] =
   {
      0x06C60260u, 0x07040010u, 0x03000000u, 0x04000002u
   };
   unsigned char hdr[0x800];
   size_t        i, frame, skip, gte, wait, ready, rd, vb, image;
   FILE         *f;

   build_list();
   ncode = 0;

   emit(LUI(T0, 0x1F80));
   emit(SW(0, 0x1074, T0));                 /* I_MASK = 0 */
   emit_li(T1, 0x07654B21u);
   emit(SW(T1, 0x10F0, T0));                /* DPCR: DMA2 on */
   emit(LUI(S1, 0x800F));
   /* COP2 on; the GTE: identity rotation, no translation, H = 200, the
    * screen offset at (160, 120) */
   emit(MFC0(T1, 12));
   emit(LUI(T2, 0x4000));
   emit(OR(T1, T1, T2));
   emit(MTC0(T1, 12));
   emit(NOP);
   emit(ADDIU(T1, 0, 0x1000));
   emit(CTC2(T1, 0));
   emit(CTC2(0, 1));
   emit(CTC2(T1, 2));
   emit(CTC2(0, 3));
   emit(CTC2(T1, 4));
   emit(CTC2(0, 5));
   emit(CTC2(0, 6));
   emit(CTC2(0, 7));
   emit(LUI(T1, 160));
   emit(CTC2(T1, 24));
   emit(LUI(T1, 120));
   emit(CTC2(T1, 25));
   emit(ADDIU(T1, 0, 200));
   emit(CTC2(T1, 26));
   emit(ADDIU(S0, 0, 0));                   /* DMA waits */
   emit(ADDIU(S2, 0, 0));                   /* frame */
   frame = ncode;
   /* GP1(00), every eighth frame */
   emit(ANDI(T1, S2, 7));
   emit(ADDIU(T2, 0, 5));
   skip = ncode;
   emit(0);
   emit(NOP);
   emit(SW(0, 0x1814, T0));
   code[skip] = op_i(BNE, T1, T2, (uint32_t)(ncode - skip - 1));
   for (i = 0; i < sizeof(gp1) / sizeof(gp1[0]); i++)
   {
      emit_li(T1, gp1[i]);
      emit(SW(T1, 0x1814, T0));
   }
   /* GP1(08): 480i every other four frames, 240p otherwise */
   emit(ANDI(T1, S2, 4));
   emit(SLL(T2, T1, 3));
   emit(OR(T1, T1, T2));
   emit(ORI(T1, T1, 1));
   emit(LUI(T2, 0x0800));
   emit(OR(T1, T1, T2));
   emit(SW(T1, 0x1814, T0));
   /* GP1(05): display start moves down 8 lines a frame, four times */
   emit(ANDI(T1, S2, 3));
   emit(SLL(T1, T1, 13));
   emit(LUI(T2, 0x0500));
   emit(OR(T1, T1, T2));
   emit(SW(T1, 0x1814, T0));
   /* GP1(01) */
   emit(LUI(T1, 0x0100));
   emit(SW(T1, 0x1814, T0));

   /* project the GTE triangles' vertices into the list */
   emit_li(T6, TABLE_ADDR);
   emit(ADDIU(T4, 0, GTE_TRIS * 3));
   gte = ncode;
   emit(LW(T1, 0, T6));
   emit(LW(T2, 4, T6));
   emit(LW(T5, 8, T6));
   emit(MTC2(T1, 0));
   emit(MTC2(T2, 1));
   emit(NOP);
   emit(NOP);
   emit(RTPS);
   emit(NOP);
   emit(NOP);
   emit(SWC2(14, 0, T5));                   /* SXY2 */
   emit(ADDIU(T6, T6, 12));
   emit(ADDIU(T4, T4, 0xFFFF));
   emit_branch(BNE, T4, 0, gte);

   emit_li(T1, LIST_ADDR & 0xFFFFFFu);
   emit(SW(T1, 0x10A0, T0));                /* D2_MADR */
   emit(SW(0, 0x10A4, T0));                 /* D2_BCR = 0 */
   emit_li(T1, 0x01000401u);
   emit(SW(T1, 0x10A8, T0));                /* D2_CHCR: list */
   wait = ncode;
   emit(ADDIU(S0, S0, 1));
   emit(LW(T1, 0x10A8, T0));
   emit(NOP);
   emit(LUI(T2, 0x0100));
   emit(AND(T1, T1, T2));
   emit_branch(BNE, T1, 0, wait);

   /* GPUSTAT: ready for a command, the FIFO drained */
   ready = ncode;
   emit(ADDIU(S0, S0, 1));
   emit(LW(T1, 0x1814, T0));
   emit(NOP);
   emit(LUI(T2, 0x0400));
   emit(AND(T1, T1, T2));
   emit_branch(BEQ, T1, 0, ready);
   /* GP0(C0): 32x8 pixels at x = 32 * (frame & 15), y = 100 */
   emit_li(T1, 0xC0000000u);
   emit(SW(T1, 0x1810, T0));
   emit(ANDI(T1, S2, 15));
   emit(SLL(T1, T1, 5));
   emit(LUI(T2, 100));
   emit(OR(T1, T1, T2));
   emit(SW(T1, 0x1810, T0));
   emit_li(T1, (8u << 16) | 32u);
   emit(SW(T1, 0x1810, T0));
   ready = ncode;                           /* GPUSTAT: ready to send */
   emit(LW(T1, 0x1814, T0));
   emit(NOP);
   emit(LUI(T2, 0x0800));
   emit(AND(T1, T1, T2));
   emit_branch(BEQ, T1, 0, ready);
   emit(ADDU(T3, 0, 0));
   emit(ADDIU(T4, 0, 128));
   rd = ncode;
   emit(LW(T1, 0x1810, T0));
   emit(NOP);
   emit(SLL(T5, T3, 1));
   emit(XOR(T3, T5, T1));
   emit(ADDIU(T4, T4, 0xFFFF));
   emit_branch(BNE, T4, 0, rd);
   emit(SLL(T5, S2, 2));
   emit(ADDU(T5, T5, S1));
   emit(SW(T3, 8, T5));                     /* checksum[frame] */
   emit(SW(S0, 0, S1));                     /* DMA waits */
   emit(ADDIU(S2, S2, 1));

   emit_li(T1, 0xFFFFFFFEu);
   emit(SW(T1, 0x1070, T0));                /* ack VBLANK */
   vb = ncode;
   emit(LW(T1, 0x1070, T0));
   emit(NOP);
   emit(ANDI(T1, T1, 1));
   emit_branch(BEQ, T1, 0, vb);
   emit_branch(BEQ, 0, 0, frame);

   image = (LIST_ADDR - 0x80010000u) + list_len * 4;
   image = (image + 0x7FF) & ~(size_t)0x7FF;
   memset(hdr, 0, sizeof(hdr));
   memcpy(hdr, "PS-X EXE", 8);
#define PUT32(o, v) do { hdr[(o)] = (unsigned char)(v); hdr[(o) + 1] = (unsigned char)((v) >> 8); \
      hdr[(o) + 2] = (unsigned char)((v) >> 16); hdr[(o) + 3] = (unsigned char)((v) >> 24); } while (0)
   PUT32(0x10, 0x80010000u);
   PUT32(0x18, 0x80010000u);
   PUT32(0x1C, (uint32_t)image);
   PUT32(0x30, 0x801FFFF0u);
#undef PUT32
   if (!(f = fopen(PROBE_PATH, "wb")))
      return 0;
   fwrite(hdr, 1, sizeof(hdr), f);
   for (i = 0; i < image / 4; i++)
   {
      uint32_t v = 0;
      unsigned char b[4];
      if (i < ncode)
         v = code[i];
      else if (i >= (TABLE_ADDR - 0x80010000u) / 4
            && i - (TABLE_ADDR - 0x80010000u) / 4 < sizeof(table) / sizeof(table[0]))
         v = table[i - (TABLE_ADDR - 0x80010000u) / 4];
      else if (i >= (LIST_ADDR - 0x80010000u) / 4
            && i - (LIST_ADDR - 0x80010000u) / 4 < list_len)
         v = list[i - (LIST_ADDR - 0x80010000u) / 4];
      b[0] = (unsigned char)v;
      b[1] = (unsigned char)(v >> 8);
      b[2] = (unsigned char)(v >> 16);
      b[3] = (unsigned char)(v >> 24);
      fwrite(b, 1, 4, f);
   }
   fclose(f);

   if (!(f = fopen(BIOS_PATH, "wb")))
      return 0;
   memset(hdr, 0, sizeof(hdr));
   for (i = 0; i < 512 * 1024 / sizeof(hdr); i++)
      fwrite(hdr, 1, sizeof(hdr), f);
   fclose(f);
   return 1;
}

/* --- frontend ------------------------------------------------------- */

static uint32_t fnv(const unsigned char *p, size_t n, uint32_t h)
{
   size_t i;
   for (i = 0; i < n; i++)
   {
      h ^= p[i];
      h *= 16777619u;
   }
   return h;
}

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_ERROR && !getenv("GPU_THREAD_DEBUG"))
      return;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

/* "toggle" switches the thread on and off every five frames */
static int thread_on(void)
{
   if (!strcmp(thread_mode, "toggle"))
      return (frame_no / 5) & 1;
   return !strcmp(thread_mode, "on");
}

static const char *get_var(const char *key)
{
   if (!strcmp(key, "beetle_psx_skip_bios"))
      return "enabled";
   if (!strcmp(key, "beetle_psx_renderer"))
      return "software";
   if (!strcmp(key, "beetle_psx_cpu_dynarec"))
      return "disabled";
   if (!strcmp(key, "beetle_psx_internal_resolution"))
      return resolution;
   if (!strcmp(key, "beetle_psx_gpu_thread"))
   {
      /* a lane count: the lanes draw bands at every resolution */
      if (thread_mode[0] >= '1' && thread_mode[0] <= '8')
         return thread_mode;
      return thread_on() ? "enabled" : "disabled";
   }
   if (!strcmp(key, "beetle_psx_pgxp_mode"))
      return pgxp_mode;
   if (!strcmp(key, "beetle_psx_pgxp_texture"))
      return "enabled";
   if (!strcmp(key, "beetle_psx_deinterlacer"))
      return deint_mode;
   if (!strcmp(key, "beetle_psx_line_render"))
      return line_mode;
   return NULL;
}

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = log_cb;
         return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = ".";
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
         return true;
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         *(bool *)data = false;
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         var->value = get_var(var->key);
         return var->value != NULL;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = !strcmp(thread_mode, "toggle") && frame_no % 5 == 0;
         return true;
      default:
         return false;
   }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
   unsigned y;
   uint32_t hh = 2166136261u;
   if (!data)
      return;
   for (y = 0; y < h; y++)
      hh = fnv((const unsigned char *)data + y * pitch, (size_t)w * 4, hh);
   /* every frame's picture counts, not only the last */
   video_hash ^= hh + frame_no;
}
static void input_poll_cb(void) { }
static int16_t input_state_cb(unsigned p, unsigned d, unsigned i, unsigned id)
{
   (void)p; (void)d; (void)i; (void)id;
   return 0;
}
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *data, size_t frames)
{
   (void)data;
   return frames;
}

int main(int argc, char **argv)
{
   core_handle core;
   void   (*set_env)(retro_environment_t);
   void   (*set_video)(retro_video_refresh_t);
   void   (*set_poll)(retro_input_poll_t);
   void   (*set_state)(retro_input_state_t);
   void   (*set_audio)(retro_audio_sample_t);
   void   (*set_batch)(retro_audio_sample_batch_t);
   void   (*init)(void);
   void   (*deinit)(void);
   bool   (*load_game)(const struct retro_game_info *);
   void   (*unload_game)(void);
   void   (*run)(void);
   size_t (*serialize_size)(void);
   bool   (*serialize)(void *, size_t);
   bool   (*unserialize)(const void *, size_t);
   void  *(*memory_data)(unsigned);
   void   (*set_port)(unsigned, unsigned);
   static const char key[] = "\015&GPURAM[0][0]";
   struct retro_game_info info;
   unsigned char *state, *saved;
   size_t size, i, klen = sizeof(key) - 1;
   uint32_t vram_hash = 0, ram_hash = 0, waits = 0, state_hash = 0;
   unsigned f;
   int found = 0, gun = 0, a;

   if (argc == 2 && !strcmp(argv[1], "probe"))
      return write_probe() ? 0 : 2;
   if (argc < 5)
   {
      fprintf(stderr, "usage: %s <core> <1x|2x|4x> <off|on|toggle|lanes> <pgxp|nopgxp> [deint] [gun] [noline|aggline]\n",
            argv[0]);
      return 2;
   }
   resolution  = argv[2];
   thread_mode = argv[3];
   pgxp_mode   = strcmp(argv[4], "pgxp") ? "disabled" : "memory only";
   for (a = 5; a < argc; a++)
   {
      if (!strcmp(argv[a], "deint"))
         deint_mode = "off";
      else if (!strcmp(argv[a], "gun"))
         gun = 1;
      else if (!strcmp(argv[a], "noline"))
         line_mode = "disabled";
      else if (!strcmp(argv[a], "aggline"))
         line_mode = "aggressive";
   }
   if (!write_probe())
   {
      fprintf(stderr, "cannot write the probe\n");
      return 2;
   }
   if (!(core = CORE_OPEN(argv[1])))
   {
      fprintf(stderr, "cannot open %s\n", argv[1]);
      return 2;
   }
   if (  !core_sym(core, "retro_set_environment",        &set_env,        sizeof(set_env))
      || !core_sym(core, "retro_set_video_refresh",      &set_video,      sizeof(set_video))
      || !core_sym(core, "retro_set_input_poll",         &set_poll,       sizeof(set_poll))
      || !core_sym(core, "retro_set_input_state",        &set_state,      sizeof(set_state))
      || !core_sym(core, "retro_set_audio_sample",       &set_audio,      sizeof(set_audio))
      || !core_sym(core, "retro_set_audio_sample_batch", &set_batch,      sizeof(set_batch))
      || !core_sym(core, "retro_init",                   &init,           sizeof(init))
      || !core_sym(core, "retro_deinit",                 &deinit,         sizeof(deinit))
      || !core_sym(core, "retro_load_game",              &load_game,      sizeof(load_game))
      || !core_sym(core, "retro_unload_game",            &unload_game,    sizeof(unload_game))
      || !core_sym(core, "retro_run",                    &run,            sizeof(run))
      || !core_sym(core, "retro_serialize_size",         &serialize_size, sizeof(serialize_size))
      || !core_sym(core, "retro_serialize",              &serialize,      sizeof(serialize))
      || !core_sym(core, "retro_unserialize",            &unserialize,    sizeof(unserialize))
      || !core_sym(core, "retro_get_memory_data",        &memory_data,    sizeof(memory_data))
      || !core_sym(core, "retro_set_controller_port_device", &set_port,   sizeof(set_port)))
      return 2;

   set_env(env_cb);
   init();
   set_video(video_cb);
   set_poll(input_poll_cb);
   set_state(input_state_cb);
   set_audio(audio_cb);
   set_batch(audio_batch_cb);
   memset(&info, 0, sizeof(info));
   info.path = PROBE_PATH;
   if (!load_game(&info))
   {
      fprintf(stderr, "[gpu_thread] retro_load_game failed\n");
      return 2;
   }
   if (gun)
   {
      /* Aimed at the middle of the picture, with crosshairs */
      set_port(0, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 0));
      set_port(1, RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_LIGHTGUN, 1));
   }
   size  = serialize_size();
   saved = (unsigned char *)malloc(size);
   state = (unsigned char *)malloc(size);
   if (!saved || !state)
      return 2;
   /* A state saved part way, loaded back later: the frames between are
    * run twice, and the picture of the second run is what counts. */
   for (f = 0; f < FRAMES + (LOAD_AT - SAVE_AT); f++)
   {
      frame_no = f;
      if (f == SAVE_AT && !serialize(saved, size))
         return 2;
      if (f == LOAD_AT && !unserialize(saved, size))
         return 2;
      run();
   }
   {
      const unsigned char *ram = (const unsigned char *)memory_data(RETRO_MEMORY_SYSTEM_RAM);
      if (!ram)
         return 2;
      ram_hash = fnv(ram, 2048 * 1024, 2166136261u);
      memcpy(&waits, ram + 0xF0000, 4);
   }
   if (!serialize(state, size))
      return 2;
   state_hash = fnv(state, size, 2166136261u);
   for (i = 0; i + klen + 4 + 1024 * 512 * 2 <= size; i++)
      if (!memcmp(state + i, key, klen))
      {
         vram_hash = fnv(state + i + klen + 4, 1024 * 512 * 2, 2166136261u);
         found = 1;
         break;
      }
   free(state);
   free(saved);
   unload_game();
   deinit();
   CORE_CLOSE(core);
   remove(PROBE_PATH);
   remove(BIOS_PATH);
   if (!found)
   {
      fprintf(stderr, "[gpu_thread] no GPURAM entry in the state\n");
      return 2;
   }
   printf("%08lx %08lx %08lx %lu", (unsigned long)vram_hash,
         (unsigned long)video_hash, (unsigned long)ram_hash, (unsigned long)waits);
   if (gun)
      printf(" %08lx", (unsigned long)state_hash);
   printf("\n");
   return 0;
}
