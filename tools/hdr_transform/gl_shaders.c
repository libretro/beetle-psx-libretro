/* Writes every GL/GLES shader source the HDR display path compiles - the
 * generated analog chain, bloom and post composite (desktop and ES twins)
 * and the output fragment with the Display Transform spliced in - to the
 * directory in argv[1], for run.sh to compile with glslangValidator. Built
 * once plain and once with -DHAVE_OPENGLES3, which selects the ES output
 * shaders exactly as the renderer's build does. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rhi/shaders_gl/hdr_transform.glsl.h"
#include "rhi/shaders_gl/output_vertex.glsl.h"
#include "rhi/shaders_gl/output_fragment.glsl.h"
#include "rhi/shaders_gl/hdr_splice.h"
#include "rhi/shaders_gl/analog_vertex.glsl.h"
#include "rhi/shaders_gl/analog_downsample.glsl.h"
#include "rhi/shaders_gl/analog_resolve.glsl.h"
#include "rhi/shaders_gl/analog_resolve_hdr.glsl.h"
#include "rhi/shaders_gl/hdr_bloom_bright.glsl.h"
#include "rhi/shaders_gl/hdr_bloom_blur.glsl.h"
#include "rhi/shaders_gl/hdr_bloom_down.glsl.h"
#include "rhi/shaders_gl/hdr_bloom_down_karis.glsl.h"
#include "rhi/shaders_gl/hdr_bloom_up.glsl.h"
#include "rhi/shaders_gl/hdr_post.glsl.h"

#ifdef HAVE_OPENGLES3
#define SUFFIX "_es"
#define PICK(desktop, es) (es)
#else
#define SUFFIX "_gl"
#define PICK(desktop, es) (desktop)
#endif

static int put(const char *dir, const char *name, const char *ext, const char *src)
{
   char  path[1024];
   FILE *f;
   sprintf(path, "%s/%s%s.%s", dir, name, SUFFIX, ext);
   f = fopen(path, "wb");
   if (!f)
   {
      fprintf(stderr, "cannot write %s\n", path);
      return 1;
   }
   fputs(src, f);
   fclose(f);
   return 0;
}

int main(int argc, char **argv)
{
   int   bad = 0;
   char *out;
   if (argc < 2 || strlen(argv[1]) > 900)
   {
      fprintf(stderr, "usage: gl_shaders <dir>\n");
      return 2;
   }
   out = gl_hdr_splice_output_fragment(output_fragment, hdr_transform_glsl);
   if (!out)
   {
      fprintf(stderr, "output_fragment has no encode_hdr_output to splice before\n");
      return 1;
   }
   bad |= put(argv[1], "output", "vert", output_vertex);
   bad |= put(argv[1], "output", "frag", out);
   free(out);
   bad |= put(argv[1], "analog_vertex", "vert", PICK(analog_vertex_glsl, analog_vertex_glsl_ES));
   bad |= put(argv[1], "analog_downsample", "frag", PICK(analog_downsample_glsl, analog_downsample_glsl_ES));
   bad |= put(argv[1], "analog_resolve", "frag", PICK(analog_resolve_glsl, analog_resolve_glsl_ES));
   bad |= put(argv[1], "analog_resolve_hdr", "frag", PICK(analog_resolve_hdr_glsl, analog_resolve_hdr_glsl_ES));
   bad |= put(argv[1], "hdr_bloom_bright", "frag", PICK(hdr_bloom_bright_glsl, hdr_bloom_bright_glsl_ES));
   bad |= put(argv[1], "hdr_bloom_blur", "frag", PICK(hdr_bloom_blur_glsl, hdr_bloom_blur_glsl_ES));
   bad |= put(argv[1], "hdr_bloom_down", "frag", PICK(hdr_bloom_down_glsl, hdr_bloom_down_glsl_ES));
   bad |= put(argv[1], "hdr_bloom_down_karis", "frag", PICK(hdr_bloom_down_karis_glsl, hdr_bloom_down_karis_glsl_ES));
   bad |= put(argv[1], "hdr_bloom_up", "frag", PICK(hdr_bloom_up_glsl, hdr_bloom_up_glsl_ES));
   bad |= put(argv[1], "hdr_post", "frag", PICK(hdr_post_glsl, hdr_post_glsl_ES));
   return bad;
}
