#include "shaders_common.h"

static const char *output_fragment = GLSL_FRAGMENT(
   // We're sampling from the internal framebuffer texture
   uniform sampler2D fb;
   // Framebuffer sampling: 0: Normal 16bpp mode, 1: Use 24bpp mode
   uniform int depth_24bpp;
   // Internal resolution upscaling factor. Necessary for proper 24bpp
   // display since we need to know how the pixels are laid out in RAM.
   uniform uint internal_upscaling;
   // Coordinates of the top-left displayed pixel in VRAM (1x resolution)
   uniform uvec2 offset;
   // Normalized relative offset in the displayed area in VRAM. Absolute
   // coordinates must take `offset` into account.
   in vec2 frag_fb_coord;

   out vec4 frag_color;

   // Take a normalized color and convert it into a 16bit 1555 ABGR
   // integer in the format used internally by the Playstation GPU.
   uint rebuild_color(vec4 color) {
      uint a = uint(floor(color.a + 0.5));
      uint r = uint(floor(color.r * 31. + 0.5));
      uint g = uint(floor(color.g * 31. + 0.5));
      uint b = uint(floor(color.b * 31. + 0.5));

      return (a << 15) | (b << 10) | (g << 5) | r;
   }

   // ---- HDR10 (PQ Rec.2020) output ------------------------------------
   // Ported from shaders_vulkan/hdr.h; same math as the Vulkan display
   // shaders so 30-bit output matches across renderers. hdr_active
   // follows the negotiated pixel format: once the frontend accepted
   // HDR10_2101010 the presented image must be PQ Rec.2020.
   uniform int hdr_active;
   uniform float hdr_paper_white;
   uniform float hdr_max_nits;
   // 0 Accurate (709->2020), 1 Expanded, 2 Wide (P3), 3 Super (709)
   uniform int hdr_expand_gamut;
   // highlight roll-off: 0 Reinhard, 1 filmic
   uniform int hdr_shoulder;
   // reference SDR transfer: 0 pure 2.4, 1 pure 2.2, 2 sRGB piecewise
   uniform int hdr_sdr_eotf;
   // 0 Rec.709, 1 SMPTE-C, 2 EBU, 3 NTSC 1953
   uniform int hdr_src_primaries;

   vec3 pq_encode(vec3 nits) {
      float pq_m1 = 2610.0 / 16384.0;
      float pq_m2 = (2523.0 / 4096.0) * 128.0;
      float pq_c1 = 3424.0 / 4096.0;
      float pq_c2 = (2413.0 / 4096.0) * 32.0;
      float pq_c3 = (2392.0 / 4096.0) * 32.0;
      vec3 y = clamp(nits / 10000.0, vec3(0.0), vec3(1.0));
      vec3 ym = pow(y, vec3(pq_m1));
      return pow((pq_c1 + pq_c2 * ym) / (1.0 + pq_c3 * ym), vec3(pq_m2));
   }

   vec3 sdr_to_linear(vec3 c) {
      if (hdr_sdr_eotf == 1)
         return pow(c, vec3(2.2));
      if (hdr_sdr_eotf == 2)
         return mix(pow((c + vec3(0.055)) / 1.055, vec3(2.4)),
                    c / 12.92,
                    vec3(lessThanEqual(c, vec3(0.04045))));
      return pow(c, vec3(2.4));
   }

   vec3 src_primaries_to_709(vec3 c) {
      if (hdr_src_primaries == 1)          /* SMPTE-C */
         return vec3(
             0.939542064 * c.r +  0.050181357 * c.g +  0.010276579 * c.b,
             0.017772223 * c.r +  0.965792862 * c.g +  0.016434914 * c.b,
            -0.001621600 * c.r + -0.004369750 * c.g +  1.005991350 * c.b);
      else if (hdr_src_primaries == 2)     /* EBU */
         return vec3(
             1.044043209 * c.r + -0.044043209 * c.g,
                                   c.g,
                                   0.011793378 * c.g +  0.988206622 * c.b);
      else if (hdr_src_primaries == 3)     /* NTSC 1953 */
         return vec3(
             1.486156846 * c.r + -0.403554906 * c.g + -0.082601940 * c.b,
            -0.025101109 * c.r +  0.954024686 * c.g +  0.071076423 * c.b,
            -0.027224002 * c.r + -0.044095233 * c.g +  1.071319235 * c.b);
      return c;
   }

   vec3 rec709_to_target(vec3 c) {
      if (hdr_expand_gamut == 1)           /* Expanded */
         return vec3(
             0.6274040 * c.r +  0.3292820 * c.g +  0.0433136 * c.b,
             0.0457456 * c.r +  0.9417770 * c.g +  0.0124772 * c.b,
            -0.0012106 * c.r +  0.0176041 * c.g +  0.9836070 * c.b);
      else if (hdr_expand_gamut == 2)      /* Wide (DCI-P3) */
         return vec3(
             0.8215873 * c.r +  0.1763479 * c.g +  0.0020641 * c.b,
             0.0328261 * c.r +  0.9695096 * c.g + -0.0023367 * c.b,
             0.0188038 * c.r +  0.0725063 * c.g +  0.9086907 * c.b);
      else if (hdr_expand_gamut == 3)      /* Super (stay Rec.709) */
         return c;
      return vec3(                         /* Accurate: 709 -> 2020 */
          0.6274040 * c.r + 0.3292820 * c.g + 0.0433136 * c.b,
          0.0690970 * c.r + 0.9195400 * c.g + 0.0113612 * c.b,
          0.0163916 * c.r + 0.0880132 * c.g + 0.8955950 * c.b);
   }

   vec3 encode_hdr10(vec3 rgb) {
      // One transfer function across the whole range, then compress the
      // overshoot above paper white with a C1-continuous knee driven by
      // the peak channel (chromaticity-preserving). See hdr.h for the
      // full derivation; the small-o series avoids the catastrophic
      // cancellation of (1-exp(-o))/o right at reference white.
      float headroom = max(hdr_max_nits - hdr_paper_white, 0.0);
      vec3 lin = src_primaries_to_709(sdr_to_linear(max(rgb, vec3(0.0)))) * hdr_paper_white;
      vec3 over = (headroom > 0.0)
                     ? max(lin - vec3(hdr_paper_white), vec3(0.0))
                     : vec3(0.0);
      float omax = max(over.r, max(over.g, over.b));
      float o = (omax > 0.0) ? (omax / headroom) : 0.0;
      float knee = 0.0;
      if (o > 0.0) {
         if (hdr_shoulder == 1)
            knee = (o < 1e-2)
                      ? (1.0 - o * (0.5 - o * (1.0 / 6.0)))
                      : ((1.0 - exp(-o)) / o);
         else
            knee = 1.0 / (o + 1.0);
      }
      lin = min(lin, vec3(hdr_paper_white)) + over * knee;
      lin = rec709_to_target(lin);
      return pq_encode(lin);
   }

   // Debanding dither for the genuinely quantised paths (see hdr.h):
   // ~1 8-bit-LSB of triangular-PDF noise via interleaved gradient
   // noise, three decorrelated per-channel fields so the grain carries
   // no chroma tint. Applied in gamma space before the PQ encode.
   float hdr_ign(vec2 p, vec2 k) {
      return fract(52.9829189 * fract(dot(p, k)));
   }

   float hdr_tri(float u) {
      return u < 0.5 ? sqrt(2.0 * u) - 1.0 : 1.0 - sqrt(2.0 - 2.0 * u);
   }

   vec3 hdr_deband(vec3 rgb, vec2 fragcoord) {
      vec3 t = vec3(
         hdr_tri(hdr_ign(fragcoord, vec2( 0.06711056,  0.00583715))),
         hdr_tri(hdr_ign(fragcoord, vec2( 0.00583715,  0.06711056))),
         hdr_tri(hdr_ign(fragcoord, vec2( 0.06711056, -0.00583715))));
      return rgb + t * (1.0 / 255.0);
   }

   void main() {
      vec3 color;

      if (depth_24bpp == 0) {
         // Use the regular 16bpp mode, fetch directly from the framebuffer
         // texture. The alpha/mask bit is ignored here.
	vec2 off = vec2(offset) / vec2(1024., 512.);

	color = texture(fb, frag_fb_coord + off).rgb;
      } else {
         // In this mode we have to interpret the framebuffer as containing
         // 24bit RGB values instead of the usual 16bits 1555.
         ivec2 fb_size = textureSize(fb, 0);

         uint x_24 = uint(frag_fb_coord.x * float(fb_size.x));
         uint y = uint(frag_fb_coord.y * float(fb_size.y));

         uint x_native = x_24 / internal_upscaling;

         x_24 = x_native * internal_upscaling;

         // The 24bit color is stored over two 16bit pixels, convert the
         // coordinates
         uint x0_16 = (x_24 * 3U) / 2U;

	 // Add the offsets
	 x0_16 += offset.x * internal_upscaling;
	 y     += offset.y * internal_upscaling;

         // Move on to the next pixel at native resolution
         uint x1_16 = x0_16 + internal_upscaling;

         uint col0 = rebuild_color(texelFetch(fb, ivec2(x0_16, y), 0));
         uint col1 = rebuild_color(texelFetch(fb, ivec2(x1_16, y), 0));

         uint col = (col1 << 16) | col0;

         // If we're drawing an odd 24 bit pixel we're starting in the
         // middle of a 16bit cell so we need to adjust accordingly.
         col >>= 8U * (x_native & 1U);

         // Finally we can extract and normalize the 24bit pixel
         float b = float((col >> 16U) & 0xffU) / 255.;
         float g = float((col >> 8U) & 0xffU) / 255.;
         float r = float(col & 0xffU) / 255.;

         color = vec3(r, g, b);
      }

      if (hdr_active != 0) {
         // Both display paths carry quantised content (5-bit VRAM or
         // 8-bit FMV) that would band at 10-bit; deband, then encode.
         color = hdr_deband(color, gl_FragCoord.xy);
         frag_color = vec4(encode_hdr10(color), 1.0);
         return;
      }

      frag_color = vec4(color, 1.0);
   }
);
