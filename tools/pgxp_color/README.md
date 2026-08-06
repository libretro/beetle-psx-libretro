# PGXP precise-colour offline checks

Two checks on the PGXP precise-colour path, outside the emulator. No GPU, no
Vulkan device, no PSX content. `oracle` verifies that an accepted shadow is
*correct*; `transport` verifies that a shadow *arrives* at all.

    make -C tools/pgxp_color check

## Oracle (`oracle.c`) — what it actually covers

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

## Transport (`transport.c`)

`oracle.c` injects the shadow straight into the command buffer, so it says
nothing about whether a colour *survives the journey* from the GTE to a GP0
packet. `transport.c` drives that journey through the real PGXP functions —
`PGXP_pushRGBf`, the GTE register hooks, CPU register and memory tracking,
and the FIFO → command-buffer copy — in the same order `gte.c`,
`pgxp_cpu.c` and `gpu.c` call them. Only the two MIPS instruction words are
assembled by hand.

Five cases: the `swc2 $22` display-list idiom; the `mfc2` + OR-in-opcode +
`sw` idiom; a three-vertex gouraud packet with colours checked at their real
command-buffer offsets (and vertex slots checked *not* to be mistaken for
colours); ColorFIFO ordering across three pushes, which would catch an
off-by-one in the `DR[20..22]` shadow shift; and a negative control where
the CPU overwrites a list slot and the stale shadow must be refused.

### The finding: only the direct path carries colour

`swc2 $22` works — that is the dominant display-list idiom and it arrives
fully tracked, including through DMA, since `gpu.c` reads the tracked value
out of memory by address.

`mfc2` + OR + `sw` **does not**, and this is structural rather than a bug.
PGXP's CPU-side value model treats a tracked word as two 16-bit halves
(`x` = low, `y` = high) because PGXP exists to carry packed screen
coordinates. A colour word packs three 8-bit channels, which does not fit
that model, so `PGXP_CPU_AND` (which OR/XOR/NOR route through) reinterprets
the payload instead of preserving it.

That bounds the achievable hit rate: content that composes colour words on
the CPU cannot contribute, no matter how the renderer slice is written.
It is not a *safety* problem — the accept rule's requantization test refuses
the mangled shadow, which is exactly what it is for — but it means a low hit
rate in content should be read as "games touch colours on the CPU", not as
"transport is broken".

## Range (`range.c`)

The hit rate answers whether the precise colour can be *recovered*. It does
not answer whether recovering it would change any pixel, and for an HDR
renderer slice that is the deciding question. A colour whose channels all
sit at or below 255 requantizes to exactly the byte the architectural path
already carried, so a wide framebuffer has nothing to hold: a 90% hit rate
over content that never lights above white buys nothing at all.

`PGXP_GetColorRangeStats` measures that, over accepted words only -- a
refused shadow cannot be used however bright it is, and counting it would
overstate the case. `range.c` checks the counters against MAC values whose
classification is known by construction: the 255 ceiling is not over-white,
each bucket boundary lands in the lower bucket, the peak tracks the
brightest channel, and a refused shadow is not counted.

The measurement slice logs both lines together:

    [PGXP color] words=N hit=N (P%) value-miss=N quant-miss=N
    [PGXP color] over-white=N (P% of hits) buckets<=1.25x/1.5x/2x/>2x=... peak=...

Read the second one for the go/no-go. `over-white=0` means stop.

## What it does not cover

**The hit rate, and the over-white rate.** The three harnesses together
prove that an accepted shadow is correct, that the direct path delivers one,
and that the over-range counters count what they claim to. None of them says
how often real content uses that path or lights above white. Those two
numbers are the go/no-go for the renderer slice; read them from the
`[PGXP color]` lines the measurement slice logs.

**Real content.** Every colour here is synthesised. No game has been run
against either harness, and neither one is a substitute for doing so.

## Configurations verified

Both harnesses pass under all of:

* `-O0`, `-O2`, `-O3 -ffast-math` (64-bit)
* `-fsanitize=address,undefined`
* `-m32 -mfpmath=387`, including at `-O0` and with `-ffloat-store`
* `-m32 -mfpmath=sse -msse2`

The x87 runs matter because the round trip is a `float` divide by a power of
two followed by a widening `float -> double`; excess precision was argued to
be inert there rather than measured, and now it is measured. Neither the
divide (exact, exponent-only) nor the widening can round, so `-ffloat-store`
changes nothing — which is the expected result, and a mismatch under it
would have meant the argument was wrong.
