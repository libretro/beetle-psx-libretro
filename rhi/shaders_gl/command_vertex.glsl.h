#include "shaders_common.h"

#undef command_vertex_name_
#if defined(FILTER_SABR) || defined(FILTER_XBR)
#define command_vertex_name_ command_vertex_xbr
#else
#define command_vertex_name_ command_vertex
#endif

static const char * command_vertex_name_ = GLSL_VERTEX(
// Vertex shader for rendering GPU draw commands in the framebuffer
in uvec4 position;
in uvec3 color;
in uvec4 fog;
in uvec2 texture_page;
in uvec2 texture_coord;
in uvec2 clut;
in uint texture_blend_mode;
in uint depth_shift;
in uint dither;
in uint semi_transparent;
in uvec4 texture_window;
in uvec4 texture_limits;
in uint framebuffer_feedback;
in uint palette_cached;
in uint native_flags;
in uvec2 native_uv;

// Drawing offset
uniform ivec2 offset;

out vec3 frag_shading_color;
out vec4 frag_fog;
flat out uvec2 frag_texture_page;
out vec2 smooth_texture_coord;
flat out uvec2 frag_clut;
flat out uint frag_texture_blend_mode;
flat out uint frag_depth_shift;
flat out uint frag_dither;
flat out uint frag_semi_transparent;
flat out uvec4 frag_texture_window;
flat out uvec4 frag_texture_limits;
flat out uint frag_framebuffer_feedback;
flat out uint frag_palette_cached;
)

#if !defined(FILTER_SABR) && !defined(FILTER_XBR)
STRINGIZE(
flat out uint frag_native_flags;
flat out uvec3 frag_color_origin;
flat out uvec3 frag_color_dx;
flat out uvec3 frag_color_dy;
flat out uvec4 frag_native_uv;
)
#endif

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
   vec4 vertex_position = uintBitsToFloat(position);
   if ((native_flags & 2u) != 0u) {
      ivec2 xy = ivec2(int(position.x << 16u) >> 16, int(position.x) >> 16);
      vertex_position = vec4(vec2(xy), uintBitsToFloat(position.z), 1.0);
   }
   vec2 pos = vertex_position.xy + vec2(offset);
   // Match the PS1 integer pixel origin only for eligible native geometry.
   if ((native_flags & 12u) != 0u)
      pos += vec2(0.5);

   // Convert VRAM coordinates (0;1023, 0;511) into OpenGL coordinates
   // (-1;1, -1;1)
   float wpos = vertex_position.w;
   float xpos = (pos.x / 512.) - 1.0;
   float ypos = (pos.y / 256.) - 1.0;

   // position.z increases as the primitives near the camera so we
   // reverse the order to match the common GL convention
   float zpos = 1.0 - (vertex_position.z / 32768.);

   gl_Position.xyzw = vec4(xpos * wpos, ypos * wpos, zpos * wpos, wpos);
   //gl_Position.xyzw = vec4(xpos, ypos, zpos, 1.);

   // Glium doesn't support 'normalized' for now
   /* Already in 0..1+ scale (1.0 == 0xFF). May exceed 1.0 when the PGXP
   // precise-colour path supplies the GTE's pre-saturation value. */
   frag_shading_color = uintBitsToFloat(color);
   frag_fog = uintBitsToFloat(fog);
)
#if !defined(FILTER_SABR) && !defined(FILTER_XBR)
STRINGIZE(
   frag_native_flags = native_flags;
   frag_native_uv = (native_flags & 2u) != 0u ?
         uvec4(position.yw, native_uv) : uvec4(native_uv, 0u, 0u);
   frag_color_origin = uvec3(0u);
   frag_color_dx = uvec3(0u);
   frag_color_dy = uvec3(0u);
   if ((native_flags & 1u) != 0u) {
      uvec3 low = uvec3(color.x, color.z, fog.y);
      uvec3 high = uvec3(color.y, fog.x, fog.z);
      frag_color_origin = low & 0xfffffu;
      frag_color_dx = (low >> 20u) | ((high & 0xffu) << 12u);
      frag_color_dy = high >> 8u;
      frag_shading_color = vec3(uvec3(fog.w, fog.w >> 8u,
            fog.w >> 16u) & 255u) / 255.0;
      frag_fog = vec4(0.0);
   }
)
#endif
STRINGIZE(

   // Let OpenGL interpolate the texel position
   smooth_texture_coord = vec2(texture_coord) + vec2(0.001, 0.001);

   frag_texture_page = texture_page;
   frag_clut = clut;
   frag_texture_blend_mode = texture_blend_mode;
   frag_depth_shift = depth_shift;
   frag_dither = dither;
   frag_semi_transparent = semi_transparent;
   frag_texture_window = texture_window;
   frag_texture_limits = texture_limits;
   frag_framebuffer_feedback = framebuffer_feedback;
   frag_palette_cached = palette_cached;
)
#if defined(FILTER_SABR) || defined(FILTER_XBR)
STRINGIZE(
	tc = smooth_texture_coord.xy;
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
