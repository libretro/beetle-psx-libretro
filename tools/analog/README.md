# Analog chain offline check

Numeric A/B of the analog video path against an independent reference model,
outside the emulator. Runs on any Vulkan device including lavapipe, so it needs
no GPU and no PSX content.

## What it actually covers

`reference_chain.comp` includes the *real* `rhi/shaders_vulkan/analog.h` and
mirrors the four signal stages — encode, comb, demodulate, resolve — against
`compare.py`, which reimplements them from the filter design in numpy.

So it verifies the shared math: the prefix-sum tap tables, the zero-order hold
collapse, the phase model, the colour matrices, the comb gain, the modulation
axes, the luma tiers and the RF beat. A disagreement means one of those is
wrong.

## What it does not cover

The compute shader mirrors the fragment shaders rather than being them. It will
not catch a mistake in texture addressing, a push-constant layout mismatch, a
barrier that is missing, or a render pass wired to the wrong target. It also
does not exercise the IIR trap, the PAL delay line, interlaced input, or HDR
encoding.

A reference model agreeing with the shader that implements it is weaker
evidence than it looks: it catches transcription errors and misses conceptual
ones. Treat a pass as "the arithmetic survived the last refactor", not as
"the feature works".

## Why it is in the tree

It used to live outside the repo and silently stopped compiling for several
commits, when the encode moved to prefix sums and the band-pass was deleted.
Nothing noticed. `rebuild_shaders.sh` now compiles `reference_chain.comp`, so
an incompatible change to `analog.h` breaks the shader build rather than
quietly disabling the check.

## Running it

    glslc -o reference_chain_ntsc.spv reference_chain.comp
    glslc -o reference_chain_pal.spv  -DPAL reference_chain.comp
    cc -O2 -o runner runner.c -lvulkan
    python3 compare.py          # needs numpy + scipy

Expect worst-case error around 1.6e-05, which is the fp32 floor from carrying
subcarrier phase in single precision. Anything materially above that is a real
regression; the first thing to check is whether the harness itself is at fault,
because historically it usually has been.
