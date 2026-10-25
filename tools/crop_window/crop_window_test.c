/* crop_window_test: the overscan crop keeps the game's horizontal
 * display range.
 *
 * Loads the software core with a PS-X EXE written here: it programs a
 * display mode and a GP1(06h) range, fills the displayed VRAM white and
 * spins.  The cropped picture must be exactly the programmed range wide
 * and white edge to edge, and the requested aspect ratio must follow
 * the range: 4/3 * window/2800 for "Corrected".  The frontend lends its
 * own framebuffer, and one case widens the range part way through, so a
 * window change while the loan is held is covered too.
 *
 * Usage: crop_window_test <core> <case>
 *   cases: std320 std368 wide368 wide368_static wide368_off switch368
 *          switch368_dupe
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "libretro.h"

#define PROBE_PATH "crop_window_probe.exe"
/* An all-zero kernel: the CPU runs straight to the EXE loader's patch */
#define BIOS_PATH  "scph5501.bin"
#define MAX_W      1024
#define MAX_H      576
#define RUN_FRAMES 240

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

struct probe_case
{
   const char *name;
   const char *crop;           /* beetle_psx_crop_overscan */
   const char *dupe;           /* beetle_psx_frame_duping */
   uint32_t    gp1_mode;       /* GP1(08h) */
   uint32_t    x1, x2;         /* GP1(06h) at boot */
   uint32_t    x1_late, x2_late; /* GP1(06h) after the delay, 0 = none */
   unsigned    want_w;
   unsigned    want_white;     /* white columns on the middle line */
   double      want_ar;
};

static const struct probe_case cases[] =
{
   /* The standard 2560-clock range keeps its window in every mode */
   { "std320",        "smart",    "disabled", 0x08000001, 0x260, 0xC60, 0, 0, 320, 320, 4.0 / 3.0 * 2560.0 / 2800.0 },
   { "std368",        "smart",    "disabled", 0x08000040, 0x260, 0xC60, 0, 0, 366, 366, 4.0 / 3.0 * 2560.0 / 2800.0 },
   /* 384 dots at divider 7 are 2688 clocks */
   { "wide368",       "smart",    "disabled", 0x08000040, 0x260, 0xCE0, 0, 0, 384, 384, 4.0 / 3.0 * 2688.0 / 2800.0 },
   { "wide368_static","static",   "disabled", 0x08000040, 0x258, 0xCD8, 0, 0, 384, 384, 4.0 / 3.0 * 2688.0 / 2800.0 },
   /* Uncropped: the whole 2800-clock line, border included, which ends
    * one dot before this range does */
   { "wide368_off",   "disabled", "disabled", 0x08000040, 0x260, 0xCE0, 0, 0, 400, 383, 4.0 / 3.0 },
   /* The range widens on a still picture: with frame duping on, the
    * new range alone has to get a frame out */
   { "switch368",     "smart",    "disabled", 0x08000040, 0x260, 0xC60, 0x260, 0xCE0, 384, 384, 4.0 / 3.0 * 2688.0 / 2800.0 },
   { "switch368_dupe","smart",    "enabled",  0x08000040, 0x260, 0xC60, 0x260, 0xCE0, 384, 384, 4.0 / 3.0 * 2688.0 / 2800.0 }
};

static const struct probe_case *pc;

static double   last_ar;
static unsigned geometry_calls;
static uint32_t *lent;
static size_t    lent_size;
static uint32_t  shot[MAX_W * MAX_H];
static unsigned  shot_w, shot_h;
static unsigned  frames_shown;
static unsigned  frames_lent;

/* --- probe EXE ------------------------------------------------------ */

static size_t emit(uint32_t *code, size_t n, uint32_t word)
{
   code[n] = word;
   return n + 1;
}

/* lui t1,hi; ori t1,t1,lo; sw t1,off(t0) */
static size_t emit_store(uint32_t *code, size_t n, uint16_t off, uint32_t value)
{
   n = emit(code, n, 0x3C090000u | (value >> 16));
   n = emit(code, n, 0x35290000u | (value & 0xFFFFu));
   return emit(code, n, 0xAD090000u | off);
}

/* About half a second of emulated time */
static size_t emit_delay(uint32_t *code, size_t n)
{
   n = emit(code, n, 0x3C0A0010u);                    /* lui t2,0x0010 */
   n = emit(code, n, 0x254AFFFFu);                    /* addiu t2,t2,-1 */
   n = emit(code, n, 0x1540FFFEu);                    /* bnez t2,-2 */
   return emit(code, n, 0x00000000u);
}

static int write_probe(const char *path)
{
   uint32_t code[64];
   unsigned char hdr[0x800];
   size_t   n = 0, i;
   FILE    *f;

   n = emit(code, n, 0x3C081F80u);                    /* lui t0,0x1F80 */
   n = emit_store(code, n, 0x1814, 0x00000000u);       /* GP1(00h) reset */
   n = emit_store(code, n, 0x1814, pc->gp1_mode);
   n = emit_store(code, n, 0x1814, 0x06000000u | pc->x1 | (pc->x2 << 12));
   n = emit_store(code, n, 0x1814, 0x07000000u | 16 | (256u << 10));
   n = emit_store(code, n, 0x1814, 0x05000000u);       /* display at 0,0 */
   n = emit_store(code, n, 0x1814, 0x03000000u);       /* display on */
   n = emit_store(code, n, 0x1810, 0x02FFFFFFu);       /* fill white */
   n = emit_store(code, n, 0x1810, 0x00000000u);
   n = emit_store(code, n, 0x1810, (240u << 16) | 400u);
   n = emit_store(code, n, 0x1814, pc->gp1_mode);
   /* The core takes the display mode's interlacing, and only lends the
    * frontend's framebuffer to a progressive picture, once GP1(08h) has
    * been written 20 times; games rewrite it every frame.  The late
    * range comes after a delay, while the loan is held. */
   n = emit(code, n, 0x240B0018u);                    /* li t3,24 */
   n = emit(code, n, 0xAD091814u);                    /* sw t1,GP1 */
   n = emit(code, n, 0x256BFFFFu);                    /* addiu t3,t3,-1 */
   n = emit(code, n, 0x1560FFFDu);                    /* bnez t3,-3 */
   n = emit(code, n, 0x00000000u);
   if (pc->x2_late)
   {
      n = emit_delay(code, n);
      n = emit_store(code, n, 0x1814, 0x06000000u | pc->x1_late | (pc->x2_late << 12));
   }
   n = emit(code, n, 0x1000FFFFu);                    /* b . */
   n = emit(code, n, 0x00000000u);

   memset(hdr, 0, sizeof(hdr));
   memcpy(hdr, "PS-X EXE", 8);
#define PUT32(o, v) do { hdr[(o)] = (unsigned char)(v); hdr[(o) + 1] = (unsigned char)((v) >> 8); \
      hdr[(o) + 2] = (unsigned char)((v) >> 16); hdr[(o) + 3] = (unsigned char)((v) >> 24); } while (0)
   PUT32(0x10, 0x80010000u);
   PUT32(0x18, 0x80010000u);
   PUT32(0x1C, 0x800u);              /* the loader reads whole sectors */
   PUT32(0x30, 0x801FFFF0u);

   if (!(f = fopen(path, "wb")))
      return 0;
   fwrite(hdr, 1, sizeof(hdr), f);
   for (i = 0; i < n; i++)
   {
      unsigned char w[4];
      w[0] = (unsigned char)code[i];
      w[1] = (unsigned char)(code[i] >> 8);
      w[2] = (unsigned char)(code[i] >> 16);
      w[3] = (unsigned char)(code[i] >> 24);
      fwrite(w, 1, 4, f);
   }
   memset(hdr, 0, sizeof(hdr));
   fwrite(hdr, 1, 0x800 - n * 4, f);
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

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_INFO && !getenv("CROP_WINDOW_DEBUG"))
      return;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static const char *get_var(const char *key)
{
   if (!strcmp(key, "beetle_psx_crop_overscan"))
      return pc->crop;
   if (!strcmp(key, "beetle_psx_aspect_ratio"))
      return "corrected";
   if (!strcmp(key, "beetle_psx_skip_bios"))
      return "enabled";
   if (!strcmp(key, "beetle_psx_frame_duping"))
      return pc->dupe;
   if (!strcmp(key, "beetle_psx_renderer"))
      return "software";
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
         return *(const enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         *(bool *)data = true;
         return true;
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
         last_ar = ((const struct retro_game_geometry *)data)->aspect_ratio;
         geometry_calls++;
         return true;
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
         last_ar = ((const struct retro_system_av_info *)data)->geometry.aspect_ratio;
         return true;
      case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER:
      {
         struct retro_framebuffer *fb = (struct retro_framebuffer *)data;
         size_t need = (size_t)fb->width * fb->height * sizeof(uint32_t);
         if (need > lent_size)
         {
            free(lent);
            if (!(lent = (uint32_t *)malloc(need)))
            {
               lent_size = 0;
               return false;
            }
            lent_size = need;
         }
         /* Poison it: columns the core forgets to scan out show up */
         memset(lent, 0x5A, need);
         fb->data   = lent;
         fb->pitch  = fb->width * sizeof(uint32_t);
         fb->format = RETRO_PIXEL_FORMAT_XRGB8888;
         return true;
      }
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
   if (!data || w > MAX_W || h > MAX_H)
      return;
   for (y = 0; y < h; y++)
      memcpy(shot + y * w, (const uint8_t *)data + y * pitch, w * sizeof(uint32_t));
   shot_w = w;
   shot_h = h;
   frames_shown++;
   if (lent && data == (const void *)lent)
      frames_lent++;
}

static void input_poll_cb(void) { }
static int16_t input_state_cb(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }
static size_t audio_batch_cb(const int16_t *data, size_t frames)
{ (void)data; return frames; }
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }

int main(int argc, char **argv)
{
   core_handle core;
   struct retro_game_info info;
   struct retro_system_av_info av;
   unsigned i, x, white = 0;
   int fail = 0;
   void (*set_env)(retro_environment_t);
   void (*set_video)(retro_video_refresh_t);
   void (*set_poll)(retro_input_poll_t);
   void (*set_state)(retro_input_state_t);
   void (*set_audio)(retro_audio_sample_t);
   void (*set_batch)(retro_audio_sample_batch_t);
   void (*init)(void);
   void (*deinit)(void);
   bool (*load)(const struct retro_game_info *);
   void (*unload)(void);
   void (*run)(void);
   void (*get_av)(struct retro_system_av_info *);

   if (argc < 3)
   {
      fprintf(stderr, "usage: %s <core> <case>\n", argv[0]);
      return 2;
   }
   for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
      if (!strcmp(cases[i].name, argv[2]))
         pc = &cases[i];
   if (!pc)
   {
      fprintf(stderr, "unknown case %s\n", argv[2]);
      return 2;
   }
   if (!write_probe(PROBE_PATH))
   {
      fprintf(stderr, "cannot write %s\n", PROBE_PATH);
      return 2;
   }

   if (!(core = CORE_OPEN(argv[1])))
   {
      fprintf(stderr, "cannot load %s\n", argv[1]);
      return 2;
   }
#define SYM(var, name) if (!core_sym(core, name, &var, sizeof(var))) return 2
   SYM(set_env,   "retro_set_environment");
   SYM(set_video, "retro_set_video_refresh");
   SYM(set_poll,  "retro_set_input_poll");
   SYM(set_state, "retro_set_input_state");
   SYM(set_audio, "retro_set_audio_sample");
   SYM(set_batch, "retro_set_audio_sample_batch");
   SYM(init,      "retro_init");
   SYM(deinit,    "retro_deinit");
   SYM(load,      "retro_load_game");
   SYM(unload,    "retro_unload_game");
   SYM(run,       "retro_run");
   SYM(get_av,    "retro_get_system_av_info");

   set_env(env_cb);
   init();
   set_video(video_cb);
   set_poll(input_poll_cb);
   set_state(input_state_cb);
   set_audio(audio_cb);
   set_batch(audio_batch_cb);

   memset(&info, 0, sizeof(info));
   info.path = PROBE_PATH;
   if (!load(&info))
   {
      fprintf(stderr, "retro_load_game failed\n");
      return 3;
   }
   get_av(&av);
   last_ar = av.geometry.aspect_ratio;

   for (i = 0; i < RUN_FRAMES; i++)
      run();

   /* The middle line, edge to edge */
   if (shot_w && shot_h)
      for (x = 0; x < shot_w; x++)
         if ((shot[(shot_h / 2) * shot_w + x] & 0x00F0F0F0u) == 0x00F0F0F0u)
            white++;
   get_av(&av);

   fprintf(stderr, "[crop_window] %s: %ux%u, %u white columns, aspect %.4f "
         "(av %.4f, %u geometry changes, %u/%u frames lent), want %u wide, %u white, at %.4f\n",
         pc->name, shot_w, shot_h, white, last_ar, av.geometry.aspect_ratio,
         geometry_calls, frames_lent, frames_shown, pc->want_w, pc->want_white, pc->want_ar);

   /* Without duping every frame is shown: a window change while the loan
    * is held must not cost one.  And the frontend's framebuffer has to be
    * in play for the lane to cover the loan at all. */
   if (   !frames_shown
       || (!strcmp(pc->dupe, "disabled") && (frames_shown != RUN_FRAMES || !frames_lent)))
      fail = 1;
   if (shot_w != pc->want_w || white != pc->want_white)
      fail = 1;
   if (fabs(last_ar - pc->want_ar) > 0.001 || fabs(av.geometry.aspect_ratio - pc->want_ar) > 0.001)
      fail = 1;

   unload();
   deinit();
   CORE_CLOSE(core);
   free(lent);
   remove(PROBE_PATH);
   remove(BIOS_PATH);

   fprintf(stderr, "[crop_window] %s: %s\n", pc->name, fail ? "FAIL" : "OK");
   return fail;
}
