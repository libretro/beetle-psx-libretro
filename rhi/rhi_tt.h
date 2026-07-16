#ifndef __RHI_TT_H__
#define __RHI_TT_H__

/* ============================================================
 * rhi_tt.h - shared HD-texture replacement / tracking engine.
 *
 * The TextureTracker (VRAM rect tracking, texture/palette hashing,
 * dump.cfg, dump-to-disk, replacement loading + decode, the 3-tier
 * decode-once cache and the IO worker pool) is renderer-agnostic and
 * lives in rhi_tt.c. A renderer (RHI Vulkan or RHI GL) drives it
 * through the entry points below and supplies GPU services through a
 * TTGpuBackend vtable: uploading decoded RGBA mip chains, refcounting
 * the resulting opaque TTGpuImage handles, and compositing fused
 * page textures.
 *
 * Image decode goes through libretro-common's image_texture /
 * image_transfer front end; replacement files may be PNG, JPEG, BMP,
 * TGA, WEBP or DDS. Dumps are written as PNG via rpng.
 *
 * All of this compiles as C89 (MSVC-compatible: no // comments, no
 * declarations after statements).
 * ============================================================ */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <boolean.h>
#include <retro_inline.h>
#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

extern retro_log_printf_t log_cb;

/* Texture-tracker logging. The [hdcache] INFO diagnostics and the
 * load/dimension WARN/ERROR lines are meant to be visible in a normal
 * RetroArch INFO log, so TT_LOG always calls log_cb. TT_LOG_VERBOSE
 * (per-draw spam) is gated: it compiles to real log_cb calls only when
 * both DEBUG and VERBOSE_TEXTURE_TRACKING are defined, and to a dead
 * `if (0) log_cb(...)` everywhere else - no runtime code, yet the
 * arguments stay syntactically consumed and type-checked (so locals
 * used only in a log line don't trip -Wunused-but-set-variable). */
/* #define VERBOSE_TEXTURE_TRACKING */
#define TT_LOG(...) log_cb(__VA_ARGS__)
#if defined(DEBUG) && defined(VERBOSE_TEXTURE_TRACKING)
#define TT_LOG_VERBOSE(...) TT_LOG(__VA_ARGS__)
#else
#define TT_LOG_VERBOSE(...) do { if (0) log_cb(__VA_ARGS__); } while (0)
#endif


#ifndef POD_VEC_DECLARE
/* ------------------------------------------------------------------------- *
 * POD_VEC - a typed dynamic array of trivially-relocatable elements, MSVC C89.
 *
 * Used for the renderer's per-frame draw queues, whose elements (BufferVertex,
 * PrimitiveInfo, BlitInfo, VkRect2D, ...) are all POD / trivially relocatable.
 * Growth is a realloc (bitwise relocation - no per-element copy step), which is
 * what the hot per-vertex push path wants. clear keeps the allocation for reuse
 * (these are refilled every frame). The struct is brace-initialisable to
 * { NULL, 0, 0 } so it needs no separate initialiser. */
#define POD_VEC_DECLARE(NAME, T)                                              \
struct NAME {                                                                 \
    T  *items;                                                                \
    int count;                                                                \
    int cap;                                                                  \
};                                                                            \
typedef struct NAME NAME;                                                     \
static INLINE T   *NAME##_data(struct NAME *v)        { return v->items; }    \
static INLINE int  NAME##_size(const struct NAME *v)  { return v->count; }    \
static INLINE int  NAME##_empty(const struct NAME *v) { return v->count == 0; }\
static INLINE void NAME##_clear(struct NAME *v)       { v->count = 0; }        \
static INLINE T   *NAME##_at(struct NAME *v, int i)   { return &v->items[i]; } \
static INLINE T   *NAME##_back(struct NAME *v)        { return &v->items[v->count - 1]; } \
static INLINE T   *NAME##_front(struct NAME *v)       { return &v->items[0]; } \
static INLINE int  NAME##_grow_by_one(struct NAME *v) {                        \
    if (v->count == v->cap) {                                                 \
        int ncap = v->cap ? v->cap * 2 : 64;                                  \
        T *nitems = (T *)realloc(v->items, (size_t)ncap * sizeof(T));         \
        if (!nitems)                                                          \
            return 0;                                                         \
        v->items = nitems;                                                    \
        v->cap = ncap;                                                        \
    }                                                                         \
    return 1;                                                                 \
}                                                                             \
static INLINE void NAME##_push(struct NAME *v, const T *valp) {                \
    T tmp = *valp;  /* copy before grow: *valp may alias items[] (realloc) */ \
    if (NAME##_grow_by_one(v))                                                \
        v->items[v->count++] = tmp;                                           \
}                                                                             \
static INLINE void NAME##_pop_back(struct NAME *v) { if (v->count) v->count--; } \
static INLINE void NAME##_free_storage(struct NAME *v) { free(v->items); v->items = NULL; v->count = 0; v->cap = 0; } \
struct NAME##_force_semicolon_
#endif /* POD_VEC_DECLARE */

#ifndef min_

/* Local single-evaluation min/max. */
/* min_/max_ as macros. Arguments must be free of side effects: every call site
 * passes pure expressions, and the two that used accessor calls
 * (get_width()/get_height()) hoist them into locals first. Returns the first
 * argument on a tie; the one max_(...) call on a VkDeviceSize casts its 32-bit
 * literal to VkDeviceSize so the comparison and result stay 64-bit. */
#define min_(a, b) ((b) < (a) ? (b) : (a))
#define max_(a, b) ((a) < (b) ? (b) : (a))
#endif /* min_ */

/* PSX VRAM geometry, shared by the tracker and both renderers. */
enum { FB_WIDTH = 1024 };
enum { FB_HEIGHT = 512 };
enum { BLOCK_WIDTH = 8 };
enum { BLOCK_HEIGHT = 8 };
enum { NUM_BLOCKS_X = FB_WIDTH / BLOCK_WIDTH };
enum { NUM_BLOCKS_Y = FB_HEIGHT / BLOCK_HEIGHT };

enum TextureMode {
   TextureMode_None,
   TextureMode_Palette4bpp,
   TextureMode_Palette8bpp,
   TextureMode_ABGR1555
};
typedef enum TextureMode TextureMode;

/* TTRect: plain C struct (was a value type with ctors / equality helper/!= /
 * contains / intersects / scissor / extend_bounding_box). The default
 * initialiser + zero NSDMIs become brace-initialisation { 0, 0, 0, 0 }; the
 * 4-arg initialiser becomes make_rect. The methods become rect_* free
 * functions taking const struct TTRect * (extend_bounding_box mutates, so its
 * first arg is non-const). Most Rects are already built by brace-init, which
 * a plain struct supports unchanged. */
struct TTRect
{
   unsigned x;
   unsigned y;
   unsigned width;
   unsigned height;
};
typedef struct TTRect TTRect;

static INLINE struct TTRect make_rect(unsigned x,
      unsigned y,
      unsigned width,
      unsigned height)
{
   struct TTRect r;
   r.x = x; r.y = y; r.width = width; r.height = height;
   return r;
}
static INLINE bool rect_eq(const struct TTRect *a, const struct TTRect *b)
{
   return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}
static INLINE bool rect_contains(const struct TTRect *self,
      const struct TTRect *rect)
{
   return self->x <= rect->x && self->y <= rect->y &&
      (self->x + self->width) >= (rect->x + rect->width) &&
      (self->y + self->height) >= (rect->y + rect->height);
}
static INLINE bool rect_intersects(const struct TTRect *self,
      const struct TTRect *rect)
{
   unsigned x_end_self = self->x + self->width;
   unsigned x_end_other = rect->x + rect->width;
   unsigned y_end_self = self->y + self->height;
   unsigned y_end_other = rect->y + rect->height;
   unsigned xend = (x_end_self < x_end_other) ? x_end_self : x_end_other;
   unsigned xbegin = (self->x > rect->x) ? self->x : rect->x;
   unsigned yend = (y_end_self < y_end_other) ? y_end_self : y_end_other;
   unsigned ybegin = (self->y > rect->y) ? self->y : rect->y;
   return xbegin < xend && ybegin < yend;
}
static INLINE struct TTRect rect_scissor(const struct TTRect *self,
      const struct TTRect *rect)
{
   unsigned x_end_self = self->x + self->width;
   unsigned x_end_other = rect->x + rect->width;
   unsigned y_end_self = self->y + self->height;
   unsigned y_end_other = rect->y + rect->height;
   unsigned x0 = (self->x > rect->x) ? self->x : rect->x;
   unsigned y0 = (self->y > rect->y) ? self->y : rect->y;
   unsigned x1 = (x_end_self < x_end_other) ? x_end_self : x_end_other;
   unsigned y1 = (y_end_self < y_end_other) ? y_end_self : y_end_other;
   unsigned w = (x1 > x0) ? (x1 - x0) : 0u;
   unsigned h = (y1 > y0) ? (y1 - y0) : 0u;
   struct TTRect out;
   out.x = x0; out.y = y0; out.width = w; out.height = h;
   return out;
}
static INLINE void rect_extend_bounding_box(struct TTRect *self,
      const struct TTRect *rect)
{
   unsigned x_end_self = self->x + self->width;
   unsigned x_end_other = rect->x + rect->width;
   unsigned y_end_self = self->y + self->height;
   unsigned y_end_other = rect->y + rect->height;
   unsigned x0 = (self->x < rect->x) ? self->x : rect->x;
   unsigned y0 = (self->y < rect->y) ? self->y : rect->y;
   unsigned x1 = (x_end_self > x_end_other) ? x_end_self : x_end_other;
   unsigned y1 = (y_end_self > y_end_other) ? y_end_self : y_end_other;
   self->x = x0;
   self->y = y0;
   self->width = x1 - x0;
   self->height = y1 - y0;
}

/* SRect: plain C struct (was a value type with ctors / accessors / equality
 * helper). The validating 4-arg initialiser becomes make_srect; the
 * zero-init default initialiser becomes the brace-initialiser { 0, 0, 0, 0 }
 * (a placeholder slot to be overwritten before use). Accessors
 * left/right/top/bottom and the comparison become srect_* free functions. */
struct SRect {
   int x;
   int y;
   int width;
   int height;
};
typedef struct SRect SRect;

/* Validated SRect builder (was the 4-arg initialiser). width/height must be
 * positive; a zero/negative size is a hard error, matching the old
 * initialiser. */
static INLINE struct SRect make_srect(int x, int y, int width, int height)
{
   struct SRect r;
   if (width <= 0 || height <= 0) {
      printf("Illegally sized SRect: %d, %d\n", width, height);
      exit(1);
   }
   r.x = x; r.y = y; r.width = width; r.height = height;
   return r;
}
static INLINE int srect_left(const struct SRect *s)   { return s->x; }
static INLINE int srect_right(const struct SRect *s)  { return s->x + s->width; }
static INLINE int srect_top(const struct SRect *s)    { return s->y; }
static INLINE int srect_bottom(const struct SRect *s) { return s->y + s->height; }
static INLINE bool srect_eq(const struct SRect *a, const struct SRect *b)
{
   return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

typedef int RectIndex;

struct HdTextureId {
   uint32_t hash;
   uint32_t palette_hash;
   /* false = upload-rect combo, true = page-aligned combo (page-aligned
    * experiment). Namespaces the SHARED requested/hd_cache/hd_gpu_cache via
    * hd_pack_key's salt so the two pack types never alias. C field-assignment
    * construction sites must set this explicitly (no aggregate zero-fill). */
   bool pages;
};
typedef struct HdTextureId HdTextureId;

/* HdTextureHandle: plain C struct (was a value type with equality
 * helper/!=/>, a private 3-arg initialiser and static factories). The
 * factories become hd_handle_make / _make_fused / _make_none; the
 * comparisons become hd_handle_eq / _ne / _gt free functions. */
struct HdTextureHandle {
   RectIndex index;
   uint32_t palette_hash;
   bool fused;
   /* Page-aligned experiment: a page-replacement handle. index points into the
    * tracker's ephemeral page_bindings; resolved by get_hd_texture. Mutually
    * exclusive with `fused`. (See PAGE_ALIGN.md section 6.) */
   bool page;
};
typedef struct HdTextureHandle HdTextureHandle;

static INLINE struct HdTextureHandle hd_handle_make(RectIndex index,
      uint32_t palette_hash)
{
   struct HdTextureHandle h;
   h.index = index; h.palette_hash = palette_hash; h.fused = false; h.page = false;
   return h;
}
static INLINE struct HdTextureHandle hd_handle_make_fused(RectIndex index)
{
   struct HdTextureHandle h;
   h.index = index; h.palette_hash = 0; h.fused = true; h.page = false;
   return h;
}
static INLINE struct HdTextureHandle hd_handle_make_page(RectIndex index,
      uint32_t palette_hash)
{
   struct HdTextureHandle h;
   h.index = index; h.palette_hash = palette_hash; h.fused = false; h.page = true;
   return h;
}
static INLINE struct HdTextureHandle hd_handle_make_none(void)
{
   return hd_handle_make(-1, 0);
}
static INLINE bool hd_handle_is_none(const struct HdTextureHandle *h)
{
   return h->index == (RectIndex)-1 && h->palette_hash == 0 && h->fused == false && h->page == false;
}
static INLINE bool hd_handle_eq(const struct HdTextureHandle *a,
      const struct HdTextureHandle *b)
{
   return a->index == b->index && a->palette_hash == b->palette_hash && a->fused == b->fused && a->page == b->page;
}
static INLINE bool hd_handle_ne(const struct HdTextureHandle *a,
      const struct HdTextureHandle *b)
{
   return !hd_handle_eq(a, b);
}
static INLINE bool hd_handle_gt(const struct HdTextureHandle *a,
      const struct HdTextureHandle *b)
{
   if (a->index != b->index)
      return a->index > b->index;
   if (a->palette_hash != b->palette_hash)
      return a->palette_hash > b->palette_hash;
   if (a->fused != b->fused)
      return a->fused > b->fused;
   return a->page > b->page;
}

/* UsedMode: plain POD (its equality helper was unused and has been dropped). */
struct UsedMode {
   TextureMode mode;
   unsigned int palette_offset_x;
   unsigned int palette_offset_y;
};
typedef struct UsedMode UsedMode;

/* A decoded RGBA image level. owned_data is width*height*4 bytes of
 * straight RGBA8 (R at byte 0), owned by the struct. */
struct LoadedImage {
   uint8_t *owned_data; /* RGBA, owned_size bytes (NULL if empty) */
   size_t   owned_size;
   int width;
   int height;
};
typedef struct LoadedImage LoadedImage;

/* A decoded texture as a set of mip levels. C-style dynamic array: `levels`
 * is a malloc'd array of `count` LoadedImage, owning all buffers. */
struct LoadedLevels {
   LoadedImage *levels;
   int          count;
};
typedef struct LoadedLevels LoadedLevels;

/* ============================================================
 * GPU backend interface.
 *
 * TTGpuImage is an opaque, backend-defined GPU texture handle
 * (the Vulkan backend hands out its refcounted Image*, the GL
 * backend a small refcounted wrapper around a GL texture name).
 * The tracker never looks inside one; every operation goes
 * through the vtable. Reference contract: any TTGpuImage* the
 * tracker receives from upload_levels/page_begin arrives with
 * one reference owned by the tracker; image_addref/image_release
 * adjust it. HdTexture.texture returned by
 * texture_tracker_get_hd_texture carries an owned +1 reference
 * that the CONSUMER must release (via its own backend).
 * ============================================================ */

typedef struct TTGpuImage TTGpuImage; /* opaque; backend-defined */

/* One source-rect blit into a fused page composite, in LEVEL-0
 * coordinates on both sides. The backend replays it across the page's
 * mip chain exactly like the old Vulkan-only rebuild loop: for each
 * dstLevel in [0, page mip count) it blits from source mip
 * max(0, dstLevel - full_res_levels) with the source rect components
 * shifted right by that source level, into the destination rect
 * halved per destination level (clamped to >= 1). Linear filtering. */
struct TTPageBlit {
   int dst_x, dst_y;       /* destination offset, level 0            */
   int dst_w, dst_h;       /* destination extent, level 0            */
   int src_x, src_y;       /* source offset, level 0 (pre-scaled)    */
   int src_w, src_h;       /* source extent, level 0 (pre-scaled)    */
   int full_res_levels;    /* srcLevel = max(0, dstLevel - this)     */
};
typedef struct TTPageBlit TTPageBlit;

struct TTGpuBackend {
   void *ctx;

   /* Upload a decoded RGBA8 mip chain as an immutable sampled texture.
    * Returns a handle carrying one reference owned by the caller, or
    * NULL on failure. */
   TTGpuImage *(*upload_levels)(void *ctx, const LoadedLevels *levels);

   void (*image_addref)(void *ctx, TTGpuImage *img);
   void (*image_release)(void *ctx, TTGpuImage *img);

   /* Level-0 dimensions. */
   unsigned (*image_width)(void *ctx, TTGpuImage *img);
   unsigned (*image_height)(void *ctx, TTGpuImage *img);

   /* Approximate VRAM footprint of the whole mip chain, for the
    * byte-budgeted GPU cache. */
   size_t (*image_vram_bytes)(void *ctx, TTGpuImage *img);

   /* Fused-page compositing. page_begin returns a cleared
    * (0,0,0,1-sentinel) render/transfer target of the given size and
    * mip count, reusing `reuse` when it is non-NULL and already
    * matches the dimensions (in which case the SAME handle is
    * returned, no reference change); otherwise a NEW handle carrying
    * one reference is returned and the caller releases `reuse`
    * itself. Between page_begin and page_end any number of page_blit
    * calls composite HD sources in. page_end makes the page
    * shader-readable. */
   TTGpuImage *(*page_begin)(void *ctx, TTGpuImage *reuse,
         int width, int height, int mip_levels);
   void (*page_blit)(void *ctx, TTGpuImage *page, TTGpuImage *src,
         const TTPageBlit *blit);
   void (*page_end)(void *ctx, TTGpuImage *page);
};
typedef struct TTGpuBackend TTGpuBackend;

/* HdTexture: what a draw needs to sample a replacement. texture holds an
 * owned +1 reference (release through the renderer's backend when the
 * bind has been recorded). */
struct HdTexture {
   SRect vram_rect;
   SRect texel_rect; /* hd texels */
   TTGpuImage *texture;
};
typedef struct HdTexture HdTexture;

/* Opaque tracker + save-state. */
typedef struct TextureTracker TextureTracker;
typedef struct TextureTrackerSaveState TextureTrackerSaveState;

/* Runtime configuration mirror of the HD-texture core options. Applied
 * atomically by texture_tracker_set_config (the renderers build one of
 * these in their refresh_variables paths). */
struct TextureTrackerConfig {
   bool dump_enabled;
   bool hd_textures_enabled;
   bool eager_textures;          /* true = prefetch all palettes of a hash on upload; false = lazy per-draw */
   bool lazy_sync;               /* Lazy mode only: load+upload synchronously on the render thread */
   bool dump_mode_rect;          /* HD Dump Mode: rect -> -texture-dump/ */
   bool dump_mode_pages;         /* HD Dump Mode: pages -> -texture-dump-pages/ */
   bool replacement_mode_pages;  /* HD Replacement Mode = Page-aligned */
   bool replacement_fallback;    /* on a miss in the active mode, also try the other pack */
   bool reduce_palette_range;    /* opt-in: hash only the CLUT entries a texture uses */
};
typedef struct TextureTrackerConfig TextureTrackerConfig;

/* ---- Lifecycle -------------------------------------------------------- */

/* Allocate + initialise a tracker bound to the given GPU backend. The
 * backend struct is copied; ctx must stay valid for the tracker's
 * lifetime. default_hd_texture is a 1x1 (or any) fallback texture the
 * tracker returns for broken handles; the tracker takes one reference. */
TextureTracker *texture_tracker_new(const TTGpuBackend *backend,
      TTGpuImage *default_hd_texture);
void texture_tracker_free(TextureTracker *self);

void texture_tracker_set_config(TextureTracker *self,
      const TextureTrackerConfig *cfg);
void texture_tracker_set_cache_budgets(TextureTracker *self,
      size_t ram_bytes,
      size_t vram_bytes);

/* HD Texture Folder mode: 0 = content dir (default), 1 = system, 2 = save.
 * Process-wide (folder layout, not per-tracker state). */
void texture_tracker_set_texture_dir_mode(int mode);

/* ---- VRAM mutation + frame hooks -------------------------------------- */

void texture_tracker_upload(TextureTracker *self,
      TTRect rect,
      uint16_t *vram);
void texture_tracker_blit(TextureTracker *self,
      TTRect dst,
      TTRect src);
void texture_tracker_clearRegion(TextureTracker *self,
      TTRect rect, uint16_t fill16);
void texture_tracker_notifyReadback(TextureTracker *self,
      TTRect rect,
      uint16_t *vram);

void texture_tracker_endFrame(TextureTracker *self);
void texture_tracker_on_queues_reset(TextureTracker *self);

/* ---- Draw-path resolution --------------------------------------------- */

HdTextureHandle texture_tracker_get_hd_texture_index(TextureTracker *self,
      TTRect rect,
      UsedMode *mode,
      unsigned int page_x,
      unsigned int page_y,
      bool *fastpath_capable,
      bool *cache_hit);
HdTexture texture_tracker_get_hd_texture(TextureTracker *self,
      HdTextureHandle index);

/* ---- Misc -------------------------------------------------------------- */

void texture_tracker_reload_textures_from_disk(TextureTracker *self);

/* Free all HD VRAM/RAM cache state immediately (used when Replace
 * Textures is turned off mid-session). */
void texture_tracker_flush_hd_state(TextureTracker *self);

/* Auto-create the dump/replacement folders once the corresponding
 * feature is enabled and a game is loaded (call after set_config so it
 * picks the right rect/pages folder). */
void texture_tracker_ensure_directories(TextureTracker *self,
      bool dump, bool replace);

/* ---- Save states ------------------------------------------------------- */

TextureTrackerSaveState *tts_new(void);
void tts_free(TextureTrackerSaveState *s);

void texture_tracker_save_state(TextureTracker *self,
      TextureTrackerSaveState *out);
void texture_tracker_load_state(TextureTracker *self,
      const TextureTrackerSaveState *state);

#ifdef __cplusplus
}
#endif

#endif /* __RHI_TT_H__ */
