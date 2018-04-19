#include "shaders_common.h"

#undef command_vertex_name_
#if defined(FILTER_SABR) || defined(FILTER_XBR)
#define command_vertex_name_ command_vertex_xbr
#else
#define command_vertex_name_ command_vertex
#endif

static const char * command_vertex_name_ = GLSL(
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
)

#if defined(FILTER_SABR) || defined(FILTER_XBR)
STRINGIZE(
out vec2 tc;
out vec4 xyp_1_2_3;
out vec4 xyp_6_7_8;
out vec4 xyp_11_12_13;
out vec4 xyp_16_17_18;
out vec4 xyp_21_22_23;
out vec4 xyp_5_10_15;
out vec4 xyp_9_14_9;
)
#endif
STRINGIZE(
void main() {
   vec2 pos = position.xy + vec2(offset);

   // Convert VRAM coordinates (0;1023, 0;511) into OpenGL coordinates
   // (-1;1, -1;1)
   float wpos = position.w;
   float xpos = (pos.x / 512) - 1.0;
   float ypos = (pos.y / 256) - 1.0;

   // position.z increases as the primitives near the camera so we
   // reverse the order to match the common GL convention
   float zpos = 1.0 - (position.z / 32768.);

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
)
#if defined(FILTER_SABR) || defined(FILTER_XBR)
STRINGIZE(
	tc = frag_texture_coord.xy;
   xyp_1_2_3    = tc.xxxy + vec4(-1.,  0., 1., -2.);
   xyp_6_7_8    = tc.xxxy + vec4(-1.,  0., 1., -1.);
   xyp_11_12_13 = tc.xxxy + vec4(-1.,  0., 1.,  0.);
   xyp_16_17_18 = tc.xxxy + vec4(-1.,  0., 1.,  1.);
   xyp_21_22_23 = tc.xxxy + vec4(-1.,  0., 1.,  2.);
   xyp_5_10_15  = tc.xyyy + vec4(-2., -1., 0.,  1.);
   xyp_9_14_9   = tc.xyyy + vec4( 2., -1., 0.,  1.);
)
#endif
STRINGIZE(
}
);

#undef command_vertex_name_
