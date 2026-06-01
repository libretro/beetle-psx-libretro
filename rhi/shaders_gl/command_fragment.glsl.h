#include "shaders_common.h"

#undef command_fragment_name_

#ifdef FILTER_SABR
#define command_fragment_name_ command_fragment_sabr
#elif defined(FILTER_XBR)
#define command_fragment_name_ command_fragment_xbr
#elif defined(FILTER_BILINEAR)
#define command_fragment_name_ command_fragment_bilinear
#elif defined(FILTER_3POINT)
#define command_fragment_name_ command_fragment_3point
#elif defined(FILTER_JINC2)
#define command_fragment_name_ command_fragment_jinc2
#else
#define command_fragment_name_ command_fragment
#endif

static const char * command_fragment_name_ = GLSL_FRAGMENT(
uniform sampler2D fb_texture;

// Scaling to apply to the dither pattern
uniform uint dither_scaling;

// 0: Only draw opaque pixels, 1: only draw semi-transparent pixels
uniform uint draw_semi_transparent;

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
// Texture limits: [Umin, Vmin, Umax, Vmax]
flat in uvec4 frag_texture_limits;

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

)
#ifdef HAVE_OPENGLES3
STRINGIZE(
  return (r << 11) | (g << 6) | (b << 1) | a;
)
#else
STRINGIZE(
  return (a << 15) | (b << 10) | (g << 5) | r;
)
#endif
STRINGIZE(
}

// Texture color 0x0000 is special in the Playstation GPU, it denotes
// a fully transparent texel (even for opaque draw commands). If you
// want black you have to use an opaque draw command and use `0x8000`
// instead.
bool is_transparent(vec4 texel) {
  return rebuild_psx_color(texel) == 0U;
}

// reinterpret 5551 color for GLES (doesn't support 1555 REV)
vec4 reinterpret_color(vec4 color) {
  // rebuild as 5551
  uint pre_bits = rebuild_psx_color(color);

  // interpret as 1555
  float a = float((pre_bits & 0x8000U) >> 15) / 31.;
  float b = float((pre_bits & 0x7C00U) >> 10) / 31.;
  float g = float((pre_bits & 0x3E0U) >> 5) / 31.;
  float r = float(pre_bits & 0x1FU) / 31.;

  return vec4(r, g, b, a);
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
   
   //coords += vec2(0.001, 0.001);
   
   // Texture pages are limited to 256x256 pixels
   uint tex_x = clamp(uint(coords.x), 0x0U, 0xffU);
   uint tex_y = clamp(uint(coords.y), 0x0U, 0xffU);

   // Clamp to primitive limits
   tex_x = clamp(tex_x, frag_texture_limits[0], frag_texture_limits[2]);
   tex_y = clamp(tex_y, frag_texture_limits[1], frag_texture_limits[3]);

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

)
#ifdef HAVE_OPENGLES3
STRINGIZE(
   return reinterpret_color(texel);
)
#else
STRINGIZE(
   return texel;
)
#endif
STRINGIZE(
}
)

#if defined(FILTER_SABR) || defined(FILTER_XBR)
STRINGIZE(
in vec2 tc;
in vec4 xyp_1_2_3;
in vec4 xyp_6_7_8;
in vec4 xyp_11_12_13;
in vec4 xyp_16_17_18;
in vec4 xyp_21_22_23;
in vec4 xyp_5_10_15;
in vec4 xyp_9_14_9;

float c_df(vec3 c1, vec3 c2) {
	vec3 df = abs(c1 - c2);
	return df.r + df.g + df.b;
}

const float coef = 2.0;
)
#endif

#ifdef FILTER_SABR
STRINGIZE(
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

//sabr
vec4 get_texel_sabr()
{
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
)
#endif

#ifdef FILTER_XBR
STRINGIZE(
const int BLEND_NONE = 0;
const int BLEND_NORMAL = 1;
const int BLEND_DOMINANT = 2;
const float LUMINANCE_WEIGHT = 1.0;
const float EQUAL_COLOR_TOLERANCE = 0.1176470588235294;
const float STEEP_DIRECTION_THRESHOLD = 2.2;
const float DOMINANT_DIRECTION_THRESHOLD = 3.6;
const vec4 w = vec4(0.2627, 0.6780, 0.0593, 0.5);

float DistYCbCr(vec4 pixA, vec4 pixB)
{
  const float scaleB = 0.5 / (1.0 - w.b);
  const float scaleR = 0.5 / (1.0 - w.r);
  vec4 diff = pixA - pixB;
  float Y = dot(diff, w);
  float Cb = scaleB * (diff.b - Y);
  float Cr = scaleR * (diff.r - Y);

  return sqrt(((LUMINANCE_WEIGHT * Y) * (LUMINANCE_WEIGHT * Y)) + (Cb * Cb) + (Cr * Cr));
}

bool IsPixEqual(const vec4 pixA, const vec4 pixB)
{
  return (DistYCbCr(pixA, pixB) < EQUAL_COLOR_TOLERANCE);
}

float get_left_ratio(vec2 center, vec2 origin, vec2 direction, vec2 scale)
{
  vec2 P0 = center - origin;
  vec2 proj = direction * (dot(P0, direction) / dot(direction, direction));
  vec2 distv = P0 - proj;
  vec2 orth = vec2(-direction.y, direction.x);
  float side = sign(dot(P0, orth));
  float v = side * length(distv * scale);

//  return step(0, v);
  return smoothstep(-sqrt(2.0)/2.0, sqrt(2.0)/2.0, v);
}

bool eq(vec4 a, vec4 b){
   return (a == b);
}

bool neq(vec4 a, vec4 b){
   return (a != b);
}

vec4 P(vec2 coord, int x, int y){
   return sample_texel(coord + vec2(x, y));
}

vec4 get_texel_xbr(out float opacity)
{
  //---------------------------------------
  // Input Pixel Mapping:  -|x|x|x|-
  //                       x|A|B|C|x
  //                       x|D|E|F|x
  //                       x|G|H|I|x
  //                       -|x|x|x|-

  vec2 scale = vec2(8.0);
  vec2 pos = fract(frag_texture_coord.xy) - vec2(0.5, 0.5);
  vec2 coord = frag_texture_coord.xy - pos;

  vec4 A = P(coord, -1,-1);
  A.w = 1. - float(is_transparent(A));
  vec4 B = P(coord,  0,-1);
  B.w = 1. - float(is_transparent(B));
  vec4 C = P(coord,  1,-1);
  C.w = 1. - float(is_transparent(C));
  vec4 D = P(coord, -1, 0);
  D.w = 1. - float(is_transparent(D));
  vec4 E = P(coord,  0, 0);
  E.w = 1. - float(is_transparent(E));
  vec4 F = P(coord,  1, 0);
  F.w = 1. - float(is_transparent(F));
  vec4 G = P(coord, -1, 1);
  G.w = 1. - float(is_transparent(G));
  vec4 H = P(coord,  0, 1);
  H.w = 1. - float(is_transparent(H));
  vec4 I = P(coord,  1, 1);
  I.w = 1. - float(is_transparent(I));

  // blendResult Mapping: x|y|
  //                      w|z|
  ivec4 blendResult = ivec4(BLEND_NONE,BLEND_NONE,BLEND_NONE,BLEND_NONE);

  // Preprocess corners
  // Pixel Tap Mapping: -|-|-|-|-
  //                    -|-|B|C|-
  //                    -|D|E|F|x
  //                    -|G|H|I|x
  //                    -|-|x|x|-
  if (!((eq(E,F) && eq(H,I)) || (eq(E,H) && eq(F,I))))
  {
    float dist_H_F = DistYCbCr(G, E) + DistYCbCr(E, C) + DistYCbCr(P(coord, 0,2), I) + DistYCbCr(I, P(coord, 2,0)) + (4.0 * DistYCbCr(H, F));
    float dist_E_I = DistYCbCr(D, H) + DistYCbCr(H, P(coord, 1,2)) + DistYCbCr(B, F) + DistYCbCr(F, P(coord, 2,1)) + (4.0 * DistYCbCr(E, I));
    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_H_F) < dist_E_I;
    blendResult.z = ((dist_H_F < dist_E_I) && neq(E,F) && neq(E,H)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
  }


  // Pixel Tap Mapping: -|-|-|-|-
  //                    -|A|B|-|-
  //                    x|D|E|F|-
  //                    x|G|H|I|-
  //                    -|x|x|-|-
  if (!((eq(D,E) && eq(G,H)) || (eq(D,G) && eq(E,H))))
  {
    float dist_G_E = DistYCbCr(P(coord, -2,1)  , D) + DistYCbCr(D, B) + DistYCbCr(P(coord, -1,2), H) + DistYCbCr(H, F) + (4.0 * DistYCbCr(G, E));
    float dist_D_H = DistYCbCr(P(coord, -2,0)  , G) + DistYCbCr(G, P(coord, 0,2)) + DistYCbCr(A, E) + DistYCbCr(E, I) + (4.0 * DistYCbCr(D, H));
    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_H) < dist_G_E;
    blendResult.w = ((dist_G_E > dist_D_H) && neq(E,D) && neq(E,H)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
  }

  // Pixel Tap Mapping: -|-|x|x|-
  //                    -|A|B|C|x
  //                    -|D|E|F|x
  //                    -|-|H|I|-
  //                    -|-|-|-|-
  if (!((eq(B,C) && eq(E,F)) || (eq(B,E) && eq(C,F))))
  {
    float dist_E_C = DistYCbCr(D, B) + DistYCbCr(B, P(coord, 1,-2)) + DistYCbCr(H, F) + DistYCbCr(F, P(coord, 2,-1)) + (4.0 * DistYCbCr(E, C));
    float dist_B_F = DistYCbCr(A, E) + DistYCbCr(E, I) + DistYCbCr(P(coord, 0,-2), C) + DistYCbCr(C, P(coord, 2,0)) + (4.0 * DistYCbCr(B, F));
    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_B_F) < dist_E_C;
    blendResult.y = ((dist_E_C > dist_B_F) && neq(E,B) && neq(E,F)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
  }

  // Pixel Tap Mapping: -|x|x|-|-
  //                    x|A|B|C|-
  //                    x|D|E|F|-
  //                    -|G|H|-|-
  //                    -|-|-|-|-
  if (!((eq(A,B) && eq(D,E)) || (eq(A,D) && eq(B,E))))
  {
    float dist_D_B = DistYCbCr(P(coord, -2,0), A) + DistYCbCr(A, P(coord, 0,-2)) + DistYCbCr(G, E) + DistYCbCr(E, C) + (4.0 * DistYCbCr(D, B));
    float dist_A_E = DistYCbCr(P(coord, -2,-1), D) + DistYCbCr(D, H) + DistYCbCr(P(coord, -1,-2), B) + DistYCbCr(B, F) + (4.0 * DistYCbCr(A, E));
    bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_B) < dist_A_E;
    blendResult.x = ((dist_D_B < dist_A_E) && neq(E,D) && neq(E,B)) ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL) : BLEND_NONE;
  }

  vec4 res = E;

  // Pixel Tap Mapping: -|-|-|-|-
  //                    -|-|B|C|-
  //                    -|D|E|F|x
  //                    -|G|H|I|x
  //                    -|-|x|x|-
  if(blendResult.z != BLEND_NONE)
  {
    float dist_F_G = DistYCbCr(F, G);
    float dist_H_C = DistYCbCr(H, C);
    bool doLineBlend = (blendResult.z == BLEND_DOMINANT ||
                !((blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) || (blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) ||
                  (IsPixEqual(G, H) && IsPixEqual(H, I) && IsPixEqual(I, F) && IsPixEqual(F, C) && !IsPixEqual(E, I))));

    vec2 origin = vec2(0.0, 1.0 / sqrt(2.0));
    vec2 direction = vec2(1.0, -1.0);
    if(doLineBlend)
    {
      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_F_G <= dist_H_C) && neq(E,G) && neq(D,G);
      bool haveSteepLine = (STEEP_DIRECTION_THRESHOLD * dist_H_C <= dist_F_G) && neq(E,C) && neq(B,C);
      origin = haveShallowLine? vec2(0.0, 0.25) : vec2(0.0, 0.5);
      direction.x += haveShallowLine? 1.0: 0.0;
      direction.y -= haveSteepLine? 1.0: 0.0;
    }

    vec4 blendPix = mix(H,F, step(DistYCbCr(E, F), DistYCbCr(E, H)));
    res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
  }

  // Pixel Tap Mapping: -|-|-|-|-
  //                    -|A|B|-|-
  //                    x|D|E|F|-
  //                    x|G|H|I|-
  //                    -|x|x|-|-
  if(blendResult.w != BLEND_NONE)
  {
    float dist_H_A = DistYCbCr(H, A);
    float dist_D_I = DistYCbCr(D, I);
    bool doLineBlend = (blendResult.w == BLEND_DOMINANT ||
                !((blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) || (blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) ||
                  (IsPixEqual(A, D) && IsPixEqual(D, G) && IsPixEqual(G, H) && IsPixEqual(H, I) && !IsPixEqual(E, G))));

    vec2 origin = vec2(-1.0 / sqrt(2.0), 0.0);
    vec2 direction = vec2(1.0, 1.0);
    if(doLineBlend)
    {
      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_H_A <= dist_D_I) && neq(E,A) && neq(B,A);
      bool haveSteepLine  = (STEEP_DIRECTION_THRESHOLD * dist_D_I <= dist_H_A) && neq(E,I) && neq(F,I);
      origin = haveShallowLine? vec2(-0.25, 0.0) : vec2(-0.5, 0.0);
      direction.y += haveShallowLine? 1.0: 0.0;
      direction.x += haveSteepLine? 1.0: 0.0;
    }
    origin = origin;
    direction = direction;

    vec4 blendPix = mix(H,D, step(DistYCbCr(E, D), DistYCbCr(E, H)));
    res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
  }

  // Pixel Tap Mapping: -|-|x|x|-
  //                    -|A|B|C|x
  //                    -|D|E|F|x
  //                    -|-|H|I|-
  //                    -|-|-|-|-
  if(blendResult.y != BLEND_NONE)
  {
    float dist_B_I = DistYCbCr(B, I);
    float dist_F_A = DistYCbCr(F, A);
    bool doLineBlend = (blendResult.y == BLEND_DOMINANT ||
                !((blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) || (blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) ||
                  (IsPixEqual(I, F) && IsPixEqual(F, C) && IsPixEqual(C, B) && IsPixEqual(B, A) && !IsPixEqual(E, C))));

    vec2 origin = vec2(1.0 / sqrt(2.0), 0.0);
    vec2 direction = vec2(-1.0, -1.0);

    if(doLineBlend)
    {
      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_B_I <= dist_F_A) && neq(E,I) && neq(H,I);
      bool haveSteepLine  = (STEEP_DIRECTION_THRESHOLD * dist_F_A <= dist_B_I) && neq(E,A) && neq(D,A);
      origin = haveShallowLine? vec2(0.25, 0.0) : vec2(0.5, 0.0);
      direction.y -= haveShallowLine? 1.0: 0.0;
      direction.x -= haveSteepLine? 1.0: 0.0;
    }

    vec4 blendPix = mix(F,B, step(DistYCbCr(E, B), DistYCbCr(E, F)));
    res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
  }

  // Pixel Tap Mapping: -|x|x|-|-
  //                    x|A|B|C|-
  //                    x|D|E|F|-
  //                    -|G|H|-|-
  //                    -|-|-|-|-
  if(blendResult.x != BLEND_NONE)
  {
    float dist_D_C = DistYCbCr(D, C);
    float dist_B_G = DistYCbCr(B, G);
    bool doLineBlend = (blendResult.x == BLEND_DOMINANT ||
                !((blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) || (blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) ||
                  (IsPixEqual(C, B) && IsPixEqual(B, A) && IsPixEqual(A, D) && IsPixEqual(D, G) && !IsPixEqual(E, A))));

    vec2 origin = vec2(0.0, -1.0 / sqrt(2.0));
    vec2 direction = vec2(-1.0, 1.0);
    if(doLineBlend)
    {
      bool haveShallowLine = (STEEP_DIRECTION_THRESHOLD * dist_D_C <= dist_B_G) && neq(E,C) && neq(F,C);
      bool haveSteepLine  = (STEEP_DIRECTION_THRESHOLD * dist_B_G <= dist_D_C) && neq(E,G) && neq(H,G);
      origin = haveShallowLine? vec2(0.0, -0.25) : vec2(0.0, -0.5);
      direction.x -= haveShallowLine? 1.0: 0.0;
      direction.y += haveSteepLine? 1.0: 0.0;
    }

    vec4 blendPix = mix(D,B, step(DistYCbCr(E, B), DistYCbCr(E, D)));
    res = mix(res, blendPix, get_left_ratio(pos, origin, direction, scale));
  }
     
    opacity = res.w;
    res.xyz = res.xyz * (1./opacity);
    return vec4(res);
}
)
#endif

#ifdef FILTER_3POINT
STRINGIZE(
vec4 get_texel_3point(out float opacity)
{
  float x = frag_texture_coord.x;
  float y = frag_texture_coord.y;
  
  // interpolate from centre of texel
  vec2 uv_frac = fract(vec2(x, y)) - vec2(0.5, 0.5);
  vec2 uv_offs = sign(uv_frac);
  uv_frac = abs(uv_frac);

  vec4 texel_00;

  if (uv_frac.x + uv_frac.y < 1.0) {
    // Use bottom-left
    texel_00 = sample_texel(frag_texture_coord.xy);
  } else {
    // Use top-right
    texel_00 = sample_texel(vec2(x + uv_offs.x, y + uv_offs.y));

    float tmp = 1. - uv_frac.y;
    uv_frac.y = 1. - uv_frac.x;
    uv_frac.x = tmp;
  }

   vec4 texel_10 = sample_texel(vec2(x + uv_offs.x, y));
   vec4 texel_01 = sample_texel(vec2(x, y + uv_offs.y));
   
   float opacity_00 = 1. - float(is_transparent(texel_00));
   float opacity_10 = 1. - float(is_transparent(texel_10));
   float opacity_01 = 1. - float(is_transparent(texel_01));

   vec4 texel = texel_00
     + uv_frac.x * (texel_10 - texel_00)
     + uv_frac.y * (texel_01 - texel_00);
     
   opacity = opacity_00
     + uv_frac.x * (opacity_10 - opacity_00)
     + uv_frac.y * (opacity_01 - opacity_00);
	
   // adjust colour to account for black transparent samples (assume rgb would be average of other pixels)
   texel.rgb = texel.rgb * (1./opacity);

   return texel;
}
)
#endif

#ifdef FILTER_BILINEAR
STRINGIZE(
// Bilinear filtering
vec4 get_texel_bilinear(out float opacity)
{
  float x = frag_texture_coord.x;
  float y = frag_texture_coord.y;

  // interpolate from centre of texel
  vec2 uv_frac = fract(vec2(x, y)) - vec2(0.5, 0.5);
  vec2 uv_offs = sign(uv_frac);
  uv_frac = abs(uv_frac);

  // sample 4 nearest texels
  vec4 texel_00 = sample_texel(vec2(x + 0, y + 0));
  vec4 texel_10 = sample_texel(vec2(x + uv_offs.x, y + 0));
  vec4 texel_01 = sample_texel(vec2(x + 0, y + uv_offs.y));
  vec4 texel_11 = sample_texel(vec2(x + uv_offs.x, y + uv_offs.y));
  
  // test for fully transparent texel
  float opacity00 = 1. - float(is_transparent(texel_00));
  float opacity10 = 1. - float(is_transparent(texel_10));
  float opacity01 = 1. - float(is_transparent(texel_01));
  float opacity11 = 1. - float(is_transparent(texel_11));
  
  // average opacity of texels
  opacity = opacity00 * (1. - uv_frac.x) * (1. - uv_frac.y)
     + opacity10 * uv_frac.x * (1. - uv_frac.y)
     + opacity01 * (1. - uv_frac.x) * uv_frac.y
     + opacity11 * uv_frac.x * uv_frac.y; 
	 
   // average samples
   vec4 texel = texel_00 * (1. - uv_frac.x) * (1. - uv_frac.y)
     + texel_10 * uv_frac.x * (1. - uv_frac.y)
     + texel_01 * (1. - uv_frac.x) * uv_frac.y
     + texel_11 * uv_frac.x * uv_frac.y;
	
   // adjust colour to account for black transparent samples (assume rgb would be average of other pixels)
   texel.rgb = texel.rgb * (1./opacity);

   return texel;
}
)
#endif

#ifdef FILTER_JINC2
STRINGIZE(
const float JINC2_WINDOW_SINC = 0.44;
const float JINC2_SINC = 0.82;
const float JINC2_AR_STRENGTH = 0.8;

const   float halfpi            = 1.5707963267948966192313216916398;
const   float pi                = 3.1415926535897932384626433832795;
const   float wa                = 1.382300768;
const   float wb                = 2.576105976;

// Calculates the distance between two points
float d(vec2 pt1, vec2 pt2)
{
  vec2 v = pt2 - pt1;
  return sqrt(dot(v,v));
}

vec4 min4(vec4 a, vec4 b, vec4 c, vec4 d)
{
    return min(a, min(b, min(c, d)));
}

vec4 max4(vec4 a, vec4 b, vec4 c, vec4 d)
{
    return max(a, max(b, max(c, d)));
}

vec4 resampler(vec4 x)
{
   vec4 res;

   // res = (x==vec4(0.0, 0.0, 0.0, 0.0)) ?  vec4(wa*wb)  :  sin(x*wa)*sin(x*wb)/(x*x);
   // Need to use mix(.., equal(..)) since we want zero check to be component wise
   res = mix(sin(x*wa)*sin(x*wb)/(x*x), vec4(wa*wb), equal(x,vec4(0.0, 0.0, 0.0, 0.0)));

   return res;
}

vec4 get_texel_jinc2(out float opacity)
{
    vec4 color;
    vec4 weights[4];

    vec2 dx = vec2(1.0, 0.0);
    vec2 dy = vec2(0.0, 1.0);

    vec2 pc = frag_texture_coord.xy;

    vec2 tc = (floor(pc-vec2(0.5,0.5))+vec2(0.5,0.5));

    weights[0] = resampler(vec4(d(pc, tc    -dx    -dy), d(pc, tc           -dy), d(pc, tc    +dx    -dy), d(pc, tc+2.0*dx    -dy)));
    weights[1] = resampler(vec4(d(pc, tc    -dx       ), d(pc, tc              ), d(pc, tc    +dx       ), d(pc, tc+2.0*dx       )));
    weights[2] = resampler(vec4(d(pc, tc    -dx    +dy), d(pc, tc           +dy), d(pc, tc    +dx    +dy), d(pc, tc+2.0*dx    +dy)));
    weights[3] = resampler(vec4(d(pc, tc    -dx+2.0*dy), d(pc, tc       +2.0*dy), d(pc, tc    +dx+2.0*dy), d(pc, tc+2.0*dx+2.0*dy)));

    dx = dx;
    dy = dy;
    tc = tc;

    vec4 c00 = sample_texel(tc    -dx    -dy);
    c00.w = 1. - float(is_transparent(c00));
    vec4 c10 = sample_texel(tc           -dy);
    c10.w = 1. - float(is_transparent(c10));
    vec4 c20 = sample_texel(tc    +dx    -dy);
    c20.w = 1. - float(is_transparent(c20));
    vec4 c30 = sample_texel(tc+2.0*dx    -dy);
    c30.w = 1. - float(is_transparent(c30));
    vec4 c01 = sample_texel(tc    -dx       );
    c01.w = 1. - float(is_transparent(c01));
    vec4 c11 = sample_texel(tc              );
    c11.w = 1. - float(is_transparent(c11));
    vec4 c21 = sample_texel(tc    +dx       );
    c21.w = 1. - float(is_transparent(c21));
    vec4 c31 = sample_texel(tc+2.0*dx       );
    c31.w = 1. - float(is_transparent(c31));
    vec4 c02 = sample_texel(tc    -dx    +dy);
    c02.w = 1. - float(is_transparent(c02));
    vec4 c12 = sample_texel(tc           +dy);
    c12.w = 1. - float(is_transparent(c12));
    vec4 c22 = sample_texel(tc    +dx    +dy);
    c22.w = 1. - float(is_transparent(c22));
    vec4 c32 = sample_texel(tc+2.0*dx    +dy);
    c32.w = 1. - float(is_transparent(c32));
    vec4 c03 = sample_texel(tc    -dx+2.0*dy);
    c03.w = 1. - float(is_transparent(c03));
    vec4 c13 = sample_texel(tc       +2.0*dy);
    c13.w = 1. - float(is_transparent(c13));
    vec4 c23 = sample_texel(tc    +dx+2.0*dy);
    c23.w = 1. - float(is_transparent(c23));
    vec4 c33 = sample_texel(tc+2.0*dx+2.0*dy);
    c33.w = 1. - float(is_transparent(c33));

    color = sample_texel(frag_texture_coord.xy);

    //  Get min/max samples
    vec4 min_sample = min4(c11, c21, c12, c22);
    vec4 max_sample = max4(c11, c21, c12, c22);

    color = vec4(dot(weights[0], vec4(c00.x, c10.x, c20.x, c30.x)), dot(weights[0], vec4(c00.y, c10.y, c20.y, c30.y)), dot(weights[0], vec4(c00.z, c10.z, c20.z, c30.z)), dot(weights[0], vec4(c00.w, c10.w, c20.w, c30.w)));
    color+= vec4(dot(weights[1], vec4(c01.x, c11.x, c21.x, c31.x)), dot(weights[1], vec4(c01.y, c11.y, c21.y, c31.y)), dot(weights[1], vec4(c01.z, c11.z, c21.z, c31.z)), dot(weights[1], vec4(c01.w, c11.w, c21.w, c31.w)));
    color+= vec4(dot(weights[2], vec4(c02.x, c12.x, c22.x, c32.x)), dot(weights[2], vec4(c02.y, c12.y, c22.y, c32.y)), dot(weights[2], vec4(c02.z, c12.z, c22.z, c32.z)), dot(weights[2], vec4(c02.w, c12.w, c22.w, c32.w)));
    color+= vec4(dot(weights[3], vec4(c03.x, c13.x, c23.x, c33.x)), dot(weights[3], vec4(c03.y, c13.y, c23.y, c33.y)), dot(weights[3], vec4(c03.z, c13.z, c23.z, c33.z)), dot(weights[3], vec4(c03.w, c13.w, c23.w, c33.w)));
    color = color/(dot(weights[0], vec4(1,1,1,1)) + dot(weights[1], vec4(1,1,1,1)) + dot(weights[2], vec4(1,1,1,1)) + dot(weights[3], vec4(1,1,1,1)));

    // Anti-ringing
    vec4 aux = color;
    color = clamp(color, min_sample, max_sample);
    color = mix(aux, color, JINC2_AR_STRENGTH);

    // final sum and weight normalization
    vec4 texel = vec4(color);
    opacity = texel.w;
    texel.rgb = texel.rgb * (1./opacity);
    return texel;
}
)
#endif
STRINGIZE(
void main() {
   vec4 color;
   float opacity=1.;
   
      if (frag_texture_blend_mode == BLEND_MODE_NO_TEXTURE)
      {
         color = vec4(frag_shading_color, 0.);
      }
      else
      {
         vec4 texel;
         vec4 texel0 = sample_texel(vec2(frag_texture_coord.x,
                  frag_texture_coord.y));
				  
		 opacity = float(!is_transparent(texel0));
)
#if defined(FILTER_SABR)
STRINGIZE(
		texel = get_texel_sabr();
)
#elif defined(FILTER_XBR)
STRINGIZE(
         texel = get_texel_xbr(opacity);
)
#elif defined(FILTER_BILINEAR)
STRINGIZE(
         texel = get_texel_bilinear(opacity);
		 texel0 = texel; // use bilinear lookup for all tests to smooth edges
)
#elif defined(FILTER_3POINT)
STRINGIZE(
         texel = get_texel_3point(opacity);
       texel0 = texel;
)
#elif defined(FILTER_JINC2)
STRINGIZE(
         texel = get_texel_jinc2(opacity);
)
#else
STRINGIZE(
         texel = texel0; //use nearest if nothing else is chosen
)
#endif
STRINGIZE(
	 // texel color 0x0000 is always fully transparent (even for opaque
         // draw commands)
      //   if (is_transparent(texel0)) {
		  if(opacity < 0.5) {
	   // Fully transparent texel, discard
	   discard;
         }

         // Bit 15 (stored in the alpha) is used as a flag for
         // semi-transparency, but only if this is a semi-transparent draw
         // command
         uint transparency_flag = uint(floor(texel0.a + 0.5));

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

#undef command_fragment_name_
