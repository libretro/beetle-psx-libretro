/* GTE register lane.
 *
 * The probe EXE first reads every GTE data register it checks into RAM
 * (the power-on values), then loads one set of distinct values into the
 * registers the state code carries through local copies (OTZ, SZ0-3,
 * RES1, LZCS and the LZCR computed from it, with IR0 and SXY2 alongside,
 * and SXYP read as its mirror), reads
 * them back into RAM in a loop, and after a countdown overwrites them with
 * a second set.  Cases:
 *
 *   load    save while the first set is live, run until the second set is
 *           in, load: the first set reads back, and a save straight after
 *           the load matches the first byte for byte.
 *   reset   reset with the first set live: the restarted probe reads the
 *           power-on values.
 *   toggle  start on the dynarec, switch to the interpreter: the first
 *           set stays; let the second set go in, switch back: the second
 *           set stays.
 *   reload  unload, deinit, init and load again in the same process: the
 *           second run starts clean.
 *
 * usage: gte_state_test <core> <case> [cpu_dynarec mode]
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

#define PROBE_PATH  "gte_state_probe.exe"
/* An all-zero kernel: the CPU runs straight to the EXE loader's patch */
#define BIOS_PATH   "scph5501.bin"
#define READBACK    0xF0000
#define POWERON     0xF0100
#define MARKER      0x600DF00Du
/* Loop passes before the second set goes in: tens of frames in every
 * CPU mode, so the save lands well before it and the end well after */
#define COUNTDOWN   0x00080000u
#define SAVE_FRAME  5
#define END_FRAME   300

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

struct gte_reg
{
   const char *name;
   unsigned    index;   /* GTE data register */
   uint32_t    write[2];/* MTC2 value per set, ignored for LZCR */
   uint32_t    read[2]; /* what MFC2 returns afterwards */
};

static const struct gte_reg regs[] =
{
   { "OTZ",  7,  { 0x00001234u, 0x00005678u }, { 0x00001234u, 0x00005678u } },
   { "IR0",  8,  { 0x00000777u, 0x00000333u }, { 0x00000777u, 0x00000333u } },
   { "SXY2", 14, { 0x00110022u, 0x00330044u }, { 0x00110022u, 0x00330044u } },
   /* Not written (a write pushes the FIFO): reads as SXY2 */
   { "SXYP", 15, { 0, 0 },                    { 0x00110022u, 0x00330044u } },
   { "SZ0",  16, { 0x00001111u, 0x00005555u }, { 0x00001111u, 0x00005555u } },
   { "SZ1",  17, { 0x00002222u, 0x00006666u }, { 0x00002222u, 0x00006666u } },
   { "SZ2",  18, { 0x00003333u, 0x00007777u }, { 0x00003333u, 0x00007777u } },
   { "SZ3",  19, { 0x00004444u, 0x00008888u }, { 0x00004444u, 0x00008888u } },
   { "RES1", 23, { 0xDEADBEEFu, 0x01234567u }, { 0xDEADBEEFu, 0x01234567u } },
   /* LZCR counts 12 leading ones, then 16 leading zeroes */
   { "LZCS", 30, { 0xFFF00000u, 0x0000FFFFu }, { 0xFFF00000u, 0x0000FFFFu } },
   { "LZCR", 31, { 0, 0 },                    { 12u, 16u } }
};
#define N_REGS (sizeof(regs) / sizeof(regs[0]))

static const char *cpu_mode = "disabled";
static bool        vars_dirty;

/* --- probe EXE ------------------------------------------------------ */

static size_t emit(uint32_t *code, size_t n, uint32_t word)
{
   code[n] = word;
   return n + 1;
}

/* lui t1,hi; ori t1,t1,lo; mtc2 t1,rN for every writable register */
static size_t emit_set(uint32_t *code, size_t n, unsigned set)
{
   size_t i;
   for (i = 0; i < N_REGS; i++)
   {
      uint32_t v = regs[i].write[set];
      if (regs[i].index == 15 || regs[i].index == 31)
         continue;
      n = emit(code, n, 0x3C090000u | (v >> 16));
      n = emit(code, n, 0x35290000u | (v & 0xFFFFu));
      n = emit(code, n, 0x48890000u | (regs[i].index << 11));
      n = emit(code, n, 0x00000000u);
   }
   return n;
}

static int write_probe(void)
{
   uint32_t      code[256];
   unsigned char hdr[0x800];
   size_t        n = 0, i, loop;
   FILE         *f;

   n = emit(code, n, 0x3C084000u);                    /* lui t0,0x4000 */
   n = emit(code, n, 0x40886000u);                    /* mtc0 t0,SR: COP2 on */
   n = emit(code, n, 0x00000000u);
   n = emit(code, n, 0x00000000u);
   n = emit(code, n, 0x3C11800Fu);                    /* lui s1,0x800F */
   for (i = 0; i < N_REGS; i++)
   {
      n = emit(code, n, 0x48090000u | (regs[i].index << 11));    /* mfc2 t1 */
      n = emit(code, n, 0x00000000u);
      n = emit(code, n, 0xAE290000u | (uint32_t)(0x100 + i * 4));/* sw t1,(s1) */
   }
   n = emit(code, n, 0x3C090000u | (MARKER >> 16));          /* lui t1 */
   n = emit(code, n, 0x35290000u | (MARKER & 0xFFFFu));      /* ori t1 */
   n = emit(code, n, 0xAE290000u | (uint32_t)(0x100 + N_REGS * 4));
   n = emit_set(code, n, 0);
   n = emit(code, n, 0x3C11800Fu);                    /* lui s1,0x800F */
   n = emit(code, n, 0x3C100000u | (COUNTDOWN >> 16));     /* lui s0 */
   n = emit(code, n, 0x36100000u | (COUNTDOWN & 0xFFFFu)); /* ori s0,s0 */
   loop = n;
   for (i = 0; i < N_REGS; i++)
   {
      n = emit(code, n, 0x48090000u | (regs[i].index << 11));    /* mfc2 t1 */
      n = emit(code, n, 0x00000000u);
      n = emit(code, n, 0xAE290000u | (uint32_t)(i * 4));        /* sw t1,(s1) */
   }
   n = emit(code, n, 0x2610FFFFu);                    /* addiu s0,s0,-1 */
   /* bnez s0,loop */
   n = emit(code, n, 0x16000000u | ((uint32_t)(loop - n - 1) & 0xFFFFu));
   n = emit(code, n, 0x00000000u);
   n = emit_set(code, n, 1);
   /* b loop */
   n = emit(code, n, 0x10000000u | ((uint32_t)(loop - n - 1) & 0xFFFFu));
   n = emit(code, n, 0x00000000u);

   memset(hdr, 0, sizeof(hdr));
   memcpy(hdr, "PS-X EXE", 8);
#define PUT32(o, v) do { hdr[(o)] = (unsigned char)(v); hdr[(o) + 1] = (unsigned char)((v) >> 8); \
      hdr[(o) + 2] = (unsigned char)((v) >> 16); hdr[(o) + 3] = (unsigned char)((v) >> 24); } while (0)
   PUT32(0x10, 0x80010000u);
   PUT32(0x18, 0x80010000u);
   PUT32(0x1C, 0x800u);              /* the loader reads whole sectors */
   PUT32(0x30, 0x801FFFF0u);
#undef PUT32
   if (!(f = fopen(PROBE_PATH, "wb")))
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
   for (i = 0; i < 512 * 1024 / sizeof(hdr); i++)
      fwrite(hdr, 1, sizeof(hdr), f);
   fclose(f);
   return 1;
}

/* --- frontend ------------------------------------------------------- */

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_ERROR && !getenv("GTE_STATE_DEBUG"))
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
      return cpu_mode;
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
         *(bool *)data = true;
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         var->value = get_var(var->key);
         return var->value != NULL;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = vars_dirty;
         vars_dirty    = false;
         return true;
      default:
         return false;
   }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
   (void)data; (void)w; (void)h; (void)pitch;
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

static uint32_t ram32(const uint8_t *ram, size_t off)
{
   return (uint32_t)ram[off] | ((uint32_t)ram[off + 1] << 8)
        | ((uint32_t)ram[off + 2] << 16) | ((uint32_t)ram[off + 3] << 24);
}

/* 0 when every register reads back as the given set; set 2 is power-on */
static int check_regs(const uint8_t *ram, size_t base, unsigned set, const char *when)
{
   size_t i;
   int    bad = 0;
   for (i = 0; i < N_REGS; i++)
   {
      uint32_t got  = ram32(ram, base + i * 4);
      uint32_t want = set < 2 ? regs[i].read[set] : 0;
      if (got != want)
      {
         fprintf(stderr, "[gte_state] %s: %s (r%u) = 0x%08lX, want 0x%08lX\n",
               when, regs[i].name, regs[i].index,
               (unsigned long)got, (unsigned long)want);
         bad = 1;
      }
   }
   return bad;
}

static struct
{
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
   void   (*reset)(void);
   size_t (*serialize_size)(void);
   bool   (*serialize)(void *, size_t);
   bool   (*unserialize)(const void *, size_t);
   void  *(*memory_data)(unsigned);
} c;
static uint8_t *ram;

static int bind_core(core_handle core)
{
   return core_sym(core, "retro_set_environment",        &c.set_env,        sizeof(c.set_env))
       && core_sym(core, "retro_set_video_refresh",      &c.set_video,      sizeof(c.set_video))
       && core_sym(core, "retro_set_input_poll",         &c.set_poll,       sizeof(c.set_poll))
       && core_sym(core, "retro_set_input_state",        &c.set_state,      sizeof(c.set_state))
       && core_sym(core, "retro_set_audio_sample",       &c.set_audio,      sizeof(c.set_audio))
       && core_sym(core, "retro_set_audio_sample_batch", &c.set_batch,      sizeof(c.set_batch))
       && core_sym(core, "retro_init",                   &c.init,           sizeof(c.init))
       && core_sym(core, "retro_deinit",                 &c.deinit,         sizeof(c.deinit))
       && core_sym(core, "retro_load_game",              &c.load_game,      sizeof(c.load_game))
       && core_sym(core, "retro_unload_game",            &c.unload_game,    sizeof(c.unload_game))
       && core_sym(core, "retro_run",                    &c.run,            sizeof(c.run))
       && core_sym(core, "retro_reset",                  &c.reset,          sizeof(c.reset))
       && core_sym(core, "retro_serialize_size",         &c.serialize_size, sizeof(c.serialize_size))
       && core_sym(core, "retro_serialize",              &c.serialize,      sizeof(c.serialize))
       && core_sym(core, "retro_unserialize",            &c.unserialize,    sizeof(c.unserialize))
       && core_sym(core, "retro_get_memory_data",        &c.memory_data,    sizeof(c.memory_data));
}

static int start(void)
{
   struct retro_game_info info;
   c.set_env(env_cb);
   c.init();
   c.set_video(video_cb);
   c.set_poll(input_poll_cb);
   c.set_state(input_state_cb);
   c.set_audio(audio_cb);
   c.set_batch(audio_batch_cb);
   memset(&info, 0, sizeof(info));
   info.path = PROBE_PATH;
   if (!c.load_game(&info))
   {
      fprintf(stderr, "[gte_state] retro_load_game failed\n");
      return 0;
   }
   if (!(ram = (uint8_t *)c.memory_data(RETRO_MEMORY_SYSTEM_RAM)))
   {
      fprintf(stderr, "[gte_state] no system RAM\n");
      return 0;
   }
   return 1;
}

static void stop(void)
{
   c.unload_game();
   c.deinit();
}

static void run_frames(unsigned n)
{
   while (n--)
      c.run();
}

/* Returns 0 pass, 1 fail, 2 harness error */
static int case_load(void)
{
   unsigned char *first, *second;
   size_t size, i, diff = 0, first_diff = 0;
   int fail = 0;

   run_frames(SAVE_FRAME);
   if (check_regs(ram, READBACK, 0, "at save"))
      return 1;
   size   = c.serialize_size();
   first  = (unsigned char *)malloc(size);
   second = (unsigned char *)calloc(1, size);
   if (!first || !second || !c.serialize(first, size))
   {
      fprintf(stderr, "[gte_state] retro_serialize failed\n");
      return 2;
   }
   run_frames(END_FRAME - SAVE_FRAME);
   if (check_regs(ram, READBACK, 1, "before load"))
      return 1;
   /* Only the state can bring the first set back */
   if (!c.unserialize(first, size) || !c.serialize(second, size))
   {
      fprintf(stderr, "[gte_state] state round trip failed\n");
      return 2;
   }
   for (i = 0; i < size; i++)
      if (first[i] != second[i] && !diff++)
         first_diff = i;
   if (diff)
   {
      fprintf(stderr, "[gte_state] save after load differs in %lu bytes (first at %lu)\n",
            (unsigned long)diff, (unsigned long)first_diff);
      fail = 1;
   }
   run_frames(1);
   if (check_regs(ram, READBACK, 0, "after load"))
      fail = 1;
   free(first);
   free(second);
   return fail;
}

static int case_reset(void)
{
   run_frames(SAVE_FRAME);
   if (check_regs(ram, READBACK, 0, "before reset"))
      return 1;
   memset(ram + POWERON, 0xA5, (N_REGS + 1) * 4);
   c.reset();
   run_frames(SAVE_FRAME);
   if (ram32(ram, POWERON + N_REGS * 4) != MARKER)
   {
      fprintf(stderr, "[gte_state] the probe did not restart\n");
      return 2;
   }
   return check_regs(ram, POWERON, 2, "after reset");
}

static int case_toggle(void)
{
   run_frames(SAVE_FRAME);
   if (check_regs(ram, READBACK, 0, "on the dynarec"))
      return 1;
   cpu_mode   = "disabled";
   vars_dirty = true;
   run_frames(1);
   if (check_regs(ram, READBACK, 0, "switched to the interpreter"))
      return 1;
   run_frames(END_FRAME - SAVE_FRAME - 1);
   if (check_regs(ram, READBACK, 1, "on the interpreter"))
      return 1;
   cpu_mode   = "execute";
   vars_dirty = true;
   run_frames(2);
   return check_regs(ram, READBACK, 1, "back on the dynarec");
}

static int case_reload(void)
{
   run_frames(SAVE_FRAME);
   if (check_regs(ram, READBACK, 0, "first run"))
      return 1;
   stop();
   if (!start())
      return 2;
   run_frames(SAVE_FRAME);
   if (check_regs(ram, POWERON, 2, "second run, power-on"))
      return 1;
   return check_regs(ram, READBACK, 0, "second run");
}

int main(int argc, char **argv)
{
   core_handle core;
   const char *name;
   int ret;

   if (argc < 3)
   {
      fprintf(stderr, "usage: %s <core> <load|reset|toggle|reload> [cpu_dynarec mode]\n", argv[0]);
      return 2;
   }
   name = argv[2];
   if (argc > 3)
      cpu_mode = argv[3];
   if (!strcmp(name, "toggle"))
      cpu_mode = "execute";
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
   if (!bind_core(core) || !start())
      return 2;

   if (!strcmp(name, "load"))
      ret = case_load();
   else if (!strcmp(name, "reset"))
      ret = case_reset();
   else if (!strcmp(name, "toggle"))
      ret = case_toggle();
   else if (!strcmp(name, "reload"))
      ret = case_reload();
   else
   {
      fprintf(stderr, "unknown case %s\n", name);
      ret = 2;
   }

   stop();
   CORE_CLOSE(core);
   remove(PROBE_PATH);
   remove(BIOS_PATH);
   fprintf(stderr, "[gte_state] %s cpu_dynarec=%s %s\n", name,
         argc > 3 ? argv[3] : "disabled", ret ? "FAIL" : "OK");
   return ret;
}
