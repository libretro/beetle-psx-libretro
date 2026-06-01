#include "shaders_common.h"

static const char *image_load_vertex = GLSL_VERTEX(
      // Vertex shader for uploading textures from the VRAM texture buffer
      // into the output framebuffer

      // The position in the input and output framebuffer are the same
      in uvec2 position;
      out vec2 frag_fb_coord;

      void main() {
      // Convert VRAM position into OpenGL coordinates
      float xpos = (float(position.x) / 512.) - 1.0;
      float ypos = (float(position.y) / 256.) - 1.0;

      gl_Position.xyzw = vec4(xpos, ypos, 0.0, 1.0);

      // frag_fb_coord will remain in PlayStation fb coordinates for
      // texelFetch
      frag_fb_coord = vec2(position);
      }
);
