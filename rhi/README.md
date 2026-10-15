# RHI API and OpenGL 3.3 Renderer

## RHI API

The RHI API is the means by which components of the Beetle PSX libretro core interface with the hardware renderers. The RHI API offers two general classes of functions for the core. The first class consists of various functions used to perform libretro-specific actions such as reading core options or preparing/finalizing the current emulation loop frame. The second class of functions consists of RHI interface functions that the emulated PSX GPU uses to issue commands to the hardware renderer. Not every emulated PSX GPU command has a corresponding RHI interface function, but the set of available functions can be extended or modified as necessary when bugs are discovered and higher accuracy is required.

Each unique hardware renderer will implement RHI interface functions as another layer of function calls, typically but not necessarily one per RHI interface function. The RHI interface should then select the correct function to call based on the currently running hardware renderer.

The RHI API also includes support for dumping RHI API calls to file, which can be utilized for debugging purposes by any renderers that implement RHI playback.

## OpenGL 3.3 Renderer

The OpenGL renderer is currently implemented in `rhi_lib_gl.c` and can be called via the functions exposed in `rhi_lib_gl.h`.

## Color modes

OpenGL and Vulkan share **Internal Color Depth**. **16 bpp** is the default.
These names describe rendering policy, not the monitor output format or the
physical format of every intermediate GPU image.

| Setting | Ordinary PS1 drawing | Filtered/HD drawing and core resolves | SDR display |
| --- | --- | --- | --- |
| Native (Unexpanded) | RGB5 source/result operations | Reduce to RGB5, including the final resolved image | Five-bit levels expanded as q*8; white is 248 |
| 16 bpp | The same RGB5 operations | Preserve extra shades from enhancements | Expand the RGB5 range to full white 255 |
| 32 bpp | Preserve byte color precision | Preserve extra shades | Full byte range, without RGB5 scanout reduction |

Native and 16 bpp have the same 32 ordinary levels per channel. Their white
expansion differs. Native deliberately also reduces enhanced results; 16 bpp
preserves the extra shades produced by filtering, replacement textures and
supported antialiasing/downsampling. An ordinary draw over an enhanced
background still applies its ordinary RGB5 destination/result rules.

**Dithering Pattern** remains independent of color depth. Eligible GP0 drawing
commands add the signed PS1 pattern to the source before saturation and color
reduction. Raw textures bypass modulation and this dither. In 32 bpp, enabling
dither does not impose RGB5 scanout. Native-resolution and internal-resolution
patterns retain their existing choices. OpenGL does not gain Vulkan-only
antialiasing/downsampling settings.

The emulated PS1's actual 24-bit display mode, commonly used for FMV, bypasses
RGB5 reduction and the 248 white limit in all three settings.

Modulated framebuffer-texture feedback retains its native-word arithmetic.
Native and 16 bpp store these RGB5 results as q*8 for later display expansion;
32 bpp expands them to full-range byte codes before blending. Ordinary RGB5
uploads and texture sources use that same 32-bpp expansion. Direct raw feedback
preserves already-rendered byte colors rather than expanding them again.
Converting color modes preserves whether textures came from GPU drawing or CPU
uploads, so a setting change cannot misclassify their source encoding.

OpenGL's existing rendered-palette cache retains RGB5 words. In 32 bpp this can
discard extra shades from a rendered palette that Vulkan preserves. Native-word
agreement does not establish complete 32-bpp color parity between the renderers.

### HDR

**Color Format** controls frontend output and requires a restart. When the
frontend accepts HDR10, the existing wide HDR rendering policy overrides the
selected internal color mode, preserves its extra precision and disables PS1
dithering. Rejected HDR requests use the selected SDR policy. Internal Color
Depth therefore does not select the display's 8-bit or 10-bit output format.
FP16 rendering is used where supported. If the device cannot render that
format, the existing byte-target fallback retains HDR output and clamps
over-white content.

### Existing configurations

The option key and saved values `16bpp(native)` and `32bpp` are retained.
The former now displays as **16 bpp**; `native` selects **Native (Unexpanded)**.
The separate Vulkan native-color toggle is removed. Re-select Internal Color
Depth when updating a configuration that used that toggle; there is no
automatic migration between the old and new settings.

Vulkan's old default retained higher precision while drawing, then reduced the
display when dithering was enabled. With dithering disabled it also retained
higher-color output. The shared default **16 bpp** now reduces ordinary draws
while rendering, even with dithering disabled. This affects existing Vulkan
users who left the old native-color toggle off as well as users who enabled it.
Select **32 bpp** to retain higher-precision ordinary drawing; dithering is then
an independent visual choice. These settings do not recreate the old late
display-only dither formula. Existing OpenGL depth selections retain their
saved values; enhanced 16-bpp sources have the preservation policy above.

### Scope and validation

Legacy Vulkan dithering was intentionally a late scanout presentation effect.
This change applies eligible PS1 dithering during drawing and uses scanout for
color expansion. Changing that presentation is intentional; the old effect was
not originally presented as hardware-accurate rendering.

These modes do not establish complete PS1 hardware accuracy. Geometry, texture
sampling, timing and interlacing still have separate limitations. Exact native
line selection remains limited to 1x with PGXP off; expanded
quads retain the enhanced/PGXP coverage needed to avoid geometry gaps.
Pre-existing mask/copy defects are separate from this color policy.

Line coverage and color interpolation have separate contracts. Exact pixel
selection is shared between the APIs as follows (integral coordinates,
unit W, PGXP off, and Vulkan single-sample unless stated):

| Drawing conditions | OpenGL coverage | Vulkan coverage |
| --- | --- | --- |
| 1x, Native/16 bpp, nearest, SDR | Exact native pixels | Exact native pixels |
| 1x, Native/16 bpp, texture filtering | Exact native pixels | Exact native pixels |
| 1x, 32 bpp or accepted HDR | Exact native pixels | Exact native pixels |
| 1x, nearest, HD replacements enabled | Exact native pixels for lines | Exact native pixels for lines |
| Upscaled, fractional geometry, or Vulkan MSAA | Expanded quads | Expanded quads |
| PGXP geometry | Expanded quads by default | Expanded quads |

PS1 lines are untextured: GL passes no HD handle for a line, so the polygon's
HD-handle check does not exclude lines merely because replacements are enabled.
Both renderers separate line coverage from shading eligibility. GL integer
color/UV interpolation still requires Native/16-bpp nearest non-HD drawing
without precise PGXP color or FP16 storage; broadening coverage does not change
those shading rules. GL's existing PGXP line-expansion opt-out retains its driver-line
route; the default quads protect contacts with PGXP polygons from geometry gaps.

Arithmetic fixtures, canonical VRAM comparisons and displayed gameplay
comparisons test different contracts. A passing finite suite is not a claim
that every game, enhancement or driver matches original hardware.

See [validation results and limits](COLOR_VALIDATION.md) for the tested domains,
platforms and performance baselines.

## Building

The RHI API and OpenGL renderer are components of the Beetle PSX libretro core. To build with OpenGL support, run `make HAVE_OPENGL=1` in the repository's top level directory. To build with all possible hardware renderers, instead run `make HAVE_HW=1`. To build with dump support, additionally pass `RHI_DUMP=1`.

## Coding Style

The preferred coding style for the rhi subdirectory is the libretro coding style. See: https://docs.libretro.com/development/coding-standards/

## Credits

The OpenGL renderer was originally authored by simias as a port/plugin of Rustation's GL renderer to Beetle PSX.
