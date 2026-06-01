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

   void main() {
      vec3 color;

      if (depth_24bpp == 0) {
         // Use the regular 16bpp mode, fetch directly from the framebuffer
         // texture. The alpha/mask bit is ignored here.
	vec2 off = vec2(offset) / vec2(1024., 512.);

	// GLES 5551 note: color reinterpretation shouldn't be done here
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

      frag_color = vec4(color, 1.0);
   }
);
