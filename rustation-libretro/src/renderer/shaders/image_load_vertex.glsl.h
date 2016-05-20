static const char *image_load_vertex = {
"#version 330 core\n"

// Vertex shader for uploading textures from the VRAM texture buffer
// into the output framebuffer

// The position in the input and output framebuffer are the same
"in uvec2 position;\n"

"out vec2 frag_fb_coord;\n"

"void main() {\n"
  // Convert VRAM position into OpenGL coordinates
"  float xpos = (float(position.x) / 512) - 1.0;\n"
"  float ypos = (float(position.y) / 256) - 1.0;\n"

"  gl_Position.xyzw = vec4(xpos, ypos, 0.0, 1.0);\n"

  // frag_fb_coord will remain in PlayStation fb coordinates for
  // texelFetch
"  frag_fb_coord = vec2(position);\n"
"}\n"
};
