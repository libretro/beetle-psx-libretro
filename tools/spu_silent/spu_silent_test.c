/* spu_silent_test: the SPU's silent-voice skip against every voice mixed.
 *
 * Writes a PS-X EXE that plays the SPU for 64 frames over and over: each
 * frame it keys voices on with a looping sample or with a sample that
 * plays once and ends, so its voice falls silent, keys others off, moves
 * pitches and envelopes, switches the SPU IRQ on and off and noise mode
 * on and off for two voices, and keeps reverb running.  The host runs it
 * with the "SPU Silent Voice Optimization" option on or off, saving the
 * state partway and loading it back later, and with it on, switches it
 * off for a few frames, which mixes the silent voices again, and back on; and hashes every audio sample
 * and SPU RAM after every frame, the capture buffers among it.  It
 * writes the registers only every fourth frame, and halfway keys every
 * voice off, so the voices also fall silent and the state saved while
 * they play is loaded while they are silent, with nothing written to the
 * SPU for a while.  The skip promises the same
 * sound, so run.sh requires the same hashes either way.
 *
 * Usage: spu_silent_test <core> <on|off>
 *        spu_silent_test probe    (just writes the EXE)
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

#define PROBE_PATH  "spu_silent_probe.exe"
/* An all-zero kernel: the CPU runs straight to the EXE loader's patch */
#define BIOS_PATH   "scph5501.bin"
#define FRAMES      160
#define SAVE_AT     60
#define LOAD_AT     85
/* With the skip on, it is switched off for these frames and back on */
#define OFF_FROM    120
#define OFF_TO      130

#ifdef _WIN32
typedef HMODULE core_handle;
typedef FARPROC core_proc;
#define CORE_OPEN(p)    LoadLibraryA(p)
#define CORE_SYM(h, n)  GetProcAddress((h), (n))
#else
typedef void *core_handle;
typedef void *core_proc;
#define CORE_OPEN(p)    dlopen((p), RTLD_NOW | RTLD_LOCAL)
#define CORE_SYM(h, n)  dlsym((h), (n))
#endif

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

/* --- the probe ------------------------------------------------------ */

static uint32_t code[16384];
static size_t   ncode;

#define T0 8
#define T1 9
#define T2 10
#define LUI(rt, imm)      ((0x0Fu << 26) | ((rt) << 16) | ((imm) & 0xFFFFu))
#define ORI(rt, rs, imm)  ((0x0Du << 26) | ((rs) << 21) | ((rt) << 16) | ((imm) & 0xFFFFu))
#define ANDI(rt, rs, imm) ((0x0Cu << 26) | ((rs) << 21) | ((rt) << 16) | ((imm) & 0xFFFFu))
#define SH(rt, off, b)    ((0x29u << 26) | ((b) << 21) | ((rt) << 16) | ((off) & 0xFFFFu))
#define SW(rt, off, b)    ((0x2Bu << 26) | ((b) << 21) | ((rt) << 16) | ((off) & 0xFFFFu))
#define LW(rt, off, b)    ((0x23u << 26) | ((b) << 21) | ((rt) << 16) | ((off) & 0xFFFFu))
#define NOP               0u
#define BEQ               0x04u

static void emit(uint32_t w) { code[ncode++] = w; }
static void emit_li(unsigned rt, uint32_t v)
{
   emit(LUI(rt, v >> 16));
   emit(ORI(rt, rt, v & 0xFFFFu));
}
static void emit_branch(uint32_t op, unsigned rs, unsigned rt, size_t to)
{
   emit((op << 26) | (rs << 21) | (rt << 16)
         | ((uint32_t)(to - (ncode + 1)) & 0xFFFFu));
   emit(NOP);
}
/* A 16-bit SPU register (T0 holds 0x1F800000) */
static void spu(uint32_t addr, uint32_t v)
{
   emit_li(T1, v & 0xFFFFu);
   emit(SH(T1, addr - 0x1F800000u, T0));
}

/* 16 bytes of ADPCM: filter 1, shift 4, then the given flags and noise */
static void upload_block(uint8_t flags, unsigned k)
{
   uint8_t  b[16];
   unsigned i;
   b[0] = 0x14;
   b[1] = flags;
   for (i = 2; i < 16; i++)
      b[i] = (uint8_t)(i * 37 + k * 11);
   for (i = 0; i < 16; i += 2)
      spu(0x1F801DA8u, b[i] | ((uint32_t)b[i + 1] << 8));
}

static int write_probe(void)
{
   unsigned char hdr[0x800];
   size_t   top, vb, i, image;
   unsigned f, v;
   FILE    *fp;

   ncode = 0;
   emit_li(T0, 0x1F800000u);
   spu(0x1F801DAAu, 0xC000u);
   /* A looping sample at 0x1000; one that plays once, at 0x2000 */
   spu(0x1F801DA6u, 0x1000u / 8);
   spu(0x1F801DAAu, 0xC010u);
   upload_block(0x07, 1);
   spu(0x1F801DA6u, 0x2000u / 8);
   upload_block(0x04, 2);
   upload_block(0x00, 3);
   upload_block(0x00, 4);
   upload_block(0x01, 5);
   spu(0x1F801DAAu, 0xC000u);
   spu(0x1F801D80u, 0x3FFFu);
   spu(0x1F801D82u, 0x3FFFu);
   spu(0x1F801D84u, 0x2000u);
   spu(0x1F801D86u, 0x2000u);
   spu(0x1F801DA2u, 0x4000u);         /* reverb work area */
   spu(0x1F801DA4u, 0x7FFFu);         /* an IRQ address nothing reaches */

   top = ncode;
   for (f = 0; f < 64; f++)
   {
      uint32_t on = 0, off;
      /* registers written every fourth frame only: between, the voices
       * play on and fall silent with nothing written to the SPU */
      if (f % 4)
         goto vblank;
      /* halfway, every voice keyed off: the ones that loop fall silent */
      if (f == 32)
      {
         spu(0x1F801D8Cu, 0xFFFFu);
         spu(0x1F801D8Eu, 0x00FFu);
         goto vblank;
      }
      spu(0x1F801DAAu, 0xC080u | (f % 7 == 3 ? 0x40u : 0u));
      spu(0x1F801D94u, f % 11 == 5 ? 0x0300u : 0u);
      for (v = 0; v < 24; v++)
         if ((v * 5 + f) % 9 == 0)
         {
            const uint32_t b = 0x1F801C00u + v * 16;
            spu(b + 0x0, 0x2000u);
            spu(b + 0x2, 0x2000u);
            spu(b + 0x4, 0x0800u + ((v * 97 + f * 31) & 0x0FFFu));
            spu(b + 0x6, ((v + f / 4) % 4 ? 0x2000u : 0x1000u) / 8);
            spu(b + 0x8, 0x000Fu | ((f << 4) & 0xF0u));
            spu(b + 0xA, 0x8000u | ((v * 3 + f) & 0x07u));   /* quick release */
            on |= 1u << v;
         }
      spu(0x1F801D98u, on & 0xFFFFu);
      spu(0x1F801D9Au, on >> 16);
      spu(0x1F801D88u, on & 0xFFFFu);
      spu(0x1F801D8Au, on >> 16);
      off = ((f * 2654435761u) >> 7) & 0xFFFFFFu & ~on;
      if (f % 3 == 1)
      {
         spu(0x1F801D8Cu, off & 0xFFFFu);
         spu(0x1F801D8Eu, off >> 16);
      }
vblank:
      /* the next vertical blank */
      emit_li(T1, 0xFFFFFFFEu);
      emit(SW(T1, 0x1070, T0));
      vb = ncode;
      emit(LW(T1, 0x1070, T0));
      emit(NOP);
      emit(ANDI(T1, T1, 1));
      emit_branch(BEQ, T1, 0, vb);
   }
   emit_branch(BEQ, 0, 0, top);

   image = (ncode * 4 + 0x7FF) & ~(size_t)0x7FF;
   memset(hdr, 0, sizeof(hdr));
   memcpy(hdr, "PS-X EXE", 8);
#define PUT32(o, v) do { hdr[(o)] = (unsigned char)(v); hdr[(o) + 1] = (unsigned char)((v) >> 8); \
      hdr[(o) + 2] = (unsigned char)((v) >> 16); hdr[(o) + 3] = (unsigned char)((v) >> 24); } while (0)
   PUT32(0x10, 0x80010000u);
   PUT32(0x18, 0x80010000u);
   PUT32(0x1C, (uint32_t)image);
   PUT32(0x30, 0x801FFFF0u);
#undef PUT32
   if (!(fp = fopen(PROBE_PATH, "wb")))
      return 0;
   fwrite(hdr, 1, sizeof(hdr), fp);
   for (i = 0; i < image / 4; i++)
   {
      const uint32_t w = i < ncode ? code[i] : 0;
      unsigned char  b[4];
      b[0] = (unsigned char)w;
      b[1] = (unsigned char)(w >> 8);
      b[2] = (unsigned char)(w >> 16);
      b[3] = (unsigned char)(w >> 24);
      fwrite(b, 1, 4, fp);
   }
   fclose(fp);

   if (!(fp = fopen(BIOS_PATH, "wb")))
      return 0;
   memset(hdr, 0, sizeof(hdr));
   for (i = 0; i < 512 * 1024 / sizeof(hdr); i++)
      fwrite(hdr, 1, sizeof(hdr), fp);
   fclose(fp);
   return 1;
}

/* --- frontend ------------------------------------------------------- */

/* SPU RAM in a state: the data of its "SPURAM" entry */
static const unsigned char *find_spuram(const unsigned char *st, size_t size)
{
   size_t i;
   for (i = 0; i + 11 + 512 * 1024 <= size; i++)
      if (st[i] == 6 && !memcmp(st + i + 1, "SPURAM", 6))
         return st + i + 7 + 4;
   return NULL;
}

static const char *silent_opt = "enabled";
static unsigned    frame_no;
static uint32_t    audio_hash = 2166136261u;
static unsigned long samples;

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_ERROR)
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
   if (!strcmp(key, "beetle_psx_gpu_thread"))
      return "disabled";
   if (!strcmp(key, "beetle_psx_spu_silent_voice"))
      return frame_no >= OFF_FROM && frame_no < OFF_TO ? "disabled" : silent_opt;
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
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         var->value = get_var(var->key);
         return var->value != NULL;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = frame_no == OFF_FROM || frame_no == OFF_TO;
         return true;
      default:
         return false;
   }
}
static void video_cb(const void *d, unsigned w, unsigned h, size_t p)
{
   (void)d; (void)w; (void)h; (void)p;
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
   size_t i;
   for (i = 0; i < frames * 2; i++)
      audio_hash = (audio_hash ^ (uint16_t)data[i]) * 16777619u;
   samples += frames;
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
   size_t (*memory_size)(unsigned);
   struct retro_game_info info;
   unsigned char *saved = NULL, *state;
   const unsigned char *spuram = NULL;
   size_t   size = 0, state_size, n;
   uint32_t ram_hash = 2166136261u;
   unsigned f;

   if (argc == 2 && !strcmp(argv[1], "probe"))
      return write_probe() ? 0 : 2;
   if (argc < 3)
   {
      fprintf(stderr, "usage: %s <core> <on|off>\n", argv[0]);
      return 2;
   }
   silent_opt = strcmp(argv[2], "off") ? "enabled" : "disabled";
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
      || !core_sym(core, "retro_get_memory_size",        &memory_size,    sizeof(memory_size)))
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
      fprintf(stderr, "[spu_silent] retro_load_game failed\n");
      return 2;
   }

   state_size = serialize_size();
   if (!(state = (unsigned char *)malloc(state_size)))
      return 2;

   /* Saved partway and loaded back: the frames between run twice */
   for (f = 0; f < FRAMES + (LOAD_AT - SAVE_AT); f++)
   {
      frame_no = f;
      if (f == SAVE_AT)
      {
         size = serialize_size();
         if (!(saved = (unsigned char *)malloc(size)) || !serialize(saved, size))
            return 2;
      }
      if (f == LOAD_AT && !unserialize(saved, size))
         return 2;
      run();
      /* SPU RAM after every frame: the voices' capture buffers are rings
       * a few hundred samples long */
      if (!serialize(state, state_size))
         return 2;
      if (!spuram)
         spuram = find_spuram(state, state_size);
      if (!spuram)
      {
         fprintf(stderr, "[spu_silent] no SPURAM in the state\n");
         return 2;
      }
      for (n = 0; n < 512 * 1024; n++)
         ram_hash = (ram_hash ^ spuram[n]) * 16777619u;
   }

   printf("%08lx %08lx %lu\n", (unsigned long)audio_hash,
         (unsigned long)ram_hash, samples);
   free(saved);
   free(state);
   unload_game();
   deinit();
   return 0;
}
