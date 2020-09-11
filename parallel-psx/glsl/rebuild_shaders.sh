#!/bin/bash

# Prebuilds the shaders.
GLSLC="$(which glslc)"
if [ $? -ne 0 ]; then
	echo "=== Cannot find glslc (shaderc project) on your system. Cannot rebuild prebuilt shaders! ==="
	exit 1
fi

set -x

mkdir -p prebuilt

# Primitives shaders
"$GLSLC" -o prebuilt/flat.vert.inc -mfmt=c primitive.vert
"$GLSLC" -o prebuilt/flat.frag.inc -mfmt=c primitive.frag
"$GLSLC" -o prebuilt/textured.vert.inc -mfmt=c -DTEXTURED primitive.vert
"$GLSLC" -o prebuilt/textured.frag.inc -mfmt=c -DTEXTURED primitive.frag
"$GLSLC" -o prebuilt/textured.unscaled.frag.inc -mfmt=c -DTEXTURED -DUNSCALED primitive.frag
"$GLSLC" -o prebuilt/textured.msaa.frag.inc -mfmt=c -DTEXTURED -DMSAA primitive.frag
"$GLSLC" -o prebuilt/textured.msaa.unscaled.frag.inc -mfmt=c -DTEXTURED -DMSAA -DUNSCALED primitive.frag

# Feedback shaders
"$GLSLC" -o prebuilt/feedback.frag.inc -mfmt=c -DTEXTURED primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.unscaled.frag.inc -mfmt=c -DTEXTURED -DUNSCALED primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.flat.frag.inc -mfmt=c primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.frag.inc -mfmt=c -DTEXTURED -DMSAA primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.unscaled.frag.inc -mfmt=c -DTEXTURED -DMSAA -DUNSCALED primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.flat.frag.inc -mfmt=c -DMSAA primitive_feedback.frag

# Resolve shaders
"$GLSLC" -o prebuilt/resolve.scaled.comp.inc -mfmt=c -DSCALED resolve.comp
"$GLSLC" -o prebuilt/resolve.msaa.scaled.comp.inc -mfmt=c -DSCALED -DMSAA resolve.comp
"$GLSLC" -o prebuilt/resolve.unscaled.comp.inc -mfmt=c -DUNSCALED resolve.comp
"$GLSLC" -o prebuilt/resolve.msaa.unscaled.comp.inc -mfmt=c -DUNSCALED -DMSAA resolve.comp

# Quads
"$GLSLC" -o prebuilt/quad.vert.inc -mfmt=c quad.vert
"$GLSLC" -o prebuilt/unscaled.quad.frag.inc -mfmt=c -DUNSCALED quad.frag
"$GLSLC" -o prebuilt/scaled.quad.frag.inc -mfmt=c -DSCALED quad.frag
"$GLSLC" -o prebuilt/unscaled.dither.quad.frag.inc -mfmt=c -DDITHER -DUNSCALED quad.frag
"$GLSLC" -o prebuilt/scaled.dither.quad.frag.inc -mfmt=c -DDITHER -DSCALED quad.frag
"$GLSLC" -o prebuilt/bpp24.quad.frag.inc -mfmt=c -DUNSCALED -DBPP24 quad.frag
"$GLSLC" -o prebuilt/bpp24.yuv.quad.frag.inc -mfmt=c -DUNSCALED -DBPP24 -DBPP24_YUV quad.frag

# Copy VRAM shaders
"$GLSLC" -o prebuilt/copy_vram.comp.inc -mfmt=c copy_vram.comp
"$GLSLC" -o prebuilt/copy_vram.masked.comp.inc -mfmt=c -DMASKED copy_vram.comp

# Blit VRAM shaders
"$GLSLC" -o prebuilt/blit_vram.scaled.comp.inc -mfmt=c -DSCALED blit_vram.comp
"$GLSLC" -o prebuilt/blit_vram.masked.scaled.comp.inc -mfmt=c -DSCALED -DMASKED blit_vram.comp
"$GLSLC" -o prebuilt/blit_vram.cached.scaled.comp.inc -mfmt=c -DSCALED blit_vram_cached.comp
"$GLSLC" -o prebuilt/blit_vram.cached.masked.scaled.comp.inc -mfmt=c -DSCALED -DMASKED blit_vram_cached.comp

"$GLSLC" -o prebuilt/blit_vram.msaa.scaled.comp.inc -mfmt=c -DMSAA -DSCALED blit_vram.comp
"$GLSLC" -o prebuilt/blit_vram.msaa.masked.scaled.comp.inc -mfmt=c -DMSAA -DSCALED -DMASKED blit_vram.comp
"$GLSLC" -o prebuilt/blit_vram.msaa.cached.scaled.comp.inc -mfmt=c -DMSAA -DSCALED blit_vram_cached.comp
"$GLSLC" -o prebuilt/blit_vram.msaa.cached.masked.scaled.comp.inc -mfmt=c -DMSAA -DSCALED -DMASKED blit_vram_cached.comp

"$GLSLC" -o prebuilt/blit_vram.unscaled.comp.inc -mfmt=c -DUNSCALED blit_vram.comp
"$GLSLC" -o prebuilt/blit_vram.masked.unscaled.comp.inc -mfmt=c -DUNSCALED -DMASKED blit_vram.comp
"$GLSLC" -o prebuilt/blit_vram.cached.unscaled.comp.inc -mfmt=c -DUNSCALED blit_vram_cached.comp
"$GLSLC" -o prebuilt/blit_vram.cached.masked.unscaled.comp.inc -mfmt=c -DUNSCALED -DMASKED blit_vram_cached.comp

# Mipmap shaders
"$GLSLC" -o prebuilt/mipmap.vert.inc -mfmt=c mipmap.vert
"$GLSLC" -o prebuilt/mipmap.shifted.vert.inc -mfmt=c -DSHIFT_QUAD mipmap.vert
"$GLSLC" -o prebuilt/mipmap.resolve.frag.inc -mfmt=c mipmap_resolve.frag
"$GLSLC" -o prebuilt/mipmap.dither.resolve.frag.inc -mfmt=c -DDITHER mipmap_resolve.frag
"$GLSLC" -o prebuilt/mipmap.energy.first.frag.inc -mfmt=c -DFIRST_PASS mipmap_energy.frag
"$GLSLC" -o prebuilt/mipmap.energy.frag.inc -mfmt=c mipmap_energy.frag
"$GLSLC" -o prebuilt/mipmap.energy.blur.frag.inc -mfmt=c mipmap_blur.frag

