[![Build Status](https://travis-ci.org/libretro/beetle-psx-libretro.svg?branch=master)](https://travis-ci.org/libretro/beetle-psx-libretro)
[![Build status](https://ci.appveyor.com/api/projects/status/qd1ew088woadbqhc/branch/master?svg=true)](https://ci.appveyor.com/project/bparker06/beetle-psx-libretro/branch/master)

# Beetle PSX libretro

Beetle PSX is a port/fork of Mednafen's PSX module to the libretro API. It can be compiled in C++98 mode, excluding the Vulkan renderer, which is written in C++11 for the time being. Beetle PSX currently runs on Linux, OSX and Windows.

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

## Building

Beetle PSX can be built with `make`. To build with hardware renderer support, run `make HAVE_HW=1`. `make clean` is required when switching between HW and non-HW builds.

The prebuilt core in this fork is built and tested on **Windows** (`mednafen_psx_hw_libretro.dll`, via MSYS2 / MinGW-w64; `strip` the result to shrink it). The source is cross-platform, so the same `make HAVE_HW=1` produces `mednafen_psx_hw_libretro.so` on **Linux** and `mednafen_psx_hw_libretro.dylib` on **macOS** with no fork-specific changes — only the Windows binary is provided/tested here.

## Coding Style

The preferred coding style for Beetle PSX is the libretro coding style. See: https://docs.libretro.com/development/coding-standards/. Preexisting Mednafen code and various subdirectories may adhere to different styles; in those instances the preexisting style is preferred.

## Documentation

https://docs.libretro.com/library/beetle_psx/

https://docs.libretro.com/library/beetle_psx_hw/
