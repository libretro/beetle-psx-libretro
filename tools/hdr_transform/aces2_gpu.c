/* ACES 2.0 (Full Output Transform) on the GPU against the reference.
 *
 * Runs the shipping display-transform GLSL on an EGL surfaceless desktop GL
 * context (llvmpipe in CI) with the block, hue table and baked cube bound
 * as the GL renderer binds them, over every RGB5 colour the PlayStation
 * can show and a spread of over-white light up to 4x paper white, and
 * compares each output with hdr_dt_aces2_cpu() - the double-precision
 * reference the cube is baked from - as BT.2124 Delta E ITP, where 1.0 is
 * about one just-noticeable difference:
 *
 *   - the baked cube (dt_aces2_cube) must stay within 1.5 everywhere, with
 *     at most 0.2% of colours at or over 1;
 *   - its worst case must not exceed the per-pixel solve's (dt_aces2),
 *     which it replaces once baked.
 *
 * usage: aces2_gpu */
#define _POSIX_C_SOURCE 200112L
#define GL_GLEXT_PROTOTYPES
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include "rhi/hdr_transform.h"
#include "rhi/hdr_aces2.h"
#include "rhi/shaders_gl/hdr_transform.glsl.h"

#define W 256
#define H 256
#define NIN (W * H)

static float  in_rgb[NIN * 4];
static float  out_rgb[NIN * 4];
static double ref[NIN * 3];
static struct hdr_dt_block blk;

static GLuint shader(GLenum type, const char *const *src, int n)
{
   GLuint s = glCreateShader(type);
   GLint  ok = 0;
   char   log[4096];
   glShaderSource(s, n, src, NULL);
   glCompileShader(s);
   glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
   if (!ok)
   {
      glGetShaderInfoLog(s, sizeof(log), NULL, log);
      fprintf(stderr, "shader: %s\n", log);
      exit(2);
   }
   return s;
}

/* Colour difference as BT.2124 Delta E ITP: both colours to Rec.2020 nits
 * through the block's own output matrix, then ICtCp. 1.0 is about one
 * just-noticeable difference; unlike per-channel PQ codes it does not
 * blow up over a near-zero channel of an otherwise bright colour. */
static double pq_eotf_inv(double nits)
{
   double y = nits / 10000.0, p;
   if (y < 0.0) y = 0.0;
   p = pow(y, 0.1593017578125);
   return pow((0.8359375 + 18.8515625 * p) / (1.0 + 18.6875 * p), 78.84375);
}

static void to_ictcp(const double lim[3], double itp[3])
{
   double r2020[3], lms[3];
   int i;
   /* std140 mat3: three columns of four floats. */
   for (i = 0; i < 3; i++)
      r2020[i] = (blk.aces2_to_out[i] * lim[0] + blk.aces2_to_out[4 + i] * lim[1]
            + blk.aces2_to_out[8 + i] * lim[2]) * 100.0;
   lms[0] = pq_eotf_inv((1688.0 * r2020[0] + 2146.0 * r2020[1] +  262.0 * r2020[2]) / 4096.0);
   lms[1] = pq_eotf_inv(( 683.0 * r2020[0] + 2951.0 * r2020[1] +  462.0 * r2020[2]) / 4096.0);
   lms[2] = pq_eotf_inv((  99.0 * r2020[0] +  309.0 * r2020[1] + 3688.0 * r2020[2]) / 4096.0);
   itp[0] = 0.5 * lms[0] + 0.5 * lms[1];
   itp[1] = (6610.0 * lms[0] - 13613.0 * lms[1] + 7003.0 * lms[2]) / 4096.0 * 0.5;
   itp[2] = (17933.0 * lms[0] - 17390.0 * lms[1] - 543.0 * lms[2]) / 4096.0;
}

static double delta_e_itp(const double a[3], const double b[3])
{
   double x[3], y[3];
   to_ictcp(a, x);
   to_ictcp(b, y);
   return 720.0 * sqrt((x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1])
         + (x[2] - y[2]) * (x[2] - y[2]));
}

struct stats { double max, sum; long over_jnd, n; };

static void tally(struct stats *s, const double got[3], const double want[3])
{
   double d = delta_e_itp(got, want);
   if (d > s->max)
      s->max = d;
   s->sum += d;
   s->over_jnd += d >= 1.0;
   s->n++;
}

static GLuint program(const char *body)
{
   static const char *vs[] = { "#version 330 core\n"
      "void main() { vec2 p = vec2(gl_VertexID & 1, gl_VertexID >> 1) * 2.0 - 1.0;"
      " gl_Position = vec4(p, 0.0, 1.0); }\n" };
   const char *fs[3];
   GLuint prog = glCreateProgram();
   fs[0] = "#version 330 core\n";
   fs[1] = hdr_transform_glsl;
   fs[2] = body;
   glAttachShader(prog, shader(GL_VERTEX_SHADER, vs, 1));
   glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs, 3));
   glLinkProgram(prog);
   glUseProgram(prog);
   glUniform1i(glGetUniformLocation(prog, "uIn"), 0);
   glUniform1i(glGetUniformLocation(prog, "uDtAces2Lut"), 1);
   glUniform1i(glGetUniformLocation(prog, "uDtAces2Cube"), 3);
   glUniformBlockBinding(prog, glGetUniformBlockIndex(prog, "DisplayTransform"), 0);
   return prog;
}

static void run(const char *body, struct stats *lattice, struct stats *over)
{
   int k;
   program(body);
   glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
   glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out_rgb);
   memset(lattice, 0, sizeof(*lattice));
   memset(over, 0, sizeof(*over));
   for (k = 0; k < NIN; k++)
   {
      double o[3];
      o[0] = out_rgb[k * 4]; o[1] = out_rgb[k * 4 + 1]; o[2] = out_rgb[k * 4 + 2];
      tally(k < 32768 ? lattice : over, o, ref + k * 3);
   }
}

static void report(const char *what, const struct stats *s)
{
   printf("  %-28s Delta E ITP max %.2f, mean %.3f, %.2f%% at or over 1\n",
         what, s->max, s->sum / s->n, 100.0 * s->over_jnd / s->n);
}

int main(void)
{
   static const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, 0,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
   static const EGLint ctx_attr[] = { EGL_CONTEXT_MAJOR_VERSION, 3,
      EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
      EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
   const float paper = 200.0f;
   struct hdr_dt_settings set;
   struct stats solve_l, solve_o, cube_l, cube_o, cube_all, solve_all;
   uint16_t no_xy[8];
   EGLDisplay dpy;
   EGLConfig  cfg;
   EGLContext ctx;
   EGLint     n;
   GLuint vao, tin, tout, tlut, tcube, fbo, ubo;
   const uint16_t *lut, *cube;
   int i, k, fails = 0;

   memset(&set, 0, sizeof(set));
   memset(no_xy, 0, sizeof(no_xy));
   set.curve = HDR_DT_ACES2FULL;
   set.aces2_surround = 1;
   set.aces2_gamut = 3;
   set.gt_toe = 0.22f;
   set.gt_linear = 0.40f;
   set.filmic_contrast = 3;
   set.filmic_max_ev = 4.026068811f;
   hdr_dt_configure(&set);
   hdr_dt_fill(&blk, paper, 1000.0f, 0.0f, no_xy);
   hdr_dt_aces2_cube_finish(paper);
   hdr_dt_fill(&blk, paper, 1000.0f, 0.0f, no_xy);
   lut  = hdr_dt_aces2_lut();
   cube = hdr_dt_aces2_cube();
   if (!lut || !cube || blk.gm[2] != 1.0f
         || (int)(blk.mode[0] + 0.5f) != HDR_DT_ACES2FULL)
   {
      fprintf(stderr, "ACES 2.0 tables or cube were not built\n");
      return 1;
   }

   /* Every RGB5 colour through the 2.4 reference transfer, then over-white
    * light up to 4x paper white. */
   srand(1);
   for (k = 0; k < NIN; k++)
   {
      double v[3];
      for (i = 0; i < 3; i++)
      {
         if (k < 32768)
            v[i] = pow(((k >> (5 * i)) & 31) / 31.0, 2.4);
         else
            v[i] = pow(rand() / (double)RAND_MAX, 2.0) * 4.0;
         in_rgb[k * 4 + i] = (float)v[i];
         v[i] = in_rgb[k * 4 + i];
      }
      in_rgb[k * 4 + 3] = 1.0f;
      hdr_dt_aces2_cpu(v, paper, ref + k * 3);
   }

   setenv("EGL_PLATFORM", "surfaceless", 0);
   dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL) || !eglBindAPI(EGL_OPENGL_API)
         || !eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
   {
      fprintf(stderr, "no EGL\n");
      return 2;
   }
   ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
   if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
   {
      fprintf(stderr, "no GL 3.3 core context\n");
      return 2;
   }

   glGenTextures(1, &tin);
   glActiveTexture(GL_TEXTURE0);
   glBindTexture(GL_TEXTURE_2D, tin);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H, 0, GL_RGBA, GL_FLOAT, in_rgb);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

   /* The hue table and the cube exactly as the desktop GL renderer
    * uploads them. */
   glGenTextures(1, &tlut);
   glActiveTexture(GL_TEXTURE1);
   glBindTexture(GL_TEXTURE_2D, tlut);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, HDR_DT_ACES2_LUT_WIDTH, 1, 0,
         GL_RGBA, GL_UNSIGNED_SHORT, lut);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glGenTextures(1, &tcube);
   glActiveTexture(GL_TEXTURE3);
   glBindTexture(GL_TEXTURE_3D, tcube);
   glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F, HDR_DT_ACES2_CUBE, HDR_DT_ACES2_CUBE,
         HDR_DT_ACES2_CUBE, 0, GL_RGBA, GL_HALF_FLOAT, cube);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

   glGenBuffers(1, &ubo);
   glBindBuffer(GL_UNIFORM_BUFFER, ubo);
   glBufferData(GL_UNIFORM_BUFFER, sizeof(blk), &blk, GL_STATIC_DRAW);
   glBindBufferBase(GL_UNIFORM_BUFFER, 0, ubo);

   glGenTextures(1, &tout);
   glActiveTexture(GL_TEXTURE2);
   glBindTexture(GL_TEXTURE_2D, tout);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H, 0, GL_RGBA, GL_FLOAT, NULL);
   glGenFramebuffers(1, &fbo);
   glBindFramebuffer(GL_FRAMEBUFFER, fbo);
   glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tout, 0);
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
   {
      fprintf(stderr, "RGBA32F target incomplete\n");
      return 2;
   }
   glViewport(0, 0, W, H);
   glGenVertexArrays(1, &vao);
   glBindVertexArray(vao);

   printf("aces2_gpu: %s\n", (const char *)glGetString(GL_RENDERER));
   run("uniform sampler2D uIn;\nout vec4 o;\n"
       "void main() { o = vec4(dt_aces2(texelFetch(uIn, ivec2(gl_FragCoord.xy), 0).rgb), 1.0); }\n",
       &solve_l, &solve_o);
   run("uniform sampler2D uIn;\nout vec4 o;\n"
       "void main() { o = vec4(dt_aces2_cube(texelFetch(uIn, ivec2(gl_FragCoord.xy), 0).rgb), 1.0); }\n",
       &cube_l, &cube_o);
   report("per-pixel solve, RGB5:", &solve_l);
   report("per-pixel solve, over-white:", &solve_o);
   report("baked cube, RGB5:", &cube_l);
   report("baked cube, over-white:", &cube_o);

   cube_all = cube_l;
   cube_all.max = cube_l.max > cube_o.max ? cube_l.max : cube_o.max;
   cube_all.over_jnd += cube_o.over_jnd;
   cube_all.n += cube_o.n;
   solve_all.max = solve_l.max > solve_o.max ? solve_l.max : solve_o.max;
   if (cube_all.max > 1.5)
   {
      printf("FAIL: the cube is %.2f Delta E ITP off the reference\n", cube_all.max);
      fails++;
   }
   if (100.0 * cube_all.over_jnd / cube_all.n > 0.2)
   {
      printf("FAIL: %.2f%% of colours are a JND or more off through the cube\n",
            100.0 * cube_all.over_jnd / cube_all.n);
      fails++;
   }
   if (cube_all.max > solve_all.max)
   {
      printf("FAIL: the cube's worst case %.2f exceeds the per-pixel solve's %.2f\n",
            cube_all.max, solve_all.max);
      fails++;
   }
   hdr_dt_free();
   printf(fails ? "aces2_gpu: %d failures\n" : "aces2_gpu: all checks passed\n", fails);
   return fails ? 1 : 0;
}
