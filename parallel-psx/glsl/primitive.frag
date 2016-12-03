#version 310 es
precision mediump float;
precision mediump int;

#include "common.h"
#include "primitive.h"

void main()
{
#ifdef TEXTURED
   vec4 NNColor = sample_vram_atlas(ivec2(vUV));

   // Even for opaque draw calls, this pixel is transparent.
   // Sample in NN space since we need to do an exact test against 0.0.
   // Doing it in a filtered domain is a bit awkward.
#ifdef SEMI_TRANS_OPAQUE
   // In this pass, only accept opaque pixels.
   if (all(equal(NNColor, vec4(0.0))) || NNColor.a > 0.5)
      discard;
#elif defined(OPAQUE) || defined(SEMI_TRANS)
   if (all(equal(NNColor, vec4(0.0))))
      discard;
#else
#error "Invalid defines."
#endif

#ifdef SEMI_TRANS
   // To avoid opaque pixels from bleeding into the semi-transparent parts,
   // sample nearest-neighbor only in semi-transparent parts of the image.
   vec4 color = NNColor;
#else
   //vec4 color = sample_vram_bilinear(NNColor);
   vec4 color = NNColor;
#endif
   vec3 shaded = color.rgb * vColor.rgb * (255.0 / 128.0);
   FragColor = vec4(shaded, NNColor.a + vColor.a);
#else
   FragColor = vColor;
#endif

   // Get round down behavior instead of round-to-nearest.
   // This is required for various "fade" out effects.
   // However, don't accidentially round down if we are already rounded to avoid
   // unintended feedback effects.
   FragColor.rgb -= 0.49 / 255.0;

#if 0
#if defined(TEXTURED)
   if ((vParam.z & 0x100) != 0)
      FragColor.rgb += textureLod(uDitherLUT, gl_FragCoord.xy * 0.25, 0.0).xxx - 4.0 / 255.0;
#endif
   FragColor.rgb = quantize_bgr555(FragColor.rgb);
#endif
}
