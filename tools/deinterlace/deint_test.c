/* deint_test: the software renderer's 480i output, per deinterlacer.
 *
 * Loads the software core with a PS-X EXE written here: 640x480i, every
 * VRAM row of the displayed area filled with its own colour (red from the
 * row's low five bits, green from the next five), then GP1(08h) written
 * often enough for the core to take the mode. Each frame's middle column
 * is checked row by row against those colours:
 *
 *   bob     240 rows, row k from VRAM row 2k + field, one field per frame,
 *           alternating
 *   weave   480 rows, row y from VRAM row y
 *   off     the same, scanned out at the end of the frame straight into
 *           the frontend's framebuffer, which is lent and poisoned
 *   others  frames hashed only (bob_offset, fastmad)
 *
 * at the internal resolution given, whose rows repeat each native row.
 * Every mode prints a hash of all frames shown, for comparing builds.
 *
 * Usage: deint_test <core> <weave|bob|bob_offset|fastmad|off> [1x(native)|2x|4x]
 *        [disabled|static|smart]  (overscan crop, default disabled)
 */
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

#define PROBE_PATH "deint_probe.exe"
#define BIOS_PATH  "scph5501.bin"
#define RUN_FRAMES 90
#define MAX_W      (700 * 4)
#define MAX_H      (576 * 4)

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

static const char *deint_mode;
static const char *resolution;
static const char *crop;

static uint32_t *shot;
static unsigned  shot_w, shot_h, frames_shown, frames_lent, bad_frames, bob_fields[2];
static uint32_t *lent;
static size_t    lent_size;
static unsigned long long frame_hash = 1469598103934665603ULL;

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

/* --- probe EXE ------------------------------------------------------ */

static uint32_t *code;
static size_t    code_n;

static void emit(uint32_t word)
{
   code[code_n++] = word;
}

/* lui t1,hi; ori t1,t1,lo; sw t1,off(t0) */
static void emit_store(uint16_t off, uint32_t value)
{
   emit(0x3C090000u | (value >> 16));
   emit(0x35290000u | (value & 0xFFFFu));
   emit(0xAD090000u | off);
}

/* Spin until GPUSTAT says a command word can be taken (t7 = bit 26) */
static void emit_wait_ready(void)
{
   emit(0x8D0E1814u);                  /* lw t6,GP1(t0) */
   emit(0x00000000u);
   emit(0x01CF7024u);                  /* and t6,t6,t7 */
   emit(0x11C0FFFCu);                  /* beqz t6,-4 */
   emit(0x00000000u);
}

static uint32_t row_colour(uint32_t y)
{
   return ((y << 3) & 0xF8u) | ((((y >> 5) << 3) & 0xF8u) << 8);
}

static int write_files(void)
{
   unsigned char hdr[0x800];
   size_t        bytes, i;
   uint32_t      y;
   FILE         *f;

   code   = (uint32_t *)calloc(16384, sizeof(uint32_t));
   code_n = 0;
   if (!code)
      return 0;

   emit(0x3C081F80u);                  /* lui t0,0x1F80 */
   emit(0x3C0F0400u);                  /* lui t7,0x0400 */
   emit_store(0x1814, 0x00000000u);     /* reset */
   emit_store(0x1814, 0x08000027u);     /* 640 x 480i NTSC */
   emit_store(0x1814, 0x06000000u | 0x260u | (0xC60u << 12));
   emit_store(0x1814, 0x07000000u | 16u | (256u << 10));
   emit_store(0x1814, 0x05000000u);
   emit_store(0x1814, 0x03000000u);
   emit_wait_ready();
   emit_store(0x1810, 0xE1000400u);     /* draw to the displayed field */
   for (y = 0; y < 480; y++)
   {
      emit_wait_ready();
      emit_store(0x1810, 0x02000000u | row_colour(y));
      emit_store(0x1810, y << 16);
      emit_store(0x1810, (1u << 16) | 640u);
   }
   /* The core takes the mode's interlacing once GP1(08h) has been
    * written twenty times; games write it every frame */
   emit(0x3C090800u);                  /* lui t1,0x0800 */
   emit(0x35290027u);                  /* ori t1,t1,0x27 */
   emit(0x240B0018u);                  /* li t3,24 */
   emit(0xAD091814u);                  /* sw t1,GP1 */
   emit(0x256BFFFFu);                  /* addiu t3,t3,-1 */
   emit(0x1560FFFDu);                  /* bnez t3,-3 */
   emit(0x00000000u);
   emit(0x1000FFFFu);                  /* b . */
   emit(0x00000000u);

   bytes = (code_n * 4 + 0x7FF) & ~(size_t)0x7FF;

   memset(hdr, 0, sizeof(hdr));
   memcpy(hdr, "PS-X EXE", 8);
#define PUT32(o, v) do { hdr[(o)] = (unsigned char)(v); hdr[(o) + 1] = (unsigned char)((v) >> 8); \
      hdr[(o) + 2] = (unsigned char)((v) >> 16); hdr[(o) + 3] = (unsigned char)((v) >> 24); } while (0)
   PUT32(0x10, 0x80010000u);
   PUT32(0x18, 0x80010000u);
   PUT32(0x1C, (uint32_t)bytes);
   PUT32(0x30, 0x801FFFF0u);

   if (!(f = fopen(PROBE_PATH, "wb")))
      return 0;
   fwrite(hdr, 1, sizeof(hdr), f);
   for (i = 0; i < bytes / 4; i++)
   {
      uint32_t      w = i < code_n ? code[i] : 0;
      unsigned char b[4];
      b[0] = (unsigned char)w;         b[1] = (unsigned char)(w >> 8);
      b[2] = (unsigned char)(w >> 16); b[3] = (unsigned char)(w >> 24);
      fwrite(b, 1, 4, f);
   }
   fclose(f);
   free(code);

   /* An all-zero kernel: the CPU runs straight to the EXE loader's patch */
   if (!(f = fopen(BIOS_PATH, "wb")))
      return 0;
   memset(hdr, 0, sizeof(hdr));
   for (i = 0; i < 512 * 1024 / sizeof(hdr); i++)
      fwrite(hdr, 1, sizeof(hdr), f);
   fclose(f);
   return 1;
}

/* --- checks --------------------------------------------------------- */

/* The VRAM row a pixel shows, from its colour; -1 for none */
static int vram_row_of(uint32_t px)
{
   uint32_t r5 = ((px >> 16) & 0xFFu) >> 3;
   uint32_t g5 = ((px >> 8) & 0xFFu) >> 3;
   return (int)(r5 | (g5 << 5));
}

/* 0 when frame rows match the mode's mapping */
static int check_frame(const uint32_t *px, unsigned w, unsigned h, size_t pitch_px)
{
   unsigned scale = 1, k, field = 2;

   if (!strcmp(resolution, "2x"))
      scale = 2;
   else if (!strcmp(resolution, "4x"))
      scale = 4;

   if (!strcmp(deint_mode, "bob"))
   {
      if (h != 240 * scale)
         return 1;
      for (k = 0; k < h; k++)
      {
         int row = vram_row_of(px[k * pitch_px + w / 2]);
         unsigned native = k / scale;
         if (field == 2)
            field = (unsigned)row & 1u;
         if (row != (int)(native * 2 + field))
            return 1;
      }
      bob_fields[field]++;
   }
   else if (!strcmp(deint_mode, "weave") || !strcmp(deint_mode, "off"))
   {
      if (h != 480 * scale)
         return 1;
      for (k = 0; k < h; k++)
         if (vram_row_of(px[k * pitch_px + w / 2]) != (int)(k / scale))
            return 1;
   }
   return 0;
}

/* --- frontend ------------------------------------------------------- */

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_WARN)
      return;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static const char *get_var(const char *key)
{
   if (!strcmp(key, "beetle_psx_deinterlacer"))
      return deint_mode;
   if (!strcmp(key, "beetle_psx_internal_resolution"))
      return resolution;
   if (!strcmp(key, "beetle_psx_crop_overscan"))
      return crop;
   if (!strcmp(key, "beetle_psx_skip_bios"))
      return "enabled";
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
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
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
         /* Poisoned: rows or columns left unwritten show up */
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
   {
      const unsigned char *row = (const unsigned char *)data + y * pitch;
      size_t i;
      memcpy(shot + (size_t)y * w, row, w * sizeof(uint32_t));
      for (i = 0; i < w * sizeof(uint32_t); i++)
      {
         frame_hash ^= row[i];
         frame_hash *= 1099511628211ULL;
      }
   }
   shot_w = w;
   shot_h = h;
   frames_shown++;
   if (lent && data == (const void *)lent)
      frames_lent++;
   /* The picture settles once the core has taken the 480i mode */
   if (frames_shown > RUN_FRAMES / 2 && check_frame(shot, w, h, w))
      bad_frames++;
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
   unsigned i;
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

   if (argc < 3)
   {
      fprintf(stderr, "usage: %s <core> <mode> [resolution]\n", argv[0]);
      return 2;
   }
   deint_mode = argv[2];
   resolution = argc > 3 ? argv[3] : "1x(native)";
   crop       = argc > 4 ? argv[4] : "disabled";

   if (!(shot = (uint32_t *)malloc((size_t)MAX_W * MAX_H * sizeof(uint32_t)))
         || !write_files())
      return 2;

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

   for (i = 0; i < RUN_FRAMES; i++)
      run();

   unload();
   deinit();
   CORE_CLOSE(core);
   remove(PROBE_PATH);
   remove(BIOS_PATH);

   if (frames_shown < RUN_FRAMES / 2 + 2 || bad_frames)
      fail = 1;
   if (!strcmp(deint_mode, "bob") && (!bob_fields[0] || !bob_fields[1]))
      fail = 1;
   if (!strcmp(deint_mode, "off") && frames_lent < RUN_FRAMES / 2)
      fail = 1;

   fprintf(stderr, "[deint] %s %s %s: %u frames (%u lent), last %ux%u, %u wrong, "
         "bob fields %u/%u, hash %016llx: %s\n", deint_mode, resolution, crop,
         frames_shown, frames_lent, shot_w, shot_h, bad_frames, bob_fields[0],
         bob_fields[1], frame_hash, fail ? "FAIL" : "OK");
   free(shot);
   free(lent);
   return fail;
}
