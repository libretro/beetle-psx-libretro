/* GTE savestate lane.
 *
 * The probe EXE loads one set of distinct values into the GTE data
 * registers that the state code carries through local copies (OTZ, SZ0-3,
 * RES1, LZCS and the LZCR computed from it, with IR0 alongside), reads
 * every one of them back into RAM in a loop, and after a countdown
 * overwrites them with a second set.  The host saves a state while the
 * first set is live, runs on until the second set is in, loads the state
 * and runs one more frame:
 *
 *   - the read-back in RAM must hold the first set again, so a register
 *     the load path does not write back fails the lane by name;
 *   - saving straight after the load must give the same bytes as the
 *     first save.
 *
 * usage: gte_state_test <core> [cpu_dynarec mode]
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
      if (regs[i].index == 31)                        /* read-only */
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
         *(bool *)data = false;
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

/* 0 when every register reads back as the given set */
static int check_readback(const uint8_t *ram, unsigned set, const char *when)
{
   size_t i;
   int    bad = 0;
   for (i = 0; i < N_REGS; i++)
   {
      uint32_t got = ram32(ram, READBACK + i * 4);
      if (got != regs[i].read[set])
      {
         fprintf(stderr, "[gte_state] %s: %s (r%u) = 0x%08lX, want 0x%08lX\n",
               when, regs[i].name, regs[i].index,
               (unsigned long)got, (unsigned long)regs[i].read[set]);
         bad = 1;
      }
   }
   return bad;
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
   struct retro_game_info info;
   const uint8_t *ram;
   unsigned char *first = NULL, *second = NULL;
   size_t size, i, diff = 0, first_diff = 0;
   unsigned f;
   int fail = 0;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s <core> [cpu_dynarec mode]\n", argv[0]);
      return 2;
   }
   if (argc > 2)
      cpu_mode = argv[2];
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
      fprintf(stderr, "[gte_state] retro_load_game failed\n");
      return 2;
   }
   if (!(ram = (const uint8_t *)memory_data(RETRO_MEMORY_SYSTEM_RAM)))
   {
      fprintf(stderr, "[gte_state] no system RAM\n");
      return 2;
   }

   for (f = 0; f < SAVE_FRAME; f++)
      run();
   if (check_readback(ram, 0, "at save"))
   {
      fprintf(stderr, "[gte_state] FAIL: the probe is not in its first set\n");
      return 1;
   }

   size   = serialize_size();
   first  = (unsigned char *)malloc(size);
   second = (unsigned char *)calloc(1, size);
   if (!first || !second || !serialize(first, size))
   {
      fprintf(stderr, "[gte_state] retro_serialize failed\n");
      return 2;
   }

   for (; f < END_FRAME; f++)
      run();
   if (check_readback(ram, 1, "before load"))
   {
      fprintf(stderr, "[gte_state] FAIL: the probe did not reach its second set\n");
      return 1;
   }

   /* Only the state can bring the first set back */
   if (!unserialize(first, size) || !serialize(second, size))
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

   run();
   if (check_readback(ram, 0, "after load"))
      fail = 1;

   unload_game();
   deinit();
   CORE_CLOSE(core);
   free(first);
   free(second);
   remove(PROBE_PATH);
   remove(BIOS_PATH);

   fprintf(stderr, "[gte_state] cpu_dynarec=%s %s\n", cpu_mode, fail ? "FAIL" : "OK");
   return fail;
}
