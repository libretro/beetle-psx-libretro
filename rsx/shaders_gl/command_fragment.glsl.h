#include "shaders_common.h"

#ifdef FILTER_SABR
static const char * command_fragment_sabr = GLSL(
#elif defined(FILTER_XBR)
static const char * command_fragment_xbr = GLSL(
#elif defined(FILTER_BILINEAR)
static const char * command_fragment_bilinear = GLSL(
#elif defined(FILTER_3POINT)
static const char * command_fragment_3point = GLSL(
#elif defined(FILTER_JINC2)
static const char * command_fragment_jinc2 = GLSL(
#else
static const char * command_fragment = GLSL(
#endif

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
   coords.x += 0.5 / 1024U;
   coords.y += 0.5 / 512U;
   // Number of texel per VRAM 16bit "pixel" for the current depth
   uint pix_per_hw = 1U << frag_depth_shift;

   // Texture pages are limited to 256x256 pixels
   uint tex_x = clamp(uint(coords.x), 0x0U, 0xffU);
   uint tex_y = clamp(uint(coords.y), 0x0U, 0xffU);

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

#if defined(FILTER_SABR) || defined(FILTER_XBR)
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
#endif

#ifdef FILTER_SABR
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
#endif

#ifdef FILTER_XBR

// constants and functions for xbr
const   vec3  rgbw          = vec3(14.352, 28.176, 5.472);
const   vec4  eq_threshold  = vec4(15.0, 15.0, 15.0, 15.0);

const vec4 delta   = vec4(1.0/4., 1.0/4., 1.0/4., 1.0/4.);
const vec4 delta_l = vec4(0.5/4., 1.0/4., 0.5/4., 1.0/4.);
const vec4 delta_u = delta_l.yxwz;

const  vec4 Ao = vec4( 1.0, -1.0, -1.0, 1.0 );
const  vec4 Bo = vec4( 1.0,  1.0, -1.0,-1.0 );
const  vec4 Co = vec4( 1.5,  0.5, -0.5, 0.5 );
const  vec4 Ax = vec4( 1.0, -1.0, -1.0, 1.0 );
const  vec4 Bx = vec4( 0.5,  2.0, -0.5,-2.0 );
const  vec4 Cx = vec4( 1.0,  1.0, -0.5, 0.0 );
const  vec4 Ay = vec4( 1.0, -1.0, -1.0, 1.0 );
const  vec4 By = vec4( 2.0,  0.5, -2.0,-0.5 );
const  vec4 Cy = vec4( 2.0,  0.0, -1.0, 0.5 );
const  vec4 Ci = vec4(0.25, 0.25, 0.25, 0.25);

// Difference between vector components.
vec4 df(vec4 A, vec4 B)
{
    return vec4(abs(A-B));
}

// Compare two vectors and return their components are different.
vec4 diff(vec4 A, vec4 B)
{
    return vec4(notEqual(A, B));
}

// Determine if two vector components are equal based on a threshold.
vec4 eq(vec4 A, vec4 B)
{
    return (step(df(A, B), vec4(15.)));
}

// Determine if two vector components are NOT equal based on a threshold.
vec4 neq(vec4 A, vec4 B)
{
    return (vec4(1.0, 1.0, 1.0, 1.0) - eq(A, B));
}

// Weighted distance.
vec4 wd(vec4 a, vec4 b, vec4 c, vec4 d, vec4 e, vec4 f, vec4 g, vec4 h)
{
    return (df(a,b) + df(a,c) + df(d,e) + df(d,f) + 4.0*df(g,h));
}

vec4 get_texel_xbr()
{
    vec4 edri;
    vec4 edr;
    vec4 edr_l;
    vec4 edr_u;
    vec4 px; // px = pixel, edr = edge detection rule
    vec4 irlv0;
    vec4 irlv1;
    vec4 irlv2l;
    vec4 irlv2u;
    vec4 block_3d;
    vec4 fx;
    vec4 fx_l;
    vec4 fx_u; // inequations of straight lines.

    vec2 fp  = fract(tc);

    vec3 A1 = sample_texel(xyp_1_2_3.xw    ).xyz;
    vec3 B1 = sample_texel(xyp_1_2_3.yw    ).xyz;
    vec3 C1 = sample_texel(xyp_1_2_3.zw    ).xyz;
    vec3 A  = sample_texel(xyp_6_7_8.xw    ).xyz;
    vec3 B  = sample_texel(xyp_6_7_8.yw    ).xyz;
    vec3 C  = sample_texel(xyp_6_7_8.zw    ).xyz;
    vec3 D  = sample_texel(xyp_11_12_13.xw ).xyz;
    vec3 E  = sample_texel(xyp_11_12_13.yw ).xyz;
    vec3 F  = sample_texel(xyp_11_12_13.zw ).xyz;
    vec3 G  = sample_texel(xyp_16_17_18.xw ).xyz;
    vec3 H  = sample_texel(xyp_16_17_18.yw ).xyz;
    vec3 I  = sample_texel(xyp_16_17_18.zw ).xyz;
    vec3 G5 = sample_texel(xyp_21_22_23.xw ).xyz;
    vec3 H5 = sample_texel(xyp_21_22_23.yw ).xyz;
    vec3 I5 = sample_texel(xyp_21_22_23.zw ).xyz;
    vec3 A0 = sample_texel(xyp_5_10_15.xy  ).xyz;
    vec3 D0 = sample_texel(xyp_5_10_15.xz  ).xyz;
    vec3 G0 = sample_texel(xyp_5_10_15.xw  ).xyz;
    vec3 C4 = sample_texel(xyp_9_14_9.xy   ).xyz;
    vec3 F4 = sample_texel(xyp_9_14_9.xz   ).xyz;
    vec3 I4 = sample_texel(xyp_9_14_9.xw   ).xyz;

    vec4 b  = vec4(dot(B ,rgbw), dot(D ,rgbw), dot(H ,rgbw), dot(F ,rgbw));
    vec4 c  = vec4(dot(C ,rgbw), dot(A ,rgbw), dot(G ,rgbw), dot(I ,rgbw));
    vec4 d  = b.yzwx;
    vec4 e  = vec4(dot(E,rgbw));
    vec4 f  = b.wxyz;
    vec4 g  = c.zwxy;
    vec4 h  = b.zwxy;
    vec4 i  = c.wxyz;
    vec4 i4 = vec4(dot(I4,rgbw), dot(C1,rgbw), dot(A0,rgbw), dot(G5,rgbw));
    vec4 i5 = vec4(dot(I5,rgbw), dot(C4,rgbw), dot(A1,rgbw), dot(G0,rgbw));
    vec4 h5 = vec4(dot(H5,rgbw), dot(F4,rgbw), dot(B1,rgbw), dot(D0,rgbw));
    vec4 f4 = h5.yzwx;

    // These inequations define the line below which interpolation occurs.
    fx   = (Ao*fp.y+Bo*fp.x);
    fx_l = (Ax*fp.y+Bx*fp.x);
    fx_u = (Ay*fp.y+By*fp.x);

    irlv1 = irlv0 = diff(e,f) * diff(e,h);

//#ifdef CORNER_B
//    irlv1      = (irlv0 * ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) );
//#endif
//#ifdef CORNER_D
//    vec4 c1 = i4.yzwx;
//    vec4 g0 = i5.wxyz;
//    irlv1     = (irlv0  *  ( neq(f,b) * neq(h,d) + eq(e,i) * neq(f,i4) * neq(h,i5) + eq(e,g) + eq(e,c) ) * (diff(f,f4) * diff(f,i) + diff(h,h5) * diff(h,i) + diff(h,g) + diff(f,c) + eq(b,c1) * eq(d,g0)));
//#endif
//#ifdef CORNER_C
    irlv1     = (irlv0  * ( neq(f,b) * neq(f,c) + neq(h,d) * neq(h,g) + eq(e,i) * (neq(f,f4) * neq(f,i4) + neq(h,h5) * neq(h,i5)) + eq(e,g) + eq(e,c)) );
//#endif

    irlv2l = diff(e,g) * diff(d,g);
    irlv2u = diff(e,c) * diff(b,c);

    vec4 fx45i = clamp((fx   + delta   -Co - Ci)/(2.0*delta  ), 0.0, 1.0);
    vec4 fx45  = clamp((fx   + delta   -Co     )/(2.0*delta  ), 0.0, 1.0);
    vec4 fx30  = clamp((fx_l + delta_l -Cx     )/(2.0*delta_l), 0.0, 1.0);
    vec4 fx60  = clamp((fx_u + delta_u -Cy     )/(2.0*delta_u), 0.0, 1.0);

    vec4 wd1 = wd( e, c,  g, i, h5, f4, h, f);
    vec4 wd2 = wd( h, d, i5, f, i4,  b, e, i);

    edri  = step(wd1, wd2) * irlv0;
    edr   = step(wd1 + vec4(0.1, 0.1, 0.1, 0.1), wd2) * step(vec4(0.5, 0.5, 0.5, 0.5), irlv1);
    edr_l = step( 2.*df(f,g), df(h,c) ) * irlv2l * edr;
    edr_u = step( 2.*df(h,c), df(f,g) ) * irlv2u * edr;

    fx45  = edr   * fx45;
    fx30  = edr_l * fx30;
    fx60  = edr_u * fx60;
    fx45i = edri  * fx45i;

    px = step(df(e,f), df(e,h));

//#ifdef SMOOTH_TIPS
    vec4 maximos = max(max(fx30, fx60), max(fx45, fx45i));
//#endif
//#ifndef SMOOTH_TIPS
//    vec4 maximos = max(max(fx30, fx60), fx45);
//#endif

    vec3 res1 = E;
    res1 = mix(res1, mix(H, F, px.x), maximos.x);
    res1 = mix(res1, mix(B, D, px.z), maximos.z);

    vec3 res2 = E;
    res2 = mix(res2, mix(F, B, px.y), maximos.y);
    res2 = mix(res2, mix(D, H, px.w), maximos.w);

    vec3 res = mix(res1, res2, step(c_df(E, res1), c_df(E, res2)));
    float texel_alpha = sample_texel(tc).a;

    return vec4(res, texel_alpha);
}
#endif

#ifdef FILTER_3POINT
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

   vec4 texel_10 = sample_texel(vec2(x + 1, y + 0));
   vec4 texel_01 = sample_texel(vec2(x + 0, y + 1));

   vec4 texel = texel_00
     + u_frac * (texel_10 - texel_00)
     + v_frac * (texel_01 - texel_00);

   return texel;
}
#endif

#ifdef FILTER_BILINEAR
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

   vec4 texel = texel_00 * (1. - u_frac) * (1. - v_frac)
     + texel_10 * u_frac * (1. - v_frac)
     + texel_01 * (1. - u_frac) * v_frac
     + texel_11 * u_frac * v_frac;

   return texel;
}
#endif

#ifdef FILTER_JINC2
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

vec3 min4(vec3 a, vec3 b, vec3 c, vec3 d)
{
    return min(a, min(b, min(c, d)));
}

vec3 max4(vec3 a, vec3 b, vec3 c, vec3 d)
{
    return max(a, max(b, max(c, d)));
}

vec4 resampler(vec4 x)
{
   vec4 res;

   res = (x==vec4(0.0, 0.0, 0.0, 0.0)) ?  vec4(wa*wb)  :  sin(x*wa)*sin(x*wb)/(x*x);

   return res;
}

vec4 get_texel_jinc2()
{
    vec3 color;
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

    vec3 c00 = sample_texel(tc    -dx    -dy).xyz;
    vec3 c10 = sample_texel(tc           -dy).xyz;
    vec3 c20 = sample_texel(tc    +dx    -dy).xyz;
    vec3 c30 = sample_texel(tc+2.0*dx    -dy).xyz;
    vec3 c01 = sample_texel(tc    -dx       ).xyz;
    vec3 c11 = sample_texel(tc              ).xyz;
    vec3 c21 = sample_texel(tc    +dx       ).xyz;
    vec3 c31 = sample_texel(tc+2.0*dx       ).xyz;
    vec3 c02 = sample_texel(tc    -dx    +dy).xyz;
    vec3 c12 = sample_texel(tc           +dy).xyz;
    vec3 c22 = sample_texel(tc    +dx    +dy).xyz;
    vec3 c32 = sample_texel(tc+2.0*dx    +dy).xyz;
    vec3 c03 = sample_texel(tc    -dx+2.0*dy).xyz;
    vec3 c13 = sample_texel(tc       +2.0*dy).xyz;
    vec3 c23 = sample_texel(tc    +dx+2.0*dy).xyz;
    vec3 c33 = sample_texel(tc+2.0*dx+2.0*dy).xyz;

    color = sample_texel(frag_texture_coord.xy).xyz;

    //  Get min/max samples
    vec3 min_sample = min4(c11, c21, c12, c22);
    vec3 max_sample = max4(c11, c21, c12, c22);
/*
      color = mat4x3(c00, c10, c20, c30) * weights[0];
      color+= mat4x3(c01, c11, c21, c31) * weights[1];
      color+= mat4x3(c02, c12, c22, c32) * weights[2];
      color+= mat4x3(c03, c13, c23, c33) * weights[3];
      mat4 wgts = mat4(weights[0], weights[1], weights[2], weights[3]);
      vec4 wsum = wgts * vec4(1.0,1.0,1.0,1.0);
      color = color/(dot(wsum, vec4(1.0,1.0,1.0,1.0)));
*/


    color = vec3(dot(weights[0], vec4(c00.x, c10.x, c20.x, c30.x)), dot(weights[0], vec4(c00.y, c10.y, c20.y, c30.y)), dot(weights[0], vec4(c00.z, c10.z, c20.z, c30.z)));
    color+= vec3(dot(weights[1], vec4(c01.x, c11.x, c21.x, c31.x)), dot(weights[1], vec4(c01.y, c11.y, c21.y, c31.y)), dot(weights[1], vec4(c01.z, c11.z, c21.z, c31.z)));
    color+= vec3(dot(weights[2], vec4(c02.x, c12.x, c22.x, c32.x)), dot(weights[2], vec4(c02.y, c12.y, c22.y, c32.y)), dot(weights[2], vec4(c02.z, c12.z, c22.z, c32.z)));
    color+= vec3(dot(weights[3], vec4(c03.x, c13.x, c23.x, c33.x)), dot(weights[3], vec4(c03.y, c13.y, c23.y, c33.y)), dot(weights[3], vec4(c03.z, c13.z, c23.z, c33.z)));
    color = color/(dot(weights[0], vec4(1,1,1,1)) + dot(weights[1], vec4(1,1,1,1)) + dot(weights[2], vec4(1,1,1,1)) + dot(weights[3], vec4(1,1,1,1)));

    // Anti-ringing
    vec3 aux = color;
    color = clamp(color, min_sample, max_sample);
    color = mix(aux, color, JINC2_AR_STRENGTH);

    // final sum and weight normalization
    vec4 texel = vec4(color, 1.0);
    return texel;
}

#endif

void main() {
   vec4 color;

      if (frag_texture_blend_mode == BLEND_MODE_NO_TEXTURE)
      {
         color = vec4(frag_shading_color, 0.);
      }
      else
      {
         vec4 texel;
         vec4 texel0 = sample_texel(vec2(frag_texture_coord.x,
                  frag_texture_coord.y));

#if defined(FILTER_SABR)
         texel = get_texel_sabr();
#elif defined(FILTER_XBR)
         texel = get_texel_xbr();
#elif defined(FILTER_BILINEAR)
         texel = get_texel_bilinear();
#elif defined(FILTER_3POINT)
         texel = get_texel_3point();
#elif defined(FILTER_JINC2)
         texel = get_texel_jinc2();
#else
         texel = texel0; //use nearest if nothing else is chosen
#endif

	 // texel color 0x0000 is always fully transparent (even for opaque
         // draw commands)
         if (is_transparent(texel0)) {
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
