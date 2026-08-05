/* Compiles and links every generated analog GL program in a real GL 3.3 core
 * context (EGL surfaceless; llvmpipe is fine - this checks the compiler, not
 * the pixels), then reports whether each declared uniform actually survived
 * linking.
 *
 * A uniform that the linker drops is not necessarily a bug - GLSL removes
 * uniforms with no effect on the output - but a *program* that fails to
 * compile is, and so is a uniform the C side will later try to set that the
 * shader never declared. Both are cheap to catch here and expensive to debug
 * on a user's driver.
 *
 * cc -o gl_shader_check gl_shader_check.c -lEGL && ./gl_shader_check
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "analog_vertex.glsl.h"
#include "analog_downsample.glsl.h"
#include "analog_encode.glsl.h"
#include "analog_encode_pal.glsl.h"
#include "analog_comb.glsl.h"
#include "analog_comb_pal.glsl.h"
#include "analog_demod.glsl.h"
#include "analog_demod_pal.glsl.h"
#include "analog_rgb.glsl.h"
#include "analog_rgb_pal.glsl.h"
#include "analog_resolve.glsl.h"
#include "analog_resolve_hdr.glsl.h"
#include "analog_yc.glsl.h"
#include "analog_yc_pal.glsl.h"
#include "analog_notch.glsl.h"
#include "analog_notch_pal.glsl.h"

typedef unsigned int GLenum_, GLuint_;
typedef int GLint_, GLsizei_;
#define GL_VERTEX_SHADER_   0x8B31
#define GL_FRAGMENT_SHADER_ 0x8B30
#define GL_COMPILE_STATUS_  0x8B81
#define GL_LINK_STATUS_     0x8B82
#define GL_ACTIVE_UNIFORMS_ 0x8B86
#define GL_COMPUTE_SHADER_  0x91B9

static GLuint_ (*p_glCreateShader)(GLenum_);
static void (*p_glShaderSource)(GLuint_, GLsizei_, const char *const *, const GLint_ *);
static void (*p_glCompileShader)(GLuint_);
static void (*p_glGetShaderiv)(GLuint_, GLenum_, GLint_ *);
static void (*p_glGetShaderInfoLog)(GLuint_, GLsizei_, GLsizei_ *, char *);
static GLuint_ (*p_glCreateProgram)(void);
static void (*p_glAttachShader)(GLuint_, GLuint_);
static void (*p_glLinkProgram)(GLuint_);
static void (*p_glGetProgramiv)(GLuint_, GLenum_, GLint_ *);
static void (*p_glGetProgramInfoLog)(GLuint_, GLsizei_, GLsizei_ *, char *);
static GLint_ (*p_glGetUniformLocation)(GLuint_, const char *);
static void (*p_glDeleteShader)(GLuint_);
static void (*p_glDeleteProgram)(GLuint_);
static const unsigned char *(*p_glGetString)(GLenum_);

static int failures;

static GLuint_ compile(const char *src, GLenum_ type, const char *label)
{
   GLuint_ sh = p_glCreateShader(type);
   GLint_ ok = 0;
   p_glShaderSource(sh, 1, &src, NULL);
   p_glCompileShader(sh);
   p_glGetShaderiv(sh, GL_COMPILE_STATUS_, &ok);
   if (!ok)
   {
      char log[8192];
      GLsizei_ n = 0;
      p_glGetShaderInfoLog(sh, sizeof(log) - 1, &n, log);
      log[n] = 0;
      printf("  FAIL compile %s:\n%s\n", label, log);
      failures++;
      return 0;
   }
   return sh;
}

static void check(const char *name, const char *fs, const char **uniforms, int nu)
{
   GLuint_ v, f, prog;
   GLint_ ok = 0, active = 0;
   int i, missing = 0;

   v = compile(analog_vertex_glsl, GL_VERTEX_SHADER_, "analog.vert");
   f = compile(fs, GL_FRAGMENT_SHADER_, name);
   if (!v || !f)
      return;

   prog = p_glCreateProgram();
   p_glAttachShader(prog, v);
   p_glAttachShader(prog, f);
   p_glLinkProgram(prog);
   p_glGetProgramiv(prog, GL_LINK_STATUS_, &ok);
   if (!ok)
   {
      char log[8192];
      GLsizei_ n = 0;
      p_glGetProgramInfoLog(prog, sizeof(log) - 1, &n, log);
      log[n] = 0;
      printf("  FAIL link %s:\n%s\n", name, log);
      failures++;
      return;
   }
   p_glGetProgramiv(prog, GL_ACTIVE_UNIFORMS_, &active);

   for (i = 0; i < nu; i++)
      if (p_glGetUniformLocation(prog, uniforms[i]) < 0)
      {
         if (!missing)
            printf("  %-22s linked, but optimised out:", name);
         printf(" %s", uniforms[i]);
         missing++;
      }
   if (missing)
      printf("\n");
   else
      printf("  %-22s OK (%d active uniforms)\n", name, (int)active);

   p_glDeleteShader(v);
   p_glDeleteShader(f);
   p_glDeleteProgram(prog);
}

/* Compute stages: same check, but they need a 4.3 context, so a failure here
 * is informative rather than fatal - the backend falls back to analog_yc when
 * the driver cannot run them. */
static void check_compute(const char *name, const char *cs,
                          const char **uniforms, int nu)
{
   GLuint_ c, prog;
   GLint_ ok = 0, active = 0;
   int i, missing = 0;

   c = compile(cs, GL_COMPUTE_SHADER_, name);
   if (!c)
      return;
   prog = p_glCreateProgram();
   p_glAttachShader(prog, c);
   p_glLinkProgram(prog);
   p_glGetProgramiv(prog, GL_LINK_STATUS_, &ok);
   if (!ok)
   {
      char log[8192];
      GLsizei_ n = 0;
      p_glGetProgramInfoLog(prog, sizeof(log) - 1, &n, log);
      log[n] = 0;
      printf("  FAIL link %s:\n%s\n", name, log);
      failures++;
      return;
   }
   p_glGetProgramiv(prog, GL_ACTIVE_UNIFORMS_, &active);
   for (i = 0; i < nu; i++)
      if (p_glGetUniformLocation(prog, uniforms[i]) < 0)
      {
         if (!missing)
            printf("  %-22s linked, but optimised out:", name);
         printf(" %s", uniforms[i]);
         missing++;
      }
   if (missing)
      printf("\n");
   else
      printf("  %-22s OK (%d active uniforms)\n", name, (int)active);
   p_glDeleteShader(c);
   p_glDeleteProgram(prog);
}

int main(int argc, char **argv)
{
   EGLDisplay dpy;
   EGLContext ctx;
   EGLConfig cfg;
   EGLint n = 0;
   static const EGLint cfg_attr[] = {
      EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE
   };
   /* Version to request, default 4.3. Pass "3.3" to check that the fragment
    * stages still build at the renderer's practical floor; compute is skipped
    * below 4.3, which is the situation the analog_yc fallback exists for. */
   int want_major = (argc > 1) ? atoi(argv[1]) : 4;
   int want_minor = (argc > 2) ? atoi(argv[2]) : 3;
   EGLint ctx_attr[] = {
      EGL_CONTEXT_MAJOR_VERSION, 4, EGL_CONTEXT_MINOR_VERSION, 3,
      EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
      EGL_NONE
   };
   ctx_attr[1] = want_major;
   ctx_attr[3] = want_minor;
   PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
      (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");

   dpy = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL)
             : eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (!eglInitialize(dpy, NULL, NULL) || !eglBindAPI(EGL_OPENGL_API) ||
       !eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
   {
      printf("EGL setup failed\n");
      return 2;
   }
   ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
   if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
   {
      printf("no GL %d.%d core context\n", want_major, want_minor);
      return 2;
   }

#define L(x) p_##x = (typeof(p_##x))eglGetProcAddress(#x); if (!p_##x) { printf("missing " #x "\n"); return 2; }
   L(glCreateShader) L(glShaderSource) L(glCompileShader) L(glGetShaderiv)
   L(glGetShaderInfoLog) L(glCreateProgram) L(glAttachShader) L(glLinkProgram)
   L(glGetProgramiv) L(glGetProgramInfoLog) L(glGetUniformLocation)
   L(glDeleteShader) L(glDeleteProgram) L(glGetString)
#undef L

   printf("GL: %s\n", (const char *)p_glGetString(0x1F02) /* GL_VERSION */);

   {
      static const char *u_down[] = { "reg_src_size", "reg_native_size", "reg_sdr_eotf" };
      static const char *u_enc[]  = { "reg_src_size", "reg_div", "reg_x1", "reg_inv_ratio",
                                      "reg_line_adv", "reg_line_split", "reg_field_off",
                                      "reg_field_adv", "reg_cable" };
      static const char *u_comb[] = { "reg_sig_size", "reg_line_split", "reg_svideo" };
      static const char *u_dem[]  = { "reg_sig_size", "reg_x1", "reg_inv_ratio", "reg_line_adv",
                                      "reg_line_split", "reg_field_off", "reg_field_adv",
                                      "reg_svideo" };
      static const char *u_rgb[]  = { "reg_src_size", "reg_div" };
      static const char *u_res[]  = { "reg_sig_size", "reg_out_size", "reg_sdr_eotf",
                                      "reg_paper_white_nits", "reg_peak_nits",
                                      "reg_expand_gamut", "reg_shoulder", "reg_src_primaries" };

      check("downsample",   analog_downsample_glsl,   u_down, 3);
      check("encode",       analog_encode_glsl,       u_enc,  9);
      check("encode_pal",   analog_encode_pal_glsl,   u_enc,  9);
      check("comb",         analog_comb_glsl,         u_comb, 3);
      check("comb_pal",     analog_comb_pal_glsl,     u_comb, 3);
      check("demod",        analog_demod_glsl,        u_dem,  8);
      check("demod_pal",    analog_demod_pal_glsl,    u_dem,  8);
      check("rgb",          analog_rgb_glsl,          u_rgb,  2);
      check("rgb_pal",      analog_rgb_pal_glsl,      u_rgb,  2);
      check("resolve",      analog_resolve_glsl,      u_res,  8);
      check("resolve_hdr",  analog_resolve_hdr_glsl,  u_res,  8);

      {
         static const char *u_yc[] = { "reg_sig_size", "reg_line_split",
                                       "reg_black_scale", "reg_black_offset" };
         static const char *u_no[] = { "reg_sig_size", "reg_line_split",
                                       "reg_black_scale", "reg_black_offset",
                                       "reg_b0", "reg_b1", "reg_b2",
                                       "reg_a1", "reg_a2", "reg_enable" };
         check("yc",          analog_yc_glsl,      u_yc, 4);
         check("yc_pal",      analog_yc_pal_glsl,  u_yc, 4);
         if (want_major > 4 || (want_major == 4 && want_minor >= 3))
         {
            printf("  -- compute (GL 4.3 / GLES 3.1 only) --\n");
            check_compute("notch",     analog_notch_glsl,     u_no, 10);
            check_compute("notch_pal", analog_notch_pal_glsl, u_no, 10);
         }
         else
            printf("  -- compute skipped (context below 4.3; analog_yc path) --\n");
      }
   }

   printf("%s\n", failures ? "FAILURES" : "ALL PROGRAMS COMPILE AND LINK");
   return failures ? 1 : 0;
}
