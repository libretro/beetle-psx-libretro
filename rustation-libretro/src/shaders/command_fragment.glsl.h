#include "shaders_common.h"

static const char *command_fragment = GLSL(
uniform sampler2D fb_texture;

// Scaling to apply to the dither pattern
uniform uint dither_scaling;

// 0: Only draw opaque pixels, 1: only draw semi-transparent pixels
uniform uint draw_semi_transparent;
uniform uint texture_flt;

//uniform uint mask_setor;
//uniform uint mask_evaland;

in vec3 frag_shading_color;
// Texture page: base offset for texture lookup.
flat in uvec2 frag_texture_page;
// Texel coordinates within the page. Interpolated by OpenGL.
in vec2 frag_texture_coord;
// Clut coordinates in VRAM
flat in uvec2 frag_clut;
// 0: no texture, 1: raw-texture, 2: blended
flat in uint frag_texture_blend_mode;
// 0: 16bpp (no clut), 1: 8bpp, 2: 4bpp
flat in uint frag_depth_shift;
// 0: No dithering, 1: dithering enabled
flat in uint frag_dither;
// 0: Opaque primitive, 1: semi-transparent primitive
flat in uint frag_semi_transparent;
// Texture window: [ X mask, X or, Y mask, Y or ]
flat in uvec4 frag_texture_window;

out vec4 frag_color;

const uint BLEND_MODE_NO_TEXTURE    = 0U;
const uint BLEND_MODE_RAW_TEXTURE   = 1U;
const uint BLEND_MODE_TEXTURE_BLEND = 2U;

const uint FILTER_MODE_NEAREST      = 0U;
const uint FILTER_MODE_SABR         = 1U;

// Read a pixel in VRAM
vec4 vram_get_pixel(uint x, uint y) {
  x = (x & 0x3ffU);
  y = (y & 0x1ffU);

  return texelFetch(fb_texture, ivec2(x, y), 0);
}

// Take a normalized color and convert it into a 16bit 1555 ABGR
// integer in the format used internally by the Playstation GPU.
uint rebuild_psx_color(vec4 color) {
  uint a = uint(floor(color.a + 0.5));
  uint r = uint(floor(color.r * 31. + 0.5));
  uint g = uint(floor(color.g * 31. + 0.5));
  uint b = uint(floor(color.b * 31. + 0.5));

  return (a << 15) | (b << 10) | (g << 5) | r;
}

// Texture color 0x0000 is special in the Playstation GPU, it denotes
// a fully transparent texel (even for opaque draw commands). If you
// want black you have to use an opaque draw command and use `0x8000`
// instead.
bool is_transparent(vec4 texel) {
  return rebuild_psx_color(texel) == 0U;
}

// PlayStation dithering pattern. The offset is selected based on the
// pixel position in VRAM, by blocks of 4x4 pixels. The value is added
// to the 8bit color components before they're truncated to 5 bits.
//// TODO: r5 - There might be extra line breaks in here
const int dither_pattern[16] =
  int[16](-4,  0, -3,  1,
           2, -2,  3, -1,
          -3,  1, -4,  0,
           3, -1,  2, -2);

vec4 sample_texel(vec2 coords) {
   // Number of texel per VRAM 16bit "pixel" for the current depth
   uint pix_per_hw = 1U << frag_depth_shift;

   // Texture pages are limited to 256x256 pixels
   uint tex_x = uint(coords.x) & 0xffU;
   uint tex_y = uint(coords.y) & 0xffU;

   // Texture window adjustments
   tex_x = (tex_x & frag_texture_window[0]) | frag_texture_window[1];
   tex_y = (tex_y & frag_texture_window[2]) | frag_texture_window[3];

   // Adjust x coordinate based on the texel color depth.
   uint tex_x_pix = tex_x / pix_per_hw;

   tex_x_pix += frag_texture_page.x;
   tex_y += frag_texture_page.y;

   vec4 texel = vram_get_pixel(tex_x_pix, tex_y);

   if (frag_depth_shift > 0U) {
      // 8 and 4bpp textures are paletted so we need to lookup the
      // real color in the CLUT

      // 8 and 4bpp textures contain several texels per 16bit VRAM
      // "pixel"
      float tex_x_float = coords.x / float(pix_per_hw);

      uint icolor = rebuild_psx_color(texel);

      // A little bitwise magic to get the index in the CLUT. 4bpp
      // textures have 4 texels per VRAM "pixel", 8bpp have 2. We need
      // to shift the current color to find the proper part of the
      // halfword and then mask away the rest.

      // Bits per pixel (4 or 8)
      uint bpp = 16U >> frag_depth_shift;

      // 0xf for 4bpp, 0xff for 8bpp
      uint mask = ((1U << bpp) - 1U);

      // 0...3 for 4bpp, 0...1 for 8bpp
      uint align = tex_x & ((1U << frag_depth_shift) - 1U);

      // 0, 4, 8 or 12 for 4bpp, 0 or 8 for 8bpp
      uint shift = (align * bpp);

      // Finally we have the index in the CLUT
      uint index = (icolor >> shift) & mask;

      uint clut_x = frag_clut.x + index;
      uint clut_y = frag_clut.y;

      // Look up the real color for the texel in the CLUT
      texel = vram_get_pixel(clut_x, clut_y);
   }
   return texel;
}

// constants and functions for sabr
/*
	Inequation coefficients for interpolation
Equations are in the form: Ay + Bx = C
45, 30, and 60 denote the angle from x each line the cooeficient variable set builds
*/
const vec4 Ai  = vec4( 1.0, -1.0, -1.0,  1.0);
const vec4 B45 = vec4( 1.0,  1.0, -1.0, -1.0);
const vec4 C45 = vec4( 1.5,  0.5, -0.5,  0.5);
const vec4 B30 = vec4( 0.5,  2.0, -0.5, -2.0);
const vec4 C30 = vec4( 1.0,  1.0, -0.5,  0.0);
const vec4 B60 = vec4( 2.0,  0.5, -2.0, -0.5);
const vec4 C60 = vec4( 2.0,  0.0, -1.0,  0.5);

const vec4 M45 = vec4(0.4, 0.4, 0.4, 0.4);
const vec4 M30 = vec4(0.2, 0.4, 0.2, 0.4);
const vec4 M60 = M30.yxwz;
const vec4 Mshift = vec4(0.2);

// Coefficient for weighted edge detection
const float coef = 2.0;
// Threshold for if luminance values are "equal"
const vec4 threshold = vec4(0.32);

// Conversion from RGB to Luminance (from GIMP)
const vec3 lum = vec3(0.21, 0.72, 0.07);

// Performs same logic operation as && for vectors
bvec4 _and_(bvec4 A, bvec4 B) {
	return bvec4(A.x && B.x, A.y && B.y, A.z && B.z, A.w && B.w);
}

// Performs same logic operation as || for vectors
bvec4 _or_(bvec4 A, bvec4 B) {
	return bvec4(A.x || B.x, A.y || B.y, A.z || B.z, A.w || B.w);
}

// Converts 4 3-color vectors into 1 4-value luminance vector
vec4 lum_to(vec3 v0, vec3 v1, vec3 v2, vec3 v3) {
	return vec4(dot(lum, v0), dot(lum, v1), dot(lum, v2), dot(lum, v3));
}

// Gets the difference between 2 4-value luminance vectors
vec4 lum_df(vec4 A, vec4 B) {
	return abs(A - B);
}

// Determines if 2 4-value luminance vectors are "equal" based on threshold
bvec4 lum_eq(vec4 A, vec4 B) {
	return lessThan(lum_df(A, B), threshold);
}

vec4 lum_wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
	return lum_df(a, b) + lum_df(a, c) + lum_df(d, e) + lum_df(d, f) + 4.0 * lum_df(g, h);
}

// Gets the difference between 2 3-value rgb colors
float c_df(vec3 c1, vec3 c2) {
	vec3 df = abs(c1 - c2);
	return df.r + df.g + df.b;
}

//sabr
vec4 get_texel_sabr()
{
	vec2 tc = vec2(frag_texture_coord.x, frag_texture_coord.y);// * vec2(1.00001);
	vec4 xyp_1_2_3    = tc.xxxy + vec4(-1.,  0., 1., -2.);
	vec4 xyp_6_7_8    = tc.xxxy + vec4(-1.,  0., 1., -1.);
	vec4 xyp_11_12_13 = tc.xxxy + vec4(-1.,  0., 1.,  0.);
	vec4 xyp_16_17_18 = tc.xxxy + vec4(-1.,  0., 1.,  1.);
	vec4 xyp_21_22_23 = tc.xxxy + vec4(-1.,  0., 1.,  2.);
	vec4 xyp_5_10_15  = tc.xyyy + vec4(-2., -1., 0.,  1.);
	vec4 xyp_9_14_9   = tc.xyyy + vec4( 2., -1., 0.,  1.);

/*
Mask for algorhithm
+-----+-----+-----+-----+-----+
|     |  1  |  2  |  3  |     |
+-----+-----+-----+-----+-----+
|  5  |  6  |  7  |  8  |  9  |
+-----+-----+-----+-----+-----+
| 10  | 11  | 12  | 13  | 14  |
+-----+-----+-----+-----+-----+
| 15  | 16  | 17  | 18  | 19  |
+-----+-----+-----+-----+-----+
|     | 21  | 22  | 23  |     |
+-----+-----+-----+-----+-----+
	*/
	// Get mask values by performing texture lookup with the uniform sampler
	vec3 P1  = sample_texel(xyp_1_2_3.xw   ).rgb;
	vec3 P2  = sample_texel(xyp_1_2_3.yw   ).rgb;
	vec3 P3  = sample_texel(xyp_1_2_3.zw   ).rgb;

	vec3 P6  = sample_texel(xyp_6_7_8.xw   ).rgb;
	vec3 P7  = sample_texel(xyp_6_7_8.yw   ).rgb;
	vec3 P8  = sample_texel(xyp_6_7_8.zw   ).rgb;

	vec3 P11 = sample_texel(xyp_11_12_13.xw).rgb;
	vec3 P12 = sample_texel(xyp_11_12_13.yw).rgb;
	vec3 P13 = sample_texel(xyp_11_12_13.zw).rgb;

	vec3 P16 = sample_texel(xyp_16_17_18.xw).rgb;
	vec3 P17 = sample_texel(xyp_16_17_18.yw).rgb;
	vec3 P18 = sample_texel(xyp_16_17_18.zw).rgb;

	vec3 P21 = sample_texel(xyp_21_22_23.xw).rgb;
	vec3 P22 = sample_texel(xyp_21_22_23.yw).rgb;
	vec3 P23 = sample_texel(xyp_21_22_23.zw).rgb;

	vec3 P5  = sample_texel(xyp_5_10_15.xy ).rgb;
	vec3 P10 = sample_texel(xyp_5_10_15.xz ).rgb;
	vec3 P15 = sample_texel(xyp_5_10_15.xw ).rgb;

	vec3 P9  = sample_texel(xyp_9_14_9.xy  ).rgb;
	vec3 P14 = sample_texel(xyp_9_14_9.xz  ).rgb;
	vec3 P19 = sample_texel(xyp_9_14_9.xw  ).rgb;

	// Store luminance values of each point in groups of 4
	// so that we may operate on all four corners at once
	vec4 p7  = lum_to(P7,  P11, P17, P13);
	vec4 p8  = lum_to(P8,  P6,  P16, P18);
	vec4 p11 = p7.yzwx;                      // P11, P17, P13, P7
	vec4 p12 = lum_to(P12, P12, P12, P12);
	vec4 p13 = p7.wxyz;                      // P13, P7,  P11, P17
	vec4 p14 = lum_to(P14, P2,  P10, P22);
	vec4 p16 = p8.zwxy;                      // P16, P18, P8,  P6
	vec4 p17 = p7.zwxy;                      // P17, P13, P7,  P11
	vec4 p18 = p8.wxyz;                      // P18, P8,  P6,  P16
	vec4 p19 = lum_to(P19, P3,  P5,  P21);
	vec4 p22 = p14.wxyz;                     // P22, P14, P2,  P10
	vec4 p23 = lum_to(P23, P9,  P1,  P15);

	// Scale current texel coordinate to [0..1]
	vec2 fp = fract(tc);

	// Determine amount of "smoothing" or mixing that could be done on texel corners
	vec4 ma45 = smoothstep(C45 - M45, C45 + M45, Ai * fp.y + B45 * fp.x);
	vec4 ma30 = smoothstep(C30 - M30, C30 + M30, Ai * fp.y + B30 * fp.x);
	vec4 ma60 = smoothstep(C60 - M60, C60 + M60, Ai * fp.y + B60 * fp.x);
	vec4 marn = smoothstep(C45 - M45 + Mshift, C45 + M45 + Mshift, Ai * fp.y + B45 * fp.x);

	// Perform edge weight calculations
	vec4 e45   = lum_wd(p12, p8, p16, p18, p22, p14, p17, p13);
	vec4 econt = lum_wd(p17, p11, p23, p13, p7, p19, p12, p18);
	vec4 e30   = lum_df(p13, p16);
	vec4 e60   = lum_df(p8, p17);

	// Calculate rule results for interpolation
	bvec4 r45_1   = _and_(notEqual(p12, p13), notEqual(p12, p17));
	bvec4 r45_2   = _and_(not(lum_eq(p13, p7)), not(lum_eq(p13, p8)));
	bvec4 r45_3   = _and_(not(lum_eq(p17, p11)), not(lum_eq(p17, p16)));
	bvec4 r45_4_1 = _and_(not(lum_eq(p13, p14)), not(lum_eq(p13, p19)));
	bvec4 r45_4_2 = _and_(not(lum_eq(p17, p22)), not(lum_eq(p17, p23)));
	bvec4 r45_4   = _and_(lum_eq(p12, p18), _or_(r45_4_1, r45_4_2));
	bvec4 r45_5   = _or_(lum_eq(p12, p16), lum_eq(p12, p8));
	bvec4 r45     = _and_(r45_1, _or_(_or_(_or_(r45_2, r45_3), r45_4), r45_5));
	bvec4 r30 = _and_(notEqual(p12, p16), notEqual(p11, p16));
	bvec4 r60 = _and_(notEqual(p12, p8), notEqual(p7, p8));

	// Combine rules with edge weights
	bvec4 edr45 = _and_(lessThan(e45, econt), r45);
	bvec4 edrrn = lessThanEqual(e45, econt);
	bvec4 edr30 = _and_(lessThanEqual(coef * e30, e60), r30);
	bvec4 edr60 = _and_(lessThanEqual(coef * e60, e30), r60);

	// Finalize interpolation rules and cast to float (0.0 for false, 1.0 for true)
	vec4 final45 = vec4(_and_(_and_(not(edr30), not(edr60)), edr45));
	vec4 final30 = vec4(_and_(_and_(edr45, not(edr60)), edr30));
	vec4 final60 = vec4(_and_(_and_(edr45, not(edr30)), edr60));
	vec4 final36 = vec4(_and_(_and_(edr60, edr30), edr45));
	vec4 finalrn = vec4(_and_(not(edr45), edrrn));

	// Determine the color to mix with for each corner
	vec4 px = step(lum_df(p12, p17), lum_df(p12, p13));

	// Determine the mix amounts by combining the final rule result and corresponding
	// mix amount for the rule in each corner
	vec4 mac = final36 * max(ma30, ma60) + final30 * ma30 + final60 * ma60 + final45 * ma45 + finalrn * marn;

/*
Calculate the resulting color by traversing clockwise and counter-clockwise around
the corners of the texel
Finally choose the result that has the largest difference from the texel's original
color
*/
	vec3 res1 = P12;
	res1 = mix(res1, mix(P13, P17, px.x), mac.x);
	res1 = mix(res1, mix(P7, P13, px.y), mac.y);
	res1 = mix(res1, mix(P11, P7, px.z), mac.z);
	res1 = mix(res1, mix(P17, P11, px.w), mac.w);

	vec3 res2 = P12;
	res2 = mix(res2, mix(P17, P11, px.w), mac.w);
	res2 = mix(res2, mix(P11, P7, px.z), mac.z);
	res2 = mix(res2, mix(P7, P13, px.y), mac.y);
	res2 = mix(res2, mix(P13, P17, px.x), mac.x);

	float texel_alpha = sample_texel(tc).w;

   vec4 texel = vec4(mix(res1, res2, step(c_df(P12, res1), c_df(P12, res2))), texel_alpha);

   return texel;
}

void main() {
   vec4 color;

      if (frag_texture_blend_mode == BLEND_MODE_NO_TEXTURE)
      {
         color = vec4(frag_shading_color, 0.);
      }
      else
      {
         vec4 texel;

         if (texture_flt == FILTER_MODE_SABR)
         {
            texel = get_texel_sabr();
         }
         else
         {
            texel = sample_texel(vec2(frag_texture_coord.x,
                     frag_texture_coord.y));
         }

	 // texel color 0x0000 is always fully transparent (even for opaque
         // draw commands)
         if (is_transparent(texel)) {
	   // Fully transparent texel, discard
	   discard;
         }

         // Bit 15 (stored in the alpha) is used as a flag for
         // semi-transparency, but only if this is a semi-transparent draw
         // command
         uint transparency_flag = uint(floor(texel.a + 0.5));

         uint is_texel_semi_transparent = transparency_flag & frag_semi_transparent;

         if (is_texel_semi_transparent != draw_semi_transparent) {
            // We're not drawing those texels in this pass, discard
            discard;
         }

         if (frag_texture_blend_mode == BLEND_MODE_RAW_TEXTURE) {
            color = texel;
         } else /* BLEND_MODE_TEXTURE_BLEND */ {
            // Blend the texel with the shading color. `frag_shading_color`
            // is multiplied by two so that it can be used to darken or
            // lighten the texture as needed. The result of the
            // multiplication should be saturated to 1.0 (0xff) but I think
            // OpenGL will take care of that since the output buffer holds
            // integers. The alpha/mask bit bit is taken directly from the
            // texture however.
            color = vec4(frag_shading_color * 2. * texel.rgb, texel.a);
         }
      }

   // 4x4 dithering pattern scaled by `dither_scaling`
   uint x_dither = (uint(gl_FragCoord.x) / dither_scaling) & 3U;
   uint y_dither = (uint(gl_FragCoord.y) / dither_scaling) & 3U;

   // The multiplication by `frag_dither` will result in
   // `dither_offset` being 0 if dithering is disabled
   int dither_offset =
      dither_pattern[y_dither * 4U + x_dither] * int(frag_dither);

   float dither = float(dither_offset) / 255.;

   frag_color = color + vec4(dither, dither, dither, 0.);
}
);
