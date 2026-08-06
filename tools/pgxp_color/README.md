# PGXP precise-colour oracle check

Verifies the accept rule that the PGXP precise-colour path rests on, outside
the emulator. No GPU, no Vulkan device, no PSX content.

    make -C tools/pgxp_color check

## What it actually covers

`oracle.c` links the **real** `pgxp/pgxp_gpu.c`, so `PGXP_GetColor()` under
test is the shipping function rather than a copy of it. Against it sits a
replica of the GTE side — the `Lm_C(MACn >> 4)` saturation from
`MAC_to_RGB_FIFO()` in `mednafen/psx/gte.c`, mirrored because `Lm_C` is a
`static INLINE` that also mutates GTE `FLAGS`.

The property under test:

> requantizing the float shadow that `MAC_to_RGB_FIFO` pushed reproduces the
> architectural RGB bytes exactly, for every MAC value

That is what makes the accept rule sound. If it holds, an accepted shadow
cannot belong to a colour the game altered: any alteration changes at least
one byte, and any byte change fails requantization. If it fails anywhere,
precise colours could be applied to a primitive the game meant to be a
different colour — a wrong-colour polygon, not a subtle one.

Five phases:

1. Exhaustive, stride 1, over `MAC in [-256, 4351]` — every value that
   produces a distinct byte, both clamp boundaries, and all 16 sub-LSB
   fractional positions of each. Swept per channel and against fixed
   extremes on the other two, so a channel mix-up cannot pass.
2. Saturating tails and `INT32_MIN`/`INT32_MAX`, where the `int -> float`
   conversion is lossy. The claim being checked is that lossy conversion is
   harmless there because both sides saturate to the same byte.
3. Three million uniform random triples over the full `int32` domain.
4. Half a million single-bit perturbations of the colour word, standing in
   for a game that read the colour back and modified it. Every one must be
   refused. Current result: all 500,000 refused, zero false accepts.
5. Command-code byte independence — games OR the GP0 opcode into byte 3 and
   the GTE carries an unrelated CD value there, so the match deliberately
   ignores it.

Clean under `-fsanitize=address,undefined`, and at `-O0`, `-O2`, and
`-O3 -ffast-math` (the round trip is a power-of-two divide, so it does not
depend on strict FP).

## What it does not cover

**The hit rate.** This harness proves that an accepted shadow is correct. It
says nothing about how often a shadow survives from the GTE to the GP0
packet in real content — that depends on what games do with colours between
the two, and only running content answers it. That number, not this check,
is the go/no-go for the renderer slice; read it from the `[PGXP color]` line
the measurement slice logs.

**Transport.** The shadow is injected directly with `PGXP_WriteCB`. The real
path (MFC2/SWC2 → CPU register tracking → memory tracking → command-buffer
shadow) is PGXP's existing machinery and is not exercised here.

**32-bit x87.** Not run: no multilib in the environment it was written in.
The round trip is a power-of-two divide and a widening `float -> double`, so
excess precision should be inert, but this is unverified on x87 targets.
