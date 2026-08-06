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

* **Daughterboard mode is best-effort.** The CD-ROM sub-CPU only relays five
  opaque bytes in each direction, and the daughterboard's own firmware has
  never been dumped, so the meaning of the exchanged status bytes is not
  publicly known. The encoding used here is this fork's own: it presents the
  kernel with a live, well-formed board and a sane clock, but the kernel's
  player may not advance exactly as it would on real hardware. Prefer HLE
  until someone captures a Port F trace from a real machine.
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
