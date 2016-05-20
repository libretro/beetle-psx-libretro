static const char *image_load_fragment = {
"#version 330 core\n"

"uniform sampler2D fb_texture;\n"

"in vec2 frag_fb_coord;\n"

"out vec4 frag_color;\n"

// Read a pixel in VRAM
"vec4 vram_get_pixel(int x, int y) {\n"
"  return texelFetch(fb_texture, ivec2(x, y), 0);\n"
"}\n"

"void main() {\n"
"  frag_color = vram_get_pixel(int(frag_fb_coord.x), int(frag_fb_coord.y));\n"
"}\n"
};
