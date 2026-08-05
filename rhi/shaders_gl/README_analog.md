# Analog chain on the GL renderer

The analog video cable simulation was written for the Vulkan renderer. This
directory holds the GL half of it.

## How the shaders get here

They are not written twice. `gen_analog_gl.py` translates the sources in
`rhi/shaders_vulkan/` into GL/GLES GLSL and emits one `analog_*.glsl.h` per
program, containing a desktop (`#version 330 core`) and an ES
(`#version 300 es`) string. `rebuild_shaders.sh` runs the generator, so a
change to the signal math regenerates both backends and the two cannot drift.

Everything the translation touches is mechanical:

| Vulkan GLSL | GL GLSL |
| --- | --- |
| `#version 450` | `#version 330 core` / `300 es` |
| `#include "analog.h"` | expanded inline |
| `layout(set=,binding=) uniform sampler2D` | `uniform sampler2D` |
| `layout(push_constant) uniform R { ... } reg;` | `uniform` per member, `reg.x` → `reg_x` |
| `layout(location=) in/out` | plain `in`/`out` |

The uniform names the C side must set are listed in a comment at the top of
each generated header.

The signal math itself is untouched, which is the point: the tap tables, the
phase model, the colour matrices and the comb gain are the same text compiled
twice.

## Verifying

`gl_shader_check.c` builds every generated program in a real GL 3.3 core
context (EGL surfaceless - llvmpipe is fine, this checks the compiler, not the
picture) and reports whether each declared uniform survived linking:

```
cc -o gl_shader_check gl_shader_check.c -lEGL
LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless ./gl_shader_check
```

All eleven programs compile and link. The SDR `resolve` reports four uniforms
optimised out (`reg_paper_white_nits`, `reg_peak_nits`, `reg_expand_gamut`,
`reg_shoulder`); that is correct, since they are unreachable without `HDR`, and
the harness prints it separately from a failure so it does not read as one.

This checks that the GLSL is valid and that the names the C side will look up
exist. It does not check that the chain produces the right picture.

## Status

The chain runs on GL: the passes are wired into `rhi_gl_finalize_frame`, which
redirects the display draw into a texture, runs encode -> comb -> demod ->
trap -> resolve (or the single RGB band-limit pass), and blits the result.

It has *not* been looked at. No display was available where this was written,
so the evidence is that every pass executes with no GL error and no sanitizer
complaint - not that the picture is right. Before trusting it, compare against
the Vulkan renderer on the same content and settings: they run the same shader
sources, so any visible difference is a bug in this plumbing, most likely a
uniform that is set wrong or a pass reading the wrong target.

## What is not done yet

The core option description now says the chain works on OpenGL, so this is
user-facing: a bug here is visible to people, not just to whoever is reading
the code.


**The C plumbing.** `rhi_lib_gl.c` has no analog path at all yet: no
render targets for the signal/separated/decoded stages, no program objects, no
uniform upload, and no hook into the display path where the Vulkan renderer
calls `renderer_analog_apply`. That is the bulk of the remaining work, and
until it exists the option remains Vulkan-only in practice.

**The IIR trap needs compute, and degrades rather than disappearing.**
`analog_notch.comp` uses shared memory, a barrier, a cross-lane Hillis-Steele
scan and an `imageStore`, so it needs GL 4.3 or GLES 3.1 against this
renderer's GL/GLES 3.0 floor. The policy:

- **GL 4.3 / GLES 3.1 and above:** run the real trap, translated from the same
  compute source the Vulkan backend uses. Identical output.
- **Below that:** run `analog_yc` instead - the trap's own `enable == 0`
  branch, as a fragment shader. Composite and RF keep their band limits, comb,
  demodulation, PAL delay line, pedestal and RF beat, and show the hanging dots
  along horizontal colour edges that the trap removes.

The cable simulation is never switched off for lack of compute. Losing the
trap costs one artifact; losing the simulation costs all of it, and a composite
picture with hanging dots is far closer to composite than a sharp RGB one is.

That fallback is not a special case written for GL: `enable == 0` is what
S-Video already takes on both backends, because luma never shared a wire there
and a notch would only cost detail. `analog_yc.frag` is that branch lifted into
a fragment shader, kept beside the compute shader it mirrors so the delay line
and the colour matrices are not copied.

RGB/SCART never touches the trap at all - one band-limit pass, no comb - so it
is unaffected on every driver.

## Uniforms reported as optimised out

The harness prints these separately from failures because they are expected:

- SDR `resolve`: `reg_paper_white_nits`, `reg_peak_nits`, `reg_expand_gamut`,
  `reg_shoulder` - unreachable without `HDR`.
- NTSC `yc` and `notch`: `reg_line_split` - only read by the PAL delay line.

If one of these appears on a variant *not* listed here, something is wrong: the
C side would be setting a uniform the shader never uses, which usually means a
`#if` went the wrong way.
