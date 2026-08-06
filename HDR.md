# HDR output

Beetle PSX can present its output as HDR10 — 10 bits per channel, PQ-encoded,
Rec.2020 — instead of the historical 8-bit-per-channel path. This document
covers what that does, every option that affects it, how the pieces interact,
and where the limits are.

Set **Color Format** to **30-bit Color (HDR)** and restart. Everything else on
this page is optional tuning.

---

## Why a PS1 emulator has anything to put in HDR

Two separate things, and it is worth keeping them apart because they are
solved by different parts of the pipeline.

**Precision.** The renderer already carries more than 8 bits internally. At
any internal resolution above 1x, upscaled gradients are computed at higher
precision than the 8-bit output can express, so the final quantisation is
where banding is introduced — not the source material. A 10-bit output keeps
what the renderer already had.

**Range.** The PSX blends by adding: additive semi-transparency sums the
source and destination, and where translucent layers stack the result exceeds
white. On an 8-bit path that overshoot is clamped and the detail inside it is
gone. A wide float framebuffer keeps it, and the HDR encode maps it into the
headroom above reference white instead of throwing it away.

Neither is an invention of new detail. Both are about not destroying detail
the renderer already produced.

---

## Requirements

| | |
|---|---|
| Renderer | Vulkan or OpenGL. The Software renderer has no HDR path and ignores every option here. |
| Frontend | Must accept `SET_PIXEL_FORMAT(HDR10_2101010)`. |
| Display | An HDR-capable screen, in HDR mode, with the frontend's HDR output enabled. |
| Restart | **Color Format** takes effect on restart. The rest apply immediately. |

### How the core knows HDR is really on

The core does not guess. `RETRO_PIXEL_FORMAT_HDR10_2101010` is a contract: it
tells the frontend the presented image is PQ Rec.2020, and a frontend that
cannot present HDR10 is required to **reject** it rather than silently
down-convert. So the return value is authoritative.

* Accepted → HDR encode engaged.
* Rejected → the core restores `XRGB8888` and falls back to the 24-bit path.
  Nothing is half-enabled and nothing is quietly wrong.

Either way it is logged, so a configuration that did not take is visible
rather than mysterious:

```
[Color Format] 30-bit HDR requested: engaged (paper white 200 nits, gamut 0, output mode 1).
[HDR] Display peak: 1000 nits
```

### Values taken from the frontend

Three values are the frontend's to own, not the core's, and are queried
rather than configured here:

| Value | Source | Used for |
|---|---|---|
| Paper white | `GET_HDR_PAPER_WHITE_NITS` | Where reference white lands, in nits |
| Display peak | `GET_HDR_MAX_NITS` | The ceiling highlights roll off toward |
| Colour Boost | `GET_HDR_EXPAND_GAMUT` | Which gamut rotation is applied |

RetroArch's paper-white and Colour Boost sliders change at runtime and do
**not** fire the core's option-update path, so all three are re-queried every
frame while HDR is active and take effect on the next present. Display peak is
re-read for the same reason: a stale value silently changes how highlights
roll off, which is harder to notice than a brightness shift.

If the frontend answers nothing, the fallbacks are the HDR10 reference:
200 nits paper white, 1000 nits peak.

---

## The encode, step by step

Applied at the display stage, to the finished frame.

**1. Linearise.** The framebuffer is gamma-encoded, so it is decoded to linear
light with the transfer selected by **Reference Display Gamma**.

This is applied to the *whole* value, including anything above 1.0 left by
additive blending. That is deliberate and it matters. Decoding only `[0,1]`
and treating the overshoot as if it were already linear mixes two domains in
one sum, and the slope then steps by `headroom / (2.4 × paper_white)` —
1.67× at 200/1000 nits — **exactly at reference white**. That contours any
gradient crossing white and makes dither grain visibly coarsen at the same
threshold. One transfer across the whole range keeps the encode continuous.

Content in `[0,1]` is unaffected: every transfer maps 1.0 to 1.0, so ordinary
colour lands on paper white and the roll-off below never engages. **The
standard range maps onto the SDR result.**

**2. Source primaries.** If **Source Colour Primaries** is not `Rec.709`, a
matrix rotates the coordinates from the assumed authoring display. Applied in
linear light.

**3. Scale to paper white.** Linear light is multiplied by the frontend's
paper-white value.

**4. Highlight roll-off.** Everything above paper white is compressed toward
the display peak. See below.

**5. Gamut rotation.** Rec.709 to the target container, keyed to the
frontend's Colour Boost, using the same matrices RetroArch applies — so
switching between SDR and HDR10 does not shift saturation.

**6. PQ encode.** SMPTE ST.2084 over 0–10000 nits.

---

## Highlight roll-off

`headroom = peak_nits − paper_white_nits`. Overshoot is normalised against it
and compressed by a shoulder function.

**HDR Highlight Roll-Off** selects the shoulder:

| | Curve | Behaviour |
|---|---|---|
| **Reinhard (Soft Knee)** *(default)* | `o/(o+1)` | Gentle, gradual |
| **ACES (Filmic)** | `1 − e^(−o)` | Rises faster, reaches peak sooner, punchier |

Only over-white content is affected. Everything in the standard range is
identical either way.

Two details that are easy to get wrong and are handled explicitly:

**Both shoulders have unit slope at the origin.** That is what keeps the
encode C1-continuous where overshoot meets reference white. A Narkowicz ACES
fit was used here previously and has slope 0.214 at the origin — it dropped
the slope 4.7× exactly at white, which is a visible step. Normalising it
restores the origin slope but sends the peak derivative to ~8.4, trading the
step for a worse spike just above white. `1 − e^(−x)` has unit slope at the
origin, a maximum derivative of 1, and is cheaper than the rational fit.

**The knee is driven by the brightest channel**, and the overshoot is scaled
by that shared factor. Kneeing each channel independently compresses the
brightest hardest, which desaturates hot coloured highlights toward white — a
saturated additive red would wash out as it got brighter. Sharing the factor
preserves the overshoot's chromaticity, so a hot red stays red.

If the frontend reports a peak at or below paper white, headroom clamps to
zero and the roll-off degenerates to a clamp at paper white, which is correct
rather than a negative range.

---

## Options

### Reference Display Gamma

Which display transfer the console's output is assumed to be viewed through.
Used to linearise before the HDR encode, and to decide what "average" means
when supersampled or multisampled samples are combined — those estimate
emitted light, so they are averaged as light rather than as stored values.

| | Suits |
|---|---|
| **BT.1886 (Gamma 2.4)** *(default)* | Matches the frontend's own SDR→HDR conversion and a TV-like reference |
| **Gamma 2.2** | A PC monitor tracking sRGB's nominal gamma |
| **sRGB (Piecewise)** | How Windows composites SDR content onto an HDR desktop; lifts shadow detail |

**This is a viewing-reference choice, not a correctness one.** It decides
whether HDR lands at the same brightness the 24-bit path did on *your*
display. Against a 2.2 monitor, 2.4 is 12.9% down in linear light at code 0.5
and 24.2% down at 0.25 — which reads as *"HDR looks dimmer and more
contrasty"*. If you see that, try **Gamma 2.2** or **sRGB (Piecewise)**.

All three agree exactly at 0.0 and 1.0, so paper white and the roll-off knee
do not move, and all extend monotonically past 1.0 so the additive overshoot
decodes with the same curve as everything else.

### HDR Additive Overbright

Whether additive and subtractive blend *sources* may exceed reference white.

| | Behaviour |
|---|---|
| **Off (Reference White Sources)** *(default)* | Each source is clamped to reference white before blending, matching real hardware. Highlights rise above white only where translucent layers stack. |
| **On (Boosted Sources)** | Bright modulated sources push roughly twice as hard, for punchier single-layer glow — lasers, lightning, flames — at the cost of accuracy. |

Opaque surfaces are clamped either way. On OpenGL this affects additive
sources only.

### HDR True Multi-Pass Blending

**Vulkan only.** How subtractive semi-transparency is blended. Both settings
floor the result at zero exactly as real hardware does.

| | Behaviour |
|---|---|
| **Off** *(default)* | Fixed-function blending plus one cheap floor pass per batch. Hardware-accurate at almost no cost. |
| **On** | Routes every subtractive primitive through the per-primitive programmable blend path used for mask-tested draws. Additionally lets **HDR Additive Overbright** boost subtractive sources, for deeper single-layer cuts. |

**On** costs a per-primitive synchronisation that adds up in subtractive-heavy
scenes — drop shadows, screen fades. Leave it off unless you specifically want
boosted subtractive sources.

### Source Colour Primaries

Which chromaticities the game's RGB values are interpreted against. PSX
content was not authored on a Rec.709 display, and which real colours a given
RGB triple stands for is a property of the monitor it was made on.

This is **independent of Analog Video Cable** — primaries belong to the
authoring display, not the wire — so it applies to RGB output too, and works
in 24-bit mode as well.

| | |
|---|---|
| **Rec.709 (Match 24-bit)** *(default)* | No rotation; HDR and 24-bit agree |
| **Auto (By Region)** | Picks SMPTE-C or EBU from the disc region |
| **SMPTE-C (NTSC Studio)** | NTSC-era studio standard; shifts colour ~6% |
| **EBU (PAL)** | Differs from Rec.709 in green alone, ~4% |
| **NTSC 1953 (Wide)** | Original FCC primaries; green sits 40% outside Rec.709 |

Every option maps some primary outside Rec.709. Under HDR10 the Rec.2020
container holds it; on an SDR output it clips. **NTSC 1953 in particular only
shows properly on a wide-gamut HDR display.** On the SDR path this costs two
extra `pow()` per pixel, so the default returns immediately and pays nothing.

NTSC 1953 is flavour rather than accuracy — it was reportedly retained in
Japan, but the better-documented NTSC-J difference is black setup, not
primaries.

---

## Interactions

**Internal Color Depth is overridden.** HDR needs a wide float target for
additive overshoot and the subtractive floor, so the scaled framebuffer is
`RGBA16F` regardless of the 16/32-bit setting, on both renderers.

**Dithering is force-disabled.** The wide target carries the precision the
dither exists to fake. This matches between renderers.

**Analog Video Cable works with HDR.** The analog chain resolves through a
dedicated HDR variant, so cable simulation and HDR compose rather than
conflict. The analog path already needs a wide intermediate — a UNORM target
would clamp the signal overshoot before the encode ever saw it.

**Debanding, where it is needed and where it is not.** Genuinely-8-bit
sources whose gradients are *already* quantised would band at 10-bit, so they
get about one 8-bit LSB of triangular-PDF noise, spatially distributed with
interleaved gradient noise and applied in gamma space. Three decorrelated
per-channel fields keep the grain free of chroma tint.

Interpolated content is deliberately **not** dithered — mipmap resolve and
YUV chroma already carry sub-8-bit precision that 10-bit preserves. FMV gets a
luma-only variant: its chroma is reconstructed at sub-8-bit precision by a
2×2 average plus bilinear, so only the per-pixel 8-bit luma steps need it. An
equal offset to R, G and B is chroma-neutral through the BT.601 matrix.

**Adaptive smoothing has no HDR variant.** When adaptive smoothing would
engage (Vulkan, scaled, non-24bpp, non-SSAA), the HDR path falls back to the
plain scaled quad.

---

## Renderer differences

| | Vulkan | OpenGL | Software |
|---|---|---|---|
| HDR10 output | yes | yes | — |
| 16F framebuffer | yes | yes, if fp16 is colour-renderable | — |
| Additive overbright | additive + subtractive | additive only | — |
| True multi-pass blending | yes | — | — |
| Analog cable under HDR | yes | yes | — |
| Adaptive smoothing under HDR | falls back to plain scaled quad | — | — |

---

## Troubleshooting

**Nothing changed.** Check the log for `[Color Format] 30-bit HDR requested`.
If it says *rejected by frontend*, the frontend is not presenting HDR10 —
check its HDR output setting and that the display is in HDR mode. Did you
restart after changing **Color Format**?

**Dimmer or more contrasty than 24-bit on the same display.** This is the
gamma reference, not a fault. Try **Reference Display Gamma** →
**Gamma 2.2**, then **sRGB (Piecewise)**.

**Highlights clip instead of rolling off.** The frontend is reporting a peak
at or below paper white. Check the frontend's peak-luminance and paper-white
settings.

**Colours look oversaturated or wrong.** Set **Source Colour Primaries** back
to **Rec.709 (Match 24-bit)**. `NTSC 1953` in particular is far outside
Rec.709 by design.

**Subtractive-heavy scenes stutter.** Turn **HDR True Multi-Pass Blending**
off — its per-primitive synchronisation is the expected cost.

**Software renderer shows nothing different.** Correct. There is no HDR path
there; the options are inert.

---

## Implementation

| | |
|---|---|
| `rhi/shaders_vulkan/hdr.h` | Shared encode math — PQ, transfers, primaries, gamut, shoulders, deband |
| `rhi/shaders_gl/analog_resolve_hdr.glsl.h` | OpenGL analog-chain HDR resolve |
| `libretro.c` | Frontend negotiation, per-frame re-query, option parsing |
| `rhi/rhi_lib_vulkan.c` | Vulkan HDR blitters, 16F target, blending paths |
| `rhi/rhi_lib_gl.c` | OpenGL HDR output program and 16F target |

The encode is parameterised throughout — paper white, peak, gamut, shoulder,
transfer and primaries are all passed in — so each shader supplies them from
its own push constant rather than the header reaching into a fixed layout.

The colour math deliberately matches RetroArch's own HDR composition, so an
HDR frame lands at the same brightness and saturation as the SDR one.
