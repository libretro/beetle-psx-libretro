/* Software rasterizer span lane.
 *
 * The probe EXE sends one fixed GP0 command list through DMA channel 2 in
 * linked-list mode every frame. The list is generated here from a fixed
 * seed and covers what the textured span paths specialise on: 4, 8 and
 * 15 bit textures, raw and modulated texels, flat and Gouraud colour,
 * dithering on and off, the four blend modes and opaque, mask set and
 * mask test, and drawing into the texture pages and CLUT rows that later
 * primitives sample, so the texture cache refills from VRAM the frame
 * just changed.
 *
 * After FRAMES frames the host hashes native VRAM (the savestate's GPURAM
 * entry) and the last video frame, and compares them with values pinned
 * from the scalar renderer. A vector path that drifts from the scalar
 * one by a single pixel changes the hash. It also hashes main RAM, where
 * the probe counts how long it waited for the GPU each frame: that hash is
 * the same at every resolution, as emulated GPU timing must be.
 *
 * usage: gpu_span_test <core> <1x|2x|4x>
 * Runs in the current directory: it writes the probe EXE and an all-zero
 * BIOS image there. */
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

#define PROBE_PATH  "gpu_span_probe.exe"
/* An all-zero kernel: the CPU runs straight to the EXE loader's patch */
#define BIOS_PATH   "scph5501.bin"
#define FRAMES      24
#define LIST_ADDR   0x80020000u
#define LIST_MAX    (64 * 1024)        /* words */
#define PRIMS       360

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

struct pinned
{
   const char *res;
   uint32_t    vram;
   uint32_t    video;
};

/* From the scalar renderer (an ARMv7 build without NEON), FRAMES frames. */
static const struct pinned pins[] =
{
   { "1x", 0x3f9b89b2u, 0x699cb235u },
   { "2x", 0xb957beabu, 0x669e695du },
   { "4x", 0x69ab585fu, 0xc1bb0055u }
};

/* Main RAM after FRAMES frames, the same at every internal resolution:
 * the probe counts its waits for the GPU's DMA into RAM, and emulated GPU
 * timing does not depend on the resolution. */
#define PINNED_RAM 0x90ee9471u

static const char *resolution = "1x";
static uint32_t    video_hash;

/* --- probe EXE ------------------------------------------------------ */

static uint32_t rng_state = 0x1031BEE7u;
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
 * within one expression unspecified, and compilers for different targets
 * pick different orders, which would change the command list. */
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
   uint32_t w[16];
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
   for (i = 0; i < 8; i++)
   {
      w[0] = 0x40000000u | rcol(); w[1] = xy((int)i * 32, 501); w[2] = xy((int)i * 32 + 31, 501);
      node(w, 3);
   }
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
      k    = rnd(10);
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
         case 2: /* Gouraud textured triangle */
            w[0] = 0x34000000u | (semi << 25) | rcol();
            w[1] = pt(x, y); w[2] = uv(clut); w[3] = rcol(); w[4] = pt(x, y); w[5] = uv(page);
            w[6] = rcol(); w[7] = pt(x, y); w[8] = uv(0);
            n = 9;
            break;
         case 3: /* Gouraud textured quad */
            w[0] = 0x3C000000u | (semi << 25) | rcol();
            w[1] = pt(x, y); w[2] = uv(clut); w[3] = rcol(); w[4] = pt(x, y); w[5] = uv(page);
            w[6] = rcol(); w[7] = pt(x, y); w[8] = uv(0);
            w[9] = rcol(); w[10] = pt(x, y); w[11] = uv(0);
            n = 12;
            break;
         case 4: /* Gouraud quad: backgrounds, with mask bits from E6 */
            w[0] = 0x38000000u | (semi << 25) | rcol();
            w[1] = pt(x, y); w[2] = rcol(); w[3] = pt(x, y); w[4] = rcol();
            w[5] = pt(x, y); w[6] = rcol(); w[7] = pt(x, y);
            n = 8;
            break;
         case 5: /* flat rectangle */
         {
            uint32_t rh, rw;
            rh = 1 + rnd(120);
            rw = 1 + rnd(200);
            w[0] = 0x60000000u | (semi << 25) | rcol(); w[1] = xy(x, y);
            w[2] = (rh << 16) | rw;
            n = 3;
         }
            break;
         case 6: /* mask set / test */
            w[0] = 0xE6000000u | rnd(4);
            n = 1;
            break;
         case 7: /* a polygon drawn into its own texture page */
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
         case 8: /* copy */
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
         default: /* long horizontal span */
            w[0] = 0x2C000000u | (semi << 25) | (raw << 24) | rcol();
            w[1] = xy(x, y); w[2] = uv(clut); w[3] = xy(x + 400, y); w[4] = uv(page);
            w[5] = xy(x, y + 6); w[6] = uv(0); w[7] = xy(x + 400, y + 6); w[8] = uv(0);
            n = 9;
            break;
      }
      node(w, n);
   }
   w[0] = 0xE6000000u; node(w, 1);
}

static size_t emit(uint32_t *code, size_t n, uint32_t word)
{
   code[n] = word;
   return n + 1;
}

/* lui t1,hi; ori t1,t1,lo */
static size_t emit_li(uint32_t *code, size_t n, uint32_t value)
{
   n = emit(code, n, 0x3C090000u | (value >> 16));
   return emit(code, n, 0x35290000u | (value & 0xFFFFu));
}

static int write_probe(void)
{
   static const uint32_t gp1[] =
   {
      0x00000000u, 0x08000001u, 0x05000000u, 0x06C60260u,
      0x07040010u, 0x03000000u, 0x04000002u
   };
   uint32_t      code[128];
   unsigned char hdr[0x800];
   size_t        n = 0, i, frame, wait, vb, image;
   FILE         *f;

   build_list();

   n = emit(code, n, 0x3C081F80u);                        /* lui t0,0x1F80 */
   n = emit(code, n, 0xAD001074u);                        /* I_MASK = 0 */
   for (i = 0; i < sizeof(gp1) / sizeof(gp1[0]); i++)
   {
      n = emit_li(code, n, gp1[i]);
      n = emit(code, n, 0xAD091814u);                     /* GP1 */
   }
   n = emit_li(code, n, 0x07654B21u);
   n = emit(code, n, 0xAD0910F0u);                        /* DPCR: DMA2 on */
   n = emit(code, n, 0x3C11800Fu);                        /* lui s1,0x800F */
   n = emit(code, n, 0x24100000u);                        /* li s0,0 */
   frame = n;
   n = emit_li(code, n, LIST_ADDR & 0xFFFFFFu);
   n = emit(code, n, 0xAD0910A0u);                        /* D2_MADR */
   n = emit(code, n, 0xAD0010A4u);                        /* D2_BCR = 0 */
   n = emit_li(code, n, 0x01000401u);
   n = emit(code, n, 0xAD0910A8u);                        /* D2_CHCR: list */
   wait = n;
   n = emit(code, n, 0x26100001u);                        /* addiu s0,s0,1 */
   n = emit(code, n, 0x8D0910A8u);                        /* lw t1,CHCR */
   n = emit(code, n, 0x00000000u);
   n = emit(code, n, 0x3C0A0100u);                        /* lui t2,0x0100 */
   n = emit(code, n, 0x012A4824u);                        /* and t1,t1,t2 */
   n = emit(code, n, 0x15200000u | ((uint32_t)(wait - n - 1) & 0xFFFFu)); /* bnez t1 */
   n = emit(code, n, 0x00000000u);
   /* the DMA wait count, which follows the GPU's emulated busy time */
   n = emit(code, n, 0xAE300000u);                        /* sw s0,0(s1) */
   n = emit_li(code, n, 0xFFFFFFFEu);
   n = emit(code, n, 0xAD091070u);                        /* ack VBLANK */
   vb = n;
   n = emit(code, n, 0x8D091070u);                        /* lw t1,I_STAT */
   n = emit(code, n, 0x00000000u);
   n = emit(code, n, 0x31290001u);                        /* andi t1,t1,1 */
   n = emit(code, n, 0x11200000u | ((uint32_t)(vb - n - 1) & 0xFFFFu));   /* beqz t1 */
   n = emit(code, n, 0x00000000u);
   n = emit(code, n, 0x10000000u | ((uint32_t)(frame - n - 1) & 0xFFFFu)); /* b frame */
   n = emit(code, n, 0x00000000u);

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
      if (i < n)
         v = code[i];
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
   if (level < RETRO_LOG_ERROR && !getenv("GPU_SPAN_DEBUG"))
      return;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
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
         *(bool *)data = false;
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
   video_hash = hh;
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
   void  *(*memory_data)(unsigned);
   static const char key[] = "\015&GPURAM[0][0]";
   struct retro_game_info info;
   const struct pinned *pin = NULL;
   unsigned char *state;
   size_t size, i, klen = sizeof(key) - 1;
   uint32_t vram_hash = 0, ram_hash = 0, waits = 0;
   unsigned f;
   int found = 0, fail = 0;

   if (argc < 3)
   {
      fprintf(stderr, "usage: %s <core> <1x|2x|4x>\n", argv[0]);
      return 2;
   }
   resolution = argv[2];
   for (i = 0; i < sizeof(pins) / sizeof(pins[0]); i++)
      if (!strcmp(pins[i].res, resolution))
         pin = &pins[i];
   if (!pin || !write_probe())
   {
      fprintf(stderr, "bad resolution or cannot write the probe\n");
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
      || !core_sym(core, "retro_get_memory_data",        &memory_data,    sizeof(memory_data)))
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
      fprintf(stderr, "[gpu_span] retro_load_game failed\n");
      return 2;
   }
   for (f = 0; f < FRAMES; f++)
      run();
   {
      const unsigned char *ram = (const unsigned char *)memory_data(RETRO_MEMORY_SYSTEM_RAM);
      if (!ram)
      {
         fprintf(stderr, "[gpu_span] no system RAM\n");
         return 2;
      }
      ram_hash = fnv(ram, 2048 * 1024, 2166136261u);
      memcpy(&waits, ram + 0xF0000, 4);
   }

   size = serialize_size();
   if (!(state = (unsigned char *)malloc(size)) || !serialize(state, size))
   {
      fprintf(stderr, "[gpu_span] retro_serialize failed\n");
      return 2;
   }
   for (i = 0; i + klen + 4 + 1024 * 512 * 2 <= size; i++)
      if (!memcmp(state + i, key, klen))
      {
         vram_hash = fnv(state + i + klen + 4, 1024 * 512 * 2, 2166136261u);
         found = 1;
         break;
      }
   free(state);
   unload_game();
   deinit();
   CORE_CLOSE(core);
   remove(PROBE_PATH);
   remove(BIOS_PATH);

   if (!found)
   {
      fprintf(stderr, "[gpu_span] no GPURAM entry in the state\n");
      return 2;
   }
   if (vram_hash != pin->vram || video_hash != pin->video || ram_hash != PINNED_RAM)
      fail = 1;
   fprintf(stderr, "[gpu_span] %s vram %08lx (want %08lx) video %08lx (want %08lx) ram %08lx waits %lu %s\n",
         resolution, (unsigned long)vram_hash, (unsigned long)pin->vram,
         (unsigned long)video_hash, (unsigned long)pin->video,
         (unsigned long)ram_hash, (unsigned long)waits, fail ? "FAIL" : "OK");
   if (ram_hash != PINNED_RAM)
      fprintf(stderr, "[gpu_span] %s: RAM %08lx, want %08lx: GPU timing differs from 1x\n",
            resolution, (unsigned long)ram_hash, (unsigned long)PINNED_RAM);
   return fail;
}
