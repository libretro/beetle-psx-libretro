#include "shaders_common.h"

static const char *image_load_fragment = GLSL_FRAGMENT(
      uniform sampler2D fb_texture;
      uniform uint internal_upscaling;
      in vec2 frag_fb_coord;
      out vec4 frag_color;

      // Read a pixel in VRAM
      vec4 vram_get_pixel(int x, int y) {
	return texelFetch(fb_texture, ivec2(x, y) * int(internal_upscaling), 0);
      }

      void main() {
      // GLES 5551 note: color reinterpretation can be done here
      frag_color = vram_get_pixel(int(frag_fb_coord.x), int(frag_fb_coord.y));
      }
);
