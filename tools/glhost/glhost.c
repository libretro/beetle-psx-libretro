/* glhost: headless libretro OpenGL (core profile) frontend for renderer
 * validation, the GL sibling of vkhost. Creates a surfaceless EGL desktop-GL
 * core context (llvmpipe in CI), hands the core an FBO as the "default
 * framebuffer", runs content, and dumps glReadPixels of that FBO as PPM.
 *
 * This exists because the GL renderer shipped a fragment shader that used
 * its varyings before declaring them: every real core-profile driver
 * rejects it, gl_renderer_new fails, and the un-propagated failure walked
 * GPU_Update into a NULL framebuffer - a boot SEGV that only reproduced on
 * end-user machines. The rule vkhost enforces extends to GL: renderer
 * changes get executed, not just compiled.
 *
 * Usage: glhost <core.so> <content> [savestate|-] [frames] [outdir]
 *   GLHOST_VARS: semicolon list of key=value core option overrides.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include "libretro.h"

/* ---- core option overrides / harvested defaults ---- */
static char *var_keys[512]; static char *var_vals[512]; static int n_vars;
static void add_var(const char *k, const char *v)
{ if (n_vars < 512) { var_keys[n_vars] = strdup(k); var_vals[n_vars] = strdup(v); n_vars++; } }
static const char *find_var(const char *k)
{ int i; for (i = 0; i < n_vars; i++) if (!strcmp(var_keys[i], k)) return var_vals[i]; return NULL; }

/* ---- state ---- */
static void *core;
static struct retro_hw_render_callback hw_render;
static EGLDisplay egl_dpy;
static EGLContext egl_ctx;
static GLuint fbo, fbo_tex, fbo_depth;
static unsigned fbo_w = 4096, fbo_h = 4096;
static unsigned last_w, last_h, frames_presented;
/* GLHOST_CTX: "core" (default) = accept OPENGL_CORE or OPENGL on a core
 * context; "gl2" = simulate the modern RetroArch gl driver: answer
 * GET_PREFERRED_HW_RENDER with RETRO_HW_CONTEXT_OPENGL, accept ONLY that,
 * serve a compatibility-profile context; "rejcore" = no preference query,
 * reject OPENGL_CORE, accept OPENGL on a compatibility context (exercises
 * the rejection-retry ladder). */
static int ctx_gl2, ctx_rejcore;
static int avflags_on;
static char sysdir[512] = "/tmp/vkhost_sys";
static char savedir[512] = "/tmp/vkhost_save";

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{ va_list ap; (void)level; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }

/* ---- HW render callbacks the core consumes ---- */
static uintptr_t get_current_framebuffer(void) { return (uintptr_t)fbo; }
static retro_proc_address_t get_proc_address(const char *sym)
{ return (retro_proc_address_t)eglGetProcAddress(sym); }

/* ---- environment ---- */
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
      case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      {
         /* Only answered once the run loop starts, so the boot savestate
          * (a labelled RetroArch state) still loads via the labelled
          * reader - RA likewise only reports fast-savestates while the
          * runahead machinery itself is saving/loading. */
         const char *f = getenv("GLHOST_AVFLAGS");
         if (!f || !avflags_on) return false;
         *(int *)data = atoi(f);
         return true;
      }
      case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
         if (!ctx_gl2) return false;
         *(unsigned *)data = RETRO_HW_CONTEXT_OPENGL;
         fprintf(stderr, "[glhost] GET_PREFERRED_HW_RENDER -> OPENGL\n");
         return true;
      case RETRO_ENVIRONMENT_SET_HW_RENDER:
      {
         struct retro_hw_render_callback *cb = (struct retro_hw_render_callback *)data;
         if ((ctx_gl2 || ctx_rejcore) && cb->context_type != RETRO_HW_CONTEXT_OPENGL)
         {
            fprintf(stderr, "[glhost] SET_HW_RENDER type=%d REJECTED (mode)\n",
                    (int)cb->context_type);
            return false;
         }
         if (cb->context_type != RETRO_HW_CONTEXT_OPENGL_CORE &&
             cb->context_type != RETRO_HW_CONTEXT_OPENGL)
            return false;
         cb->get_current_framebuffer = get_current_framebuffer;
         cb->get_proc_address        = get_proc_address;
         hw_render = *cb;
         fprintf(stderr, "[glhost] SET_HW_RENDER type=%d version %u.%u\n",
                 (int)cb->context_type, cb->version_major, cb->version_minor);
         return true;
      }
      default:
         return false;
   }
}

/* ---- video/audio/input stubs ---- */
static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch)
{ (void)data; (void)pitch;
  if (width != last_w || height != last_h)
     fprintf(stderr, "[glhost] geometry %ux%u\n", width, height);
  last_w = width; last_h = height; frames_presented++; }
static void audio_sample_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
   const char *inj = getenv("GLHOST_INPUT"); /* "first-last:id,..." */
   static unsigned frame_now; (void)dev; (void)idx;
   if (port != 0 || !inj) return 0;
   frame_now = frames_presented;
   { const char *p = inj;
     while (*p)
     { unsigned a, b, i2;
       if (sscanf(p, "%u-%u:%u", &a, &b, &i2) == 3 &&
           frame_now >= a && frame_now <= b && i2 == id)
          return 1;
       p = strchr(p, ','); if (!p) break; p++; }
   }
   return 0;
}

/* ---- EGL surfaceless desktop-GL core context ---- */
static int egl_init(void)
{
   static const EGLint cfg_attr[] = {
      EGL_SURFACE_TYPE, 0,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
      EGL_NONE };
   static const EGLint ctx_attr[] = {
      EGL_CONTEXT_MAJOR_VERSION, 3,
      EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
      EGL_NONE };
   static const EGLint ctx_attr_compat[] = {
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
      EGL_NONE };
   EGLConfig cfg; EGLint n;
   setenv("EGL_PLATFORM", "surfaceless", 0); /* headless Mesa default */
   egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (egl_dpy == EGL_NO_DISPLAY || !eglInitialize(egl_dpy, NULL, NULL))
   { fprintf(stderr, "[glhost] eglInitialize failed\n"); return 0; }
   if (!eglBindAPI(EGL_OPENGL_API))
   { fprintf(stderr, "[glhost] eglBindAPI(GL) failed\n"); return 0; }
   if (!eglChooseConfig(egl_dpy, cfg_attr, &cfg, 1, &n) || n < 1)
   { fprintf(stderr, "[glhost] eglChooseConfig failed\n"); return 0; }
   egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT,
                              (ctx_gl2 || ctx_rejcore) ? ctx_attr_compat : ctx_attr);
   if (egl_ctx == EGL_NO_CONTEXT)
   { fprintf(stderr, "[glhost] eglCreateContext failed\n"); return 0; }
   if (!eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, egl_ctx))
   { fprintf(stderr, "[glhost] surfaceless eglMakeCurrent failed\n"); return 0; }
   fprintf(stderr, "[glhost] GL: %s | %s\n",
           (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
   return 1;
}

static int fbo_init(void)
{
   typedef void (*genfb_t)(GLsizei, GLuint *);
   typedef void (*bindfb_t)(GLenum, GLuint);
   typedef void (*fbtex_t)(GLenum, GLenum, GLenum, GLuint, GLint);
   typedef GLenum (*checkfb_t)(GLenum);
   genfb_t  pglGenFramebuffers  = (genfb_t)eglGetProcAddress("glGenFramebuffers");
   bindfb_t pglBindFramebuffer  = (bindfb_t)eglGetProcAddress("glBindFramebuffer");
   fbtex_t  pglFramebufferTexture2D = (fbtex_t)eglGetProcAddress("glFramebufferTexture2D");
   checkfb_t pglCheckFramebufferStatus = (checkfb_t)eglGetProcAddress("glCheckFramebufferStatus");
   glGenTextures(1, &fbo_tex);
   glBindTexture(GL_TEXTURE_2D, fbo_tex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fbo_w, fbo_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
   glGenTextures(1, &fbo_depth);
   glBindTexture(GL_TEXTURE_2D, fbo_depth);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, fbo_w, fbo_h, 0,
                GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
   pglGenFramebuffers(1, &fbo);
   pglBindFramebuffer(GL_FRAMEBUFFER, fbo);
   pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex, 0);
   pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, fbo_depth, 0);
   if (pglCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
   { fprintf(stderr, "[glhost] FBO incomplete\n"); return 0; }
   return 1;
}

static void dump_frame(const char *path)
{
   unsigned w = last_w ? last_w : 640, h = last_h ? last_h : 480;
   unsigned char *buf = malloc((size_t)w * h * 4);
   FILE *f;
   typedef void (*bindfb_t)(GLenum, GLuint);
   bindfb_t pglBindFramebuffer = (bindfb_t)eglGetProcAddress("glBindFramebuffer");
   if (!buf) return;
   pglBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf);
   f = fopen(path, "wb");
   if (f)
   {
      unsigned x, y;
      fprintf(f, "P6\n%u %u\n255\n", w, h);
      for (y = 0; y < h; y++)          /* GL readback is bottom-up */
         for (x = 0; x < w; x++)
         {
            const unsigned char *p = buf + ((size_t)(h - 1 - y) * w + x) * 4;
            fputc(p[0], f); fputc(p[1], f); fputc(p[2], f);
         }
      fclose(f);
      fprintf(stderr, "[glhost] dumped %s (%ux%u)\n", path, w, h);
   }
   free(buf);
}

/* GLHOST_DIRTY=1: between retro_run calls, leave behind the kind of GL
 * state a frontend legitimately leaves after drawing its own UI - the
 * exact churn RetroArch's per-frame OSD produces. A robust core must
 * set everything it depends on; glsm used to normalise all of this. */
static GLuint dirty_pbo, dirty_tex;
static void frontend_dirty_state(void)
{
   typedef void (*bindbuf_t)(GLenum, GLuint);
   typedef void (*genbuf_t)(GLsizei, GLuint *);
   typedef void (*bufdata_t)(GLenum, GLsizeiptr, const void *, GLenum);
   typedef void (*acttex_t)(GLenum);
   typedef void (*bindvao_t)(GLuint);
   typedef void (*useprog_t)(GLuint);
   static bindbuf_t pBindBuffer; static genbuf_t pGenBuffers;
   static bufdata_t pBufferData; static acttex_t pActiveTexture;
   static bindvao_t pBindVertexArray; static useprog_t pUseProgram;
   if (!pBindBuffer)
   {
      pBindBuffer      = (bindbuf_t)eglGetProcAddress("glBindBuffer");
      pGenBuffers      = (genbuf_t)eglGetProcAddress("glGenBuffers");
      pBufferData      = (bufdata_t)eglGetProcAddress("glBufferData");
      pActiveTexture   = (acttex_t)eglGetProcAddress("glActiveTexture");
      pBindVertexArray = (bindvao_t)eglGetProcAddress("glBindVertexArray");
      pUseProgram      = (useprog_t)eglGetProcAddress("glUseProgram");
   }
   if (!dirty_pbo)
   {
      static unsigned char junk[4096];
      unsigned i; for (i = 0; i < sizeof(junk); i++) junk[i] = (unsigned char)(i * 37u + 11u);
      pGenBuffers(1, &dirty_pbo);
      pBindBuffer(0x88EC /*GL_PIXEL_UNPACK_BUFFER*/, dirty_pbo);
      pBufferData(0x88EC, sizeof(junk), junk, GL_STATIC_DRAW);
      glGenTextures(1, &dirty_tex);
   }
   /* the frontend drew its font atlas: unpack state + PBO left bound */
   pBindBuffer(0x88EC, dirty_pbo);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
   glPixelStorei(GL_UNPACK_ROW_LENGTH, 333);
   glPixelStorei(0x0CF3 /*GL_UNPACK_SKIP_ROWS*/, 3);
   glPixelStorei(0x0CF4 /*GL_UNPACK_SKIP_PIXELS*/, 5);
   glPixelStorei(GL_PACK_ALIGNMENT, 8);
   /* its own texture on the unit the core uses, wrong active unit */
   pActiveTexture(GL_TEXTURE0 + 5);
   glBindTexture(GL_TEXTURE_2D, dirty_tex);
   pActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, dirty_tex);
   /* no VAO, no program, blend enabled with odd funcs */
   pBindVertexArray(0);
   pUseProgram(0);
   glEnable(GL_BLEND);
   glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_SCISSOR_TEST);
}

typedef void (*set_env_t)(retro_environment_t);
typedef void (*set_vid_t)(retro_video_refresh_t);
typedef void (*set_as_t)(retro_audio_sample_t);
typedef void (*set_ab_t)(retro_audio_sample_batch_t);
typedef void (*set_ip_t)(retro_input_poll_t);
typedef void (*set_is_t)(retro_input_state_t);
typedef void (*fn_t)(void);
typedef bool (*load_t)(const struct retro_game_info *);
typedef void (*run_t)(void);
typedef bool (*unser_t)(const void *, size_t);
typedef bool (*ser_t)(void *, size_t);
typedef size_t (*ssize_t_t)(void);

int main(int argc, char **argv)
{
   const char *core_path, *content, *state_path = NULL, *outdir = "/tmp/glhost_out";
   int frames = 60, i;
   char cmd[1024];
   if (argc < 3)
   { fprintf(stderr, "usage: %s <core.so> <content> [state|-] [frames] [outdir]\n", argv[0]); return 1; }
   core_path = argv[1]; content = argv[2];
   if (argc > 3 && strcmp(argv[3], "-")) state_path = argv[3];
   if (argc > 4) frames = atoi(argv[4]);
   if (argc > 5) outdir = argv[5];
   snprintf(cmd, sizeof(cmd), "mkdir -p %s %s %s", outdir, sysdir, savedir);
   if (system(cmd) != 0) {}

   { const char *ov = getenv("GLHOST_VARS");    /* overrides win: added first */
     if (ov) { char *dup = strdup(ov), *tok = strtok(dup, ";");
       while (tok) { char *eq = strchr(tok, '=');
         if (eq) { *eq = 0; add_var(tok, eq + 1); } tok = strtok(NULL, ";"); } free(dup); } }
   add_var("beetle_psx_hw_renderer", "opengl");
   { const char *m = getenv("GLHOST_CTX");
     if (m && !strcmp(m, "gl2"))     ctx_gl2 = 1;
     if (m && !strcmp(m, "rejcore")) ctx_rejcore = 1; }

   if (!egl_init()) return 3;
   if (!fbo_init()) return 3;

   core = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
   if (!core) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
   ((set_env_t)dlsym(core, "retro_set_environment"))(env_cb);
   ((set_vid_t)dlsym(core, "retro_set_video_refresh"))(video_cb);
   ((set_as_t)dlsym(core, "retro_set_audio_sample"))(audio_sample_cb);
   ((set_ab_t)dlsym(core, "retro_set_audio_sample_batch"))(audio_batch_cb);
   ((set_ip_t)dlsym(core, "retro_set_input_poll"))(input_poll_cb);
   ((set_is_t)dlsym(core, "retro_set_input_state"))(input_state_cb);
   ((fn_t)dlsym(core, "retro_init"))();

   { struct retro_game_info info; memset(&info, 0, sizeof(info));
     info.path = content;
     if (!((load_t)dlsym(core, "retro_load_game"))(&info))
     { fprintf(stderr, "[glhost] retro_load_game failed\n"); return 4; }
   }

   if (!hw_render.context_reset)
   { fprintf(stderr, "[glhost] core did not request a GL hw context\n"); return 5; }
   hw_render.context_reset();

   if (state_path)
   {
      FILE *f = fopen(state_path, "rb");
      if (f)
      {
         long sz; void *buf;
         fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
         buf = malloc(sz);
         if (buf && fread(buf, 1, sz, f) == (size_t)sz)
         {
            /* RetroArch RASTATE container: locate the MEM chunk. */
            const unsigned char *p = buf; size_t off = 0, want = 0;
            if (sz > 16 && !memcmp(p, "RASTATE", 7))
            { size_t pos = 8;
              while (pos + 8 <= (size_t)sz)
              { unsigned len = p[pos+4] | (p[pos+5]<<8) | ((unsigned)p[pos+6]<<16) | ((unsigned)p[pos+7]<<24);
                if (!memcmp(p + pos, "MEM ", 4)) { off = pos + 8; want = len; break; }
                pos += 8 + ((len + 7u) & ~7u); } }
            else { off = 0; want = (size_t)sz; }
            if (want && ((unser_t)dlsym(core, "retro_unserialize"))(p + off, want))
               fprintf(stderr, "[glhost] savestate loaded (%zu bytes)\n", want);
            else
               fprintf(stderr, "[glhost] savestate load FAILED\n");
         }
         free(buf); fclose(f);
      }
   }

   { run_t run = (run_t)dlsym(core, "retro_run");
     /* GLHOST_PREEMPT=1: model RetroArch's Preemptive Frames runahead.
      * Each presented frame: serialize the core, unserialize the very
      * same state back, and run - the constant savestate load-and-replay
      * churn that mode produces while input is held. */
     ser_t   ser    = (ser_t)dlsym(core, "retro_serialize");
     ssize_t_t ssz  = (ssize_t_t)dlsym(core, "retro_serialize_size");
     unser_t unser  = (unser_t)dlsym(core, "retro_unserialize");
     int preempt    = getenv("GLHOST_PREEMPT") ? atoi(getenv("GLHOST_PREEMPT")) : 0;
     void *pbuf     = NULL; void *pbuf2 = NULL; size_t pcap = 0; int have_prev = 0;
     avflags_on = 1;
     for (i = 1; i <= frames; i++)
     {
        if (preempt && (i < 5 || (i % 50) == 0))
           fprintf(stderr, "[glhost] frame %d serialize_size=%zu\n", i, ssz());
        if (preempt == 1)
        {
           /* same-state cycle: serialize and immediately restore */
           size_t need = ssz();
           if (need > pcap) { free(pbuf); pbuf = malloc(need); pcap = need; }
           if (!pbuf || !ser(pbuf, need) || !unser(pbuf, need))
              fprintf(stderr, "[glhost] preempt cycle FAILED at frame %d\n", i);
        }
        else if (preempt == 2)
        {
           /* true rollback, RetroArch Preemptive Frames N=1: restore the
            * state saved before the PREVIOUS frame, re-run that frame
            * (replay), then save and run the current frame - two
            * retro_run calls per presented frame, rolling back one. */
           size_t need = ssz();
           if (need > pcap)
           { free(pbuf); free(pbuf2); pbuf = malloc(need); pbuf2 = malloc(need); pcap = need; }
           if (pbuf && pbuf2)
           {
              if (have_prev)
              {
                 if (!unser(pbuf, pcap))
                    fprintf(stderr, "[glhost] rollback unser FAILED frame %d\n", i);
                 run(); /* replay previous frame */
                 if (getenv("GLHOST_DIRTY"))
                    frontend_dirty_state();
              }
              if (!ser(pbuf2, need))
                 fprintf(stderr, "[glhost] rollback ser FAILED frame %d\n", i);
              { void *t = pbuf; pbuf = pbuf2; pbuf2 = t; }
              have_prev = 1;
           }
        }
        run();
        if (getenv("GLHOST_DIRTY"))
           frontend_dirty_state();
        if ((i % 30) == 0 || i == frames)
        { char path[1024];
          snprintf(path, sizeof(path), "%s/frame_%04d.ppm", outdir, i);
          dump_frame(path); }
     }
   free(pbuf); free(pbuf2);
   }
   fprintf(stderr, "[glhost] done: %d frames run, %u presented, last %ux%u\n",
           frames, frames_presented, last_w, last_h);

   ((fn_t)dlsym(core, "retro_unload_game"))();
   ((fn_t)dlsym(core, "retro_deinit"))();
   return 0;
}
