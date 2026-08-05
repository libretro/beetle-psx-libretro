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

## What is not done yet

**The C plumbing.** `rhi_lib_gl.c` has no analog path at all yet: no
render targets for the signal/separated/decoded stages, no program objects, no
uniform upload, and no hook into the display path where the Vulkan renderer
calls `renderer_analog_apply`. That is the bulk of the remaining work, and
until it exists the option remains Vulkan-only in practice.

**The IIR trap, which is the hard part.** `analog_notch.comp` is a compute
shader: shared memory, a barrier, a cross-lane Hillis-Steele scan and an
`imageStore`. That needs GL 4.3 or GLES 3.1. This renderer's floor is GL/GLES
3.0, so it cannot simply be translated the way the fragment stages were, and
the choice is a real one rather than a transform:

- *Require compute for the encoded tiers.* Gate on 4.3/3.1, and fall back to
  no simulation below it. Simplest, and matches what the trap is for, but
  silently downgrades on older drivers.
- *Multi-pass ping-pong scan.* A Hillis-Steele scan across texels is
  expressible as fragment passes: about 12 of them at 2560 samples, each a full
  target read and write. Works at the 3.0 floor, costs far more bandwidth than
  the compute version.
- *Skip the trap on GL.* Composite and RF would show the hanging dots the trap
  exists to remove. Cheapest and a visible quality split between backends;
  would need saying out loud in the option description rather than quietly.

S-Video is unaffected either way - it sets `enable = 0` on the trap, because
luma never shared a wire and there is no carrier residue to remove. RGB/SCART
is likewise unaffected: it is a single band-limit pass with no comb and no
trap, so it is the one tier that is complete on GL once the plumbing exists.
