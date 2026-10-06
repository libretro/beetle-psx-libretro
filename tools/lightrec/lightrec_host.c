/* lightrec_host: headless libretro frontend for the lightrec regression
 * lane.
 *
 * Loads the software core, runs the EXE built by gen_exe.py for a number
 * of frames with the chosen CPU mode, then reads the program's running
 * sum and iteration count straight out of system RAM and checks the sum
 * against the same arithmetic done here. The program patches its own
 * code every iteration, so every path of the threaded compiler (pending
 * requests cancelled, in-flight compilations discarded, covered blocks
 * retired, deferred frees) is exercised on real content, and a wrong
 * sum is a wrong instruction executed.
 *
 * Usage: lightrec_host <core.so> <content.exe> <frames> [cpu_mode]
 *   cpu_mode: execute (default), run_interpreter, disabled
 *   LRHOST_VARS: semicolon list of key=value core option overrides.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <dlfcn.h>
#include "libretro.h"

#define N_FUNCS 256
#define RESULT_OFFSET 0xF0000

static char *var_keys[512]; static char *var_vals[512]; static int n_vars;
static void add_var(const char *k, const char *v)
{ if (n_vars < 512) { var_keys[n_vars] = strdup(k); var_vals[n_vars] = strdup(v); n_vars++; } }
static const char *find_var(const char *k)
{ int i; for (i = 0; i < n_vars; i++) if (!strcmp(var_keys[i], k)) return var_vals[i]; return NULL; }

static char sysdir[512] = "/tmp/lrhost_sys";
static char savedir[512] = "/tmp/lrhost_save";

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_INFO) return;
   va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = log_cb; return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = sysdir; return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = savedir; return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         return true;
      case RETRO_ENVIRONMENT_GET_CAN_DUPE:
         *(bool *)data = true; return true;
      case RETRO_ENVIRONMENT_SET_VARIABLES:
      {
         const struct retro_variable *v = (const struct retro_variable *)data;
         for (; v && v->key; v++)
         {
            const char *semi = strchr(v->value, ';');
            if (semi && !find_var(v->key))
            {
               char buf[256]; const char *p = semi + 1; size_t n = 0;
               while (*p == ' ') p++;
               while (p[n] && p[n] != '|' && n < sizeof(buf) - 1) n++;
               memcpy(buf, p, n); buf[n] = 0;
               add_var(v->key, buf);
            }
         }
         return true;
      }
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
      {
         const struct retro_core_options_v2 *o2 =
            (cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL)
            ? ((const struct retro_core_options_v2_intl *)data)->us
            : (const struct retro_core_options_v2 *)data;
         const struct retro_core_option_v2_definition *d;
         if (!o2) return true;
         for (d = o2->definitions; d && d->key; d++)
            if (d->default_value && !find_var(d->key))
               add_var(d->key, d->default_value);
         return true;
      }
      case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         const char *v = find_var(var->key);
         var->value = v;
         return v != NULL;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = false; return true;
      default:
         return false;
   }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{ (void)data; (void)w; (void)h; (void)pitch; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }
static size_t audio_batch_cb(const int16_t *data, size_t frames) { (void)data; return frames; }
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }

/* Mirror of gen_exe.py: after iteration i the function i % N is set to
 * add 2 on even rounds and 1 on odd ones; each iteration also adds
 * 15+14+...+0 from the inner loop. */
static uint32_t expected_sum(uint32_t iterations)
{
   uint8_t imm[N_FUNCS];
   uint32_t sum = 0, i, f, idx = 0, parity = 0;

   for (f = 0; f < N_FUNCS; f++) imm[f] = 1;

   for (i = 0; i < iterations; i++)
   {
      for (f = 0; f < N_FUNCS; f++) sum += imm[f];
      imm[idx] = 2 - parity;
      if (++idx == N_FUNCS) { idx = 0; parity ^= 1; }
      sum += 120;
   }
   return sum;
}

int main(int argc, char **argv)
{
   const char *core_path, *content, *mode = "execute";
   void *core;
   unsigned frames, i;
   const uint8_t *ram;
   uint32_t sum, count, want;

   if (argc < 4)
   {
      fprintf(stderr, "usage: %s <core.so> <content.exe> <frames> [execute|run_interpreter|disabled]\n", argv[0]);
      return 2;
   }
   core_path = argv[1]; content = argv[2]; frames = (unsigned)atoi(argv[3]);
   if (argc > 4) mode = argv[4];

   add_var("beetle_psx_cpu_dynarec", mode);
   add_var("beetle_psx_skip_bios", "enabled");
   {
      const char *vars = getenv("LRHOST_VARS");
      if (vars)
      {
         char *dup = strdup(vars), *tok, *save = NULL;
         for (tok = strtok_r(dup, ";", &save); tok; tok = strtok_r(NULL, ";", &save))
         {
            char *eq = strchr(tok, '=');
            if (eq) { *eq = 0; add_var(tok, eq + 1); }
         }
         free(dup);
      }
   }

   core = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
   if (!core) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

#define SYM(name) name##_fn = dlsym(core, #name); if (!name##_fn) { fprintf(stderr, "missing %s\n", #name); return 2; }
   {
      void (*retro_set_environment_fn)(retro_environment_t); SYM(retro_set_environment);
      retro_set_environment_fn(env_cb);
   }
   { void (*retro_init_fn)(void); SYM(retro_init); retro_init_fn(); }
   { void (*f)(retro_video_refresh_t) = dlsym(core, "retro_set_video_refresh"); f(video_cb); }
   { void (*f)(retro_input_poll_t) = dlsym(core, "retro_set_input_poll"); f(input_poll_cb); }
   { void (*f)(retro_input_state_t) = dlsym(core, "retro_set_input_state"); f(input_state_cb); }
   { void (*f)(retro_audio_sample_t) = dlsym(core, "retro_set_audio_sample"); f(audio_cb); }
   { void (*f)(retro_audio_sample_batch_t) = dlsym(core, "retro_set_audio_sample_batch"); f(audio_batch_cb); }

   {
      struct retro_game_info info;
      bool (*retro_load_game_fn)(const struct retro_game_info *); SYM(retro_load_game);
      memset(&info, 0, sizeof(info));
      info.path = content;
      if (!retro_load_game_fn(&info))
      { fprintf(stderr, "[lrhost] retro_load_game failed\n"); return 3; }
   }
   {
      struct retro_system_av_info av;
      void (*f)(struct retro_system_av_info *) = dlsym(core, "retro_get_system_av_info");
      f(&av);
   }

   {
      void (*retro_run_fn)(void); SYM(retro_run);
      void *(*retro_get_memory_data_fn)(unsigned); SYM(retro_get_memory_data);

      for (i = 0; i < frames; i++)
         retro_run_fn();

      ram = retro_get_memory_data_fn(RETRO_MEMORY_SYSTEM_RAM);
      if (!ram) { fprintf(stderr, "[lrhost] no system RAM\n"); return 3; }

      memcpy(&sum, ram + RESULT_OFFSET, 4);
      memcpy(&count, ram + RESULT_OFFSET + 4, 4);
      want = expected_sum(count);

      fprintf(stderr, "[lrhost] mode=%s frames=%u iterations=%u sum=%u expected=%u\n",
              mode, frames, count, sum, want);
   }

   { void (*f)(void) = dlsym(core, "retro_unload_game"); if (f) f(); }
   { void (*f)(void) = dlsym(core, "retro_deinit"); if (f) f(); }
   dlclose(core);

   if (count < 10)
   { fprintf(stderr, "[lrhost] FAIL: program barely ran (%u iterations)\n", count); return 1; }
   if (sum != want)
   { fprintf(stderr, "[lrhost] FAIL: sum mismatch\n"); return 1; }

   fprintf(stderr, "[lrhost] OK\n");
   return 0;
}
