static const char *command_fragment = {
"#version 330 core\n"

"uniform sampler2D fb_texture;\n"

// Scaling to apply to the dither pattern
"uniform uint dither_scaling;\n"
// 0: Only draw opaque pixels, 1: only draw semi-transparent pixels
"uniform uint draw_semi_transparent;\n"

"uniform uint texture_flt;\n"

"in vec3 frag_shading_color;\n"
// Texture page: base offset for texture lookup.
"flat in uvec2 frag_texture_page;\n"
// Texel coordinates within the page. Interpolated by OpenGL.
"in vec2 frag_texture_coord;\n"
// Clut coordinates in VRAM
"flat in uvec2 frag_clut;\n"
// 0: no texture, 1: raw-texture, 2: blended
"flat in uint frag_texture_blend_mode;\n"
// 0: 16bpp (no clut), 1: 8bpp, 2: 4bpp
"flat in uint frag_depth_shift;\n"
// 0: No dithering, 1: dithering enabled
"flat in uint frag_dither;\n"
// 0: Opaque primitive, 1: semi-transparent primitive
"flat in uint frag_semi_transparent;\n"

"out vec4 frag_color;\n"

"const uint BLEND_MODE_NO_TEXTURE    = 0U;\n"
"const uint BLEND_MODE_RAW_TEXTURE   = 1U;\n"
"const uint BLEND_MODE_TEXTURE_BLEND = 2U;\n"

"const uint FILTER_MODE_NEAREST      = 0U;\n"
"const uint FILTER_MODE_3POINT       = 1U;\n"

// Read a pixel in VRAM
"vec4 vram_get_pixel(int x, int y) {"
"  return texelFetch(fb_texture, ivec2(x, y), 0);\n"
"}\n"

// Take a normalized color and convert it into a 16bit 1555 ABGR
// integer in the format used internally by the Playstation GPU.
"uint rebuild_psx_color(vec4 color) {"
"  uint a = uint(floor(color.a + 0.5));\n"
"  uint r = uint(floor(color.r * 31. + 0.5));\n"
"  uint g = uint(floor(color.g * 31. + 0.5));\n"
"  uint b = uint(floor(color.b * 31. + 0.5));\n"

"  return (a << 15) | (b << 10) | (g << 5) | r;\n"
"}\n"

// Texture color 0x0000 is special in the Playstation GPU, it denotes
// a fully transparent texel (even for opaque draw commands). If you
// want black you have to use an opaque draw command and use `0x8000`
// instead.\n"
"bool is_transparent(vec4 texel) {"
"  return rebuild_psx_color(texel) == 0U;\n"
"}\n"

// PlayStation dithering pattern. The offset is selected based on the
// pixel position in VRAM, by blocks of 4x4 pixels. The value is added
// to the 8bit color components before they're truncated to 5 bits.
//// TODO: r5 - There might be extra line breaks in here
"const int dither_pattern[16] ="
"  int[16](-4,  0, -3,  1,"
"           2, -2,  3, -1,"
"          -3,  1, -4,  0,"
"           3, -1,  2, -2);" "\n"

"vec4 sample_texel(vec2 coords) {"
    // Number of texel per VRAM 16bit "pixel" for the current depth
"    uint pix_per_hw = 1U << frag_depth_shift;\n"

    // 8 and 4bpp textures contain several texels per 16bit VRAM
    // "pixel"\n"
"    float tex_x_float = coords.x / float(pix_per_hw);\n"

    // Texture pages are limited to 256x256 pixels\n"
"    int tex_x = int(tex_x_float) & 0xff;\n"
"    int tex_y = int(coords.y) & 0xff;\n"

"    tex_x += int(frag_texture_page.x);\n"
"    tex_y += int(frag_texture_page.y);\n"

"    vec4 texel = vram_get_pixel(tex_x, tex_y);\n"

"    if (frag_depth_shift > 0U) {\n"
      // 8 and 4bpp textures are paletted so we need to lookup the
      // real color in the CLUT\n"

"      uint icolor = rebuild_psx_color(texel);\n"

      // A little bitwise magic to get the index in the CLUT. 4bpp
      // textures have 4 texels per VRAM "pixel", 8bpp have 2. We need
      // to shift the current color to find the proper part of the
      // halfword and then mask away the rest.

      // Bits per pixel (4 or 8)
"      uint bpp = 16U >> frag_depth_shift;\n"

      // 0xf for 4bpp, 0xff for 8bpp
"      uint mask = ((1U << bpp) - 1U);\n"

      // 0...3 for 4bpp, 1...2 for 8bpp
"      uint align = uint(fract(tex_x_float) * pix_per_hw);\n"

      // 0, 4, 8 or 12 for 4bpp, 0 or 8 for 8bpp
"      uint shift = (align * bpp);\n"

      // Finally we have the index in the CLUT
"      uint index = (icolor >> shift) & mask;\n"

"      int clut_x = int(frag_clut.x + index);\n"
"      int clut_y = int(frag_clut.y);\n"

      // Look up the real color for the texel in the CLUT\n"
"      texel = vram_get_pixel(clut_x, clut_y);\n"
"    }\n"
"return texel;\n"
"}\n"

    // 3-point filtering
"vec4 get_texel_3point(vec4 texel_00, float u_frac, float v_frac) {"
"vec4 texel_10 = sample_texel(vec2(frag_texture_coord.x + 1, frag_texture_coord.y + 0));\n"
"vec4 texel_01 = sample_texel(vec2(frag_texture_coord.x + 0, frag_texture_coord.y + 1));\n"
"if (is_transparent(texel_10)) {\n"
"texel_10 = texel_00;\n"
"}\n"
"if (is_transparent(texel_01)) {\n"
"texel_01 = texel_00;\n"
"}\n"
"vec4 texel = texel_00 + u_frac * (texel_10 - texel_00) + v_frac * (texel_01 - texel_00);\n"
"return texel;\n"
"}\n"

"vec4 get_texel(vec4 texel_00, float u_frac, float v_frac) {"
"return texel_00;\n"
"}\n"

"void main() {\n"

"  vec4 color;\n"

"  if (frag_texture_blend_mode == BLEND_MODE_NO_TEXTURE) {\n"
"    color = vec4(frag_shading_color, 0.);\n"
"  } else {\n"
    // Look up texture
    //
    "float u_frac = 0.0;\n"
    "float v_frac = 0.0;\n"
    "vec4 texel_00;\n"

    "if (texture_flt == FILTER_MODE_3POINT) {\n"
      "float u_frac = fract(frag_texture_coord.x);\n"
      "float v_frac = fract(frag_texture_coord.y);\n"
      "if (u_frac + v_frac < 1.0) {\n"
      // Use bottom-left
      "texel_00 = sample_texel(vec2(frag_texture_coord.x + 0, frag_texture_coord.y + 0));\n"
      "} else {\n"
      // Use top-right
      "texel_00 = sample_texel(vec2(frag_texture_coord.x + 1, frag_texture_coord.y + 1));\n"
      "float tmp = 1 - v_frac;\n"
      "v_frac = 1 - u_frac;\n"
      "u_frac = tmp;\n"
    "}\n"
    "} else {\n"
      "texel_00 = sample_texel(vec2(frag_texture_coord.x + 0, frag_texture_coord.y + 0));\n"
    "}\n"

    // texel color 0x0000 is always fully transparent (even for opaque
    // draw commands)
"    if (is_transparent(texel_00)) {\n"
      // Fully transparent texel, discard
"      discard;\n"
"    }\n"

    "vec4 texel;\n"
    "if (texture_flt == FILTER_MODE_3POINT) {\n"
      "texel = get_texel_3point(texel_00, u_frac, v_frac);\n"
    "} else {\n"
      "texel = get_texel(texel_00, u_frac, v_frac);\n"
      "texel += vec4(1.0, 0.0, 1.0, 1.0);\n"
    "}\n"

    // Bit 15 (stored in the alpha) is used as a flag for
    // semi-transparency, but only if this is a semi-transparent draw
    // command
"    uint transparency_flag = uint(floor(texel.a + 0.5));\n"

"    uint is_texel_semi_transparent = transparency_flag & frag_semi_transparent;\n"

"    if (is_texel_semi_transparent != draw_semi_transparent) {\n"
      // We're not drawing those texels in this pass, discard\n"
"      discard;\n"
"    }\n"

"    if (frag_texture_blend_mode == BLEND_MODE_RAW_TEXTURE) {\n"
"      color = texel;\n"
"    } else /* BLEND_MODE_TEXTURE_BLEND */ {\n"
      // Blend the texel with the shading color. `frag_shading_color`
      // is multiplied by two so that it can be used to darken or
      // lighten the texture as needed. The result of the
      // multiplication should be saturated to 1.0 (0xff) but I think
      // OpenGL will take care of that since the output buffer holds
      // integers. The alpha/mask bit bit is taken directly from the
      // texture however.
"      color = vec4(frag_shading_color * 2. * texel.rgb, texel.a);\n"
"    }\n"
"  }\n"

  // 4x4 dithering pattern scaled by `dither_scaling`
"  uint x_dither = (uint(gl_FragCoord.x) / dither_scaling) & 3U;\n"
"  uint y_dither = (uint(gl_FragCoord.y) / dither_scaling) & 3U;\n"

  // The multiplication by `frag_dither` will result in
  // `dither_offset` being 0 if dithering is disabled
"  int dither_offset =\n"
"    dither_pattern[y_dither * 4U + x_dither] * int(frag_dither);\n"

"  float dither = float(dither_offset) / 255.;\n"

"  frag_color = color + vec4(dither, dither, dither, 0.);\n"
"}\n"
};
