# Texture-modulation headroom

The PSX modulates a texel by the vertex colour with `0x80` as unity, so the
product carries headroom above the channel ceiling: `0x1F * 0xFF >> 4 = 494`
against a `DitherLUT` input of 255 for full scale, **1.94x**. Everything above
that is clamped away by the table (`value >>= 3`, then clamp to `0x1F`), on
real hardware and here alike.

Whether an HDR path should keep that headroom instead of discarding it is a
question about content, not about the pipeline, and it is not the same
question as additive overbright:

* Additive overshoot happens where translucent layers stack. It is extra
  light, and letting it exceed reference white reads as a highlight.
* Modulation overshoot happens on **ordinary lit geometry**. A surface lit
  with a vertex colour above `0x80` is not emissive; the clamp is part of the
  look artists worked against. Lifting it would push large areas of normal
  scenery above reference white, which is a different thing from producing
  highlights and probably a worse-looking one.

So the number that matters is not just "does it saturate" but "how much of
the frame". A few percent of pixels is a highlight opportunity; a third of
the frame means the clamp is load-bearing and should stay.

## clamp.c

Checks the arithmetic above against the LUT rebuilt from `gpu.c`'s own
construction: that `0x80` is unity before dither and within one step after
it, where the saturation boundary actually falls (index 252 for every dither
phase), and that the largest reachable index really is 494.

```sh
gcc -O2 -std=gnu99 -Wall -Wextra -o clamp tools/gpu_modulate/clamp.c && ./clamp
```

No GPU, no content, no core.

## Measuring content

`-DPSX_MEASURE_MODULATE` compiles counters into `ModTexel`, off by default
and free when off -- this is a per-pixel inner loop, and the default build is
byte-identical with and without the code present.

```sh
make EXTRA_FLAGS=-DPSX_MEASURE_MODULATE
```

`make FLAGS=...` and `make CFLAGS=...` do not work for this: a command-line
assignment overrides every `+=` in the makefile, so the include paths go with
it. `EXTRA_FLAGS` exists for exactly this. The core then logs:

    [GPU modulate] pixels=N saturating=N (P%) channels=N peak=N (N.NNx)

`saturating` is pixels with any channel past the ceiling; `peak` is the
largest index seen, so `peak <= 255` means nothing was discarded at all and
there is no headroom to keep.

Software renderer only -- `ModTexel` is the software rasteriser's path. The
hardware renderers do the same modulation in `command_fragment.glsl` and
`primitive.frag` as `frag_shading_color * 2. * texel.rgb`, so the ratio
carries over, but the counters do not.
