#include "shaders_common.h"

/* Places the renderer's unit quad on a VRAM rect: uploads from the VRAM
 * texture into the output framebuffer and resolves back. frag_fb_coord
 * stays in PlayStation framebuffer coordinates for texelFetch. */
static const char *image_load_vertex = GLSL_VERTEX(
      layout(location = 0) in vec2 corner;
      uniform vec4 rect;
      out vec2 frag_fb_coord;

      void main() {
      vec2 p = mix(rect.xy, rect.zw, corner);
      gl_Position = vec4(p.x / 512.0 - 1.0, p.y / 256.0 - 1.0, 0.0, 1.0);
      frag_fb_coord = p;
      }
);
