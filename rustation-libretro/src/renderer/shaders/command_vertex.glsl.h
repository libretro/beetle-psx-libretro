static const char *command_vertex = {
"#version 330 core\n"

// Vertex shader for rendering GPU draw commands in the framebuffer

"in ivec3 position;\n"
"in uvec3 color;\n"
"in uvec2 texture_page;\n"
"in uvec2 texture_coord;\n"
"in uvec2 clut;\n"
"in uint texture_blend_mode;\n"
"in uint depth_shift;\n"
"in uint dither;\n"
"in uint semi_transparent;\n"

// Drawing offset
"uniform ivec2 offset;\n"

"out vec3 frag_shading_color;\n"
"flat out uvec2 frag_texture_page;\n"
"out vec2 frag_texture_coord;\n"
"flat out uvec2 frag_clut;\n"
"flat out uint frag_texture_blend_mode;\n"
"flat out uint frag_depth_shift;\n"
"flat out uint frag_dither;\n"
"flat out uint frag_semi_transparent;\n"

"void main() {\n"
"  ivec2 pos = position.xy + offset;\n"

   // Convert VRAM coordinates (0;1023, 0;511) into OpenGL coordinates
   // (-1;1, -1;1)
"  float xpos = (float(pos.x) / 512) - 1.0;\n"
"  float ypos = (float(pos.y) / 256) - 1.0;\n"

   // position.z increases as the primitives near the camera so we
   // reverse the order to match the common GL convention
"  float zpos = 1.0 - (float(position.z) / 32768.);\n"

"  gl_Position.xyzw = vec4(xpos, ypos, zpos, 1.0);\n"

   // Glium doesn't support 'normalized' for now
"  frag_shading_color = vec3(color) / 255.;\n"

   // Let OpenGL interpolate the texel position
"  frag_texture_coord = vec2(texture_coord);\n"

"  frag_texture_page = texture_page;\n"
"  frag_clut = clut;\n"
"  frag_texture_blend_mode = texture_blend_mode;\n"
"  frag_depth_shift = depth_shift;\n"
"  frag_dither = dither;\n"
"  frag_semi_transparent = semi_transparent;\n"
"}\n"
};
