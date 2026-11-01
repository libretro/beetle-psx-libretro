#include "shaders_common.h"

/* VRAM readback from a float or byte target: writes each native pixel's
 * PlayStation word (ABGR1555) to an R16UI target, with the CPU readback's
 * integer rules, so the CPU reads words and converts nothing. Drawn with
 * image_load_vertex on the rect (0, 0, w, h) of the target.
 * mode 0: float, round(c * 31) clamped, mask at half.
 * mode 1: bytes, c >> 3 (SDR byte storage). mode 2: bytes, round(c * 31 /
 * 255) (HDR byte fallback). origin is the first sample, in target texels;
 * scale steps one native pixel. */
static const char *vram_pack_fragment = GLSL_FRAGMENT(
      uniform highp sampler2D src;
      uniform ivec2 origin;
      uniform int scale;
      uniform int mode;
      out uint word;

      void main() {
      vec4 c = texelFetch(src, origin + ivec2(gl_FragCoord.xy) * scale, 0);
      uvec3 q;
      uint a;
      if (mode == 0) {
         q = uvec3(clamp(c.rgb, 0.0, 1.0) * 31.0 + 0.5);
         a = c.a >= 0.5 ? 1u : 0u;
      } else {
         uvec4 b = uvec4(round(c * 255.0));
         q = mode == 1 ? b.rgb >> 3u : (b.rgb * 31u + 127u) / 255u;
         a = b.a >= 128u ? 1u : 0u;
      }
      word = (a << 15) | (q.b << 10) | (q.g << 5) | q.r;
      }
);
