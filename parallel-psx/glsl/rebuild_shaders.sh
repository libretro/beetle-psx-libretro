#!/bin/bash

# Prebuilds the shaders.
GLSLC="$(which glslc)"
if [ $? -ne 0 ]; then
	echo "=== Cannot find glslc (shaderc project) on your system. Cannot rebuild prebuilt shaders! ==="
	exit 1
fi

set -x

mkdir -p prebuilt

# nearest
"$GLSLC" -o prebuilt/flat.vert.inc -mfmt=c primitive.vert
"$GLSLC" -o prebuilt/flat.frag.inc -mfmt=c primitive.frag
"$GLSLC" -o prebuilt/textured.vert.inc -mfmt=c -DTEXTURED primitive.vert
"$GLSLC" -o prebuilt/textured.frag.inc -mfmt=c -DTEXTURED primitive.frag

# Feedback shaders
"$GLSLC" -o prebuilt/feedback.add.frag.inc -mfmt=c -DTEXTURED -DBLEND_ADD primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.avg.frag.inc -mfmt=c -DTEXTURED -DBLEND_AVG primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.sub.frag.inc -mfmt=c -DTEXTURED -DBLEND_SUB primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.add_quarter.frag.inc -mfmt=c -DTEXTURED -DBLEND_ADD_QUARTER primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.flat.add.frag.inc -mfmt=c -DBLEND_ADD primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.flat.avg.frag.inc -mfmt=c -DBLEND_AVG primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.flat.sub.frag.inc -mfmt=c -DBLEND_SUB primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.flat.add_quarter.frag.inc -mfmt=c -DBLEND_ADD_QUARTER primitive_feedback.frag

"$GLSLC" -o prebuilt/feedback.msaa.add.frag.inc -mfmt=c -DTEXTURED -DBLEND_ADD -DMSAA primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.avg.frag.inc -mfmt=c -DTEXTURED -DBLEND_AVG -DMSAA primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.sub.frag.inc -mfmt=c -DTEXTURED -DBLEND_SUB -DMSAA primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.add_quarter.frag.inc -mfmt=c -DTEXTURED -DBLEND_ADD_QUARTER -DMSAA primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.flat.add.frag.inc -mfmt=c -DBLEND_ADD -DMSAA primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.flat.avg.frag.inc -mfmt=c -DBLEND_AVG -DMSAA primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.flat.sub.frag.inc -mfmt=c -DBLEND_SUB -DMSAA primitive_feedback.frag
"$GLSLC" -o prebuilt/feedback.msaa.flat.add_quarter.frag.inc -mfmt=c -DBLEND_ADD_QUARTER -DMSAA primitive_feedback.frag

# Resolve shaders
"$GLSLC" -o prebuilt/resolve.scaled.comp.inc -mfmt=c -DSCALED resolve.comp
"$GLSLC" -o prebuilt/resolve.msaa.scaled.comp.inc -mfmt=c -DSCALED -DMSAA resolve.comp
"$GLSLC" -o prebuilt/resolve.unscaled.2.comp.inc -mfmt=c -DUNSCALED -DSCALE=2 resolve.comp
"$GLSLC" -o prebuilt/resolve.unscaled.4.comp.inc -mfmt=c -DUNSCALED -DSCALE=4 resolve.comp
"$GLSLC" -o prebuilt/resolve.unscaled.8.comp.inc -mfmt=c -DUNSCALED -DSCALE=8 resolve.comp
"$GLSLC" -o prebuilt/resolve.unscaled.16.comp.inc -mfmt=c -DUNSCALED -DSCALE=16 resolve.comp

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

