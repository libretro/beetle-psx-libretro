# Beetle PSX HW — HD Texture Replacement Caching Overhaul

## TL;DR (one paragraph)

A Beetle PSX HW fork that overhauls the Vulkan renderer's HD texture replacement
pipeline for smooth, pop-in-free packs on demanding content — particularly
multi-palette animated sprites like Alucard in *Castlevania: Symphony of the
Night*. It adds a three-tier, decode-once cache (VRAM images → RAM pixels → disk,
LRU-evicted), binds cached textures in the same frame they're drawn to eliminate
per-frame pop-in, and decodes on a 4-thread pool. New core options choose the
caching method — Eager (stock-Beetle default) or Lazy — and set the VRAM/RAM
cache budgets (defaults 3 GB / 2 GB). The on-disk pack format is unchanged.

## Summary

This fork reworks the **HD texture replacement** pipeline in the Vulkan
("Beetle PSX HW") renderer so replacement packs stay smooth on demanding content
— particularly animated sprites that reuse one texture across many palettes
(e.g. Alucard in *Castlevania: Symphony of the Night*), where the stock
implementation suffers persistent texture pop-in and load stalls.

What's different from upstream (the on-disk pack format is unchanged; the
engine is now shared between the GL and Vulkan RHI backends):

- **Selectable caching method (core option).** *Eager* (default, matches stock
  Beetle) prefetches all palette variants of a texture when it enters VRAM;
  *Lazy* loads only the specific texture+palette actually drawn, avoiding the
  load burst for large multi-palette packs. Both feed the same cache below.
- **Three-tier, decode-once cache.** A VRAM cache of ready-to-bind GPU images,
  backed by a RAM cache of decoded pixels, backed by disk. Each combination is
  read and decoded at most once; re-draws are free. Both tiers are LRU-evicted
  with budgets exposed as **core options** (defaults **3 GB VRAM / 2 GB RAM**).
- **Immediate in-frame binding.** A cached GPU image is bound on the *same* frame
  it's needed rather than one frame later — this eliminates the persistent
  per-frame pop-in that affected animated sprites even when their textures were
  already cached.
- **Multithreaded decode.** Image decode + mipmap generation runs on a 4-thread
  pool instead of a single thread, so first-appearance loads land faster.
- **Built-in diagnostics.** An `[hdcache]` line is written to the RetroArch INFO
  log every 300 frames (decodes / GPU uploads / binds + cache occupancy) for
  tuning.

Packs are authored exactly as before: textures live in
`<content>-texture-replacements/`, named `<texturehash>-<palettehash>.<ext>`.
Replacements may be **PNG, JPEG, BMP, TGA, WEBP or DDS** (probed in the order
`png, dds, webp, jpg, jpeg, bmp, tga` when several exist for the same combo);
dumps are always written as PNG. Decoding goes through libretro-common's
`image_texture` / `image_transfer` front end (rpng / rjpeg / rbmp / rtga /
rwebp / rdds) — the vendored `stb_image` is gone.

## Technical detail

The engine lives in **`rhi/rhi_tt.c` / `rhi_tt.h`** and is shared by **both the
RHI GL and RHI Vulkan renderers** (built whenever either HW renderer is —
`SET_HAVE_HW`). It is renderer-agnostic: every GPU operation goes through a
`TTGpuBackend` vtable the active renderer installs at `texture_tracker_new()`
time — uploading decoded RGBA mip chains, refcounting the opaque `TTGpuImage`
handles, and compositing fused page textures (`page_begin` / `page_blit` /
`page_end`). The Vulkan backend hands out its refcounted `Image*` and replays
the old barrier/clear/blit sequence; the GL backend wraps GL texture names and
composites pages with `glBlitFramebuffer`. On the GL side, draw batches split
on `HdTextureHandle` changes and the command fragment shader carries a port of
`shaders_vulkan/hdtextures.h` (per-batch `hd_texture` sampler + `hd_vram_rect`
/ `hd_texel_rect` uniforms, derivative-based LOD since GL 3.3/GLES3 lack
fragment `textureQueryLod`).

The engine — and this caching system with it — has been ported to C (C89), so
the type names below are C `struct`s with hand-rolled equivalents of the
original C++ containers: an intrusive LRU list indexed through `HdKeySet`, and a
manual-refcount `ImageHandle` (now backed by the `TTGpuBackend` refcount ops) in
place of the STL/`shared_ptr` types.

### New types

- **`CachedHdImage` / `HdImageCache`** — a byte-budgeted LRU cache (an intrusive
  linked list with an `HdKeySet` index, keyed by `HdTextureId = {texture hash,
  palette hash}`) holding **decoded CPU pixel levels** (RGBA + mips) and alpha
  flags. Default budget `HD_CACHE_RAM_BUDGET = 2 GB`.
- **`CachedGpuImage` / `HdGpuCache`** — an analogous LRU cache holding
  **uploaded `ImageHandle`s** (ready to bind, in VRAM). Default budget
  `HD_CACHE_VRAM_BUDGET = 3 GB`. Eviction releases the handle, freeing VRAM once
  no live draw references it.

### `TextureTracker` changes

New members: `hd_gpu_cache`, `hd_cache`, `requested` (combos with an in-flight
load or no file on disk — a negative cache), `pending_attach` (combos to bind at
the next safe point), and `dbg_*` diagnostic counters.

- **`upload()`** — in *Lazy* mode, queues nothing here. In *Eager* mode (default),
  prefetches all of the hash's palette variants via `want_combo` (so it still
  respects the cache, dedup and budgets — unlike stock Beetle's raw
  `load_hd_texture`).
- **`want_combo(HdTextureId)`** *(new)* — queues a single disk load for one
  combination, skipping it if already cached, already requested, or absent from
  disk (inserted into `requested` as a permanent negative cache so missing files
  aren't retried).
- **`request_hd_texture(upload, palette)`** *(new; replaces the eager path)* —
  called from the draw path on a cache miss:
  1. **GPU-cache hit →** bind the existing image into `upload->textures`
     **immediately** (a ref-counted handle copy, no Vulkan commands, safe
     mid-draw);
  2. **CPU-cache hit →** add to `pending_attach` for a GPU upload at the next
     safe point;
  3. **miss →** `want_combo()` (disk load).
- **`get_hd_texture_index()`** — the per-draw overlap loop now calls
  `request_hd_texture()` on a miss and **re-checks `upload->textures`
  immediately afterward**, so an in-frame GPU-cache bind is used by the current
  draw. *(This is the pop-in fix: previously every bind was deferred to
  `on_queues_reset` one frame late, so a sprite frame drawn once per VRAM
  residency always displayed the native texture.)*
- **`on_queues_reset()`** — rewritten:
  - drains IO responses into `hd_cache` (decode-once) and clears them from
    `requested`;
  - an **attach pass** over `pending_attach` + new responses binds combos whose
    base texture is currently resident — GPU-cache hit = handle copy; CPU-cache
    hit = `upload_texture()` then store the result in `hd_gpu_cache`;
  - combos whose base texture isn't resident yet are **kept in the cache, not
    discarded**, and bind on a later frame when that animation frame's data
    returns to VRAM (this removes the stock path's decode-then-discard waste);
  - dimension-mismatched replacements are evicted and negatively cached.
- **`reload_textures_from_disk()`** — clears all caches (`hd_gpu_cache` /
  `hd_cache` / `requested` / `pending_attach`) so edited files take effect.
- **`endFrame()`** — emits the `[hdcache]` INFO diagnostics every 300 frames.
- `load_hd_texture()` is retained (now used only by `load_state()` to re-warm HD
  textures after a savestate load).

### IO subsystem

- **`io_thread`** — converted from a single worker that drained the entire queue
  into a **pool worker**: it takes one request at a time, runs image decode +
  mipmap generation **outside** the channel lock (workers run in parallel),
  pushes the response under the lock, and cascade-signals the next worker.
- **`IOThread`** — spins up `NUM_IO_THREADS` (4) detached workers, each given its
  own heap-allocated pointer to the shared channel; shutdown uses
  `scond_broadcast` to wake all workers.

### Core options

- **HD Texture Caching Method** — `Eager` (default, stock-Beetle behaviour) or `Lazy`.
- **HD Texture VRAM Cache Budget** — default 3 GB.
- **HD Texture RAM Cache Budget** — default 2 GB.

Budgets are runtime-adjustable (lowering one evicts immediately). The `[hdcache]`
INFO log line shows the active mode and `used/budget` for each tier.

### Palette-range hashing (opt-in)

- **HD Reduce Palette Range** — when enabled, a texture's palette hash covers only
  the CLUT entries the texture actually indexes (`reduce_palette_bounds()` finds
  the used `lo..hi` range) instead of the whole CLUT. Games frequently leave unused
  CLUT entries as garbage or rewrite them over time; ignoring them lets a single
  replacement keep matching across those variations — fewer dumps and much better
  match coverage, most noticeably for 8bpp textures. It applies to **both** the
  upload-rect and page-aligned paths. Dump **filenames are unchanged** and remain
  loadable; when no reduced-range file is present the matcher falls back to the
  full-palette hash, so existing full-palette packs still match with it enabled.
  Off by default — re-dump a game to benefit.

### Build

Build the HW core as usual — `make HAVE_HW=1` — then optionally `strip` the
result. No new external dependencies; image decode/encode uses the
libretro-common format decoders vendored under `libretro-common/formats/`
(PNG through the bundled zlib, which gained `deflate.c`/`trees.c` for the PNG
encoder) plus the libretro threading already in the tree. `stb_image` has been
removed.

Built and tested on **Windows** (`mednafen_psx_hw_libretro.dll`, MSYS2/MinGW-w64)
against **RetroArch 1.22.2** (git 69a4f0e, build date Nov 20 2025, Compiler:
MinGW 10.2.0 64-bit). The source is cross-platform: the same `make HAVE_HW=1`
yields a `.so` on Linux and a `.dylib` on macOS.
