static const char *output_fragment = {
"#version 330 core\n"

// We're sampling from the internal framebuffer texture
"uniform sampler2D fb;\n"
// Framebuffer sampling: 0: Normal 16bpp mode, 1: Use 24bpp mode
"uniform int depth_24bpp;\n"
// Internal resolution upscaling factor. Necessary for proper 24bpp
// display since we need to know how the pixels are laid out in RAM.
"uniform uint internal_upscaling;\n"

"in vec2 frag_fb_coord;\n"

"out vec4 frag_color;\n"

// Take a normalized color and convert it into a 16bit 1555 ABGR
// integer in the format used internally by the Playstation GPU.
"int rebuild_color(vec4 color) {\n"
"  int a = int(floor(color.a + 0.5));\n"
"  int r = int(floor(color.r * 31. + 0.5));\n"
"  int g = int(floor(color.g * 31. + 0.5));\n"
"  int b = int(floor(color.b * 31. + 0.5));\n"

"  return (a << 15) | (b << 10) | (g << 5) | r;\n"
"}\n"

"void main() {\n"
"  vec3 color;\n"

"  if (depth_24bpp == 0) {\n"
    // Use the regular 16bpp mode, fetch directly from the framebuffer
    // texture. The alpha/mask bit is ignored here.
"    color = texture(fb, frag_fb_coord).rgb;\n"
"  } else {\n"
    // In this mode we have to interpret the framebuffer as containing
    // 24bit RGB values instead of the usual 16bits 1555.
"    ivec2 fb_size = textureSize(fb, 0);\n"

"    int x_24 = int(frag_fb_coord.x * float(fb_size.x));\n"
"    int y = int(frag_fb_coord.y * float(fb_size.y));\n"

"    int x_native = x_24 / int(internal_upscaling);\n"

"    x_24 = x_native * int(internal_upscaling);\n"

    // The 24bit color is stored over two 16bit pixels, convert the
    // coordinates
"    int x0_16 = (x_24 * 3) / 2;\n"

    // Move on to the next pixel at native resolution
"    int x1_16 = x0_16 + int(internal_upscaling);\n"

"    int col0 = rebuild_color(texelFetch(fb, ivec2(x0_16, y), 0));\n"
"    int col1 = rebuild_color(texelFetch(fb, ivec2(x1_16, y), 0));\n"

"    int col = (col1 << 16) | col0;\n"

    // If we're drawing an odd 24 bit pixel we're starting in the
    // middle of a 16bit cell so we need to adjust accordingly.
"    col >>= 8 * (x_native & 1);\n"

    // Finally we can extract and normalize the 24bit pixel
"    float b = float((col >> 16) & 0xff) / 255.;\n"
"    float g = float((col >> 8) & 0xff) / 255.;\n"
"    float r = float(col & 0xff) / 255.;\n"

"    color = vec3(r, g, b);\n"
"  }\n"

"  frag_color = vec4(color, 1.0);\n"
"}\n"
};
