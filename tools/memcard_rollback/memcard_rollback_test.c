/* Memory card lane: a card the game writes reaches its file during play
 * under the frontend's rollbacks, as it does without them.
 *
 * The probe EXE writes one sector of the card in port 2 over SIO, leaves
 * the card's last reply ('G' for a good write) and a marker in RAM, then
 * idles. The core writes a card out once about two seconds of emulated
 * time pass with no new write. Modes:
 *
 *   plain     every frame run once.
 *   runahead  Run-Ahead on the same instance, as RetroArch runs it: the
 *             frame kept, a save, SPEED frames run speculatively (reported
 *             SPECULATIVE), then the save loaded back.
 *   preempt   Preemptive Frames: the state saved before the previous frame
 *             loaded, that frame replayed (REPLAY), a save, then the frame
 *             kept.
 *   managed   runahead with the lifetime callbacks registered: saves and
 *             loads in the managed context, each released before its
 *             buffer is saved into again and all before unload.
 *   legacy    runahead on a frontend that reports neither the savestate
 *             context nor the execution phase: the card reaches its file
 *             by unload at the latest.
 *
 * Every mode but legacy must see the sector in the card file before unload,
 * and no sooner than two seconds of kept frames after the write.
 * The file is then deleted and the run goes on: the core must not write
 * the unchanged card again.
 *
 * usage: memcard_rollback_test <core> <mode> [speculative frames]
 * Runs in the current directory: it writes the probe EXE, an all-zero BIOS
 * image and the card file there. */
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

#define PROBE_PATH  "memcard_probe.exe"
#define CARD_PATH   "memcard_probe.1.mcr"
#define BIOS_PATH   "scph5501.bin"
#define REPLY       0xF0000
#define MARKER_AT   0xF0004
#define PROGRESS    0xF0008  /* bytes sent so far */
#define MARKER      0x600DF00Du
#define SECTOR      5u
/* The core waits about two seconds (120 frames) after the last write;
 * written sooner, it counted replayed or speculative frames' time */
#define WRITE_AFTER 110
#define WRITE_BY    400
#define QUIET       300
#define MAX_CODE    4096

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

static unsigned char pattern(unsigned i)
{
   return (unsigned char)(0xA0u + i * 7u);
}

/* --- probe EXE ------------------------------------------------------ */

static uint32_t code[MAX_CODE];
static size_t   ncode;

static void emit(uint32_t w)
{
   if (ncode < MAX_CODE)
      code[ncode] = w;
   ncode++;
}

/* Waits for the transmitter, sends one byte, waits for the reply and
 * keeps it in t2; counts the byte in s3 and RAM */
static void send_byte(unsigned b)
{
   emit(0x8E291044u);                 /* lw   t1,0x1044(s1): JOY_STAT */
   emit(0x00000000u);
   emit(0x31290001u);                 /* andi t1,t1,1: TX ready */
   emit(0x1120FFFCu);                 /* beqz t1,back to the lw */
   emit(0x00000000u);
   emit(0x34080000u | (b & 0xFFu));   /* ori  t0,zero,b */
   emit(0xA2281040u);                 /* sb   t0,0x1040(s1): JOY_TX_DATA */
   emit(0x8E291044u);                 /* lw   t1,0x1044(s1) */
   emit(0x00000000u);
   emit(0x31290002u);                 /* andi t1,t1,2: RX not empty */
   emit(0x1120FFFCu);                 /* beqz t1,back to the lw */
   emit(0x00000000u);
   emit(0x922A1040u);                 /* lbu  t2,0x1040(s1): JOY_RX_DATA */
   emit(0x26730001u);                 /* addiu s3,s3,1 */
   emit(0xAE530008u);                 /* sw   s3,8(s2) */
}

static int write_file(const char *path, const unsigned char *p, size_t n)
{
   FILE *f = fopen(path, "wb");
   if (!f)
      return 0;
   if (n && fwrite(p, 1, n, f) != n)
   {
      fclose(f);
      return 0;
   }
   return fclose(f) == 0;
}

static int write_probe(void)
{
   static unsigned char image[0x800 + MAX_CODE * 4];
   static unsigned char bios[512 * 1024];
   unsigned char xor_ = 0;
   size_t   text, i;
   unsigned b;

   ncode = 0;
   emit(0x3C111F80u);                 /* lui  s1,0x1F80 */
   emit(0x3C12800Fu);                 /* lui  s2,0x800F */
   emit(0x00009821u);                 /* move s3,zero */
   emit(0x3408000Du);                 /* ori  t0,zero,0x0D: 8 bits, x1 */
   emit(0xA6281048u);                 /* sh   t0,0x1048(s1): JOY_MODE */
   emit(0x34080088u);                 /* ori  t0,zero,0x88 */
   emit(0xA628104Eu);                 /* sh   t0,0x104E(s1): JOY_BAUD */
   emit(0x34082003u);                 /* ori  t0,zero,0x2003: TX, DTR, port 2 */
   emit(0xA628104Au);                 /* sh   t0,0x104A(s1): JOY_CTRL */
   /* An empty read arms the receiver: each byte sent then comes with one
    * received, as the BIOS does it */
   emit(0x922A1040u);                 /* lbu  t2,0x1040(s1) */
   send_byte(0x81);                   /* the memory card */
   send_byte('W');
   send_byte(0x00);
   send_byte(0x00);
   send_byte(SECTOR >> 8);
   send_byte(SECTOR & 0xFF);
   xor_ = (unsigned char)((SECTOR >> 8) ^ (SECTOR & 0xFF));
   for (b = 0; b < 128; b++)
   {
      send_byte(pattern(b));
      xor_ ^= pattern(b);
   }
   send_byte(xor_);
   send_byte(0x00);                   /* acknowledges */
   send_byte(0x00);
   send_byte(0x00);                   /* the end flag comes back */
   emit(0xA620104Au);                 /* sh   zero,0x104A(s1): deselect */
   emit(0xAE4A0000u);                 /* sw   t2,0(s2) */
   emit(0x3C090000u | (MARKER >> 16));          /* lui t1 */
   emit(0x35290000u | (MARKER & 0xFFFFu));      /* ori t1 */
   emit(0xAE490004u);                 /* sw   t1,4(s2) */
   emit(0x1000FFFFu);                 /* b    . */
   emit(0x00000000u);
   if (ncode > MAX_CODE)
      return 0;

   text = (ncode * 4 + 0x7FF) & ~(size_t)0x7FF;
   memset(image, 0, sizeof(image));
   memcpy(image, "PS-X EXE", 8);
#define PUT32(o, v) do { image[(o)] = (unsigned char)(v); image[(o) + 1] = (unsigned char)((v) >> 8); \
      image[(o) + 2] = (unsigned char)((v) >> 16); image[(o) + 3] = (unsigned char)((v) >> 24); } while (0)
   PUT32(0x10, 0x80010000u);
   PUT32(0x18, 0x80010000u);
   PUT32(0x1C, (uint32_t)text);
   PUT32(0x30, 0x801FFFF0u);
   for (i = 0; i < ncode; i++)
      PUT32(0x800 + i * 4, code[i]);
#undef PUT32
   memset(bios, 0, sizeof(bios));
   return write_file(PROBE_PATH, image, 0x800 + text)
       && write_file(BIOS_PATH, bios, sizeof(bios));
}

/* --- frontend ------------------------------------------------------- */

enum mode { MODE_PLAIN, MODE_RUNAHEAD, MODE_PREEMPT, MODE_MANAGED, MODE_LEGACY };

static enum mode mode;
static int       context = RETRO_SAVESTATE_CONTEXT_NORMAL;
static enum retro_execution_phase phase = RETRO_EXECUTION_PHASE_RETAINED;
static struct retro_savestate_lifetime lifetime;

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_ERROR && !getenv("MEMCARD_ROLLBACK_DEBUG"))
      return;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

/* Answers the software core's keys and the hardware core's alike */
static const char *get_var(const char *key)
{
   char name[64];
   if (!strncmp(key, "beetle_psx_hw_", 14) && strlen(key) < sizeof(name))
   {
      strcpy(name, "beetle_psx_");
      strcat(name, key + 14);
      key = name;
   }
   if (!strcmp(key, "beetle_psx_skip_bios"))
      return "enabled";
   if (!strcmp(key, "beetle_psx_renderer"))
      return "software";
   if (!strcmp(key, "beetle_psx_enable_memcard1"))
      return "enabled";
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
      case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:
         if (mode == MODE_LEGACY)
            return false;
         *(int *)data = context;
         return true;
      case RETRO_ENVIRONMENT_GET_EXECUTION_PHASE:
         if (mode == MODE_LEGACY)
            return false;
         *(enum retro_execution_phase *)data = phase;
         return true;
      case RETRO_ENVIRONMENT_SET_SAVESTATE_LIFETIME:
      {
         const struct retro_savestate_lifetime *l =
            (const struct retro_savestate_lifetime *)data;
         if (mode != MODE_MANAGED || !l || l->version < 1 || !l->release)
            return false;
         lifetime = *l;
         return true;
      }
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
   size_t (*serialize_size)(void);
   bool   (*serialize)(void *, size_t);
   bool   (*unserialize)(const void *, size_t);
   void  *(*memory_data)(unsigned);
} c;

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
       && core_sym(core, "retro_serialize_size",         &c.serialize_size, sizeof(c.serialize_size))
       && core_sym(core, "retro_serialize",              &c.serialize,      sizeof(c.serialize))
       && core_sym(core, "retro_unserialize",            &c.unserialize,    sizeof(c.unserialize))
       && core_sym(core, "retro_get_memory_data",        &c.memory_data,    sizeof(c.memory_data));
}

/* Two state buffers: [0] the state to roll back to, [1] the next save */
static unsigned char *buf[2];
static int            owned[2];
static size_t         buf_size;
static int            have_prev;
static unsigned       speed = 1;
static int            refused;

static void release(int i)
{
   if (owned[i] && lifetime.release)
      lifetime.release(buf[i], buf_size);
   owned[i] = 0;
}

static int save(int i)
{
   int ok;
   release(i);
   ok = c.serialize(buf[i], buf_size);
   owned[i] = ok && mode == MODE_MANAGED;
   return ok;
}

static void load(int i)
{
   if (!c.unserialize(buf[i], buf_size))
      refused++;
}

static void run_as(enum retro_execution_phase p)
{
   phase = p;
   c.run();
   phase = RETRO_EXECUTION_PHASE_RETAINED;
}

static int same_instance(void)
{
   return mode == MODE_MANAGED
      ? RETRO_SAVESTATE_CONTEXT_SAME_INSTANCE_MANAGED
      : RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE;
}

/* One frame the user sees, run as the mode's frontend runs it */
static int frame(void)
{
   unsigned i;
   switch (mode)
   {
      case MODE_PLAIN:
         c.run();
         return 1;
      case MODE_RUNAHEAD:
      case MODE_MANAGED:
      case MODE_LEGACY:
         run_as(RETRO_EXECUTION_PHASE_RETAINED);
         context = same_instance();
         if (!save(0))
            return 0;
         for (i = 0; i < speed; i++)
            run_as(RETRO_EXECUTION_PHASE_SPECULATIVE);
         load(0);
         context = RETRO_SAVESTATE_CONTEXT_NORMAL;
         return 1;
      case MODE_PREEMPT:
         context = same_instance();
         if (have_prev)
         {
            load(0);
            run_as(RETRO_EXECUTION_PHASE_REPLAY);
         }
         if (!save(1))
            return 0;
         context = RETRO_SAVESTATE_CONTEXT_NORMAL;
         run_as(RETRO_EXECUTION_PHASE_RETAINED);
         {
            unsigned char *t = buf[0];
            int            o = owned[0];
            buf[0] = buf[1]; owned[0] = owned[1];
            buf[1] = t;      owned[1] = o;
         }
         have_prev = 1;
         return 1;
   }
   return 0;
}

static uint32_t ram32(const uint8_t *ram, size_t off)
{
   return (uint32_t)ram[off] | ((uint32_t)ram[off + 1] << 8)
        | ((uint32_t)ram[off + 2] << 16) | ((uint32_t)ram[off + 3] << 24);
}

/* 1 when the card file holds the probe's sector */
static int card_written(void)
{
   unsigned char sector[128];
   unsigned      i;
   FILE         *f = fopen(CARD_PATH, "rb");
   int           ok;
   if (!f)
      return 0;
   ok = fseek(f, (long)(SECTOR * 128u), SEEK_SET) == 0
     && fread(sector, 1, sizeof(sector), f) == sizeof(sector);
   fclose(f);
   if (!ok)
      return 0;
   for (i = 0; i < 128; i++)
      if (sector[i] != pattern(i))
         return 0;
   return 1;
}

static int card_exists(void)
{
   FILE *f = fopen(CARD_PATH, "rb");
   if (!f)
      return 0;
   fclose(f);
   return 1;
}

int main(int argc, char **argv)
{
   struct retro_game_info info;
   core_handle core;
   const char *name;
   uint8_t    *ram;
   int         ret = 0, n, written_at = -1;

   if (argc < 3)
   {
      fprintf(stderr, "usage: %s <core> <plain|runahead|preempt|managed|legacy> [speculative frames]\n", argv[0]);
      return 2;
   }
   name = argv[2];
   if      (!strcmp(name, "plain"))    mode = MODE_PLAIN;
   else if (!strcmp(name, "runahead")) mode = MODE_RUNAHEAD;
   else if (!strcmp(name, "preempt"))  mode = MODE_PREEMPT;
   else if (!strcmp(name, "managed"))  mode = MODE_MANAGED;
   else if (!strcmp(name, "legacy"))   mode = MODE_LEGACY;
   else
   {
      fprintf(stderr, "unknown mode %s\n", name);
      return 2;
   }
   if (argc > 3)
      speed = (unsigned)atoi(argv[3]);
   remove(CARD_PATH);
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
   if (!bind_core(core))
      return 2;

   c.set_env(env_cb);
   c.init();
   c.set_video(video_cb);
   c.set_poll(input_poll_cb);
   c.set_state(input_state_cb);
   c.set_audio(audio_cb);
   c.set_batch(audio_batch_cb);
   memset(&info, 0, sizeof(info));
   info.path = PROBE_PATH;
   if (!c.load_game(&info)
         || !(ram = (uint8_t *)c.memory_data(RETRO_MEMORY_SYSTEM_RAM)))
   {
      fprintf(stderr, "[memcard_rollback] the probe did not load\n");
      return 2;
   }
   if (mode != MODE_PLAIN)
   {
      context  = same_instance();
      buf_size = c.serialize_size();
      context  = RETRO_SAVESTATE_CONTEXT_NORMAL;
      buf[0]   = (unsigned char *)malloc(buf_size);
      buf[1]   = (unsigned char *)malloc(buf_size);
      if (!buf[0] || !buf[1])
         return 2;
   }

   for (n = 0; n < WRITE_BY && written_at < 0; n++)
   {
      if (!frame())
      {
         fprintf(stderr, "[memcard_rollback] a save failed on frame %d\n", n);
         return 2;
      }
      if (card_written())
         written_at = n;
   }
   if (ram32(ram, MARKER_AT) != MARKER || ram[REPLY] != 'G')
   {
      fprintf(stderr, "[memcard_rollback] the probe's write did not finish: %lu bytes sent, reply 0x%02X\n",
            (unsigned long)ram32(ram, PROGRESS), (unsigned)ram[REPLY]);
      ret = 2;
   }
   else if (mode == MODE_LEGACY)
   {
      /* Wherever the card got to during play, unload writes it out */
      remove(CARD_PATH);
      written_at = -1;
   }
   else if (written_at < 0)
   {
      fprintf(stderr, "[memcard_rollback] %s: the card was not written in %d frames\n",
            name, WRITE_BY);
      ret = 1;
   }
   else if (written_at < WRITE_AFTER)
   {
      fprintf(stderr, "[memcard_rollback] %s: the card was written on frame %d, before %d\n",
            name, written_at, WRITE_AFTER);
      ret = 1;
   }
   else
   {
      /* Nothing new on the card: no second write */
      remove(CARD_PATH);
      for (n = 0; n < QUIET; n++)
         frame();
      if (card_exists())
      {
         fprintf(stderr, "[memcard_rollback] %s: the unchanged card was written again\n", name);
         ret = 1;
      }
   }
   if (refused)
   {
      fprintf(stderr, "[memcard_rollback] %s: %d loads refused\n", name, refused);
      ret = 1;
   }

   release(0);
   release(1);
   c.unload_game();
   if (mode == MODE_LEGACY && !ret && !card_written())
   {
      fprintf(stderr, "[memcard_rollback] legacy: unload did not write the card\n");
      ret = 1;
   }
   c.deinit();
   CORE_CLOSE(core);
   free(buf[0]);
   free(buf[1]);
   remove(PROBE_PATH);
   remove(BIOS_PATH);
   remove(CARD_PATH);
   if (written_at >= 0)
      fprintf(stderr, "[memcard_rollback] %s x%u: card written on frame %d, %s\n",
            name, speed, written_at, ret ? "FAIL" : "OK");
   else
      fprintf(stderr, "[memcard_rollback] %s x%u: %s\n", name, speed, ret ? "FAIL" : "OK");
   return ret;
}
