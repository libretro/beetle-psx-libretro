# Page-Aligned Texture Dump + Replacement (experimental)

An **opt-in** alternative dumping/replacement mode for the Vulkan ("Beetle PSX HW")
renderer that works at **whole VRAM texture-page** granularity instead of per-upload
rectangles. It targets the fragmented-"sections" problem with the existing dumper
(libretro/beetle-psx #918): the upload-rect dumper emits many small, oddly-cropped
fragments of a page, whereas page-aligned mode emits one clean 256×256 tile per page
— much friendlier for an artist to replace.

It is layered on top of the existing HD texture-replacement system (see
`HD_TEXTURE_CACHE.md`) and reuses its three-tier cache, IO worker pool, priority
queue, caching-method option, and budgets unchanged. Default behaviour is the
classic upload-rect mode, so existing packs are completely unaffected.

All code is in `rhi/rhi_lib_vulkan.c` + `libretro_core_options.h` (Vulkan-only,
built under `TEXTURE_DUMPING_ENABLED`). It is implemented in C on top of upstream's
C/C89 `rhi_lib_vulkan.c` (the renderer's MSVC-C89 target: declarations at block top,
`-Werror=declaration-after-statement`), reusing the C `HdImageCache`/`HdGpuCache` LRU
caches, `HdKeySet`, the `IOThread` worker pool and the manual-refcount `ImageHandle`.
The page vs upload-rect cache entries are namespaced by a `pages` flag folded into the
packed cache key, so both pack types share one cache + budget without aliasing.

## Compatibility model

Two independent core options:

- `beetle_psx_hw_hd_dump_mode`        — how textures are **dumped**: `{upload_rect (default), page_aligned, both}`.
- `beetle_psx_hw_hd_replacement_mode` — how replacements are **looked up**: `{upload_rect (default), page_aligned}`.

`hd_dump_mode = both` writes **both** pack types in a single playthrough (to their two
separate folders below), so you don't have to replay the game once per mode. The two
dumps are independent and deduplicated; the decode/encode runs on the IO worker pool,
so dumping both at once doesn't stall the render thread.

Each mode uses its own folders so the two pack types never collide (the same
filename does **not** mean the same texture between them):

| | dump | replacements |
|---|---|---|
| upload-rect (default) | `<cd>-texture-dump/` | `<cd>-texture-replacements/` |
| page-aligned | `<cd>-texture-dump-pages/` | `<cd>-texture-replacements-pages/` |

The two sides are independent — you can dump page-aligned while still replacing from
an existing upload-rect pack, or vice versa. **Page packs and upload-rect packs are
NOT interchangeable and cannot be converted offline** (a re-dump is required): the
hash is computed over a different region of VRAM.

The folder *location* honours the **HD Texture Folder** core option
(`beetle_psx_hw_texture_directory`: content / system / save directory), and the
folders are created automatically when dumping/replacement is enabled.

## Cross-mode replacement fallback

`beetle_psx_hw_hd_replacement_fallback` (`{disabled (default), enabled}`) lets the two
replacement modes back each other up rather than being strictly one-or-the-other. When
the **active** `hd_replacement_mode` finds no match for a draw, the renderer checks the
**other** mode's pack before falling back to native:

- in **upload-rect** mode, a miss is retried against the page-aligned (`-pages`) pack;
- in **page-aligned** mode, a miss is retried against the upload-rect pack.

So you can pick whichever mode fits the bulk of a game and let the other cover the
exceptions — e.g. a mostly page-aligned pack for the static art, with a handful of
upload-rect textures filling in animated sprites — without converting anything. It is
**off by default**, costs nothing on a hit, and only does the extra lookup on a genuine
miss (best paired with **Lazy** caching, so the fallback's loads stay on demand).

Internally both packs share the one three-tier cache; a 1-bit discriminator on the
cache key namespaces page vs upload-rect entries so they can never alias, even on a
32-bit hash collision.

## How it works

### Page geometry

A PS1 texture page, in VRAM 16-bit coordinates, is `{page_x, page_y, page_w, 256}`
where `page_w = 64 (4bpp) / 128 (8bpp) / 256 (16bpp)`. The draw path already knows
`page_x, page_y` (from `render_state.texture_offset_x/y`). Every page decodes to a
256×256-texel image regardless of bit depth.

### CPU VRAM mirror

Page hashing/dumping needs the live page bytes, but the draw path carries no CPU
VRAM pointer. The tracker therefore keeps a `vram_mirror` (1024×512 ×16-bit = 1 MB)
updated in every VRAM mutation hook (`upload`, `blit`, `clearRegion`/fill,
`notifyReadback`). All page reads come from this mirror. *(Correctness depends on
every VRAM write going through one of those hooks.)*

### Page hash

`hash_page()` is a CRC32 over the page rect read from `vram_mirror`. The palette hash
is unchanged from upload-rect mode. A page hash is just a `uint32_t`, so a page slots
into the existing `HdTextureId{hash, palette}` keyspace and reuses all of the cache
machinery as-is.

### Dump

When `hd_dump_mode = page_aligned` (or `both`), a textured draw snapshots the **full**
page's raw VRAM words + palette and queues a low-priority dump request for
`<cd>-texture-dump-pages/<pagehash>-<palettehash>.png`, deduplicated per
`(page_hash, palette_hash)`. The palette→RGBA decode (same conversion as the
upload-rect dumper) and the PNG encode then run on the **IO worker pool**, not the
render thread, so a burst of first-seen pages — or dumping both pack types at once
under `both` — doesn't stall input. Files named `…-missing.png` mean the palette
wasn't resident when the page was captured (grayscale index data, not a usable
replacement); `…-0.png` is a genuine palette-less (ABGR1555) texture.

### Match + binding

When `hd_replacement_mode = page_aligned`, the draw path computes the page hash,
looks it up through the **same** three-tier cache / loader / IO pool (sourced from
the `-pages` folder and a separate `known_files_pages` listing), and binds the
resulting page image. No shader changes are needed: binding returns an `HdTexture`
with `vram_rect = page_rect` and `texel_rect = {0, 0, img_w, img_h}`, which the
existing sampling push-constants map correctly for any sub-rect draw — the same shape
the renderer's fused-page path already produces. Eager / Lazy / Lazy-synchronous
caching methods and the VRAM/RAM budgets all apply unchanged.

### Performance note

Page match has no per-draw "same as last time" short-circuit, so it would otherwise
hash a 32–128 KB page on every textured draw (hundreds per frame in busy 2D scenes).
A memo (`hash_page_cached`) avoids this: a VRAM write only marks overlapping pages
dirty, and any page is re-hashed at most once per frame. Steady-state cost is ~1 page
CRC per frame. The `[hdcache]` INFO log gains a page line
(`binds / native-misses / page-CRCs / memo entries`) for tuning — `page-CRCs` should
sit near 1/frame; a value near the bind count means the memo has regressed.

## When to use it

Page-aligned mode shines on **static, reused art** — backgrounds, UI, tilemaps, and
(by extension) 3D games, where a page is uploaded once and drawn many times.

It is a poor fit for **2D animated multi-palette sprites** (e.g. Alucard in *Symphony
of the Night*): each animation frame uploads new index data, and because the page
hash also folds in the surrounding VRAM context, one logical sprite explodes into
many distinct page hashes. That needs far more replacement files than the upload-rect
equivalent and churns the cache, so such sprites mostly fall back to native. Use
upload-rect replacement for that kind of content. Since the two modes are
independent, you can choose per game.

## Usage

Build the HW core as usual (`make HAVE_HW=1`, then optionally `strip`).

**Dump page tiles:** enable **Track Textures** + **Dump Textures**, set **HD Dump
Mode = Page-aligned**, run the game, and inspect `<cd>-texture-dump-pages/` (compare
the clean tiles against the old `<cd>-texture-dump/` fragments).

**Dump both at once:** set **HD Dump Mode = Both** to populate `<cd>-texture-dump/`
*and* `<cd>-texture-dump-pages/` in one playthrough, then decide which to work from.

**Mix packs:** to use a page-aligned pack but cover its gaps from an upload-rect pack
(or vice versa), enable **HD Replacement Cross-Mode Fallback** alongside your chosen
**HD Replacement Mode**.

**Use replacements:** put edited tiles in `<cd>-texture-replacements-pages/` (keep
dimensions a power-of-two multiple of 256, e.g. 256/512/1024 square), enable **Track
Textures** + **Replace Textures**, and set **HD Replacement Mode = Page-aligned**.
The `[hdcache]` log shows `replace=page` and the page bind counts. First appearance
is native for one frame under Lazy; use **HD Texture Caching Method = Lazy
(synchronous)** for no pop-in while evaluating.
