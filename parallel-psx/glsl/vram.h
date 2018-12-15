#ifndef VRAM_H
#define VRAM_H

layout(location = 1) in mediump vec2 vUV;
layout(location = 2) flat in mediump ivec3 vParam;
layout(location = 3) flat in mediump ivec2 vBaseUV;
layout(location = 4) flat in mediump ivec4 vWindow;
layout(location = 5) flat in mediump ivec4 vTexLimits;
layout(set = 0, binding = 0) uniform mediump usampler2D uFramebuffer;

vec2 clamp_coord(vec2 coord)
{
	return clamp(coord.xy, vec2(vTexLimits.xy), vec2(vTexLimits.zw));
}

// Nearest neighbor
vec4 sample_vram_atlas(vec2 uvv)
{
    ivec3 params = vParam;
    int shift = params.z & 3;

    ivec2 uv = (ivec2(uvv) & vWindow.xy) | vWindow.zw;

    ivec2 coord;
    if (shift != 0)
    {
        int bpp = 16 >> shift;
        coord = ivec2(uv);
        int phase = coord.x & ((1 << shift) - 1);
        int align = bpp * phase;
        coord.x >>= shift;
        int value = int(texelFetch(uFramebuffer, (vBaseUV + coord) & ivec2(1023, 511), 0).x);
        int mask = (1 << bpp) - 1;
        value = (value >> align) & mask;

        params.x += value;
        coord = params.xy;
    }
    else
        coord = vBaseUV + uv;

    return abgr1555(texelFetch(uFramebuffer, coord & ivec2(1023, 511), 0).x);
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
bool is_transparent(vec4 texel)
{
	return rebuild_psx_color(texel) == 0U;
}

#ifdef FILTER_BILINEAR
vec4 sample_vram_bilinear(out float opacity)
{
  float x = vUV.x;
  float y = vUV.y;

  // interpolate from centre of texel
  vec2 uv_frac = fract(vec2(x, y)) - vec2(0.5, 0.5);
  vec2 uv_offs = sign(uv_frac);
  uv_frac = abs(uv_frac);

  // sample 4 nearest texels
  vec4 texel_00 = sample_vram_atlas(clamp_coord(vec2(x + 0., y + 0.)));
  vec4 texel_10 = sample_vram_atlas(clamp_coord(vec2(x + uv_offs.x, y + 0.)));
  vec4 texel_01 = sample_vram_atlas(clamp_coord(vec2(x + 0., y + uv_offs.y)));
  vec4 texel_11 = sample_vram_atlas(clamp_coord(vec2(x + uv_offs.x, y + uv_offs.y)));
  
  // test for fully transparent texel
  texel_00.w = 1. - float(is_transparent(texel_00));
  texel_10.w = 1. - float(is_transparent(texel_10));
  texel_01.w = 1. - float(is_transparent(texel_01));
  texel_11.w = 1. - float(is_transparent(texel_11));
	 
   // average samples
   vec4 texel = texel_00 * (1. - uv_frac.x) * (1. - uv_frac.y)
     + texel_10 * uv_frac.x * (1. - uv_frac.y)
     + texel_01 * (1. - uv_frac.x) * uv_frac.y
     + texel_11 * uv_frac.x * uv_frac.y;
     
   opacity = texel.w;
	
   // adjust colour to account for black transparent samples (assume rgb would be average of other pixels)
   texel.rgb = texel.rgb * (1./opacity);

   return texel;
}
#endif

#ifdef FILTER_XBR
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
   return sample_vram_atlas(coord + vec2(x, y));
}

vec4 sample_vram_xbr(out float opacity)
{
  //---------------------------------------
  // Input Pixel Mapping:  -|x|x|x|-
  //                       x|A|B|C|x
  //                       x|D|E|F|x
  //                       x|G|H|I|x
  //                       -|x|x|x|-

  vec2 scale = vec2(8.0);
  vec2 pos = fract(vUV.xy) - vec2(0.5, 0.5);
  vec2 coord = vUV.xy - pos;

  vec4 A = P(coord, -1,-1);
  A.w = 1. - float(is_transparent(A));
  vec4 B = P(coord,  0,-1);
  B.w = 1. - float(is_transparent(B));
  vec4 C = P(coord,  1,-1);
  C.w = 1. - float(is_transparent(C));
  vec4 D = P(coord, -1, 0);
  D.w = 1. - float(is_transparent(D));
  vec4 E = P(coord, 0, 0);
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

const  vec4 lum = vec4(0.21, 0.72, 0.07, 1.0);

vec4 lum_to(vec4 v0, vec4 v1, vec4 v2, vec4 v3) {
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

float c_df(vec4 c1, vec4 c2) {
	vec4 df = abs(c1 - c2);
	return df.r + df.g + df.b;
}

//sabr
vec4 sample_vram_sabr(out float opacity)
{
	vec2 tc = vUV.xy;
   vec4 xyp_1_2_3    = tc.xxxy + vec4(-1,  0, 1, -2);
   vec4 xyp_6_7_8    = tc.xxxy + vec4(-1,  0, 1, -1);
   vec4 xyp_11_12_13 = tc.xxxy + vec4(-1,  0, 1,  0);
   vec4 xyp_16_17_18 = tc.xxxy + vec4(-1,  0, 1,  1);
   vec4 xyp_21_22_23 = tc.xxxy + vec4(-1,  0, 1,  2);
   vec4 xyp_5_10_15  = tc.xyyy + vec4(-2, -1, 0,  1);
   vec4 xyp_9_14_9   = tc.xyyy + vec4( 2, -1, 0,  1);
   
	// Store mask values
	vec4 P1  = sample_vram_atlas(xyp_1_2_3.xw   );
	P1.w = 1. - float(is_transparent(P1));
	vec4 P2  = sample_vram_atlas(xyp_1_2_3.yw   );
	P2.w = 1. - float(is_transparent(P2));
	vec4 P3  = sample_vram_atlas(xyp_1_2_3.zw   );
	P3.w = 1. - float(is_transparent(P3));

	vec4 P6  = sample_vram_atlas(xyp_6_7_8.xw   );
	P6.w = 1. - float(is_transparent(P6));
	vec4 P7  = sample_vram_atlas(xyp_6_7_8.yw   );
	P7.w = 1. - float(is_transparent(P7));
	vec4 P8  = sample_vram_atlas(xyp_6_7_8.zw   );
	P8.w = 1. - float(is_transparent(P8));

	vec4 P11 = sample_vram_atlas(xyp_11_12_13.xw);
	P11.w = 1. - float(is_transparent(P11));
	vec4 P12 = sample_vram_atlas(xyp_11_12_13.yw);
	P12.w = 1. - float(is_transparent(P12));
	vec4 P13 = sample_vram_atlas(xyp_11_12_13.zw);
	P13.w = 1. - float(is_transparent(P13));

	vec4 P16 = sample_vram_atlas(xyp_16_17_18.xw);
	P16.w = 1. - float(is_transparent(P16));
	vec4 P17 = sample_vram_atlas(xyp_16_17_18.yw);
	P17.w = 1. - float(is_transparent(P17));
	vec4 P18 = sample_vram_atlas(xyp_16_17_18.zw);
	P18.w = 1. - float(is_transparent(P18));

	vec4 P21 = sample_vram_atlas(xyp_21_22_23.xw);
	P21.w = 1. - float(is_transparent(P21));
	vec4 P22 = sample_vram_atlas(xyp_21_22_23.yw);
	P22.w = 1. - float(is_transparent(P22));
	vec4 P23 = sample_vram_atlas(xyp_21_22_23.zw);
	P23.w = 1. - float(is_transparent(P23));

	vec4 P5  = sample_vram_atlas(xyp_5_10_15.xy );
	P5.w = 1. - float(is_transparent(P5));
	vec4 P10 = sample_vram_atlas(xyp_5_10_15.xz );
	P10.w = 1. - float(is_transparent(P10));
	vec4 P15 = sample_vram_atlas(xyp_5_10_15.xw );
	P15.w = 1. - float(is_transparent(P15));

	vec4 P9  = sample_vram_atlas(xyp_9_14_9.xy  );
	P9.w = 1. - float(is_transparent(P9));
	vec4 P14 = sample_vram_atlas(xyp_9_14_9.xz  );
	P14.w = 1. - float(is_transparent(P14));
	vec4 P19 = sample_vram_atlas(xyp_9_14_9.xw  );
	P19.w = 1. - float(is_transparent(P19));

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

	vec4 res1 = P12;
	res1 = mix(res1, mix(P13, P17, px.x), mac.x);
	res1 = mix(res1, mix(P7 , P13, px.y), mac.y);
	res1 = mix(res1, mix(P11, P7 , px.z), mac.z);
	res1 = mix(res1, mix(P17, P11, px.w), mac.w);

	vec4 res2 = P12;
	res2 = mix(res2, mix(P17, P11, px.w), mac.w);
	res2 = mix(res2, mix(P11, P7 , px.z), mac.z);
	res2 = mix(res2, mix(P7 , P13, px.y), mac.y);
	res2 = mix(res2, mix(P13, P17, px.x), mac.x);

   vec4 texel = vec4(mix(res1, res2, step(c_df(P12, res1), c_df(P12, res2))));
   opacity = texel.w;

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

   res = (x==vec4(0.0, 0.0, 0.0, 0.0)) ?  vec4(wa*wb)  :  sin(x*wa)*sin(x*wb)/(x*x);

   return res;
}

vec4 sample_vram_jinc2(out float opacity)
{
    vec4 color;
    vec4 weights[4];

    vec2 dx = vec2(1.0, 0.0);
    vec2 dy = vec2(0.0, 1.0);

    vec2 pc = vUV.xy;

    vec2 tc = (floor(pc-vec2(0.5,0.5))+vec2(0.5,0.5));

    weights[0] = resampler(vec4(d(pc, tc    -dx    -dy), d(pc, tc           -dy), d(pc, tc    +dx    -dy), d(pc, tc+2.0*dx    -dy)));
    weights[1] = resampler(vec4(d(pc, tc    -dx       ), d(pc, tc              ), d(pc, tc    +dx       ), d(pc, tc+2.0*dx       )));
    weights[2] = resampler(vec4(d(pc, tc    -dx    +dy), d(pc, tc           +dy), d(pc, tc    +dx    +dy), d(pc, tc+2.0*dx    +dy)));
    weights[3] = resampler(vec4(d(pc, tc    -dx+2.0*dy), d(pc, tc       +2.0*dy), d(pc, tc    +dx+2.0*dy), d(pc, tc+2.0*dx+2.0*dy)));

    dx = dx;
    dy = dy;
    tc = tc;

    vec4 c00 = sample_vram_atlas(clamp_coord(tc    -dx    -dy));
    c00.w = 1. - float(is_transparent(c00));
    vec4 c10 = sample_vram_atlas(clamp_coord(tc           -dy));
    c10.w = 1. - float(is_transparent(c10));
    vec4 c20 = sample_vram_atlas(clamp_coord(tc    +dx    -dy));
    c20.w = 1. - float(is_transparent(c20));
    vec4 c30 = sample_vram_atlas(clamp_coord(tc+2.0*dx    -dy));
    c30.w = 1. - float(is_transparent(c30));
    vec4 c01 = sample_vram_atlas(clamp_coord(tc    -dx       ));
    c01.w = 1. - float(is_transparent(c01));
    vec4 c11 = sample_vram_atlas(clamp_coord(tc              ));
    c11.w = 1. - float(is_transparent(c11));
    vec4 c21 = sample_vram_atlas(clamp_coord(tc    +dx       ));
    c21.w = 1. - float(is_transparent(c21));
    vec4 c31 = sample_vram_atlas(clamp_coord(tc+2.0*dx       ));
    c31.w = 1. - float(is_transparent(c31));
    vec4 c02 = sample_vram_atlas(clamp_coord(tc    -dx    +dy));
    c02.w = 1. - float(is_transparent(c02));
    vec4 c12 = sample_vram_atlas(clamp_coord(tc           +dy));
    c12.w = 1. - float(is_transparent(c12));
    vec4 c22 = sample_vram_atlas(clamp_coord(tc    +dx    +dy));
    c22.w = 1. - float(is_transparent(c22));
    vec4 c32 = sample_vram_atlas(clamp_coord(tc+2.0*dx    +dy));
    c32.w = 1. - float(is_transparent(c32));
    vec4 c03 = sample_vram_atlas(clamp_coord(tc    -dx+2.0*dy));
    c03.w = 1. - float(is_transparent(c03));
    vec4 c13 = sample_vram_atlas(clamp_coord(tc       +2.0*dy));
    c13.w = 1. - float(is_transparent(c13));
    vec4 c23 = sample_vram_atlas(clamp_coord(tc    +dx+2.0*dy));
    c23.w = 1. - float(is_transparent(c23));
    vec4 c33 = sample_vram_atlas(clamp_coord(tc+2.0*dx+2.0*dy));
    c33.w = 1. - float(is_transparent(c33));

    color = sample_vram_atlas(vUV.xy);

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
#endif

#ifdef FILTER_3POINT
vec4 sample_vram_3point(out float opacity)
{
  float x = vUV.x;
  float y = vUV.y;
  
  // interpolate from centre of texel
  vec2 uv_frac = fract(vec2(x, y)) - vec2(0.5, 0.5);
  vec2 uv_offs = sign(uv_frac);
  uv_frac = abs(uv_frac);

  vec4 texel_00;

  if (uv_frac.x + uv_frac.y < 1.0) {
    // Use bottom-left
    texel_00 = sample_vram_atlas(vUV.xy);
  } else {
    // Use top-right
    texel_00 = sample_vram_atlas(clamp_coord(vec2(x + uv_offs.x, y + uv_offs.y)));

    float tmp = 1. - uv_frac.y;
    uv_frac.y = 1. - uv_frac.x;
    uv_frac.x = tmp;
  }

   vec4 texel_10 = sample_vram_atlas(clamp_coord(vec2(x + uv_offs.x, y)));
   vec4 texel_01 = sample_vram_atlas(clamp_coord(vec2(x, y + uv_offs.y)));
   
   texel_00.w = 1. - float(is_transparent(texel_00));
   texel_10.w = 1. - float(is_transparent(texel_10));
   texel_01.w = 1. - float(is_transparent(texel_01));

   vec4 texel = texel_00
     + uv_frac.x * (texel_10 - texel_00)
     + uv_frac.y * (texel_01 - texel_00);
	
	opacity = texel.w;
   // adjust colour to account for black transparent samples (assume rgb would be average of other pixels)
   texel.rgb = texel.rgb * (1./opacity);

   return texel;
}
#endif

#endif
