/* Hidden frame lane: a change to the display made in a frame the frontend
 * does not show (video off in RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE,
 * as RetroArch runs Run-Ahead's kept frames and Preemptive Frames'
 * replays) still reaches the screen, with frame duping on.
 *
 * The probe EXE fills the top of VRAM red and the bottom blue, shows the
 * top, then waits for the host to set a word in RAM (the input the frame
 * sees) and moves the display to the bottom once. Nothing on the display
 * changes after that, so the core sends every later frame as a dupe and
 * the frontend keeps showing the last picture it was given. Modes:
 *
 *   plain     every frame run once and shown.
 *   runahead  Run-Ahead on the same instance: the kept frame run hidden,
 *             a save, SPEED frames run speculatively (only the last
 *             shown), then the save loaded back. The input arrives on a
 *             kept frame, as new input does: the frames run ahead before
 *             it did not see it.
 *   preempt   Preemptive Frames: the frame shown runs with the input
 *             predicted; on the next frame the input turns out to have
 *             changed, so that frame is replayed hidden from the state
 *             before it, with the input it really had.
 *
 * In every mode the frontend's picture must turn blue within a few frames
 * of the input and stay blue.
 *
 * usage: hidden_frames_test <core> <mode> [speculative frames]
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

#define PROBE_PATH  "hidden_probe.exe"
#define BIOS_PATH   "scph5501.bin"
#define FLAG        0xF0000  /* the input: nonzero moves the display */
#define INPUT_AT    60
#define SETTLE      4        /* frames the move may take to show */
#define FRAMES      120

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

/* --- probe EXE ------------------------------------------------------ */

static uint32_t code[256];
static size_t   ncode;

static void emit(uint32_t w)
{
   if (ncode < sizeof(code) / sizeof(code[0]))
      code[ncode] = w;
   ncode++;
}

static void li_t0(uint32_t v)
{
   emit(0x3C080000u | (v >> 16));     /* lui  t0,hi */
   emit(0x35080000u | (v & 0xFFFFu)); /* ori  t0,t0,lo */
}

static void gp0(uint32_t v) { li_t0(v); emit(0xAE281810u); } /* sw t0,0x1810(s1) */
static void gp1(uint32_t v) { li_t0(v); emit(0xAE281814u); } /* sw t0,0x1814(s1) */

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
   static unsigned char image[0x800 + 0x800];
   /* (the code is well under 0x800 bytes) */
   static unsigned char bios[512 * 1024];
   size_t i, wait;

   ncode = 0;
   emit(0x3C111F80u);                 /* lui  s1,0x1F80 */
   emit(0x3C12800Fu);                 /* lui  s2,0x800F */
   gp1(0x00000000u);                  /* reset */
   /* 320x240 NTSC, 15 bit; the core takes the first display modes a
    * game sets for start-up noise, so set it past them */
   for (i = 0; i < 24; i++)
      gp1(0x08000001u);
   gp1(0x06C60260u);                  /* horizontal range */
   gp1(0x07040010u);                  /* vertical range, 240 lines */
   gp1(0x05000000u);                  /* display from (0,0) */
   gp0(0x020000FFu);                  /* fill red */
   gp0(0x00000000u);                  /*   at (0,0) */
   gp0(0x01000140u);                  /*   320x256 */
   gp0(0x02FF0000u);                  /* fill blue */
   gp0(0x01000000u);                  /*   at (0,256) */
   gp0(0x01000140u);                  /*   320x256 */
   gp1(0x03000000u);                  /* display on */
   wait = ncode;
   emit(0x8E490000u);                 /* lw   t1,0(s2) */
   emit(0x00000000u);
   emit(0x1120FFFDu);                 /* beqz t1,back to the lw */
   emit(0x00000000u);
   gp1(0x05040000u);                  /* display from (0,256) */
   emit(0x1000FFFFu);                 /* b    . */
   emit(0x00000000u);
   (void)wait;
   if (ncode > sizeof(code) / sizeof(code[0]))
      return 0;

   memset(image, 0, sizeof(image));
   memcpy(image, "PS-X EXE", 8);
#define PUT32(o, v) do { image[(o)] = (unsigned char)(v); image[(o) + 1] = (unsigned char)((v) >> 8); \
      image[(o) + 2] = (unsigned char)((v) >> 16); image[(o) + 3] = (unsigned char)((v) >> 24); } while (0)
   PUT32(0x10, 0x80010000u);
   PUT32(0x18, 0x80010000u);
   PUT32(0x1C, 0x800u);
   PUT32(0x30, 0x801FFFF0u);
   for (i = 0; i < ncode; i++)
      PUT32(0x800 + i * 4, code[i]);
#undef PUT32
   memset(bios, 0, sizeof(bios));
   return write_file(PROBE_PATH, image, sizeof(image))
       && write_file(BIOS_PATH, bios, sizeof(bios));
}

/* --- frontend ------------------------------------------------------- */

enum mode { MODE_PLAIN, MODE_RUNAHEAD, MODE_PREEMPT };

static enum mode mode;
static int       shown = 1;          /* video on for the run in progress */
static enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
/* The frontend's picture: 'R', 'B', or '?' before the first */
static char      picture = '?';
static long      dupes;

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_ERROR && !getenv("HIDDEN_FRAMES_DEBUG"))
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
   if (!strcmp(key, "beetle_psx_frame_duping"))
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
         pixel_format = *(const enum retro_pixel_format *)data;
         return pixel_format == RETRO_PIXEL_FORMAT_XRGB8888
             || pixel_format == RETRO_PIXEL_FORMAT_0RGB1555
             || pixel_format == RETRO_PIXEL_FORMAT_RGB565;
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
      case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
         *(int *)data = RETRO_AV_ENABLE_AUDIO | (shown ? RETRO_AV_ENABLE_VIDEO : 0);
         return true;
      default:
         return false;
   }
}

/* As a frontend: a hidden frame's picture is thrown away, a dupe keeps
 * the picture there is */
static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
   const unsigned char *row;
   unsigned r, b;
   if (!shown)
      return;
   if (!data)
   {
      dupes++;
      return;
   }
   if (!w || !h)
      return;
   row = (const unsigned char *)data + (size_t)(h / 2) * pitch;
   if (pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
   {
      const unsigned char *px = row + (size_t)(w / 2) * 4;
      b = px[0];
      r = px[2];
   }
   else
   {
      const unsigned char *px = row + (size_t)(w / 2) * 2;
      unsigned v = (unsigned)px[0] | ((unsigned)px[1] << 8);
      b = (v & 0x1F) << 3;
      r = pixel_format == RETRO_PIXEL_FORMAT_RGB565
         ? ((v >> 11) & 0x1F) << 3 : ((v >> 10) & 0x1F) << 3;
   }
   picture = r > 0x80 && b < 0x40 ? 'R' : b > 0x80 && r < 0x40 ? 'B' : '?';
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

static unsigned char *state, *state_next;
static size_t         state_size;
static int            have_state;
static unsigned       speed = 1;
static uint8_t       *ram;
static int            failed_io;

static void run(int show)
{
   shown = show;
   c.run();
   shown = 1;
}

static void input(int on)
{
   ram[FLAG] = (uint8_t)on;
}

/* One frame the user sees */
static void frame(int n)
{
   unsigned i;
   switch (mode)
   {
      case MODE_PLAIN:
         if (n == INPUT_AT)
            input(1);
         run(1);
         break;
      case MODE_RUNAHEAD:
         if (have_state && !c.unserialize(state, state_size))
            failed_io = 1;
         if (n == INPUT_AT)
            input(1);
         run(0);
         if (!c.serialize(state, state_size))
            failed_io = 1;
         have_state = 1;
         for (i = 1; i <= speed; i++)
            run(i == speed);
         break;
      case MODE_PREEMPT:
         if (have_state)
         {
            if (!c.unserialize(state, state_size))
               failed_io = 1;
            /* The frame before turns out to have had the input */
            if (n == INPUT_AT + 1)
               input(1);
            run(0);
         }
         if (!c.serialize(state_next, state_size))
            failed_io = 1;
         {
            unsigned char *t = state;
            state = state_next;
            state_next = t;
         }
         have_state = 1;
         run(1);
         break;
   }
}

int main(int argc, char **argv)
{
   struct retro_game_info info;
   core_handle core;
   const char *name;
   int  ret = 0, n, blue_at = -1;

   if (argc < 3)
   {
      fprintf(stderr, "usage: %s <core> <plain|runahead|preempt> [speculative frames]\n", argv[0]);
      return 2;
   }
   name = argv[2];
   if      (!strcmp(name, "plain"))    mode = MODE_PLAIN;
   else if (!strcmp(name, "runahead")) mode = MODE_RUNAHEAD;
   else if (!strcmp(name, "preempt"))  mode = MODE_PREEMPT;
   else
   {
      fprintf(stderr, "unknown mode %s\n", name);
      return 2;
   }
   if (argc > 3)
      speed = (unsigned)atoi(argv[3]);
   if (!speed)
      speed = 1;
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
      fprintf(stderr, "[hidden_frames] the probe did not load\n");
      return 2;
   }
   state_size = c.serialize_size();
   state      = (unsigned char *)malloc(state_size);
   state_next = (unsigned char *)malloc(state_size);
   if (!state || !state_next)
      return 2;
   input(0);

   for (n = 0; n < FRAMES; n++)
   {
      frame(n);
      if (n == INPUT_AT - 1 && picture != 'R')
      {
         fprintf(stderr, "[hidden_frames] %s: the picture before the input is '%c', not red\n",
               name, picture);
         ret = 1;
         break;
      }
      if (n >= INPUT_AT)
      {
         if (picture == 'B' && blue_at < 0)
            blue_at = n;
         else if (picture != 'B' && blue_at >= 0)
         {
            fprintf(stderr, "[hidden_frames] %s: the picture went back to '%c' on frame %d\n",
                  name, picture, n);
            ret = 1;
            break;
         }
      }
   }
   if (!ret && (blue_at < 0 || blue_at > INPUT_AT + SETTLE))
   {
      fprintf(stderr, "[hidden_frames] %s: the moved display %s\n", name,
            blue_at < 0 ? "never reached the screen" : "reached the screen late");
      ret = 1;
   }
   if (failed_io)
   {
      fprintf(stderr, "[hidden_frames] %s: a save or load failed\n", name);
      ret = 2;
   }
   if (!dupes)
   {
      fprintf(stderr, "[hidden_frames] %s: no frame was sent as a dupe\n", name);
      ret = 2;
   }

   c.unload_game();
   c.deinit();
   CORE_CLOSE(core);
   free(state);
   free(state_next);
   remove(PROBE_PATH);
   remove(BIOS_PATH);
   fprintf(stderr, "[hidden_frames] %s x%u: blue on frame %d (input on %d), %ld dupes, %s\n",
         name, speed, blue_at, INPUT_AT, dupes, ret ? "FAIL" : "OK");
   return ret;
}
