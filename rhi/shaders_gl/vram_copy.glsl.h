#include "shaders_common.h"

/* GP0(80h) VRAM-to-VRAM copy: draws one destination piece of a copy step
 * from the step's snapshot. The renderer's unit quad is placed on the
 * piece's VRAM rect. */
static const char *vram_copy_vertex = GLSL_VERTEX(
      layout(location = 0) in vec2 corner;
      uniform vec4 rect;

      void main() {
      vec2 p = mix(rect.xy, rect.zw, corner);
      gl_Position = vec4(p.x / 512.0 - 1.0, p.y / 256.0 - 1.0, 0.0, 1.0);
      }
);

/* The snapshot holds the step's source contiguously from (0, 0). A
 * destination pixel maps back to it modulo the VRAM size, so pieces split
 * at the VRAM edge share one origin. mask_pass 1 marks the stencil of
 * pixels whose result carries the mask bit; it discards the rest. */
static const char *vram_copy_fragment = GLSL_FRAGMENT(
      uniform highp sampler2D snapshot;
      uniform ivec2 origin;
      uniform ivec2 period;
      uniform int mask_pass;
      uniform int set_mask;
      out vec4 frag_color;

      void main() {
      ivec2 p = (ivec2(gl_FragCoord.xy) - origin + period) % period;
      vec4 c = texelFetch(snapshot, p, 0);
      if (set_mask != 0)
         c.a = 1.0;
      if (mask_pass != 0 && c.a <= 0.5)
         discard;
      frag_color = c;
      }
);
