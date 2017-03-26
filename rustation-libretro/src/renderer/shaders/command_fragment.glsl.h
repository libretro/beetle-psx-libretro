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
const uint FILTER_MODE_3POINT       = 1U;
const uint FILTER_MODE_SABR         = 2U;
const uint FILTER_MODE_6XBRZ        = 3U;
const uint FILTER_MODE_BILINEAR     = 4U;

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
const  vec4 Ai  = vec4( 1.0, -1.0, -1.0,  1.0);
const  vec4 B45 = vec4( 1.0,  1.0, -1.0, -1.0);
const  vec4 C45 = vec4( 1.5,  0.5, -0.5,  0.5);
const  vec4 B30 = vec4( 0.5,  2.0, -0.5, -2.0);
const  vec4 C30 = vec4( 1.0,  1.0, -0.5,  0.0);
const  vec4 B60 = vec4( 2.0,  0.5, -2.0, -0.5);
const  vec4 C60 = vec4( 2.0,  0.0, -1.0,  0.5);

const  vec4 M45 = vec4(0.4, 0.4, 0.4, 0.4);
const  vec4 M30 = vec4(0.2, 0.4, 0.2, 0.4);
const  vec4 M60 = M30.yxwz;
const  vec4 Mshift = vec4(0.2, 0.2, 0.2, 0.2);

const  float coef = 2.0;

const  vec4 threshold = vec4(0.32, 0.32, 0.32, 0.32);

const  vec3 lum = vec3(0.21, 0.72, 0.07);

vec4 lum_to(vec3 v0, vec3 v1, vec3 v2, vec3 v3) {
	return vec4(dot(lum, v0), dot(lum, v1), dot(lum, v2), dot(lum, v3));
}

vec4 lum_df(vec4 A, vec4 B) {
	return abs(A - B);
}

bvec4 lum_eq(vec4 A, vec4 B) {
	return lessThan(lum_df(A, B) , vec4(threshold));
}

vec4 lum_wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h) {
	return lum_df(a, b) + lum_df(a, c) + lum_df(d, e) + lum_df(d, f) + 4.0 * lum_df(g, h);
}

float c_df(vec3 c1, vec3 c2) {
	vec3 df = abs(c1 - c2);
	return df.r + df.g + df.b;
}

// 3-point filtering
vec4 get_texel_3point()
{

  float x = frag_texture_coord.x;
  float y = frag_texture_coord.y;

  float u_frac = fract(x);
  float v_frac = fract(y);

  vec4 texel_00;

  if (u_frac + v_frac < 1.0) {
    // Use bottom-left
    texel_00 = sample_texel(vec2(x + 0, y + 0));
  } else {
    // Use top-right
    texel_00 = sample_texel(vec2(x + 1, y + 1));

    float tmp = 1 - v_frac;
    v_frac = 1 - u_frac;
    u_frac = tmp;
  }

  if (is_transparent(texel_00)) {
    return texel_00;
  }

   vec4 texel_10 = sample_texel(vec2(x + 1, y + 0));
   vec4 texel_01 = sample_texel(vec2(x + 0, y + 1));

   if (is_transparent(texel_10)) {
      texel_10 = texel_00;
   }

   if (is_transparent(texel_01)) {
      texel_01 = texel_00;
   }
   vec4 texel = texel_00
     + u_frac * (texel_10 - texel_00)
     + v_frac * (texel_01 - texel_00);

   return texel;
}

//sabr
vec4 get_texel_sabr()
{
	vec2 tc = vec2(frag_texture_coord.x, frag_texture_coord.y);// * vec2(1.00001);
	vec4 xyp_1_2_3    = tc.xxxy + vec4(      -1., 0.0,   1., -2.0 * 1.);
	vec4 xyp_6_7_8    = tc.xxxy + vec4(      -1., 0.0,   1.,       -1.);
	vec4 xyp_11_12_13 = tc.xxxy + vec4(      -1., 0.0,   1.,      0.0);
	vec4 xyp_16_17_18 = tc.xxxy + vec4(      -1., 0.0,   1.,        1.);
	vec4 xyp_21_22_23 = tc.xxxy + vec4(      -1., 0.0,   1.,  2.0 * 1.);
	vec4 xyp_5_10_15  = tc.xyyy + vec4(-2.0 * 1.,  -1., 0.0,        1.);
	vec4 xyp_9_14_9   = tc.xyyy + vec4( 2.0 * 1.,  -1., 0.0,        1.);

	// Store mask values
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

// Store luminance values of each point
	vec4 p7  = lum_to(P7,  P11, P17, P13);
	vec4 p8  = lum_to(P8,  P6,  P16, P18);
	vec4 p11 = p7.yzwx;                      // P11, P17, P13, P7
	vec4 p12 = lum_to(P12, P12, P12, P12);
	vec4 p13 = p7.wxyz;                      // P13, P7,  P11, P17
	vec4 p14 = lum_to(P14, P2,  P10, P22);
	vec4 p16 = p8.zwxy;                      // P16, P18, P8,  P6
	vec4 p17 = p7.zwxy;                      // P11, P17, P13, P7
	vec4 p18 = p8.wxyz;                      // P18, P8,  P6,  P16
	vec4 p19 = lum_to(P19, P3,  P5,  P21);
	vec4 p22 = p14.wxyz;                     // P22, P14, P2,  P10
	vec4 p23 = lum_to(P23, P9,  P1,  P15);

	vec2 fp = fract(tc);

	vec4 ma45 = smoothstep(C45 - M45, C45 + M45, Ai * fp.y + B45 * fp.x);
	vec4 ma30 = smoothstep(C30 - M30, C30 + M30, Ai * fp.y + B30 * fp.x);
	vec4 ma60 = smoothstep(C60 - M60, C60 + M60, Ai * fp.y + B60 * fp.x);
	vec4 marn = smoothstep(C45 - M45 + Mshift, C45 + M45 + Mshift, Ai * fp.y + B45 * fp.x);

	vec4 e45   = lum_wd(p12, p8, p16, p18, p22, p14, p17, p13);
	vec4 econt = lum_wd(p17, p11, p23, p13, p7, p19, p12, p18);
	vec4 e30   = lum_df(p13, p16);
	vec4 e60   = lum_df(p8, p17);

    vec4 final45 = vec4(1.0);
	vec4 final30 = vec4(0.0);
	vec4 final60 = vec4(0.0);
	vec4 final36 = vec4(0.0);
	vec4 finalrn = vec4(0.0);

	vec4 px = step(lum_df(p12, p17), lum_df(p12, p13));

	vec4 mac = final36 * max(ma30, ma60) + final30 * ma30 + final60 * ma60 + final45 * ma45 + finalrn * marn;

	vec3 res1 = P12;
	res1 = mix(res1, mix(P13, P17, px.x), mac.x);
	res1 = mix(res1, mix(P7 , P13, px.y), mac.y);
	res1 = mix(res1, mix(P11, P7 , px.z), mac.z);
	res1 = mix(res1, mix(P17, P11, px.w), mac.w);

	vec3 res2 = P12;
	res2 = mix(res2, mix(P17, P11, px.w), mac.w);
	res2 = mix(res2, mix(P11, P7 , px.z), mac.z);
	res2 = mix(res2, mix(P7 , P13, px.y), mac.y);
	res2 = mix(res2, mix(P13, P17, px.x), mac.x);

	float texel_alpha = sample_texel(vec2(frag_texture_coord.x, frag_texture_coord.y)).w;

   vec4 texel = vec4(mix(res1, res2, step(c_df(P12, res1), c_df(P12, res2))), texel_alpha);

   return texel;
}

//consts and functions for 6xbrz
//#define BLEND_NONE 0
//#define BLEND_NORMAL 1
//#define BLEND_DOMINANT 2
//#define LUMINANCE_WEIGHT 1.0
//#define EQUAL_COLOR_TOLERANCE 30.0/255.0
//#define STEEP_DIRECTION_THRESHOLD 2.2
//#define DOMINANT_DIRECTION_THRESHOLD 3.6

const float  one_sixth = 1.0 / 6.0;
const float  two_sixth = 2.0 / 6.0;
const float four_sixth = 4.0 / 6.0;
const float five_sixth = 5.0 / 6.0;

float reduce(const vec3 color)
{
	return dot(color, vec3(65536.0, 256.0, 1.0));
}

float DistYCbCr(const vec3 pixA, const vec3 pixB)
{
	const vec3 w = vec3(0.2627, 0.6780, 0.0593);
	const float scaleB = 0.5 / (1.0 - w.b);
	const float scaleR = 0.5 / (1.0 - w.r);
	vec3 diff = pixA - pixB;
	float Y = dot(diff, w);
	float Cb = scaleB * (diff.b - Y);
	float Cr = scaleR * (diff.r - Y);

	return sqrt( ((1.0 * Y) * (1.0 * Y)) + (Cb * Cb) + (Cr * Cr) );
}

bool IsPixEqual(const vec3 pixA, const vec3 pixB)
{
	return (DistYCbCr(pixA, pixB) < 0.117647059);
}

bool IsBlendingNeeded(const ivec4 blend)
{
	return any(notEqual(blend, ivec4(0)));
}

//6xbrz
vec4 get_texel_6xbrz()
{
    vec2 vTexCoord = frag_texture_coord.xy;
	vec4 t1 = vTexCoord.xxxy + vec4( -1., 0.0, 1.,-2.0); // A1 B1 C1
	vec4 t2 = vTexCoord.xxxy + vec4( -1., 0.0, 1., -1.);    //  A  B  C
	vec4 t3 = vTexCoord.xxxy + vec4( -1., 0.0, 1., 0.0);    //  D  E  F
	vec4 t4 = vTexCoord.xxxy + vec4( -1., 0.0, 1., 1.);     //  G  H  I
	vec4 t5 = vTexCoord.xxxy + vec4( -1., 0.0, 1., 2.0); // G5 H5 I5
	vec4 t6 = vTexCoord.xyyy + vec4(-2.0,-1., 0.0, 1.);  // A0 D0 G0
	vec4 t7 = vTexCoord.xyyy + vec4( 2.0,-1., 0.0, 1.);  // C4 F4 I4

	vec2 f = fract(vTexCoord.xy);

	//---------------------------------------
	// Input Pixel Mapping:  20|21|22|23|24
	//                       19|06|07|08|09
	//                       18|05|00|01|10
	//                       17|04|03|02|11
	//                       16|15|14|13|12

	vec3 src[25];

	src[21] = sample_texel(t1.xw).rgb;
	src[22] = sample_texel(t1.yw).rgb;
	src[23] = sample_texel(t1.zw).rgb;
	src[ 6] = sample_texel(t2.xw).rgb;
	src[ 7] = sample_texel(t2.yw).rgb;
	src[ 8] = sample_texel(t2.zw).rgb;
	src[ 5] = sample_texel(t3.xw).rgb;
	src[ 0] = sample_texel(t3.yw).rgb;
	src[ 1] = sample_texel(t3.zw).rgb;
	src[ 4] = sample_texel(t4.xw).rgb;
	src[ 3] = sample_texel(t4.yw).rgb;
	src[ 2] = sample_texel(t4.zw).rgb;
	src[15] = sample_texel(t5.xw).rgb;
	src[14] = sample_texel(t5.yw).rgb;
	src[13] = sample_texel(t5.zw).rgb;
	src[19] = sample_texel(t6.xy).rgb;
	src[18] = sample_texel(t6.xz).rgb;
	src[17] = sample_texel(t6.xw).rgb;
	src[ 9] = sample_texel(t7.xy).rgb;
	src[10] = sample_texel(t7.xz).rgb;
	src[11] = sample_texel(t7.xw).rgb;

		float v[9];
		v[0] = reduce(src[0]);
		v[1] = reduce(src[1]);
		v[2] = reduce(src[2]);
		v[3] = reduce(src[3]);
		v[4] = reduce(src[4]);
		v[5] = reduce(src[5]);
		v[6] = reduce(src[6]);
		v[7] = reduce(src[7]);
		v[8] = reduce(src[8]);

		ivec4 blendResult = ivec4(0);

		// Preprocess corners
		// Pixel Tap Mapping: --|--|--|--|--
		//                    --|--|07|08|--
		//                    --|05|00|01|10
		//                    --|04|03|02|11
		//                    --|--|14|13|--
		// Corner (1, 1)
		if ( ((v[0] == v[1] && v[3] == v[2]) || (v[0] == v[3] && v[1] == v[2])) == false)
		{
			float dist_03_01 = DistYCbCr(src[ 4], src[ 0]) + DistYCbCr(src[ 0], src[ 8]) + DistYCbCr(src[14], src[ 2]) + DistYCbCr(src[ 2], src[10]) + (4.0 * DistYCbCr(src[ 3], src[ 1]));
			float dist_00_02 = DistYCbCr(src[ 5], src[ 3]) + DistYCbCr(src[ 3], src[13]) + DistYCbCr(src[ 7], src[ 1]) + DistYCbCr(src[ 1], src[11]) + (4.0 * DistYCbCr(src[ 0], src[ 2]));
			bool dominantGradient = (3.6 * dist_03_01) < dist_00_02;
			blendResult[2] = ((dist_03_01 < dist_00_02) && (v[0] != v[1]) && (v[0] != v[3])) ? ((dominantGradient) ? 2 : 1) : 0;
		}

		// Pixel Tap Mapping: --|--|--|--|--
		//                    --|06|07|--|--
		//                    18|05|00|01|--
		//                    17|04|03|02|--
		//                    --|15|14|--|--
		// Corner (0, 1)
		if ( ((v[5] == v[0] && v[4] == v[3]) || (v[5] == v[4] && v[0] == v[3])) == false)
		{
			float dist_04_00 = DistYCbCr(src[17], src[ 5]) + DistYCbCr(src[ 5], src[ 7]) + DistYCbCr(src[15], src[ 3]) + DistYCbCr(src[ 3], src[ 1]) + (4.0 * DistYCbCr(src[ 4], src[ 0]));
			float dist_05_03 = DistYCbCr(src[18], src[ 4]) + DistYCbCr(src[ 4], src[14]) + DistYCbCr(src[ 6], src[ 0]) + DistYCbCr(src[ 0], src[ 2]) + (4.0 * DistYCbCr(src[ 5], src[ 3]));
			bool dominantGradient = (3.6 * dist_05_03) < dist_04_00;
			blendResult[3] = ((dist_04_00 > dist_05_03) && (v[0] != v[5]) && (v[0] != v[3])) ? ((dominantGradient) ? 2 : 1) : 0;
		}

		// Pixel Tap Mapping: --|--|22|23|--
		//                    --|06|07|08|09
		//                    --|05|00|01|10
		//                    --|--|03|02|--
		//                    --|--|--|--|--
		// Corner (1, 0)
		if ( ((v[7] == v[8] && v[0] == v[1]) || (v[7] == v[0] && v[8] == v[1])) == false)
		{
			float dist_00_08 = DistYCbCr(src[ 5], src[ 7]) + DistYCbCr(src[ 7], src[23]) + DistYCbCr(src[ 3], src[ 1]) + DistYCbCr(src[ 1], src[ 9]) + (4.0 * DistYCbCr(src[ 0], src[ 8]));
			float dist_07_01 = DistYCbCr(src[ 6], src[ 0]) + DistYCbCr(src[ 0], src[ 2]) + DistYCbCr(src[22], src[ 8]) + DistYCbCr(src[ 8], src[10]) + (4.0 * DistYCbCr(src[ 7], src[ 1]));
			bool dominantGradient = (3.6 * dist_07_01) < dist_00_08;
			blendResult[1] = ((dist_00_08 > dist_07_01) && (v[0] != v[7]) && (v[0] != v[1])) ? ((dominantGradient) ? 2 : 1) : 0;
		}

		// Pixel Tap Mapping: --|21|22|--|--
		//                    19|06|07|08|--
		//                    18|05|00|01|--
		//                    --|04|03|--|--
		//                    --|--|--|--|--
		// Corner (0, 0)
		if ( ((v[6] == v[7] && v[5] == v[0]) || (v[6] == v[5] && v[7] == v[0])) == false)
		{
			float dist_05_07 = DistYCbCr(src[18], src[ 6]) + DistYCbCr(src[ 6], src[22]) + DistYCbCr(src[ 4], src[ 0]) + DistYCbCr(src[ 0], src[ 8]) + (4.0 * DistYCbCr(src[ 5], src[ 7]));
			float dist_06_00 = DistYCbCr(src[19], src[ 5]) + DistYCbCr(src[ 5], src[ 3]) + DistYCbCr(src[21], src[ 7]) + DistYCbCr(src[ 7], src[ 1]) + (4.0 * DistYCbCr(src[ 6], src[ 0]));
			bool dominantGradient = (3.6 * dist_05_07) < dist_06_00;
			blendResult[0] = ((dist_05_07 < dist_06_00) && (v[0] != v[5]) && (v[0] != v[7])) ? ((dominantGradient) ? 2 : 1) : 0;
		}

		vec3 dst[36];
		dst[ 0] = src[0];
		dst[ 1] = src[0];
		dst[ 2] = src[0];
		dst[ 3] = src[0];
		dst[ 4] = src[0];
		dst[ 5] = src[0];
		dst[ 6] = src[0];
		dst[ 7] = src[0];
		dst[ 8] = src[0];
		dst[ 9] = src[0];
		dst[10] = src[0];
		dst[11] = src[0];
		dst[12] = src[0];
		dst[13] = src[0];
		dst[14] = src[0];
		dst[15] = src[0];
		dst[16] = src[0];
		dst[17] = src[0];
		dst[18] = src[0];
		dst[19] = src[0];
		dst[20] = src[0];
		dst[21] = src[0];
		dst[22] = src[0];
		dst[23] = src[0];
		dst[24] = src[0];
		dst[25] = src[0];
		dst[26] = src[0];
		dst[27] = src[0];
		dst[28] = src[0];
		dst[29] = src[0];
		dst[30] = src[0];
		dst[31] = src[0];
		dst[32] = src[0];
		dst[33] = src[0];
		dst[34] = src[0];
		dst[35] = src[0];

		// Scale pixel
		if (IsBlendingNeeded(blendResult) == true)
		{
			float dist_01_04 = DistYCbCr(src[1], src[4]);
			float dist_03_08 = DistYCbCr(src[3], src[8]);
			bool haveShallowLine = (2.2 * dist_01_04 <= dist_03_08) && (v[0] != v[4]) && (v[5] != v[4]);
			bool haveSteepLine   = (2.2 * dist_03_08 <= dist_01_04) && (v[0] != v[8]) && (v[7] != v[8]);
			bool needBlend = (blendResult[2] != 0);
			bool doLineBlend = (  blendResult[2] >= 2 ||
							   ((blendResult[1] != 0 && !IsPixEqual(src[0], src[4])) ||
								 (blendResult[3] != 0 && !IsPixEqual(src[0], src[8])) ||
								 (IsPixEqual(src[4], src[3]) && IsPixEqual(src[3], src[2]) && IsPixEqual(src[2], src[1]) && IsPixEqual(src[1], src[8]) && IsPixEqual(src[0], src[2]) == false) ) == false );

			vec3 blendPix = ( DistYCbCr(src[0], src[1]) <= DistYCbCr(src[0], src[3]) ) ? src[1] : src[3];
			dst[10] = mix(dst[10], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.250 : 0.000);
			dst[11] = mix(dst[11], blendPix, (needBlend && doLineBlend) ? ((haveSteepLine) ? 0.750 : ((haveShallowLine) ? 0.250 : 0.000)) : 0.000);
			dst[12] = mix(dst[12], blendPix, (needBlend && doLineBlend) ? ((!haveShallowLine && !haveSteepLine) ? 0.500 : 1.000) : 0.000);
			dst[13] = mix(dst[13], blendPix, (needBlend && doLineBlend) ? ((haveShallowLine) ? 0.750 : ((haveSteepLine) ? 0.250 : 0.000)) : 0.000);
			dst[14] = mix(dst[14], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.250 : 0.000);
			dst[25] = mix(dst[25], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.250 : 0.000);
			dst[26] = mix(dst[26], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.750 : 0.000);
			dst[27] = mix(dst[27], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 1.000 : 0.000);
			dst[28] = mix(dst[28], blendPix, (needBlend) ? ((doLineBlend) ? ((haveSteepLine) ? 1.000 : ((haveShallowLine) ? 0.750 : 0.500)) : 0.05652034508) : 0.000);
			dst[29] = mix(dst[29], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.4236372243) : 0.000);
			dst[30] = mix(dst[30], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.9711013910) : 0.000);
			dst[31] = mix(dst[31], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.4236372243) : 0.000);
			dst[32] = mix(dst[32], blendPix, (needBlend) ? ((doLineBlend) ? ((haveShallowLine) ? 1.000 : ((haveSteepLine) ? 0.750 : 0.500)) : 0.05652034508) : 0.000);
			dst[33] = mix(dst[33], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 1.000 : 0.000);
			dst[34] = mix(dst[34], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.750 : 0.000);
			dst[35] = mix(dst[35], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.250 : 0.000);

			dist_01_04 = DistYCbCr(src[7], src[2]);
			dist_03_08 = DistYCbCr(src[1], src[6]);
			haveShallowLine = (2.2 * dist_01_04 <= dist_03_08) && (v[0] != v[2]) && (v[3] != v[2]);
			haveSteepLine   = (2.2 * dist_03_08 <= dist_01_04) && (v[0] != v[6]) && (v[5] != v[6]);
			needBlend = (blendResult[1] != 0);
			doLineBlend = (  blendResult[1] >= 2 ||
						  !((blendResult[0] != 0 && !IsPixEqual(src[0], src[2])) ||
							(blendResult[2] != 0 && !IsPixEqual(src[0], src[6])) ||
							(IsPixEqual(src[2], src[1]) && IsPixEqual(src[1], src[8]) && IsPixEqual(src[8], src[7]) && IsPixEqual(src[7], src[6]) && !IsPixEqual(src[0], src[8])) ) );

			dist_01_04 = DistYCbCr(src[7], src[2]);
			dist_03_08 = DistYCbCr(src[1], src[6]);
			haveShallowLine = (2.2 * dist_01_04 <= dist_03_08) && (v[0] != v[2]) && (v[3] != v[2]);
			haveSteepLine   = (2.2 * dist_03_08 <= dist_01_04) && (v[0] != v[6]) && (v[5] != v[6]);
			needBlend = (blendResult[1] != 0);
			doLineBlend = (  blendResult[1] >= 2 ||
						  !((blendResult[0] != 0 && !IsPixEqual(src[0], src[2])) ||
							(blendResult[2] != 0 && !IsPixEqual(src[0], src[6])) ||
							(IsPixEqual(src[2], src[1]) && IsPixEqual(src[1], src[8]) && IsPixEqual(src[8], src[7]) && IsPixEqual(src[7], src[6]) && !IsPixEqual(src[0], src[8])) ) );

			blendPix = ( DistYCbCr(src[0], src[7]) <= DistYCbCr(src[0], src[1]) ) ? src[7] : src[1];
			dst[ 7] = mix(dst[ 7], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.250 : 0.000);
			dst[ 8] = mix(dst[ 8], blendPix, (needBlend && doLineBlend) ? ((haveSteepLine) ? 0.750 : ((haveShallowLine) ? 0.250 : 0.000)) : 0.000);
			dst[ 9] = mix(dst[ 9], blendPix, (needBlend && doLineBlend) ? ((!haveShallowLine && !haveSteepLine) ? 0.500 : 1.000) : 0.000);
			dst[10] = mix(dst[10], blendPix, (needBlend && doLineBlend) ? ((haveShallowLine) ? 0.750 : ((haveSteepLine) ? 0.250 : 0.000)) : 0.000);
			dst[11] = mix(dst[11], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.250 : 0.000);
			dst[20] = mix(dst[20], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.250 : 0.000);
			dst[21] = mix(dst[21], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.750 : 0.000);
			dst[22] = mix(dst[22], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 1.000 : 0.000);
			dst[23] = mix(dst[23], blendPix, (needBlend) ? ((doLineBlend) ? ((haveSteepLine) ? 1.000 : ((haveShallowLine) ? 0.750 : 0.500)) : 0.05652034508) : 0.000);
			dst[24] = mix(dst[24], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.4236372243) : 0.000);
			dst[25] = mix(dst[25], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.9711013910) : 0.000);
			dst[26] = mix(dst[26], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.4236372243) : 0.000);
			dst[27] = mix(dst[27], blendPix, (needBlend) ? ((doLineBlend) ? ((haveShallowLine) ? 1.000 : ((haveSteepLine) ? 0.750 : 0.500)) : 0.05652034508) : 0.000);
			dst[28] = mix(dst[28], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 1.000 : 0.000);
			dst[29] = mix(dst[29], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.750 : 0.000);
			dst[30] = mix(dst[30], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.250 : 0.000);

			dist_01_04 = DistYCbCr(src[5], src[8]);
			dist_03_08 = DistYCbCr(src[7], src[4]);
			haveShallowLine = (2.2 * dist_01_04 <= dist_03_08) && (v[0] != v[8]) && (v[1] != v[8]);
			haveSteepLine   = (2.2 * dist_03_08 <= dist_01_04) && (v[0] != v[4]) && (v[3] != v[4]);
			needBlend = (blendResult[0] != 0);
			doLineBlend = (  blendResult[0] >= 2 ||
						  !((blendResult[3] != 0 && !IsPixEqual(src[0], src[8])) ||
							(blendResult[1] != 0 && !IsPixEqual(src[0], src[4])) ||
							(IsPixEqual(src[8], src[7]) && IsPixEqual(src[7], src[6]) && IsPixEqual(src[6], src[5]) && IsPixEqual(src[5], src[4]) && !IsPixEqual(src[0], src[6])) ) );

			blendPix = ( DistYCbCr(src[0], src[5]) <= DistYCbCr(src[0], src[7]) ) ? src[5] : src[7];
			dst[ 4] = mix(dst[ 4], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.250 : 0.000);
			dst[ 5] = mix(dst[ 5], blendPix, (needBlend && doLineBlend) ? ((haveSteepLine) ? 0.750 : ((haveShallowLine) ? 0.250 : 0.000)) : 0.000);
			dst[ 6] = mix(dst[ 6], blendPix, (needBlend && doLineBlend) ? ((!haveShallowLine && !haveSteepLine) ? 0.500 : 1.000) : 0.000);
			dst[ 7] = mix(dst[ 7], blendPix, (needBlend && doLineBlend) ? ((haveShallowLine) ? 0.750 : ((haveSteepLine) ? 0.250 : 0.000)) : 0.000);
			dst[ 8] = mix(dst[ 8], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.250 : 0.000);
			dst[35] = mix(dst[35], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.250 : 0.000);
			dst[16] = mix(dst[16], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.750 : 0.000);
			dst[17] = mix(dst[17], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 1.000 : 0.000);
			dst[18] = mix(dst[18], blendPix, (needBlend) ? ((doLineBlend) ? ((haveSteepLine) ? 1.000 : ((haveShallowLine) ? 0.750 : 0.500)) : 0.05652034508) : 0.000);
			dst[19] = mix(dst[19], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.4236372243) : 0.000);
			dst[20] = mix(dst[20], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.9711013910) : 0.000);
			dst[21] = mix(dst[21], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.4236372243) : 0.000);
			dst[22] = mix(dst[22], blendPix, (needBlend) ? ((doLineBlend) ? ((haveShallowLine) ? 1.000 : ((haveSteepLine) ? 0.750 : 0.500)) : 0.05652034508) : 0.000);
			dst[23] = mix(dst[23], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 1.000 : 0.000);
			dst[24] = mix(dst[24], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.750 : 0.000);
			dst[25] = mix(dst[25], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.250 : 0.000);


			dist_01_04 = DistYCbCr(src[3], src[6]);
			dist_03_08 = DistYCbCr(src[5], src[2]);
			haveShallowLine = (2.2 * dist_01_04 <= dist_03_08) && (v[0] != v[6]) && (v[7] != v[6]);
			haveSteepLine   = (2.2 * dist_03_08 <= dist_01_04) && (v[0] != v[2]) && (v[1] != v[2]);
			needBlend = (blendResult[3] != 0);
			doLineBlend = (  blendResult[3] >= 2 ||
						  !((blendResult[2] != 0 && !IsPixEqual(src[0], src[6])) ||
							(blendResult[0] != 0 && !IsPixEqual(src[0], src[2])) ||
							(IsPixEqual(src[6], src[5]) && IsPixEqual(src[5], src[4]) && IsPixEqual(src[4], src[3]) && IsPixEqual(src[3], src[2]) && !IsPixEqual(src[0], src[4])) ) );

			blendPix = ( DistYCbCr(src[0], src[3]) <= DistYCbCr(src[0], src[5]) ) ? src[3] : src[5];
			dst[13] = mix(dst[13], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.250 : 0.000);
			dst[14] = mix(dst[14], blendPix, (needBlend && doLineBlend) ? ((haveSteepLine) ? 0.750 : ((haveShallowLine) ? 0.250 : 0.000)) : 0.000);
			dst[15] = mix(dst[15], blendPix, (needBlend && doLineBlend) ? ((!haveShallowLine && !haveSteepLine) ? 0.500 : 1.000) : 0.000);
			dst[ 4] = mix(dst[ 4], blendPix, (needBlend && doLineBlend) ? ((haveShallowLine) ? 0.750 : ((haveSteepLine) ? 0.250 : 0.000)) : 0.000);
			dst[ 5] = mix(dst[ 5], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.250 : 0.000);
			dst[30] = mix(dst[30], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.250 : 0.000);
			dst[31] = mix(dst[31], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 0.750 : 0.000);
			dst[32] = mix(dst[32], blendPix, (needBlend && doLineBlend && haveSteepLine) ? 1.000 : 0.000);
			dst[33] = mix(dst[33], blendPix, (needBlend) ? ((doLineBlend) ? ((haveSteepLine) ? 1.000 : ((haveShallowLine) ? 0.750 : 0.500)) : 0.05652034508) : 0.000);
			dst[34] = mix(dst[34], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.4236372243) : 0.000);
			dst[35] = mix(dst[35], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.9711013910) : 0.000);
			dst[16] = mix(dst[16], blendPix, (needBlend) ? ((doLineBlend) ? 1.000 : 0.4236372243) : 0.000);
			dst[17] = mix(dst[17], blendPix, (needBlend) ? ((doLineBlend) ? ((haveShallowLine) ? 1.000 : ((haveSteepLine) ? 0.750 : 0.500)) : 0.05652034508) : 0.000);
			dst[18] = mix(dst[18], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 1.000 : 0.000);
			dst[19] = mix(dst[19], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.750 : 0.000);
			dst[20] = mix(dst[20], blendPix, (needBlend && doLineBlend && haveShallowLine) ? 0.250 : 0.000);

		}

			vec3 res = mix( mix( mix( mix( mix( mix(dst[20], dst[21], step(one_sixth, f.x) ), dst[22], step(two_sixth, f.x) ), mix( mix(dst[23], dst[24], step(four_sixth, f.x) ), dst[25], step(five_sixth, f.x) ), step(0.50, f.x) ),
		                                  mix( mix( mix(dst[19], dst[ 6], step(one_sixth, f.x) ), dst[ 7], step(two_sixth, f.x) ), mix( mix(dst[ 8], dst[ 9], step(four_sixth, f.x) ), dst[26], step(five_sixth, f.x) ), step(0.50, f.x) ), step(one_sixth, f.y) ),
		                                  mix( mix( mix(dst[18], dst[ 5], step(one_sixth, f.x) ), dst[ 0], step(two_sixth, f.x) ), mix( mix(dst[ 1], dst[10], step(four_sixth, f.x) ), dst[27], step(five_sixth, f.x) ), step(0.50, f.x) ), step(two_sixth, f.y) ),
		                        mix( mix( mix( mix( mix(dst[17], dst[ 4], step(one_sixth, f.x) ), dst[ 3], step(two_sixth, f.x) ), mix( mix(dst[ 2], dst[11], step(four_sixth, f.x) ), dst[28], step(five_sixth, f.x) ), step(0.50, f.x) ),
		                                  mix( mix( mix(dst[16], dst[15], step(one_sixth, f.x) ), dst[14], step(two_sixth, f.x) ), mix( mix(dst[13], dst[12], step(four_sixth, f.x) ), dst[29], step(five_sixth, f.x) ), step(0.50, f.x) ), step(four_sixth, f.y) ),
		                                  mix( mix( mix(dst[35], dst[34], step(one_sixth, f.x) ), dst[33], step(two_sixth, f.x) ), mix( mix(dst[32], dst[31], step(four_sixth, f.x) ), dst[30], step(five_sixth, f.x) ), step(0.50, f.x) ), step(five_sixth, f.y) ),
		                     step(0.50, f.y) );

    float texel_alpha = sample_texel(vec2(frag_texture_coord.x, frag_texture_coord.y)).w;
   return vec4(res, texel_alpha);
}

// Bilinear filtering
vec4 get_texel_bilinear()
{
  float x = frag_texture_coord.x;
  float y = frag_texture_coord.y;

  float u_frac = fract(x);
  float v_frac = fract(y);

  vec4 texel_00 = sample_texel(vec2(x + 0, y + 0));
  vec4 texel_10 = sample_texel(vec2(x + 1, y + 0));
  vec4 texel_01 = sample_texel(vec2(x + 0, y + 1));
  vec4 texel_11 = sample_texel(vec2(x + 1, y + 1));

   if (is_transparent(texel_00)) {
     return texel_00;
   }

   if (is_transparent(texel_10)) {
      texel_10 = texel_00;
   }

   if (is_transparent(texel_01)) {
      texel_01 = texel_10;
   }

   if (is_transparent(texel_11)) {
      texel_11 = texel_01;
   }

   vec4 texel = texel_00 * (1. - u_frac) * (1. - v_frac)
     + texel_10 * u_frac * (1. - v_frac)
     + texel_01 * (1. - u_frac) * v_frac
     + texel_11 * u_frac * v_frac;

   return texel;
}


void main() {
   vec4 color;

      if (frag_texture_blend_mode == BLEND_MODE_NO_TEXTURE) {
         color = vec4(frag_shading_color, 0.);
      } else {
         vec4 texel;

         if (texture_flt == FILTER_MODE_3POINT) {
	   texel = get_texel_3point();
         } else if (texture_flt == FILTER_MODE_BILINEAR) {
	   texel = get_texel_bilinear();
	     } else if (texture_flt == FILTER_MODE_SABR) {
	   texel = get_texel_sabr();
	     } else if (texture_flt == FILTER_MODE_6XBRZ) {
	   texel = get_texel_6xbrz();
	 } else {
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
