[![Build Status](https://travis-ci.org/libretro/beetle-psx-libretro.svg?branch=master)](https://travis-ci.org/libretro/beetle-psx-libretro)
[![Build status](https://ci.appveyor.com/api/projects/status/qd1ew088woadbqhc/branch/master?svg=true)](https://ci.appveyor.com/project/bparker06/beetle-psx-libretro/branch/master)

# Beetle PSX libretro

Beetle PSX is a port/fork of Mednafen's PSX module to the libretro API. Beetle PSX currently runs on Linux, OSX and Windows.

Notable additions in this fork are:
* PBP and CHD file format support, developed by Zapeth;
* Software renderer internal resolution upscaling, implemented by simias;
* An OpenGL 3.3 renderer, developed by simias;
* A Vulkan renderer, developed by TinyTiger;
* PGXP perspective correct texturing and subpixel precision, developed by iCatButler;
* OpenBIOS, allowing the emulator to be used without a BIOS file;
* HD texture replacement caching overhaul (Vulkan renderer), see [HD_TEXTURE_CACHE.md](HD_TEXTURE_CACHE.md);
* Page-aligned HD texture dump/replacement, an opt-in mode for static/3D art (Vulkan renderer), see [PAGE_ALIGN.md](PAGE_ALIGN.md);
* HD Reduce Palette Range, an opt-in hash of only a texture's used palette entries for better replacement match coverage (Vulkan renderer);
* Video CD playback, including support for the 1 MB SCPH-5903 kernel, see [Video CD](#video-cd-scph-5903);
* HDR10 output (PQ Rec.2020, 10-bit), with highlight roll-off, gamut and reference-gamma controls, see [HDR output](#hdr-output);

## HD texture replacement caching

This fork overhauls the Vulkan renderer's HD texture replacement pipeline so packs stay smooth on demanding content — particularly multi-palette animated sprites like Alucard in *Castlevania: Symphony of the Night*. It adds a three-tier, decode-once cache (VRAM images → RAM pixels → disk, LRU-evicted), binds cached textures in the same frame they're drawn to eliminate per-frame pop-in, and decodes PNGs on a 4-thread pool. New core options let you choose the **caching method** — *Eager* (the stock-Beetle default: prefetch all of a texture's palettes) or *Lazy* (load each texture+palette on demand) — and set the **VRAM/RAM cache budgets** (defaults 3 GB / 2 GB). The on-disk pack format is unchanged. Full details: [HD_TEXTURE_CACHE.md](HD_TEXTURE_CACHE.md).

Tested with **RetroArch 1.22.2** (git 69a4f0e, build date Nov 20 2025, Compiler: MinGW 10.2.0 64-bit) on Windows.

## Page-aligned texture replacement (experimental)

An opt-in alternative to per-upload-rectangle HD textures: the Vulkan renderer can
dump and replace at whole VRAM texture-page granularity (clean 256×256 tiles) instead
of the fragmented upload-rectangle sections, which is friendlier for authoring static
backgrounds, UI and 3D art. It layers on top of the HD texture cache above and reuses
the same three-tier cache, IO pool and budgets. Default behaviour is unchanged
(upload-rect), so existing packs are unaffected.

Options (all default to the classic upload-rect behaviour):
* **HD Dump Mode** — `Upload-rect` / `Page-aligned` / `Both` (collect both pack types in one playthrough).
* **HD Replacement Mode** — `Upload-rect` / `Page-aligned`, with an optional **Cross-Mode Fallback** so one pack type can fill gaps from the other without converting packs.
* **HD Reduce Palette Range** — hash only the CLUT entries a texture actually uses (not the whole CLUT), so one replacement keeps matching across unused/rewritten palette slots; applies to both upload-rect and page paths. Backward-compatible with existing packs.
* **HD Texture Caching Method** also gains **Lazy (synchronous)** — load on first use but block until ready (no pop-in, may briefly stutter when many new textures appear at once).
* **HD Texture Folder** — keep the dump/replacement folders under the Content, System or Save directory (auto-created).
* Live hotkeys (requires RetroArch **Game Focus**): `]` toggles HD replacements with an on-screen message; `'` reloads replacements from disk.

Page packs and upload-rect packs are **not** interchangeable (the hash covers a
different region of VRAM). Full details and the authoring workflow:
[PAGE_ALIGN.md](PAGE_ALIGN.md).

## HDR output

With **Color Format** set to **30-bit Color (HDR)**, the Vulkan and OpenGL
renderers present HDR10 — 10 bits per channel, PQ-encoded, Rec.2020 — instead
of the historical 8-bit-per-channel output. Set it and restart; everything
else below is optional tuning.

### Why a PS1 emulator has anything to put in HDR

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

### Requirements

| | |
|---|---|
| Renderer | Vulkan or OpenGL. The Software renderer has no HDR path and ignores every option here. |
| Frontend | Must accept `SET_PIXEL_FORMAT(HDR10_2101010)`. |
| Display | An HDR-capable screen, in HDR mode, with the frontend's HDR output enabled. |
| Restart | **Color Format** takes effect on restart. The rest apply immediately. |

#### How the core knows HDR is really on

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

#### Values taken from the frontend

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

### The encode, step by step

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

### Highlight roll-off

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

### Options

#### Reference Display Gamma

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

#### HDR Additive Overbright

Whether additive and subtractive blend *sources* may exceed reference white.

| | Behaviour |
|---|---|
| **Off (Reference White Sources)** *(default)* | Each source is clamped to reference white before blending, matching real hardware. Highlights rise above white only where translucent layers stack. |
| **On (Boosted Sources)** | Bright modulated sources push roughly twice as hard, for punchier single-layer glow — lasers, lightning, flames — at the cost of accuracy. |

Opaque surfaces are clamped either way. On OpenGL this affects additive
sources only.

#### HDR True Multi-Pass Blending

**Vulkan only.** How subtractive semi-transparency is blended. Both settings
floor the result at zero exactly as real hardware does.

| | Behaviour |
|---|---|
| **Off** *(default)* | Fixed-function blending plus one cheap floor pass per batch. Hardware-accurate at almost no cost. |
| **On** | Routes every subtractive primitive through the per-primitive programmable blend path used for mask-tested draws. Additionally lets **HDR Additive Overbright** boost subtractive sources, for deeper single-layer cuts. |

**On** costs a per-primitive synchronisation that adds up in subtractive-heavy
scenes — drop shadows, screen fades. Leave it off unless you specifically want
boosted subtractive sources.

#### Source Colour Primaries

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

### Interactions

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

### Renderer differences

| | Vulkan | OpenGL | Software |
|---|---|---|---|
| HDR10 output | yes | yes | — |
| 16F framebuffer | yes | yes, if fp16 is colour-renderable | — |
| Additive overbright | additive + subtractive | additive only | — |
| True multi-pass blending | yes | — | — |
| Analog cable under HDR | yes | yes | — |
| Adaptive smoothing under HDR | falls back to plain scaled quad | — | — |

### Troubleshooting

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

### Implementation

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

## Video CD (SCPH-5903)

The SCPH-5903 is a PlayStation sold in Asia in 1996 with an MPEG-1 decoder
daughterboard, able to play Video CDs. It is the only PS1 model that could.
This fork plays VCDs, with or without that machine's kernel.

Two things are worth knowing about the hardware, because they shape how this
works. The daughterboard is not on the CPU bus: it hangs off three GPIO pins
of the CD-ROM sub-CPU and takes its data straight from the CD DSP's serial
audio bus, and its video and audio outputs are selected against the GPU's and
SPU's by analogue multiplexors on the mainboard. So MPEG video never enters
VRAM and MPEG audio never enters the SPU — Video CD mode substitutes the
whole video and audio front end rather than drawing into the frame the GPU
produced. And Video CD is **MPEG-1**, not the H.261 it is sometimes said to
be; MPEG-1 borrows from H.261 but adds B-frames and half-sample motion
compensation, which is exactly why the PS1's MDEC — intra-only, no motion
compensation — could not do the job and the extra board existed at all.

A Video CD is detected automatically from the disc and one of two modes is
selected:

* **HLE** (any BIOS, or OpenBIOS) — the PSX is not booted at all; there is no
  PSX-side program on a Video CD to run, and the stock shell would report
  *Audio Disk !!* or reject the disc. The core supplies the player itself.
  **Start** plays and pauses, **Select** stops, **Left**/**Right** change
  track. This is the mode to use.
* **Daughterboard** (SCPH-5903 kernel loaded) — the kernel's own Video CD
  player runs and the core stands in for the MPEG board, answering CD-ROM
  command `1Fh`. See the caveat below.

### Using the SCPH-5903 kernel

The kernel is 1 MB rather than the usual 512 KB, and the core previously
could not load it at all. It is a Japanese-region machine, so it expects SCEI
discs.

```
size 1048576
md5  81328b966e6dcf7ea1e32e55e1c104bb
sha1 15c94da3cc5a38a582429575af4198c487fe893c
```

Point the BIOS path override at it, or drop it in the system directory for
the firmware scan to find. It is not used unless selected — it is only wanted
for the Video CD case.

### Decoding

The MPEG-1 program stream demultiplexer and video decoder live in
libretro-common as `rmpeg1`, written from ITU-T H.262 (whose Annex B carries
the same code tables as ISO/IEC 11172-2) rather than derived from an existing
implementation. Audio is MPEG-1 layer II, which libretro-common's `rmp3`
already decodes. Nothing here is vendored third-party code.

The video decoder's IDCT is within the IEEE 1180-1990 peak error of 1 against
a double-precision reference, and the tables it uses are generated from the
specification by a script that proves each one prefix-free and checks its
Kraft sum before emitting it. Test harnesses are in
`libretro-common/tools/mpeg1` and `tools/vcd`.

### Limitations

* **Daughterboard mode has not been tested on real hardware.** The protocol
  itself is no longer guesswork: the daughterboard's firmware has never been
  dumped, but the kernel's interpretation of what the board says is in the
  kernel, and the kernel is dumped. The request codes were recovered from the
  jump table at `800189ECh` and the debug strings each handler logs — Sony's
  own names for them, `-- play --`, `-- pause --`, `-- stop --`,
  `-- tocread --`, `-- vcd ack --`, `-- ff --`, `-- fr --`, plus `80h`/`81h`
  for the board-present answer the "Check VideoCD..." probe reads back. The
  position bytes are BCD because the play handler feeds them straight to
  `Setloc`. The `State` and `Task` bytes the kernel sends are derived too, and
  from the drive rather than invented: `80010540h` issues `Nop` then `GetID`,
  and `800105A0h` onward turns the status into `State` (0 stopped, 1 playing,
  2 idle, held across a seek) and `Task` (`FFh` idle, `01h` disc newly valid,
  `80h` no disc, `0Ah` a pending event), with `Task` reset to `FFh` at the top
  of the response dispatcher so every other value is a one-shot. The kernel
  therefore drives the exchange from state it already has, and a stand-in
  board only has to answer. What cannot be recovered from the kernel is what
  the *real* board chooses to send and when -- that is its own firmware's
  business, and it has never been dumped -- only what the kernel will accept.
  Untested on real hardware; prefer HLE.
* **SVCD is recognised but not decoded.** SVCD video is MPEG-2; only MPEG-1
  is implemented, so an SVCD gives audio and no picture.
* **Playback Control (PBC) is detected but not interpreted.** Discs play
  their chapter list linearly rather than through their menus. Many
  commercial discs ship broken PSD files, so this is a defensible default
  regardless.
* Testing has been against synthesised sectors carrying real MPEG streams,
  not yet against a pressed disc image end to end.

## Building

Beetle PSX can be built with `make`. To build with hardware renderer support, run `make HAVE_HW=1`. `make clean` is required when switching between HW and non-HW builds.

The prebuilt core in this fork is built and tested on **Windows** (`mednafen_psx_hw_libretro.dll`, via MSYS2 / MinGW-w64; `strip` the result to shrink it). The source is cross-platform, so the same `make HAVE_HW=1` produces `mednafen_psx_hw_libretro.so` on **Linux** and `mednafen_psx_hw_libretro.dylib` on **macOS** with no fork-specific changes — only the Windows binary is provided/tested here.

## Coding Style

The preferred coding style for Beetle PSX is the libretro coding style. See: https://docs.libretro.com/development/coding-standards/. Preexisting Mednafen code and various subdirectories may adhere to different styles; in those instances the preexisting style is preferred.

## Documentation

https://docs.libretro.com/library/beetle_psx/

https://docs.libretro.com/library/beetle_psx_hw/
