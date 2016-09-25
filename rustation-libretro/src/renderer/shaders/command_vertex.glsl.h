#include "shaders_common.h"

static const char *command_vertex = GLSL(
// Vertex shader for rendering GPU draw commands in the framebuffer
in vec4 position;
in uvec3 color;
in uvec2 texture_page;
in uvec2 texture_coord;
in uvec2 clut;
in uint texture_blend_mode;
in uint depth_shift;
in uint dither;
in uint semi_transparent;
in uvec4 texture_window;

// Drawing offset
uniform ivec2 offset;

out vec3 frag_shading_color;
flat out uvec2 frag_texture_page;
out vec2 frag_texture_coord;
flat out uvec2 frag_clut;
flat out uint frag_texture_blend_mode;
flat out uint frag_depth_shift;
flat out uint frag_dither;
flat out uint frag_semi_transparent;
flat out uvec4 frag_texture_window;

void main() {
   vec2 pos = position.xy + offset;

   // Convert VRAM coordinates (0;1023, 0;511) into OpenGL coordinates
   // (-1;1, -1;1)
   float wpos = position.w;
   float xpos = (float(pos.x) / 512) - 1.0;
   float ypos = (float(pos.y) / 256) - 1.0;
  

   // position.z increases as the primitives near the camera so we
   // reverse the order to match the common GL convention
   float zpos = 1.0 - (float(position.z) / 32768.);

   gl_Position.xyzw = vec4(xpos * wpos, ypos * wpos, zpos * wpos, wpos);
   //gl_Position.xyzw = vec4(xpos, ypos, zpos, 1.);

   // Glium doesn't support 'normalized' for now
   frag_shading_color = vec3(color) / 255.;

   // Let OpenGL interpolate the texel position
   frag_texture_coord = vec2(texture_coord);

   frag_texture_page = texture_page;
   frag_clut = clut;
   frag_texture_blend_mode = texture_blend_mode;
   frag_depth_shift = depth_shift;
   frag_dither = dither;
   frag_semi_transparent = semi_transparent;
   frag_texture_window = texture_window;
});
