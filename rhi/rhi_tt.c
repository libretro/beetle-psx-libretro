/* ============================================================
 * rhi_tt.c - shared HD-texture replacement / tracking engine.
 *
 * Extracted from rhi_lib_vulkan.c (the folded parallel-psx
 * custom-textures/ + texture_tracker.hpp content) and made
 * renderer-agnostic: every GPU operation goes through the
 * TTGpuBackend vtable supplied by the active RHI backend (GL or
 * Vulkan), and image decode/encode goes through libretro-common's
 * image_texture / image_transfer front end (PNG, JPEG, BMP, TGA,
 * WEBP, DDS in; PNG out via rpng) instead of stb_image.
 *
 * See rhi_tt.h for the public API and HD_TEXTURE_CACHE.md /
 * PAGE_ALIGN.md for the design. MSVC C89: no // comments, no
 * declarations after statements.
 * ============================================================ */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <boolean.h>
#include <retro_inline.h>
#include <retro_miscellaneous.h>

#include <rthreads/rthreads.h>
#include <streams/file_stream.h>
#include <file/file_path.h>
#include <formats/image.h>
#include <formats/rpng.h>

#include "rhi_tt.h"

/* Tracker-internal forward typedefs (subset of the old rhi_lib_vulkan.c
 * typedef block; the shared types now come from rhi_tt.h). */
typedef struct RectMatch RectMatch;
typedef struct DumpedMode DumpedMode;
typedef struct HdTexEntry HdTexEntry;
typedef struct HdTexMap HdTexMap;
typedef struct TextureUpload TextureUpload;
typedef struct IORequest IORequest;
typedef struct IOResponse IOResponse;
typedef struct IOChannel IOChannel;
typedef struct IOThread IOThread;
typedef struct Palette Palette;
typedef struct CachedPaletteHash CachedPaletteHash;
typedef struct TextureRect TextureRect;
typedef struct EnduringTextureRect EnduringTextureRect;
typedef struct EnduringRectArr EnduringRectArr;
typedef struct OwnedRectVec OwnedRectVec;
typedef struct RectIndexSet RectIndexSet;
typedef struct LookupGrid LookupGrid;
typedef struct RectTracker RectTracker;
typedef struct FusionRects FusionRects;
typedef struct FusedPage FusedPage;
typedef struct FusedPageVec FusedPageVec;
typedef struct FusedPages FusedPages;
typedef struct RestorableRect RestorableRect;
typedef struct RestorableRectVec RestorableRectVec;
typedef struct DbgHotkey DbgHotkey;
typedef struct CacheEntry CacheEntry;
typedef struct HandleCacheResult HandleCacheResult;
typedef struct HandleLRUCache HandleLRUCache;
typedef struct UploadOwningMap UploadOwningMap;
typedef struct TextureRectSaveState TextureRectSaveState;
typedef struct TextureRectSaveStateVec TextureRectSaveStateVec;
typedef struct RestorableRectSaveState RestorableRectSaveState;
typedef struct RestorableRectSaveStateVec RestorableRectSaveStateVec;
typedef struct RGBAImage RGBAImage;
typedef struct SRectResult SRectResult;
typedef struct TextureRectResult TextureRectResult;
typedef struct CellBounds CellBounds;
typedef struct UploadPtrEntry UploadPtrEntry;


   /* POD match rule from dump.cfg: a value of -1 means "wildcard" (matches any)
    * for that field. Was a class with a initialiser + NSDMI + matches() method. */
   struct RectMatch {
      int x;
      int y;
      int w;
      int h;
   };

static bool rect_match_matches(const RectMatch *m, Rect r) {
      return (m->x == -1 || m->x == r.x) && (m->y == -1 || m->y == r.y) &&
         (m->w == -1 || m->w == (int)r.width) && (m->h == -1 || m->h == (int)r.height);
   }

/* Maximum number of "ignore" rules read from dump.cfg. A fixed cap keeps the
 * list a plain inline array (no heap / no teardown); real dump configs have
 * a handful of entries, so this is never approached. */
#define DUMP_IGNORE_MAX 256

/* ------------------------------------------------------------------
 * GPU backend shims. One tracker instance exists per process (the
 * core runs exactly one RHI renderer at a time), so the backend is
 * file-scope state installed by texture_tracker_new. The ImageHandle
 * struct + ih_* helpers preserve the refcounted-handle idiom the
 * extracted code was written against, now delegating to the vtable.
 * ------------------------------------------------------------------ */

static TTGpuBackend tt_backend;
static bool tt_backend_set = false;

static INLINE TTGpuImage *tt_upload_levels(LoadedLevels *levels)
{
   return tt_backend.upload_levels(tt_backend.ctx, levels);
}
static INLINE void tt_img_addref(TTGpuImage *img)
{
   tt_backend.image_addref(tt_backend.ctx, img);
}
static INLINE void tt_img_release(TTGpuImage *img)
{
   tt_backend.image_release(tt_backend.ctx, img);
}
static INLINE unsigned tt_img_width(TTGpuImage *img)
{
   return tt_backend.image_width(tt_backend.ctx, img);
}
static INLINE unsigned tt_img_height(TTGpuImage *img)
{
   return tt_backend.image_height(tt_backend.ctx, img);
}
static INLINE size_t tt_img_vram_bytes(TTGpuImage *img)
{
   return tt_backend.image_vram_bytes(tt_backend.ctx, img);
}

struct ImageHandle { TTGpuImage *data; };
typedef struct ImageHandle ImageHandle;

static struct ImageHandle ih_make(TTGpuImage *p) { struct ImageHandle h; h.data = p; return h; }
static void ih_reset(struct ImageHandle *h) { if (h->data) tt_img_release(h->data); h->data = NULL; }
static TTGpuImage *ih_get(const struct ImageHandle *h) { return h->data; }
static int ih_is_valid(const struct ImageHandle *h) { return h->data != NULL; }
static void ih_copy(struct ImageHandle *dst, const struct ImageHandle *src) {
   dst->data = src->data;
   if (dst->data)
      tt_img_addref(dst->data);
}
static void ih_assign(struct ImageHandle *dst, const struct ImageHandle *src) {
   if (dst == src)
      return;
   ih_reset(dst);
   ih_copy(dst, src);
}
static void ih_move(struct ImageHandle *dst, struct ImageHandle produced) {
   ih_reset(dst);
   dst->data = produced.data;
}
static void ih_steal(struct ImageHandle *dst, struct ImageHandle *src) { dst->data = src->data; src->data = NULL; }

/* Copy an owned (+1) raw reference out of a handle - the HdTexture
 * producer paths hand these to the renderer, which releases them once
 * the bind is recorded. */
static TTGpuImage *tt_ih_owned_copy(const ImageHandle *h)
{
   if (h->data)
      tt_img_addref(h->data);
   return h->data;
}


   /* DumpedMode: plain POD (was a value type whose only method was equality
    * helper, now the free function dumpedmode_eq). */
   struct DumpedMode {
      TextureMode mode;
      uint32_t palette_hash;
   };

   static INLINE bool dumpedmode_eq(const struct DumpedMode *a,
         const struct DumpedMode *b)
   {
      return a->mode == b->mode && a->palette_hash == b->palette_hash;
   }


   /* -------------------------------------------------------------------------
    * * HdTexMap - palette_hash -> (Vulkan image, alpha flags), MSVC C89.
    *
    * A uint32_t -> HdImageHandle map on TextureUpload. Backed by a sorted,
    * malloc'd array of POD entries keyed by palette hash (binary-search lookup,
    * insert keeps it sorted). The image is held as a raw TTGpuImage * (so the array
    * can realloc) with the intrusive refcount managed by hand: a reference is
    * taken when an entry is stored and released on replace/erase/clear/free -
    * matching the raw-pointer scheme already used by the GPU image cache.
    * Entries are POD, so TextureUpload can be a plain malloc-backed value once
    * this and the other members are converted.
    * ------------------------------------------------------------------------- */
   struct HdTexEntry {
      uint32_t       key;          /* palette hash */
      TTGpuImage *image;        /* owns one reference while stored */
      int            alpha_flags;
   };
   struct HdTexMap {
      HdTexEntry *entries;
      int         count;
      int         cap;
   };

   static void hd_tex_map_init(HdTexMap *m)
   {
      m->entries = NULL;
      m->count = 0;
      m->cap = 0;
   }
   static int hd_tex_map_lower_bound(const HdTexMap *m, uint32_t key)
   {
      int lo = 0, hi = m->count;
      while (lo < hi) {
         int mid = lo + ((hi - lo) >> 1);
         if (m->entries[mid].key < key)
            lo = mid + 1;
         else
            hi = mid;
      }
      return lo;
   }
   /* Returns the entry for key, or NULL. */
   static HdTexEntry *hd_tex_map_find(HdTexMap *m, uint32_t key)
   {
      int i = hd_tex_map_lower_bound(m, key);
      if (i < m->count && m->entries[i].key == key)
         return &m->entries[i];
      return NULL;
   }
   static int hd_tex_map_contains(const HdTexMap *m, uint32_t key)
   {
      int i = hd_tex_map_lower_bound(m, key);
      return i < m->count && m->entries[i].key == key;
   }
   /* Insert or replace key -> (image, alpha). Takes a reference on `image`;
    * releases any image previously stored at this key. */
   static void hd_tex_map_set(HdTexMap *m,
         uint32_t key,
         TTGpuImage *image,
         int alpha_flags)
   {
      int i = hd_tex_map_lower_bound(m, key);
      if (i < m->count && m->entries[i].key == key) {
         if (image) tt_img_addref(image);
         if (m->entries[i].image) tt_img_release(m->entries[i].image);
         m->entries[i].image = image;
         m->entries[i].alpha_flags = alpha_flags;
         return;
      }
      if (m->count == m->cap) {
         int ncap = m->cap ? m->cap * 2 : 8;
         HdTexEntry *ne = (HdTexEntry *)realloc(m->entries, (size_t)ncap * sizeof(HdTexEntry));
         if (!ne)
            return;
         m->entries = ne;
         m->cap = ncap;
      }
      memmove(&m->entries[i + 1], &m->entries[i], (size_t)(m->count - i) * sizeof(HdTexEntry));
      if (image) tt_img_addref(image);
      m->entries[i].key = key;
      m->entries[i].image = image;
      m->entries[i].alpha_flags = alpha_flags;
      m->count++;
   }
   /* Release all image refs and reset to empty (keeps allocation). */
   static void hd_tex_map_clear(HdTexMap *m)
   {
      int i;
      for (i = 0; i < m->count; i++)
         if (m->entries[i].image)
            tt_img_release(m->entries[i].image);
      m->count = 0;
   }
   static void hd_tex_map_free(HdTexMap *m)
   {
      hd_tex_map_clear(m);
      free(m->entries);
      m->entries = NULL;
      m->cap = 0;
   }
   /* Deep-copy src into dst (dst assumed empty/inited); re-acquires image refs. */
   static void hd_tex_map_copy(HdTexMap *dst, const HdTexMap *src)
   {
      int i;
      dst->entries = NULL;
      dst->count = src->count;
      dst->cap = src->count;
      if (src->count) {
         dst->entries = (HdTexEntry *)malloc((size_t)src->count * sizeof(HdTexEntry));
         if (!dst->entries) {
            /* OOM: keep the map consistently empty rather than leaving count/cap
             * advertising entries over a NULL buffer (which the copy loop and
             * every later entries[] access would dereference). */
            dst->count = 0;
            dst->cap = 0;
            return;
         }
         for (i = 0; i < src->count; i++) {
            dst->entries[i] = src->entries[i];
            if (dst->entries[i].image)
               tt_img_addref(dst->entries[i].image);
         }
      }
   }

   struct TextureUpload {
      /* Intrusive refcount for shared ownership across TextureRects (replaces a
       * refcounted TextureUpload). A freshly created upload starts at 0;
       * texture_upload_new() bumps it to 1. Copies (deep-copy
       * initialiser/assignment, used by the by-value save-state map) get their
       * OWN fresh count - the refcount is deliberately not copied. */
      int refcount;
      /* VRAM source pixels (owned). Owned uint16_t array; filled once at
       * creation and thereafter read-only. */
      uint16_t *image;
      int       image_count;
      bool dumpable;
      int width;
      int height;
      uint32_t hash;
      /* Modes already dumped to disk (owned growable array, append-only).
       * Owned DumpedMode array. */
      DumpedMode *dumped_modes;
      int         dumped_modes_count;
      int         dumped_modes_cap;
      HdTexMap textures; /* palette hash -> (image, alpha) */
      /* Reduce Palette Range memo: the [min,max] CLUT indices this upload's index
       * data references, cached per sampling mode (recomputed when reduce_pal_mode
       * changes). reduce_pal_min < 0 = no valid range (use the full-palette hash). */
      int reduce_pal_min;
      int reduce_pal_max;
      int reduce_pal_mode;
               /* (HD load bookkeeping lives on the TextureTracker: hd_gpu_cache
                * / hd_cache / requested / pending_attach, keyed by
                * (hash,palette) so it survives this upload being recreated as
                * the sprite animation churns VRAM.) */
   };

   /* TextureUpload owns malloc'd buffers (image, dumped_modes) and an HdTexMap,
    * but is a plain struct: it is created, copied
    * and destroyed only through the explicit helpers below. texture_upload_init
    * mirrors the former default initialiser, texture_upload_destroy the former
    * teardown, and texture_upload_copy_contents the former deep copy (a fresh
    * refcount is NOT taken from the source - the destination keeps its own). */
   static void texture_upload_init(TextureUpload *u)
   {
      u->refcount = 0;
      u->image = NULL;
      u->image_count = 0;
      u->dumpable = false;
      u->width = 0;
      u->height = 0;
      u->hash = 0;
      u->dumped_modes = NULL;
      u->dumped_modes_count = 0;
      u->dumped_modes_cap = 0;
      u->reduce_pal_min = -1;
      u->reduce_pal_max = -1;
      u->reduce_pal_mode = -1;
      hd_tex_map_init(&u->textures);
   }
   static void texture_upload_destroy(TextureUpload *u)
   {
      free(u->image);
      free(u->dumped_modes);
      hd_tex_map_free(&u->textures);
      free(u);
   }
   /* Deep-copy the owned contents of src into dst, releasing dst's existing
    * buffers/texmap first. dst->refcount is left untouched (matching the old
    * copy-assignment, which never copied the refcount). */
   static void texture_upload_copy_contents(TextureUpload *dst,
         const TextureUpload *src)
   {
      if (dst == src)
         return;
      free(dst->image);
      free(dst->dumped_modes);
      hd_tex_map_free(&dst->textures);
      dst->image = NULL;
      dst->dumped_modes = NULL;
      dst->image_count = src->image_count;
      dst->dumped_modes_count = src->dumped_modes_count;
      dst->dumped_modes_cap = src->dumped_modes_count;
      dst->dumpable = src->dumpable;
      dst->width = src->width;
      dst->height = src->height;
      dst->hash = src->hash;
      dst->reduce_pal_min = -1; /* memo: force recompute on the copy */
      dst->reduce_pal_max = -1;
      dst->reduce_pal_mode = -1;
      if (dst->image_count) {
         dst->image = (uint16_t *)malloc((size_t)dst->image_count * sizeof(uint16_t));
         if (!dst->image)
            dst->image_count = 0; /* OOM: stay consistent (no buffer, no count) */
         else
            memcpy(dst->image, src->image, (size_t)dst->image_count * sizeof(uint16_t));
      }
      if (dst->dumped_modes_count) {
         dst->dumped_modes = (DumpedMode *)malloc((size_t)dst->dumped_modes_count * sizeof(DumpedMode));
         if (!dst->dumped_modes) {
            dst->dumped_modes_count = 0; /* OOM: stay consistent */
            dst->dumped_modes_cap = 0;
         } else
            memcpy(dst->dumped_modes, src->dumped_modes, (size_t)dst->dumped_modes_count * sizeof(DumpedMode));
      }
      hd_tex_map_copy(&dst->textures, &src->textures);
   }

   /* Allocate a new TextureUpload with refcount 1 (the caller owns that ref). */
   static TextureUpload *texture_upload_new()
   {
      TextureUpload *u = (TextureUpload *)malloc(sizeof(TextureUpload));
      texture_upload_init(u);
      u->refcount = 1;
      return u;
   }
   static void texture_upload_acquire(TextureUpload *u)
   {
      if (u)
         u->refcount++;
   }
   static void texture_upload_release(TextureUpload *u)
   {
      if (u && --u->refcount == 0)
         texture_upload_destroy(u); /* frees image/dumped_modes and releases the texmap refs */
   }

   /* byte buffer plus its size, rather than a managed byte vector. This is a
    * POD (trivially copyable) struct - copying it copies the pointer, NOT the
    * bytes, so ownership is by convention: exactly one LoadedLevels owns each
    * buffer and frees it. Ownership transfers are explicit pointer-steals
    * (push_move / move-assign helpers below); there is no implicit deep copy
    * and no teardown. This keeps it storable in plain arrays and
    * realloc-movable. */

   /* Allocate the RGBA buffer for a level (width*height*4). Frees any prior
    * buffer. Returns 0 on success, -1 on allocation failure (buffer left NULL). */
static int loaded_image_alloc(LoadedImage *img, int width, int height)
   {
      size_t bytes = (size_t)width * (size_t)height * 4u;
      free(img->owned_data);
      img->owned_data = (uint8_t *)malloc(bytes ? bytes : 1);
      img->owned_size = img->owned_data ? bytes : 0;
      img->width  = width;
      img->height = height;
      return img->owned_data ? 0 : -1;
   }

   /* Zero-initialise a level (no buffer). Use before loaded_image_alloc. */
static void loaded_image_init(LoadedImage *img)
   {
      img->owned_data = NULL;
      img->owned_size = 0;
      img->width      = 0;
      img->height     = 0;
   }

   /* A decoded texture as a set of mip levels. C-style dynamic array: `levels`
    * is a malloc'd array of `count` LoadedImage, owning all buffers. Replaces a
    * dynamic array of LoadedImage. POD/trivially-copyable: a copy aliases the
    * same buffers, so callers move ownership explicitly (loaded_levels_move)
    * and free explicitly (loaded_levels_reset). Initialise with
    * loaded_levels_init or zero-init before first use. */

   /* Total owned bytes across all levels. */
   static size_t loaded_levels_byte_size(const LoadedLevels *l)
   {
      size_t b = 0;
      int i;
      for (i = 0; i < l->count; i++)
         b += l->levels[i].owned_size;
      return b;
   }

   /* Append by stealing *src's buffer (src left empty). Grows with realloc.
    * Returns the stored level, or NULL on alloc failure. */
   static LoadedImage *loaded_levels_push_move(LoadedLevels *l,
         LoadedImage *src)
   {
      LoadedImage *grown = (LoadedImage *)realloc(l->levels, (size_t)(l->count + 1) * sizeof(LoadedImage));
      if (!grown)
         return NULL;
      l->levels = grown;
      l->levels[l->count] = *src; /* POD: copies the pointer (steal) */
      loaded_image_init(src);
      return &l->levels[l->count++];
   }

   /* Free all buffers + the array, return to empty state. */
   static void loaded_levels_reset(LoadedLevels *l)
   {
      int i;
      for (i = 0; i < l->count; i++)
         free(l->levels[i].owned_data);
      free(l->levels);
      l->levels = NULL;
      l->count  = 0;
   }

   /* Zero-initialise (no allocation). */
static void loaded_levels_init(LoadedLevels *l)
   {
      l->levels = NULL;
      l->count  = 0;
   }

   /* Move ownership src -> dst (dst's prior contents freed; src left empty). */
static void loaded_levels_move(LoadedLevels *dst, LoadedLevels *src)
   {
      if (dst == src)
         return;
      loaded_levels_reset(dst);
      dst->levels = src->levels;
      dst->count  = src->count;
      src->levels = NULL;
      src->count  = 0;
   }


   /* Max length for HD texture file paths built by the path helpers below.
    * Defined here so both IORequest and the path helpers can use it. */
   enum { PATH_MAX_TT = 4096 + 256 };

   enum IORequestKind {
      IORequestKind_Load,
      IORequestKind_Dump,
   };
   typedef enum IORequestKind IORequestKind;

   struct IORequest {
      struct IORequest *next;        /* intrusive FIFO link (queue-owned) */
      IORequestKind kind;
      /* Load payload (valid when kind == Load): */
      uint32_t hash;
      uint32_t palette_hash;
      bool     pages;                /* page-aligned experiment: load/dump in the -pages folder */
      /* Dump payload (valid when kind == Dump): */
      char     path[PATH_MAX_TT];
      int      width;
      int      height;
      /* Raw VRAM source + palette snapshot; the palette->RGBA->tri-alpha decode is
       * deferred to the IO worker (decode_dump_rgba) so it doesn't burst on the
       * render thread - the render thread only takes these cheap copies. */
      uint16_t *src;                 /* VRAM source words, row-major (owned; NULL if none) */
      size_t    src_len;             /* element count */
      uint16_t *palette;             /* CLUT copy (owned; NULL = no palette) */
      size_t    palette_len;         /* element count */
      int       dump_mode;           /* TextureMode as int (distinguishes ABGR1555 direct colour) */
      int       ppp;                 /* texels packed per 16-bit source word (1/2/4) */
   };

   static void io_request_free(IORequest *r)
   {
      if (r) {
         free(r->src);
         free(r->palette);
         free(r);
      }
   }

   const int ALPHA_FLAG_OPAQUE = 1;
   const int ALPHA_FLAG_SEMI_TRANSPARENT = 2;
   const int ALPHA_FLAG_TRANSPARENT = 4;

   struct IOResponse {
      struct IOResponse *next;       /* intrusive FIFO link (queue-owned) */
      uint32_t hash;
      uint32_t palette_hash;
      int alpha_flags;
      bool pages;                    /* page-aligned experiment: routes to the page attach pass */
      LoadedLevels levels;
   };

   static void io_response_free(IOResponse *r)
   {
      if (r) {
         loaded_levels_reset(&r->levels);
         free(r);
      }
   }

   struct IOChannel {
      slock_t *lock;
      scond_t *cond;
      /* Intrusive FIFO lists (protected by `lock`). Heads are popped/drained,
       * tails are where producers append. Dynamic arrays of IORequest /
       * IOResponse. */
      IORequest  *req_head,  *req_tail;            /* low priority: prefetch, savestate warm, dumps */
      IORequest  *req_high_head, *req_high_tail;   /* high priority: on-demand draw-time loads */
      IOResponse *resp_head, *resp_tail;
      bool done;
      /* Cross-thread refcount. The owning IOThread holds one reference and each
       * detached worker holds one; whichever releases last frees the channel.
       * Mutated only outside the lock, at thread-spawn and thread-exit, so a
       * plain int with no overlap is fine. */
      int refcount;
   };

   static void io_channel_destroy(IOChannel *c);
   static IOChannel *io_channel_new() {
      IOChannel *c = (IOChannel *)malloc(sizeof(IOChannel));
      c->lock = slock_new();
      c->cond = scond_new();
      c->req_head = c->req_tail = NULL;
      c->req_high_head = c->req_high_tail = NULL;
      c->resp_head = c->resp_tail = NULL;
      c->done = false;
      c->refcount = 1;
      return c;
   }
   /* The refcount is touched from the owning thread (spawn/teardown) and from
    * the detached workers (exit), so the increment/decrement must be
    * serialised. A single process-wide lock guards every transition; the actual
    * free happens after the lock is dropped so we never reference the channel's
    * own lock once it may be gone. */
   static slock_t *io_channel_rc_lock = NULL;
   static void io_channel_rc_lock_init() {
      if (!io_channel_rc_lock)
         io_channel_rc_lock = slock_new();
   }
   static void io_channel_acquire(IOChannel *c) {
      if (!c)
         return;
      slock_lock(io_channel_rc_lock);
      c->refcount++;
      slock_unlock(io_channel_rc_lock);
   }
   static void io_channel_release(IOChannel *c) {
      bool should_free;
      if (!c)
         return;
      slock_lock(io_channel_rc_lock);
      should_free = (--c->refcount == 0);
      slock_unlock(io_channel_rc_lock);
      if (should_free)
         io_channel_destroy(c);
   }

   /* FIFO helpers (caller holds channel->lock). Defined here so the IO worker
    * (io_thread) and the producers can all see them. */
   static void io_channel_push_request(IOChannel *c, IORequest *r) {       /* low priority */
      r->next = NULL;
      if (c->req_tail) c->req_tail->next = r; else c->req_head = r;
      c->req_tail = r;
   }
   static void io_channel_push_request_high(IOChannel *c, IORequest *r) {  /* high priority */
      r->next = NULL;
      if (c->req_high_tail) c->req_high_tail->next = r; else c->req_high_head = r;
      c->req_high_tail = r;
   }
   /* True if either queue has pending work (caller holds the lock). */
   static bool io_channel_has_requests(const IOChannel *c) {
      return c->req_high_head != NULL || c->req_head != NULL;
   }
   /* Pop one request, draining the high-priority queue first so on-demand
    * draw-time loads jump ahead of background prefetch/dumps. */
   static IORequest *io_channel_pop_request(IOChannel *c) {
      IORequest *r = c->req_high_head;
      if (r) {
         c->req_high_head = r->next;
         if (!c->req_high_head) c->req_high_tail = NULL;
         r->next = NULL;
         return r;
      }
      r = c->req_head;
      if (r) {
         c->req_head = r->next;
         if (!c->req_head) c->req_tail = NULL;
         r->next = NULL;
      }
      return r;
   }
   static void io_channel_push_response(IOChannel *c, IOResponse *r) {
      r->next = NULL;
      if (c->resp_tail) c->resp_tail->next = r; else c->resp_head = r;
      c->resp_tail = r;
   }
   /* Steal the entire response list; channel left empty. Returns the head. */
   static IOResponse *io_channel_take_responses(IOChannel *c) {
      IOResponse *head = c->resp_head;
      c->resp_head = c->resp_tail = NULL;
      return head;
   }

   /* Owns the IO worker thread pool. Formerly a class with a initialiser
    * (create the channel, spin up NUM_IO_THREADS detached workers each holding
    * a channel reference) and teardown (signal done, wake the workers, drop
    * this thread's reference); now a plain struct driven by io_thread_init /
    * io_thread_deinit. The single member is the refcounted channel pointer.
    * TextureTracker embeds one by value and drives its init/deinit. */
   struct IOThread {
      IOChannel *channel; /* refcounted; one ref held here, one per worker */
   };

   static void io_thread_init(IOThread *t);
   static void io_thread_deinit(IOThread *t);

   struct Palette {
      uint16_t *data;
      uint32_t hash;
   };

   struct CachedPaletteHash {
      Rect rect;
      uint32_t hash;
   };

   /* ============
    * RectTracker */

   /* TextureRect is a trivially-copyable POD: a borrowed view of an upload plus
    * a subrect. It does NOT manage the refcount in special members (so it
    * relocates with a bitwise move - no per-copy acquire/release, no exception
    * scaffolding in the containers that hold it). Ownership lives at container
    * boundaries instead: a container retains on insert and releases on
    * erase/clear/destroy via texture_rect_retain / texture_rect_release.
    * Transient TextureRect values (subTexture results, clip pairs, scratch
    * arrays) are borrowing and need no ref ops. */
   /* TextureRect: plain C struct (was a value type with texture_subrect() /
    * equality helper / inequality helper). The accessor becomes
    * texture_rect_subrect, the comparison texture_rect_eq; inequality helper
    * was unused and is dropped. */
   struct TextureRect {
      TextureUpload *upload;
      /* the offset into the original upload rect (offset_x + vram_rect.width <= upload->width) */
      int offset_x;
      int offset_y;
      SRect vram_rect;
   };

   /* in vram size (not hd), local to the uploaded data, different hd textures
    * for different palettes could have different sizes anyway */
   static INLINE struct SRect texture_rect_subrect(const struct TextureRect *t)
   {
      return make_srect(t->offset_x, t->offset_y, t->vram_rect.width, t->vram_rect.height);
   }
   static INLINE bool texture_rect_eq(const struct TextureRect *a,
         const struct TextureRect *b)
   {
      return a->upload == b->upload && a->offset_x == b->offset_x && a->offset_y == b->offset_y && srect_eq(&a->vram_rect, &b->vram_rect);
   }

   /* Build a TextureRect (borrowing - does not take a reference). */
static TextureRect make_texture_rect(TextureUpload *upload,
      int offset_x,
      int offset_y,
      SRect vram_rect)
   {
      TextureRect t;
      t.upload = upload;
      t.offset_x = offset_x;
      t.offset_y = offset_y;
      t.vram_rect = vram_rect;
      return t;
   }
   /* Ownership transfer helpers, used by owning containers only. */
static void texture_rect_retain(const TextureRect *t)  { texture_upload_acquire(t->upload); }
static void texture_rect_release(const TextureRect *t) { texture_upload_release(t->upload); }

   /* Borrowing scratch array of (POD) TextureRects - no ownership, used for the
    * blit/clear_rect transients that are immediately re-placed into owning
    * containers. Trivially relocatable, so push is a bare realloc/append. */
   POD_VEC_DECLARE(TextureRectVec, TextureRect);


   /* TODO: better name */
   struct EnduringTextureRect {
      TextureRect texture_rect;
      bool alive;
   };

   /* Owning, trivially-relocatable array of EnduringTextureRect (replaces a
    * dynamic array of EnduringTextureRect). Because TextureRect is POD, growth
    * is a realloc (bitwise relocation, no per-element move step or exception
    * scaffolding). Ownership is explicit: push retains the upload, and any slot
    * that leaves the array (compaction drop, clear, free) releases it. */
   struct EnduringRectArr {
      EnduringTextureRect *a;
      int count;
      int cap;
   };
static void enduring_arr_init(EnduringRectArr *v) { v->a = NULL; v->count = 0; v->cap = 0; }
static void enduring_arr_push(EnduringRectArr *v, TextureRect tr, bool alive) {
      if (v->count == v->cap) {
         int ncap = v->cap ? v->cap * 2 : 16;
         EnduringTextureRect *na = (EnduringTextureRect *)realloc(v->a, (size_t)ncap * sizeof(EnduringTextureRect));
         if (!na)
            return;
         v->a = na;
         v->cap = ncap;
      }
      texture_rect_retain(&tr);            /* the array now owns a reference */
      v->a[v->count].texture_rect = tr;
      v->a[v->count].alive = alive;
      v->count++;
   }
   /* Drop !alive slots, releasing their refs; survivors relocate by bitwise move. */
static void enduring_arr_compact(EnduringRectArr *v) {
      int w = 0, i;
      for (i = 0; i < v->count; i++) {
         if (v->a[i].alive) {
            if (w != i) v->a[w] = v->a[i];
            w++;
         } else {
            texture_rect_release(&v->a[i].texture_rect);
         }
      }
      v->count = w;
   }
static void enduring_arr_clear(EnduringRectArr *v) {
      int i;
      for (i = 0; i < v->count; i++)
         texture_rect_release(&v->a[i].texture_rect);
      v->count = 0;
   }
static void enduring_arr_free(EnduringRectArr *v) {
      enduring_arr_clear(v);
      free(v->a);
      v->a = NULL;
      v->cap = 0;
   }

   /* Owning vector of (POD) TextureRects, used where the rect list is itself
    * value-copied/compared (FusionRects, RestorableRect). The element is
    * trivially relocatable so the backing array relocates cheaply, but
    * ownership must be explicit: this wrapper retains on push and on copy, and
    * releases on destroy/clear/assign. */
   /* OwnedRectVec: plain C struct (copyable with retain-on-copy
    * / release-on-destroy refcount management). The element TextureRect is POD
    * but owns a refcounted upload, so ownership is explicit: ownedrects_push
    * retains, ownedrects_copy / ownedrects_assign deep-copy then retain each
    * element, and ownedrects_destroy releases each element then frees storage.
    * ownedrects_move steals storage (no refcount change). No implicit copy
    * exists -- structs holding one must call ownedrects_copy /
    * ownedrects_destroy explicitly. */
   struct OwnedRectVec {
      TextureRectVec v;
   };

   static INLINE void ownedrects_init(struct OwnedRectVec *r)
   {
      r->v.items = NULL; r->v.count = 0; r->v.cap = 0;
   }

   static INLINE void ownedrects_destroy(struct OwnedRectVec *r)
   {
      int i;
      for (i = 0; i < r->v.count; i++) texture_rect_release(&r->v.items[i]);
      TextureRectVec_free_storage(&r->v);
   }

   /* Deep-copy the POD elements of src->v into dst->v (replacing dst's
    * storage). Refcounts are adjusted by the caller (release before, retain
    * after). */
   static INLINE void ownedrects_copy_storage(struct OwnedRectVec *dst,
         const struct OwnedRectVec *src)
   {
      dst->v.count = 0;
      if (src->v.count <= 0)
         return;
      {
         size_t n = (size_t)src->v.count;
         if (src->v.count > dst->v.cap) {
            TextureRect *ni = (TextureRect *)realloc(dst->v.items, n * sizeof(TextureRect));
            if (!ni)
               return;
            dst->v.items = ni;
            dst->v.cap = src->v.count;
         }
         memcpy(dst->v.items, src->v.items, n * sizeof(TextureRect));
         dst->v.count = src->v.count;
      }
   }

   /* Copy-construct: dst must be uninitialized; deep-copy src and retain each element. */
   static INLINE void ownedrects_copy(struct OwnedRectVec *dst,
         const struct OwnedRectVec *src)
   {
      int i;
      ownedrects_init(dst);
      ownedrects_copy_storage(dst, src);
      for (i = 0; i < dst->v.count; i++) texture_rect_retain(&dst->v.items[i]);
   }

   /* Copy-assign: release dst's current elements, deep-copy src, retain each. */
   static INLINE void ownedrects_assign(struct OwnedRectVec *dst,
         const struct OwnedRectVec *src)
   {
      int i;
      if (dst == src) return;
      for (i = 0; i < dst->v.count; i++) texture_rect_release(&dst->v.items[i]);
      ownedrects_copy_storage(dst, src);
      for (i = 0; i < dst->v.count; i++) texture_rect_retain(&dst->v.items[i]);
   }

   /* Move: steal src's storage into dst (dst must be empty/initialized), leave src empty. */
   static INLINE void ownedrects_move(struct OwnedRectVec *dst,
         struct OwnedRectVec *src)
   {
      dst->v = src->v;
      src->v.items = NULL; src->v.count = 0; src->v.cap = 0;
   }

   static INLINE void ownedrects_push(struct OwnedRectVec *r, TextureRect t)
   {
      texture_rect_retain(&t);
      TextureRectVec_push(&r->v, &t);
   }

   static INLINE int ownedrects_size(const struct OwnedRectVec *r) { return r->v.count; }

   static INLINE bool ownedrects_eq(const struct OwnedRectVec *a,
         const struct OwnedRectVec *b)
   {
      int i;
      if (a->v.count != b->v.count) return false;
      for (i = 0; i < a->v.count; i++)
         if (!texture_rect_eq(&a->v.items[i], &b->v.items[i])) return false;
      return true;
   }

   enum { LOOKUP_GRID_COLUMNS = 16 };
   enum { LOOKUP_GRID_ROWS = 2 };
   enum { LOOKUP_CELL_WIDTH = 64 };
   enum { LOOKUP_CELL_HEIGHT = 256 };

   /* Sorted set of RectIndex (int), MSVC C89. A sorted set of RectIndex for the
    * overlap-dedup scratch set: a malloc'd sorted int array with binary-search
    * insert (dedups), cleared and refilled each query, then iterated in order.
    * Order differs from the old unordered_set (now ascending by index), which
    * only affects iteration order of overlapping rects - the consumers don't
    * depend on it. */
   struct RectIndexSet {
      RectIndex *items;
      int        count;
      int        cap;
   };
   static void rect_index_set_clear(RectIndexSet *s) { s->count = 0; }
   static int rect_index_set_lower_bound(const RectIndexSet *s, RectIndex v) {
      int lo = 0, hi = s->count;
      while (lo < hi) { int mid = lo + ((hi - lo) >> 1); if (s->items[mid] < v) lo = mid + 1; else hi = mid; }
      return lo;
   }
   static void rect_index_set_insert(RectIndexSet *s, RectIndex v) {
      int i = rect_index_set_lower_bound(s, v);
      if (i < s->count && s->items[i] == v) return;
      if (s->count == s->cap) {
         int ncap = s->cap ? s->cap * 2 : 16;
         RectIndex *ni = (RectIndex *)realloc(s->items, (size_t)ncap * sizeof(RectIndex));
         if (!ni)
            return;
         s->items = ni;
         s->cap = ncap;
      }
      memmove(&s->items[i + 1], &s->items[i], (size_t)(s->count - i) * sizeof(RectIndex));
      s->items[i] = v;
      s->count++;
   }

   /* Spatial lookup grid over psx texture pages. Formerly a class with a
    * initialiser/teardown managing the per-cell malloc'd arrays; now a plain
    * struct driven by lookup_grid_init / lookup_grid_deinit. Each Cell is a
    * growable array of POD LookupEntry; insert/get/clear are unchanged methods.
    * RectTracker embeds one by value and drives its init/deinit. */
   struct LookupEntry {
      SRect rect;
      RectIndex index;
   };
   typedef struct LookupEntry LookupEntry;
   struct Cell {
      LookupEntry *entries;
      int count;
      int cap;
   };
   typedef struct Cell Cell;
   struct LookupGrid {
      /* Each cell is a psx texture page (64x256); a dynamic array of
       * LookupEntry. Now a malloc'd growable array per cell (POD entries). */
      Cell cells[LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS];
   };

   static void lookup_grid_init(LookupGrid *g);
   static void lookup_grid_deinit(LookupGrid *g);
   static void lookup_grid_insert(LookupGrid *self, SRect r, RectIndex index);
   static void lookup_grid_get(LookupGrid *self,
         SRect r,
         RectIndexSet *results);
   static void lookup_grid_clear(LookupGrid *self);

   /* RectTracker: tracks placed/uploaded texture rects with a spatial lookup
    * grid. A plain C struct + rect_tracker_* free
    * functions. The init/teardown (which init/free the EnduringRectArr and
    * LookupGrid) become rect_tracker_init / rect_tracker_deinit, called by the
    * embedding TextureTracker's initialiser/teardown. */
   struct RectTracker {
      EnduringRectArr textures; /* owning trivially-relocatable array */
      LookupGrid lookup_grid;
      bool lookup_grid_dirty;
   };

static void rect_tracker_init(struct RectTracker *self)
   {
      enduring_arr_init(&self->textures);
      lookup_grid_init(&self->lookup_grid);
      self->lookup_grid_dirty = false;
   }
static void rect_tracker_deinit(struct RectTracker *self)
   {
      enduring_arr_free(&self->textures);
      lookup_grid_deinit(&self->lookup_grid);
   }

   static void rect_tracker_place(struct RectTracker *self,
         TextureRect texture);
   static void rect_tracker_upload(struct RectTracker *self,
         SRect rect,
         TextureUpload *upload);
   static void rect_tracker_blit(struct RectTracker *self,
         SRect dst,
         SRect src);
   static void rect_tracker_clear_rect(struct RectTracker *self, SRect *rect);
static void rect_tracker_clear(struct RectTracker *self, SRect rect)
   {
      rect_tracker_clear_rect(self, &rect);
      self->lookup_grid_dirty = true;
   }
   static void rect_tracker_releaseDeadHandles(struct RectTracker *self);
   /* Returns results by pointer (was a reference). */
   static RectIndexSet *rect_tracker_overlapping(struct RectTracker *self,
         Rect rect,
         RectIndexSet *results);

   /* This pointer will be valid until the next upload/blit/clear/endFrame, so
    * use it immediately. Returns NULL when index is out of range. */
   static TextureRect *rect_tracker_get_index(struct RectTracker *self,
         RectIndex index);

   /* Returns NULL if no texture with the given hash can be found. */
   static TextureUpload *rect_tracker_find_upload(struct RectTracker *self,
         uint32_t hash);

   static void rect_tracker_rebuild_lookup_grid(struct RectTracker *self);
   /* RectTracker
    * ============ */

   /* FusionRects: plain C struct.
    *  Embeds an OwnedRectVec (refcounted), so init/destroy/move/eq are
    * explicit free functions; the scaleX/scaleY default-zero NSDMIs move into
    * fusionrects_init. */
   struct FusionRects {
      OwnedRectVec rects;
      Rect vram_rect;
      unsigned int scaleX;
      unsigned int scaleY;
   };

   static INLINE void fusionrects_init(struct FusionRects *f)
   {
      ownedrects_init(&f->rects);
      f->vram_rect.x = 0; f->vram_rect.y = 0; f->vram_rect.width = 0; f->vram_rect.height = 0;
      f->scaleX = 0;
      f->scaleY = 0;
   }
   static INLINE void fusionrects_destroy(struct FusionRects *f)
   {
      ownedrects_destroy(&f->rects);
   }
   /* Move src into dst (dst must be initialized): steal rects, copy POD fields. */
   static INLINE void fusionrects_move(struct FusionRects *dst,
         struct FusionRects *src)
   {
      ownedrects_move(&dst->rects, &src->rects);
      dst->vram_rect = src->vram_rect;
      dst->scaleX = src->scaleX;
      dst->scaleY = src->scaleY;
   }
   static INLINE bool fusionrects_eq(const struct FusionRects *a,
         const struct FusionRects *b)
   {
      return rect_eq(&a->vram_rect, &b->vram_rect) && a->scaleX == b->scaleX && a->scaleY == b->scaleY && ownedrects_eq(&a->rects, &b->rects);
   }

   struct FusedPage {
      ImageHandle texture;

      uint32_t palette;
      Rect full_page_rect;

      bool dirty;
      bool dead;

      size_t   bytes;      /* approx VRAM footprint of texture (w*h*4); for the LRU budget */
      uint64_t last_used;  /* LRU tick (higher = more recently used) */

      FusionRects fusion;
   };

   /* Copy/destroy helpers for FusedPage's ImageHandle member, replacing the
    * implicit copy step incref / teardown decref the macro used to provide.
    * The remaining fields are trivially copyable. */
static void fp_copy(FusedPage *dst, const FusedPage *src) {
      /* Copy the trivially-copyable fields explicitly (NOT a blanket *dst=*src,
       * which would shallow-alias the OwnedRectVec inside fusion and break its
       * refcount). dst->fusion.rects must already be in a valid state -- empty
       * (fp_init_raw) for raw storage, or live for in-place compaction -- so
       * ownedrects_assign releases dst's current elements then
       * deep-copies+retains src's. */
      dst->palette         = src->palette;
      dst->full_page_rect  = src->full_page_rect;
      dst->dirty           = src->dirty;
      dst->dead            = src->dead;
      dst->bytes           = src->bytes;
      dst->last_used       = src->last_used;
      dst->fusion.vram_rect = src->fusion.vram_rect;
      dst->fusion.scaleX    = src->fusion.scaleX;
      dst->fusion.scaleY    = src->fusion.scaleY;
      ownedrects_assign(&dst->fusion.rects, &src->fusion.rects);
      dst->texture.data = src->texture.data;
      if (dst->texture.data) tt_img_addref(dst->texture.data);  /* retain */
   }
   /* Seed a raw (uninitialised) FusedPage slot's only heap-owning member --
    * fusion.rects (an OwnedRectVec) -- to the empty state, so the subsequent
    * fp_copy's ownedrects_assign releases against a valid (empty) dst rather
    * than through the slot's indeterminate items/count. Required before fp_copy
    * into malloc'd storage (grow/push); the in-place compaction path already
    * has a live dst and must NOT use this. */
static void fp_init_raw(FusedPage *p) {
      ownedrects_init(&p->fusion.rects);
      p->bytes = 0;
      p->last_used = 0;
   }
static void fp_destroy(FusedPage *p) {
      ih_reset(&p->texture);
      ownedrects_destroy(&p->fusion.rects);
   }

   /* Owning array of FusedPage. A dynamic array of FusedPage. FusedPage owns an
    * ImageHandle and a FusionRects (whose OwnedRectVec retains/releases its
    * TextureRects), and is used by copy (append page, and the compaction's
    * element copy-assign), so this container copies/-assigns elements rather
    * than moving: growth copies each element into new storage into new storage
    * and clears the old slot. push() copy-inserts; indexed access and
    * pointer-range iteration keep the existing size()/ indexing and iteration
    * uses. truncate(n) clears the tail [n, count) and is how remove_dead()
    * drops compacted-out entries (replacing erase(it, end())). Ownership lives
    * at the container level. */
   /* FusedPageVec: owning growable array of FusedPage (each holds a refcounted
    * ImageHandle, so growth/teardown go through fp_copy/fp_destroy). Converted
    * from a container is a plain struct with + fused_page_vec_* free functions;
    * the embedding FusedPages drives init_empty/deinit. */
   struct FusedPageVec {
      FusedPage *items;
      int count;
      int cap;
   };

   static void fused_page_vec_grow(struct FusedPageVec *v, int ncap)
   {
      FusedPage *nitems = (FusedPage *)malloc((size_t)ncap * sizeof(FusedPage));
      int i;
      if (!nitems)
         return; /* OOM: leave the vector unchanged */
      for (i = 0; i < v->count; i++) {
         fp_init_raw(&nitems[i]);
         fp_copy(&nitems[i], &v->items[i]);
         fp_destroy(&v->items[i]);
      }
      free(v->items);
      v->items = nitems;
      v->cap = ncap;
   }
static void fused_page_vec_init_empty(struct FusedPageVec *v)
   {
      v->items = NULL; v->count = 0; v->cap = 0;
   }
static void fused_page_vec_deinit(struct FusedPageVec *v)
   {
      int i;
      for (i = 0; i < v->count; i++)
         fp_destroy(&v->items[i]);
      free(v->items);
      v->items = NULL; v->count = 0; v->cap = 0;
   }
static void fused_page_vec_push(struct FusedPageVec *v, const FusedPage *e)
   {
      if (v->count >= v->cap)
         fused_page_vec_grow(v, v->cap ? v->cap * 2 : 8);
      fp_init_raw(&v->items[v->count]);
      fp_copy(&v->items[v->count], e);
      v->count++;
   }
static int fused_page_vec_size(const struct FusedPageVec *v) { return v->count; }
static FusedPage *fused_page_vec_at(struct FusedPageVec *v, int i) { return &v->items[i]; }
   /* Destroy elements [n, count); used to drop the compacted-out tail. */
static void fused_page_vec_truncate(struct FusedPageVec *v, int n)
   {
      int i;
      for (i = n; i < v->count; i++)
         fp_destroy(&v->items[i]);
      v->count = n;
   }

   struct FusedPages {
      FusedPageVec pages;
      /* LRU budget: composited page images are unbudgeted otherwise and grow without
       * bound (they are the dominant HD VRAM sink in heavy-overlap games). Evicted at
       * the on_queues_reset safe point (no fused handles live). */
      size_t   budget_bytes;
      uint64_t tick;        /* monotonic LRU clock (bumped on each page access) */
   };

static void fused_pages_init(struct FusedPages *self) {
      fused_page_vec_init_empty(&self->pages);
      self->budget_bytes = 0;
      self->tick = 0;
   }
static void fused_pages_set_budget(struct FusedPages *self, size_t bytes) { self->budget_bytes = bytes; }
static void fused_pages_deinit(struct FusedPages *self) { fused_page_vec_deinit(&self->pages); }
   static HdTextureHandle fused_pages_get_or_make(struct FusedPages *self,
         Rect page_rect,
         uint32_t palette,
         struct RectTracker *tracker);
   static HdTexture fused_pages_get_from_handle(struct FusedPages *self,
         HdTextureHandle handle,
         ImageHandle *default_hd_texture);
   static void fused_pages_mark_dirty(struct FusedPages *self, Rect rect); /* For blit dst, upload, and hd texture load */
   static void fused_pages_mark_dead(struct FusedPages *self, Rect rect); /* For clear */
   static void fused_pages_rebuild_dirty(struct FusedPages *self,
         struct RectTracker *tracker);
   static void fused_pages_remove_dead(struct FusedPages *self);
   static void fused_pages_evict(struct FusedPages *self); /* LRU-evict live pages to the budget */
   static int64_t page_bytes(FusionRects *fusion); /* approx VRAM footprint of a fused page */

   struct RestorableRect {
      Rect rect;
      uint32_t hash;
      OwnedRectVec to_restore;
   };

   /* RestorableRect holds an OwnedRectVec, so its copy/destroy must run the
    * OwnedRectVec refcount logic explicitly (it is a plain C struct). */
   static INLINE void restorablerect_init(struct RestorableRect *r)
   {
      ownedrects_init(&r->to_restore);
   }
   static INLINE void restorablerect_destroy(struct RestorableRect *r)
   {
      ownedrects_destroy(&r->to_restore);
   }
   /* Copy-construct dst (uninitialized) from src: POD fields bitwise, to_restore deep. */
   static INLINE void restorablerect_copy(struct RestorableRect *dst,
         const struct RestorableRect *src)
   {
      dst->rect = src->rect;
      dst->hash = src->hash;
      ownedrects_copy(&dst->to_restore, &src->to_restore);
   }
   /* Copy-assign dst (initialized) from src. */
   static INLINE void restorablerect_assign(struct RestorableRect *dst,
         const struct RestorableRect *src)
   {
      dst->rect = src->rect;
      dst->hash = src->hash;
      ownedrects_assign(&dst->to_restore, &src->to_restore);
   }
   /* Move src into dst (uninitialized): POD fields bitwise, steal to_restore. */

   /* Owning array of RestorableRect. A dynamic array of RestorableRect.
    * RestorableRect holds an OwnedRectVec (retain/release on copy, move
    * suppressed), so it is copyable but not movable - the backing array
    * relocated it by copy. This container therefore copies/-assigns
    * elements: growth copies each into new storage
    * and destroys the old slot; push() copy-inserts (matching the old
    * an append, which is a copy);
    * clear()/the teardown run each element's teardown (releasing its rect
    * refs). erase_at(i) removes one element by copy-assigning the tail down and
    * destroying the now-duplicated last slot. */
   /* RestorableRectVec: plain C struct. Its element RestorableRect owns an
    * OwnedRectVec (refcounted), so element relocation / insertion / removal go
    * through restorablerect_copy / _assign / _destroy / _move rather than
    * placement-new and explicit teardown calls. The container itself is moved
    * by stealing storage; no copy exists. */
   struct RestorableRectVec {
      RestorableRect *items;
      int count;
      int cap;
   };

   /* Move o into v (v must be initialized): free v's elements, steal o's storage. */
   static INLINE void rrvec_grow(struct RestorableRectVec *v, int ncap)
   {
      int i;
      RestorableRect *nitems = (RestorableRect *)malloc((size_t)ncap * sizeof(RestorableRect));
      if (!nitems)
         return; /* OOM: leave the vector unchanged */
      for (i = 0; i < v->count; i++) {
         restorablerect_copy(&nitems[i], &v->items[i]);
         restorablerect_destroy(&v->items[i]);
      }
      free(v->items);
      v->items = nitems;
      v->cap = ncap;
   }
   /* Copy-insert v (which, move being
    * suppressed, was a copy). */
   static INLINE void rrvec_push(struct RestorableRectVec *vec,
         const struct RestorableRect *val)
   {
      if (vec->count >= vec->cap)
         rrvec_grow(vec, vec->cap ? vec->cap * 2 : 8);
      restorablerect_copy(&vec->items[vec->count], val);
      vec->count++;
   }
   static INLINE int rrvec_size(const struct RestorableRectVec *v) { return v->count; }
   static INLINE void rrvec_clear(struct RestorableRectVec *v)
   {
      int i;
      for (i = 0; i < v->count; i++)
         restorablerect_destroy(&v->items[i]);
      v->count = 0;
   }
   /* Remove element i by copy-assigning the tail down and destroying the
    * duplicated last slot. */
   static INLINE void rrvec_erase_at(struct RestorableRectVec *v, int i)
   {
      int j;
      for (j = i; j + 1 < v->count; j++)
         restorablerect_assign(&v->items[j], &v->items[j + 1]);
      restorablerect_destroy(&v->items[v->count - 1]);
      v->count--;
   }

   /* Edge-triggered debug keyboard hotkey. Formerly a class with an int
    * initialiser; now a plain struct driven by dbg_hotkey_init. query() does
    * the rising-edge detection. TextureTracker embeds three of these by value
    * and initialises them in its initialiser. */
   struct DbgHotkey {
      enum retro_key key;
      bool was_key_down;
   };

   static void dbg_hotkey_init(DbgHotkey *h, enum retro_key key)
   {
      h->key = key;
      h->was_key_down = false;
   }
   static bool dbg_hotkey_query(struct DbgHotkey *self);

   struct CacheEntry {
      Rect rect;
      /* Lookup key (the draw's full palette hash). Kept separate from
       * handle.palette_hash because with Reduce Palette Range the bound handle may
       * carry a reduced palette hash, while the cache is keyed by the full hash. */
      uint32_t key_palette_hash;
      HdTextureHandle handle;
   };

   /* Result of a handle-cache lookup (handle plus a found flag). */
   struct HandleCacheResult {
      HdTextureHandle handle;
      bool found;
   };

   /* Small fixed-capacity move-to-front cache of recently-used HD texture
    * handles. CacheEntry is POD, so the entries live in an inline array
    * (capacity HANDLE_LRU_MAX) with a count - no dynamic array, no heap. */
   enum { HANDLE_LRU_MAX = 4 };

   /* Small fixed-capacity move-to-front cache of recently-used HD texture
    * handles. Formerly a class with a initialiser; now a plain struct driven by
    * handle_lru_cache_init. CacheEntry is POD (its defensive default-init is
    * never read before being written - get only scans entries[0..count) and
    * insert writes entries[0] within that range), so the entries live in an
    * inline array (capacity HANDLE_LRU_MAX) with a count - no dynamic array, no
    * heap. */
   struct HandleLRUCache {
      int64_t dbg_hits;
      int64_t dbg_misses;
      int max_size;
      int count;
      CacheEntry entries[HANDLE_LRU_MAX];

   };

   static void handle_lru_cache_init(HandleLRUCache *c, int max_size)
   {
      if (max_size > HANDLE_LRU_MAX)
         max_size = HANDLE_LRU_MAX;
      c->max_size = max_size;
      c->count = 0;
      /* The former initialiser left these uninitialised (they were only ever
       * zeroed inside a TT_LOG_VERBOSE block after first use); zero them here
       * so the hit/miss counters start from a defined value. */
      c->dbg_hits = 0;
      c->dbg_misses = 0;
   }

   /* ========================================
    * Save State */
   /* Owning hash -> TextureUpload map for the texture-tracker save state.
    * A uint32_t -> TextureUpload map: each entry owns a heap
    * TextureUpload (deep-copied in on insert, deleted on destroy). The values
    * are non-trivially-copyable (owned image/dumped_modes/textures), so the
    * backing array holds pointers - trivially relocatable - and ownership is
    * explicit alloc/free. The type is moved (not copied): it is moved up out
    * of save_state() and move-assigned into the file-static save_state across
    * renderer recreations, so move-assign must free any entries it already
    * holds before adopting the source's. Copying is deleted to prevent any
    * accidental double-ownership. */
   /* UploadOwningMap: plain C struct. The entries are owning pointers to
    * heap-allocated TextureUploads; ownership moves via uploadmap_move (steal,
    * source emptied) and is released by uploadmap_destroy. No copy operation
    * exists -- callers must move, never copy. */
   struct UploadOwningMapEntry { uint32_t key; TextureUpload *val; };
   typedef struct UploadOwningMapEntry UploadOwningMapEntry;
   struct UploadOwningMap {
      struct UploadOwningMapEntry *items;
      int count;
      int cap;
   };

   static INLINE void uploadmap_init(struct UploadOwningMap *m)
   {
      m->items = NULL; m->count = 0; m->cap = 0;
   }

   static INLINE void uploadmap_destroy(struct UploadOwningMap *m)
   {
      int i;
      for (i = 0; i < m->count; i++)
         texture_upload_destroy(m->items[i].val);
      free(m->items);
      m->items = NULL; m->count = 0; m->cap = 0;
   }

   /* Move: steal o's storage into m (which must be empty/initialized), leave o empty. */
   static INLINE void uploadmap_move(struct UploadOwningMap *m,
         struct UploadOwningMap *o)
   {
      m->items = o->items; m->count = o->count; m->cap = o->cap;
      o->items = NULL; o->count = 0; o->cap = 0;
   }

   static INLINE bool uploadmap_contains(const struct UploadOwningMap *m,
         uint32_t key)
   {
      int i;
      for (i = 0; i < m->count; i++)
         if (m->items[i].key == key)
            return true;
      return false;
   }

   /* Takes ownership of an already-heap-allocated upload under key. Caller must
    * have ensured the key is absent (mirrors the old map's insert-if-absent
    * guard). */
   static INLINE void uploadmap_insert(struct UploadOwningMap *m,
         uint32_t key,
         TextureUpload *val)
   {
      if (m->count >= m->cap) {
         int ncap = m->cap ? m->cap * 2 : 8;
         UploadOwningMapEntry *nitems = (UploadOwningMapEntry *)realloc(m->items, (size_t)ncap * sizeof(UploadOwningMapEntry));
         if (!nitems)
            return;
         m->items = nitems;
         m->cap = ncap;
      }
      m->items[m->count].key = key;
      m->items[m->count].val = val;
      m->count++;
   }

   struct TextureRectSaveState {
      uint32_t upload_hash;
      int offset_x;
      int offset_y;
      SRect vram_rect;
   };

   /* Owning array of (POD) TextureRectSaveState. Replaces A dynamic array of
    * TextureRectSaveState. Elements are trivially relocatable, so grow is a
    * plain realloc; the type owns its buffer (free on destroy) and is moved
    * (not copied) so it can live inside the save-state structs. */
   struct TextureRectSaveStateVec {
      TextureRectSaveState *items;
      int count;
      int cap;
   };
static void TextureRectSaveStateVec_init(struct TextureRectSaveStateVec *v) { v->items = NULL; v->count = 0; v->cap = 0; }
static void TextureRectSaveStateVec_free_storage(struct TextureRectSaveStateVec *v) { free(v->items); v->items = NULL; v->count = 0; v->cap = 0; }
static int  TextureRectSaveStateVec_size(const struct TextureRectSaveStateVec *v) { return v->count; }
static TextureRectSaveState *TextureRectSaveStateVec_at(struct TextureRectSaveStateVec *v, int i) { return &v->items[i]; }
static void TextureRectSaveStateVec_push(struct TextureRectSaveStateVec *v,
      const TextureRectSaveState *valp){
      if (v->count >= v->cap) {
         int ncap = v->cap ? v->cap * 2 : 8;
         TextureRectSaveState *nitems = (TextureRectSaveState *)realloc(v->items, (size_t)ncap * sizeof(TextureRectSaveState));
         if (!nitems)
            return;
         v->items = nitems;
         v->cap = ncap;
      }
      v->items[v->count++] = *valp;
   }
   /* Move src into dst (steal buffer), leaving src empty - replaces the
    * implicit move of the container. */
static void TextureRectSaveStateVec_move(struct TextureRectSaveStateVec *dst,
      struct TextureRectSaveStateVec *src){
      dst->items = src->items; dst->count = src->count; dst->cap = src->cap;
      src->items = NULL; src->count = 0; src->cap = 0;
   }

   struct RestorableRectSaveState {
      Rect rect;
      uint32_t hash;
      TextureRectSaveStateVec to_restore;
   };
static void rrss_init(struct RestorableRectSaveState *r) {
      r->rect.x = 0; r->rect.y = 0; r->rect.width = 0; r->rect.height = 0;
      r->hash = 0;
      TextureRectSaveStateVec_init(&r->to_restore);
   }
static void rrss_destroy(struct RestorableRectSaveState *r) {
      TextureRectSaveStateVec_free_storage(&r->to_restore);
   }

   /* Move src into dst (steal to_restore), leaving src empty. */
   static void rrss_move(struct RestorableRectSaveState *dst,
      struct RestorableRectSaveState *src)
   {
      dst->rect = src->rect;
      dst->hash = src->hash;
      TextureRectSaveStateVec_move(&dst->to_restore, &src->to_restore);
   }

   /* Owning array of RestorableRectSaveState. The element owns a heap array
    * (to_restore) and is not trivially relocatable, so growth moves
    * each element into the new storage and clears the old
    * one, rather than memcpy. Moved (not copied) so it composes inside the
    * save-state structs. */
   struct RestorableRectSaveStateVec {
      RestorableRectSaveState *items;
      int count;
      int cap;
   };
static void RestorableRectSaveStateVec_init(struct RestorableRectSaveStateVec *v) { v->items = NULL; v->count = 0; v->cap = 0; }
static int  RestorableRectSaveStateVec_size(const struct RestorableRectSaveStateVec *v) { return v->count; }
static RestorableRectSaveState *RestorableRectSaveStateVec_at(struct RestorableRectSaveStateVec *v, int i) { return &v->items[i]; }
static void RestorableRectSaveStateVec_grow(struct RestorableRectSaveStateVec *v,
      int ncap){
      RestorableRectSaveState *nitems =
         (RestorableRectSaveState *)malloc((size_t)ncap * sizeof(RestorableRectSaveState));
      int i;
      if (!nitems)
         return; /* OOM: leave the vector unchanged */
      for (i = 0; i < v->count; i++) {
         rrss_move(&nitems[i], &v->items[i]);
         rrss_destroy(&v->items[i]);
      }
      free(v->items);
      v->items = nitems;
      v->cap = ncap;
   }
   /* Takes ownership of *v's contents by move (src left empty). */
static void RestorableRectSaveStateVec_push_move(struct RestorableRectSaveStateVec *v,
      struct RestorableRectSaveState *src){
      if (v->count >= v->cap)
         RestorableRectSaveStateVec_grow(v, v->cap ? v->cap * 2 : 8);
      rrss_move(&v->items[v->count], src);
      v->count++;
   }
static void RestorableRectSaveStateVec_free_storage(struct RestorableRectSaveStateVec *v) {
      int i;
      for (i = 0; i < v->count; i++)
         rrss_destroy(&v->items[i]);
      free(v->items);
      v->items = NULL; v->count = 0; v->cap = 0;
   }

   /* TextureTrackerSaveState: plain C struct. Owns two save-state vectors and
    * an UploadOwningMap; managed explicitly through tts_init / tts_destroy /
    * tts_move. No copy exists -- callers must move (steal), never copy. */
   struct TextureTrackerSaveState {
      TextureRectSaveStateVec rects;
      RestorableRectSaveStateVec restorable;
      UploadOwningMap uploads;
   };

   static INLINE void tts_init(struct TextureTrackerSaveState *s)
   {
      TextureRectSaveStateVec_init(&s->rects);
      RestorableRectSaveStateVec_init(&s->restorable);
      uploadmap_init(&s->uploads);
   }

   static INLINE void tts_destroy(struct TextureTrackerSaveState *s)
   {
      TextureRectSaveStateVec_free_storage(&s->rects);
      RestorableRectSaveStateVec_free_storage(&s->restorable);
      uploadmap_destroy(&s->uploads);
   }

   /* Move o into s (which must already be initialized): free s's current
    * storage, steal o's, and leave o empty/initialized. Mirrors the old
    * move-assignment. */
   static INLINE void tts_move(struct TextureTrackerSaveState *s,
         struct TextureTrackerSaveState *o)
   {
      if (s == o)
         return;
      TextureRectSaveStateVec_free_storage(&s->rects);
      RestorableRectSaveStateVec_free_storage(&s->restorable);
      uploadmap_destroy(&s->uploads);
      TextureRectSaveStateVec_move(&s->rects, &o->rects);
      s->restorable = o->restorable;
      RestorableRectSaveStateVec_init(&o->restorable);
      uploadmap_move(&s->uploads, &o->uploads);
   }
   /* End of Save State
    * ======================================== */

   /* Tunable HD-texture cache budgets. hd_cache = system RAM (decoded CPU
    * levels); hd_gpu_cache = VRAM (uploaded Vulkan images). The VRAM budget
    * targets 8 GB cards - lower it if you see VRAM-pressure "QueuePresent
    * failed" / swapchain churn, raise it on larger cards. */
   static const size_t HD_CACHE_RAM_BUDGET  = (size_t)2 * 1024 * 1024 * 1024; /* 2 GB */
   static const size_t HD_CACHE_VRAM_BUDGET = (size_t)3 * 1024 * 1024 * 1024; /* 3 GB */

   /* (hash, palette_hash) packed into one 64-bit key. The caches below are
    * keyed by this single integer instead of HdTextureId, so lookups compare
    * one int with no comparator / tree descent. */
   static uint64_t hd_pack_key(HdTextureId id)
   {
      uint64_t k = ((uint64_t)id.hash << 32) | (uint64_t)id.palette_hash;
      /* Namespace page-aligned combos away from upload-rect combos in the SHARED
       * structures (requested / hd_cache / hd_gpu_cache). The C key has no spare
       * bit (hash + palette fill all 64), so fold the page flag in with a fixed
       * 64-bit salt rather than the C++ struct's extra comparison field. The
       * shared structures are point-access only (never unpacked back to a hash),
       * so the salt is safe; page-only sets that ARE unpacked (pending_attach_pages)
       * store the un-salted base key instead. */
      if (id.pages)
         k ^= UINT64_C(0x9E3779B97F4A7C15);
      return k;
   }

   /* -------------------------------------------------------------------------
    * * Byte-budgeted LRU cache, MSVC C89.
    *
    * Built from an intrusive list of entries plus an id -> entry map design -
    * two heap node allocations per insert and an O(log n) red-black descent per
    * lookup - with a contiguous index-LRU + open-addressed (linear-probe) flat
    * hash table, all held in malloc'd arrays. No generics, no classes, no
    * virtual, no STL. The two cache variants (RAM levels, VRAM images) are
    * produced by macro instantiation: HD_LRU_DECLARE emits the struct and
    * prototypes, HD_LRU_DEFINE emits the bodies, with a DISPOSE callback that
    * frees payload-owned resources when a live entry leaves the cache.
    * ------------------------------------------------------------------------- */

   /* fmix64 - cheap, strong avalanche for integer keys. The multipliers are
    * built from 32-bit halves so no C99 long-long (ULL) literals appear. */
   static uint64_t hd_lru_mix(uint64_t x)
   {
      uint64_t k1 = ((uint64_t)0xff51afd7u << 32) | (uint64_t)0xed558ccdu;
      uint64_t k2 = ((uint64_t)0xc4ceb9feu << 32) | (uint64_t)0x1a85ec53u;
      x ^= x >> 33;
      x *= k1;
      x ^= x >> 33;
      x *= k2;
      x ^= x >> 33;
      return x;
   }

#define HD_LRU_DECLARE(NAME, PAYLOAD_T)                                        \
   typedef struct NAME##_slot {                                                   \
      uint64_t  key;                                                             \
      int       prev;   /* LRU links by arena index (-1 = none) */               \
      int       next;                                                            \
      size_t    bytes;  /* cached payload footprint */                           \
      PAYLOAD_T payload;                                                         \
   } NAME##_slot;                                                                 \
   \
   typedef struct NAME {                                                          \
      NAME##_slot *slots;                                                        \
      int          slots_len;                                                    \
      int          slots_cap;                                                    \
      int         *table;        /* open-addressed slot indices; -1 = empty */   \
      size_t       table_len;    /* power of two, 0 if unallocated */            \
      size_t       mask;         /* table_len - 1 */                             \
      int          head, tail;   /* MRU / LRU ends */                            \
      int          free_head;    /* recycled-slot free list head */              \
      int          live;                                                         \
      size_t       total_bytes;                                                  \
      size_t       budget_bytes;                                                 \
   } NAME;                                                                        \
   \
   static void   NAME##_init(NAME *c, size_t budget_bytes);                       \
   static int    NAME##_contains(const NAME *c, uint64_t key);                    \
   static PAYLOAD_T *NAME##_get(NAME *c, uint64_t key);                           \
   static PAYLOAD_T *NAME##_put_slot(NAME *c, uint64_t key, int *created);        \
   static void   NAME##_account(NAME *c, size_t old_bytes, size_t new_bytes);     \
   static void   NAME##_erase(NAME *c, uint64_t key);                             \
   static void   NAME##_clear(NAME *c);                                           \
   static void   NAME##_set_entry_bytes(NAME *c, uint64_t key, size_t bytes);     \
   static void   NAME##_set_budget(NAME *c, size_t bytes);                        \
   static size_t NAME##_size_bytes(const NAME *c);                                \
   static size_t NAME##_count(const NAME *c);                                     \
   static size_t NAME##_budget(const NAME *c)

#define HD_LRU_DEFINE(NAME, PAYLOAD_T, DISPOSE)                                \
   static int NAME##_find_slot(const NAME *c, uint64_t key)                       \
   {                                                                              \
      size_t i;                                                                  \
      if (c->table_len == 0)                                                     \
      return -1;                                                             \
      i = (size_t)hd_lru_mix(key) & c->mask;                                     \
      for (;;) {                                                                 \
         int s = c->table[i];                                                   \
         if (s < 0)                                                             \
         return -1;                                                         \
         if (c->slots[s].key == key)                                            \
         return s;                                                          \
         i = (i + 1) & c->mask;                                                 \
      }                                                                          \
   }                                                                              \
   static void NAME##_raw_insert(NAME *c, uint64_t key, int s)                    \
   {                                                                              \
      size_t i = (size_t)hd_lru_mix(key) & c->mask;                             \
      while (c->table[i] >= 0)                                                   \
      i = (i + 1) & c->mask;                                                 \
      c->table[i] = s;                                                           \
   }                                                                              \
   static void NAME##_ensure_table(NAME *c, size_t want)                          \
   {                                                                              \
      size_t need = 8;                                                           \
      size_t i;                                                                  \
      int s;                                                                     \
      while (need < want * 2)                                                    \
      need <<= 1;                                                            \
      if (c->table_len != 0 && need <= c->table_len)                             \
      return;                                                                \
      {                                                                          \
         int *nt = (int *)realloc(c->table, need * sizeof(int));                \
         if (!nt)                                                               \
         return;                                                            \
         c->table = nt;                                                         \
      }                                                                          \
      c->table_len = need;                                                       \
      c->mask = need - 1;                                                        \
      for (i = 0; i < need; i++)                                                 \
      c->table[i] = -1;                                                      \
      for (s = c->head; s >= 0; s = c->slots[s].next)                           \
      NAME##_raw_insert(c, c->slots[s].key, s);                             \
   }                                                                              \
   static void NAME##_hash_remove(NAME *c, uint64_t key)                          \
   {                                                                              \
      size_t i = (size_t)hd_lru_mix(key) & c->mask;                             \
      size_t j;                                                                  \
      for (;;) {                                                                 \
         int s = c->table[i];                                                   \
         if (s >= 0 && c->slots[s].key == key)                                  \
         break;                                                             \
         i = (i + 1) & c->mask;                                                 \
      }                                                                          \
      j = i;                                                                     \
      c->table[i] = -1;                                                          \
      for (;;) {                                                                 \
         int sj;                                                                \
         size_t home;                                                          \
         int can_move;                                                          \
         j = (j + 1) & c->mask;                                                 \
         sj = c->table[j];                                                      \
         if (sj < 0)                                                            \
         break;                                                             \
         home = (size_t)hd_lru_mix(c->slots[sj].key) & c->mask;               \
         if (i <= j)                                                            \
         can_move = !(home > i && home <= j);                               \
         else                                                                   \
         can_move = !(home > i || home <= j);                               \
         if (can_move) {                                                        \
            c->table[i] = sj;                                                  \
            c->table[j] = -1;                                                  \
            i = j;                                                             \
         }                                                                      \
      }                                                                          \
   }                                                                              \
   static int NAME##_alloc_slot(NAME *c)                                          \
   {                                                                              \
      int s;                                                                     \
      if (c->free_head >= 0) {                                                   \
         s = c->free_head;                                                      \
         c->free_head = c->slots[s].next;                                       \
         return s;                                                              \
      }                                                                          \
      if (c->slots_len == c->slots_cap) {                                        \
         int ncap = c->slots_cap ? c->slots_cap * 2 : 16;                       \
         c->slots = (NAME##_slot *)realloc(c->slots,                            \
               (size_t)ncap * sizeof(NAME##_slot));                    \
         c->slots_cap = ncap;                                                   \
      }                                                                          \
      s = c->slots_len++;                                                        \
      c->slots[s].key = 0;                                                       \
      c->slots[s].prev = -1;                                                     \
      c->slots[s].next = -1;                                                     \
      c->slots[s].bytes = 0;                                                     \
      return s;                                                                  \
   }                                                                              \
   static void NAME##_free_slot(NAME *c, int s)                                   \
   {                                                                              \
      c->slots[s].next = c->free_head;                                          \
      c->free_head = s;                                                          \
   }                                                                              \
   static void NAME##_link_front(NAME *c, int s)                                  \
   {                                                                              \
      c->slots[s].prev = -1;                                                     \
      c->slots[s].next = c->head;                                                \
      if (c->head >= 0)                                                          \
      c->slots[c->head].prev = s;                                            \
      c->head = s;                                                               \
      if (c->tail < 0)                                                           \
      c->tail = s;                                                           \
   }                                                                              \
   static void NAME##_unlink(NAME *c, int s)                                      \
   {                                                                              \
      int p = c->slots[s].prev;                                                  \
      int n = c->slots[s].next;                                                  \
      if (p >= 0) c->slots[p].next = n; else c->head = n;                        \
      if (n >= 0) c->slots[n].prev = p; else c->tail = p;                        \
   }                                                                              \
   static void NAME##_touch(NAME *c, int s)                                       \
   {                                                                              \
      if (c->head == s)                                                          \
      return;                                                                \
      NAME##_unlink(c, s);                                                       \
      NAME##_link_front(c, s);                                                   \
   }                                                                              \
   static void NAME##_evict(NAME *c)                                              \
   {                                                                              \
      while (c->total_bytes > c->budget_bytes && c->tail >= 0) {                 \
         int s = c->tail;                                                       \
         uint64_t key = c->slots[s].key;                                        \
         c->total_bytes -= c->slots[s].bytes;                                   \
         DISPOSE(&c->slots[s].payload);                                         \
         NAME##_unlink(c, s);                                                   \
         NAME##_hash_remove(c, key);                                            \
         NAME##_free_slot(c, s);                                                \
         c->live--;                                                             \
      }                                                                          \
   }                                                                              \
   static void NAME##_init(NAME *c, size_t budget_bytes)                          \
   {                                                                              \
      c->slots = NULL; c->slots_len = 0; c->slots_cap = 0;                       \
      c->table = NULL; c->table_len = 0; c->mask = 0;                            \
      c->head = c->tail = c->free_head = -1;                                     \
      c->live = 0; c->total_bytes = 0; c->budget_bytes = budget_bytes;           \
   }                                                                              \
   static int NAME##_contains(const NAME *c, uint64_t key)                        \
   {                                                                              \
      return NAME##_find_slot(c, key) >= 0;                                      \
   }                                                                              \
   static PAYLOAD_T *NAME##_get(NAME *c, uint64_t key)                            \
   {                                                                              \
      int s = NAME##_find_slot(c, key);                                          \
      if (s < 0)                                                                 \
      return NULL;                                                           \
      NAME##_touch(c, s);                                                        \
      return &c->slots[s].payload;                                               \
   }                                                                              \
   static PAYLOAD_T *NAME##_put_slot(NAME *c, uint64_t key, int *created)         \
   {                                                                              \
      int s = NAME##_find_slot(c, key);                                          \
      if (s >= 0) {                                                              \
         *created = 0;                                                          \
         NAME##_touch(c, s);                                                    \
         return &c->slots[s].payload;                                           \
      }                                                                          \
      *created = 1;                                                              \
      NAME##_ensure_table(c, (size_t)c->live + 1);                               \
      s = NAME##_alloc_slot(c);                                                  \
      c->slots[s].key = key;                                                     \
      NAME##_link_front(c, s);                                                   \
      NAME##_raw_insert(c, key, s);                                              \
      c->live++;                                                                 \
      return &c->slots[s].payload;                                               \
   }                                                                              \
   static void NAME##_account(NAME *c, size_t old_bytes, size_t new_bytes)        \
   {                                                                              \
      c->total_bytes = c->total_bytes - old_bytes + new_bytes;                   \
      NAME##_evict(c);                                                           \
   }                                                                              \
   static void NAME##_erase(NAME *c, uint64_t key)                                \
   {                                                                              \
      int s = NAME##_find_slot(c, key);                                          \
      if (s < 0)                                                                 \
      return;                                                                \
      c->total_bytes -= c->slots[s].bytes;                                       \
      DISPOSE(&c->slots[s].payload);                                             \
      NAME##_unlink(c, s);                                                       \
      NAME##_hash_remove(c, key);                                                \
      NAME##_free_slot(c, s);                                                    \
      c->live--;                                                                 \
   }                                                                              \
   static void NAME##_clear(NAME *c)                                              \
   {                                                                              \
      int s;                                                                     \
      for (s = c->head; s >= 0; s = c->slots[s].next)                           \
      DISPOSE(&c->slots[s].payload);                                         \
      free(c->slots); free(c->table);                                            \
      c->slots = NULL; c->slots_len = 0; c->slots_cap = 0;                       \
      c->table = NULL; c->table_len = 0; c->mask = 0;                            \
      c->head = c->tail = c->free_head = -1;                                     \
      c->live = 0; c->total_bytes = 0;                                           \
   }                                                                              \
   static void NAME##_set_entry_bytes(NAME *c, uint64_t key, size_t bytes)        \
   {                                                                              \
      int s = NAME##_find_slot(c, key);                                          \
      if (s >= 0)                                                                \
      c->slots[s].bytes = bytes;                                             \
   }                                                                              \
   static void NAME##_set_budget(NAME *c, size_t bytes)                           \
   {                                                                              \
      c->budget_bytes = bytes;                                                   \
      NAME##_evict(c);                                                           \
   }                                                                              \
   static size_t NAME##_size_bytes(const NAME *c) { return c->total_bytes; }      \
   static size_t NAME##_count(const NAME *c)      { return (size_t)c->live; }     \
   static size_t NAME##_budget(const NAME *c)     { return c->budget_bytes; }

   /* --- RAM cache: decoded CPU levels, keyed by (hash, palette) -------------
    * * Lives independent of TextureUpload lifetime, so images survive the rapid
    * VRAM upload churn of animated sprites. Decode-once: a combo is
    * read+decoded from disk at most once until evicted. CPU-side only - the GPU
    * upload happens on attach via renderer_upload_texture, so it survives
    * device/swapchain resets. The disposer frees the decoded levels. */
   typedef struct CachedHdImage {
      LoadedLevels levels; /* decoded RGBA + mips (CPU side) */
      int    alpha_flags;
      size_t bytes;
   } CachedHdImage;

   static void cached_hd_image_dispose(CachedHdImage *p)
   {
      loaded_levels_reset(&p->levels);
   }

   HD_LRU_DECLARE(HdImageCache, CachedHdImage);
   HD_LRU_DEFINE(HdImageCache, CachedHdImage, cached_hd_image_dispose)

      /* Insert/replace a combo's decoded levels; takes ownership of *levels
       * (left empty on return). */
      static void hd_image_cache_put(HdImageCache *c, HdTextureId id,
            LoadedLevels *levels, int alpha_flags)
      {
         uint64_t key = hd_pack_key(id);
         int created = 0;
         CachedHdImage *e = HdImageCache_put_slot(c, key, &created);
         size_t old_bytes = created ? 0 : e->bytes;
         if (created)
            loaded_levels_init(&e->levels); /* fresh slot: payload is indeterminate */
         loaded_levels_move(&e->levels, levels);
         e->alpha_flags = alpha_flags;
         e->bytes = loaded_levels_byte_size(&e->levels);
         HdImageCache_set_entry_bytes(c, key, e->bytes);
         HdImageCache_account(c, old_bytes, e->bytes);
      }

   /* --- VRAM cache: uploaded Vulkan images, keyed by (hash, palette) --------
    * * Sits above the RAM cache. Re-attaching a combo to a recreated
    * TextureUpload becomes a ref-counted handle copy instead of a fresh
    * upload_texture. The image is held as a RAW TTGpuImage * (trivially relocatable,
    * so the malloc arena can realloc it) with the refcount managed by hand: a
    * reference is taken on insert and released by the disposer on
    * eviction/clear, freeing VRAM once no live draw still holds the image. */
   typedef struct CachedGpuImage {
      TTGpuImage *image; /* owns one reference while resident (NULL = empty) */
      int    alpha_flags;
      size_t bytes;         /* approximate VRAM footprint (== decoded levels size) */
   } CachedGpuImage;

   static void cached_gpu_image_dispose(CachedGpuImage *p)
   {
      if (p->image)
      {
         tt_img_release(p->image);
         p->image = NULL;
      }
   }

   HD_LRU_DECLARE(HdGpuCache, CachedGpuImage);
   HD_LRU_DEFINE(HdGpuCache, CachedGpuImage, cached_gpu_image_dispose)

      /* Take a counted reference to a cached image and return it as an
       * ImageHandle the caller can store/copy/destroy normally.
       * IntrusivePtr(T*) adopts without bumping, so add the reference
       * explicitly first. */
      static ImageHandle hd_gpu_image_handle(CachedGpuImage *g)
      {
         tt_img_addref(g->image);
         return ih_make(g->image);
      }

   /* Insert/replace a combo's GPU image. Adds a reference to `handle`'s image
    * (held until eviction); a prior image at this key is released first. */
   static void hd_gpu_cache_put(HdGpuCache *c, HdTextureId id,
         ImageHandle handle, int alpha_flags, size_t bytes)
   {
      uint64_t key = hd_pack_key(id);
      int created = 0;
      CachedGpuImage *e = HdGpuCache_put_slot(c, key, &created);
      size_t old_bytes = created ? 0 : e->bytes;
      if (!created && e->image)
         tt_img_release(e->image); /* drop the image we're replacing */
      e->image = ih_get(&handle);
      if (e->image)
         tt_img_addref(e->image);      /* cache holds its own reference */
      e->alpha_flags = alpha_flags;
      e->bytes = bytes;
      HdGpuCache_set_entry_bytes(c, key, bytes);
      HdGpuCache_account(c, old_bytes, bytes);
   }

   /* -------------------------------------------------------------------------
    * * HdKeySet - an ordered set of packed (hash, palette) uint64_t keys, MSVC
    * C89.
    *
    * A set of HdTextureId. Backed by a single sorted, malloc'd uint64_t array:
    * membership is O(log n) binary search, insert/erase keep it sorted (O(n)
    * shift, fine for these small sets). Because the key packs (hash << 32) |
    * palette, the array is ordered by hash then palette, so all combos of one
    * hash form a contiguous run - found with lower_bound over the half-open key
    * range [hash<<32, (hash+1)<<32), which is exactly what the old a range
    * query over [{hash,0}, {hash,0xFFFFFFFF}] gives.
    * ------------------------------------------------------------------------- */
   typedef struct HdKeySet {
      uint64_t *keys;
      int       count;
      int       cap;
   } HdKeySet;

   static void hd_key_set_init(HdKeySet *s)
   {
      s->keys = NULL;
      s->count = 0;
      s->cap = 0;
   }
   static void hd_key_set_free(HdKeySet *s)
   {
      free(s->keys);
      s->keys = NULL;
      s->count = 0;
      s->cap = 0;
   }
   static void hd_key_set_clear(HdKeySet *s)
   {
      s->count = 0; /* keep the allocation for reuse */
   }
   /* First index with keys[i] >= key (lower bound). */
   static int hd_key_set_lower_bound(const HdKeySet *s, uint64_t key)
   {
      int lo = 0, hi = s->count;
      while (lo < hi) {
         int mid = lo + ((hi - lo) >> 1);
         if (s->keys[mid] < key)
            lo = mid + 1;
         else
            hi = mid;
      }
      return lo;
   }
   static int hd_key_set_contains(const HdKeySet *s, uint64_t key)
   {
      int i = hd_key_set_lower_bound(s, key);
      return i < s->count && s->keys[i] == key;
   }
   /* Insert key; returns 1 if newly added, 0 if already present. */
   static int hd_key_set_insert(HdKeySet *s, uint64_t key)
   {
      int i = hd_key_set_lower_bound(s, key);
      if (i < s->count && s->keys[i] == key)
         return 0;
      if (s->count == s->cap) {
         int ncap = s->cap ? s->cap * 2 : 16;
         uint64_t *nk = (uint64_t *)realloc(s->keys, (size_t)ncap * sizeof(uint64_t));
         if (!nk)
            return 0;
         s->keys = nk;
         s->cap = ncap;
      }
      memmove(&s->keys[i + 1], &s->keys[i], (size_t)(s->count - i) * sizeof(uint64_t));
      s->keys[i] = key;
      s->count++;
      return 1;
   }
   static void hd_key_set_erase(HdKeySet *s, uint64_t key)
   {
      int i = hd_key_set_lower_bound(s, key);
      if (i >= s->count || s->keys[i] != key)
         return;
      memmove(&s->keys[i], &s->keys[i + 1], (size_t)(s->count - i - 1) * sizeof(uint64_t));
      s->count--;
   }

   /* Page-aligned experiment: memoized page CRC keyed by the page rect (VRAM
    * words). dirty = the page's VRAM was written since we last hashed it;
    * hashed_frame caps re-hashing to once per page per frame (busy VRAM regions
    * otherwise re-CRC a 256-row page on nearly every draw). */
   struct CachedPageHash {
      Rect rect;
      uint32_t hash;
      bool dirty;
      uint64_t hashed_frame;
   };
   typedef struct CachedPageHash CachedPageHash;

   /* Reduce Palette Range memo for the page path: the [min,max] CLUT indices a
    * page's index data references. Keyed by (rect, hash) - content-addressed, so it
    * self-invalidates when the page content (and thus its hash) changes; no dirty
    * coupling with CachedPageHash. pal_min < 0 means "no palette / no range". */
   struct CachedPageBounds {
      Rect rect;
      uint32_t hash;
      int pal_min;
      int pal_max;
   };
   typedef struct CachedPageBounds CachedPageBounds;

   /* Page-aligned experiment: a resolved page replacement ready to bind for one
    * draw. Built each frame in get_hd_texture_index from a loaded page image; the
    * page HdTextureHandle indexes the tracker's ephemeral page_bindings vector
    * (cleared at on_queues_reset, exactly like handle_cache). page_rect is the
    * page in VRAM 16-bit words {page_x, page_y, page_w, 256}; the shader maps it
    * to the full image via (vram_rect, texel_rect={0,0,img_w,img_h}).
    * (PAGE_ALIGN.md section 6.) */
   struct ReplacedPage {
      Rect page_rect;        /* VRAM words */
      ImageHandle texture;
      int alpha_flags;
   };
   typedef struct ReplacedPage ReplacedPage;

   /* TextureTracker: the HD-texture replacement engine -- tracks VRAM rects,
    * resolves (hash,palette) combos to HD textures, and drives the disk/CPU/GPU
    * cache tiers and the IO worker thread. A
    * plain C struct + texture_tracker_* free functions. The init/teardown
    * become texture_tracker_init / texture_tracker_fini; the embedded
    * RectTracker / FusedPages are init'd/deinit'd there (already converted).
    * Default member initializers move into texture_tracker_init. */
   struct TextureTracker {
      bool dump_enabled;
      bool hd_textures_enabled;
      bool eager_textures; /* true = prefetch all palettes of a hash on upload (master-consistent); false = lazy per-draw */

      IOThread iothread;

      ImageHandle default_hd_texture;

      RectMatch dump_ignore[DUMP_IGNORE_MAX];
      int       dump_ignore_count;

      HdKeySet known_files;
      /* Palette-hash cache: a plain growable array (append-only until cleared,
       * no eviction/order). */
      CachedPaletteHash *cached_palette_hashes;
      int cached_palette_hashes_count;
      int cached_palette_hashes_cap;
      RestorableRectVec restorable_rects;
      FusedPages fused_pages;
      uint64_t frame;

      RectTracker tracker;
      HandleLRUCache handle_cache;

      /* HD image caches, independent of upload lifetime. Tier 1 = GPU (VRAM,
       * ready-to-bind Vulkan images); tier 2 = CPU (RAM, decoded levels); tier
       * 3 = disk. Initialised via HdGpuCache_init / HdImageCache_init. */
      HdGpuCache hd_gpu_cache;
      HdImageCache hd_cache;
      HdKeySet requested; /* disk load in flight, or known to have no file (negative cache) */
      HdKeySet pending_attach; /* cached combos drawn/decoded this frame, awaiting GPU attach at on_queues_reset */

      /* Diagnostics (logged every 300 frames by endFrame). */
      uint64_t dbg_responses_received;
      uint64_t dbg_responses_received_last;
      uint64_t dbg_gpu_uploads;
      uint64_t dbg_gpu_uploads_last;
      uint64_t dbg_attaches;
      uint64_t dbg_attaches_last;

      DbgHotkey frame_dump_key;
      RFILE *frame_dump;
      bool frame_dump_need_comma;

      DbgHotkey hd_toggle_key;
      DbgHotkey reload_key;
      DbgHotkey fastpath_key;
      bool fastpath_enabled;

      /* ---- Page-aligned experiment + HD QoL (see PAGE_ALIGN.md). ---- */
      /* HD Dump Mode: rect -> -texture-dump/, pages -> -texture-dump-pages/.
       * Both can be set ("Both" mode). Default = upload-rect (master-consistent). */
      bool dump_mode_rect;
      bool dump_mode_pages;
      bool replacement_mode_pages;  /* HD Replacement Mode = Page-aligned */
      bool replacement_fallback;    /* on a miss in the active mode, also try the other pack */
      bool lazy_sync;               /* Lazy mode only: load+upload synchronously on the render thread */
      bool reduce_palette_range;    /* opt-in: hash only the CLUT entries a texture uses */

      /* CPU mirror of 16-bit VRAM (FB_WIDTH x FB_HEIGHT), kept current by every
       * VRAM mutation hook. Page hashing/dumping reads page bytes from here
       * because the draw path carries no CPU VRAM pointer (PAGE_ALIGN.md sec 3). */
      uint16_t *vram_mirror;

      /* Scratch CLUT buffer for palette reads that fall back to the VRAM mirror
       * (see texture_tracker_get_palette). One 8bpp CLUT = 256 entries. Filled
       * and consumed synchronously on the render thread, so one reused buffer is
       * safe. */
      uint16_t palette_scratch[256];

      /* Last dump/replacement folders ensure_directories() created (skip re-mkdir
       * unless the target path changes - game load, mode switch, or folder option). */
      char ensured_dump_dir[PATH_MAX_TT];
      char ensured_dump_pages_dir[PATH_MAX_TT];
      char ensured_replace_dir[PATH_MAX_TT];

      /* Page-aligned replacement: reuses the 3-tier cache / requested / IO pool,
       * keyed by {page_hash, palette, pages=true}. These sets are page-only. */
      HdKeySet known_files_pages;      /* files in the -pages folder */
      HdKeySet pending_attach_pages;   /* page combos decoded, awaiting GPU attach (stores BASE keys) */
      HdKeySet dumped_pages;           /* dedup of dumped (page_hash, palette_hash) */

      /* Per-frame resolved page bindings; a page HdTextureHandle indexes this.
       * Cleared at on_queues_reset (same contract as handle_cache). */
      ReplacedPage *page_bindings;
      int page_bindings_count;
      int page_bindings_cap;

      /* Memo of page CRCs by page rect; invalidated on overlapping mirror writes
       * so the per-draw page hash isn't recomputed for repeated draws of a page. */
      CachedPageHash *cached_page_hashes;
      int cached_page_hashes_count;
      int cached_page_hashes_cap;

      /* Reduce Palette Range (page path): per-page [min,max] CLUT window memo. */
      CachedPageBounds *cached_page_bounds;
      int cached_page_bounds_count;
      int cached_page_bounds_cap;

      uint64_t dbg_gpu_uploads_peak;  /* max uploads in a single on_queues_reset within the window */
      uint64_t dbg_page_binds;        /* page combos bound for a draw (cache hit) */
      uint64_t dbg_page_binds_last;
      uint64_t dbg_page_miss;         /* page draws with no resident replacement (native) */
      uint64_t dbg_page_miss_last;
      uint64_t dbg_page_hashes;       /* actual full-page CRC32s computed (memo misses) */
      uint64_t dbg_page_hashes_last;
   };

   static void texture_tracker_init(struct TextureTracker *self);
   static void texture_tracker_fini(struct TextureTracker *self);

   void texture_tracker_save_state(struct TextureTracker *self,
         TextureTrackerSaveState *out);
   void texture_tracker_load_state(struct TextureTracker *self,
         const TextureTrackerSaveState *state);

   void texture_tracker_upload(struct TextureTracker *self,
         Rect rect,
         uint16_t *vram);
   void texture_tracker_blit(struct TextureTracker *self,
         Rect dst,
         Rect src);
   void texture_tracker_clearRegion(struct TextureTracker *self,
         Rect rect, uint16_t fill16);
   void texture_tracker_notifyReadback(struct TextureTracker *self,
         Rect rect,
         uint16_t *vram);

   HdTextureHandle texture_tracker_get_hd_texture_index(struct TextureTracker *self,
         Rect rect,
         UsedMode *mode,
         unsigned int page_x,
         unsigned int page_y,
         bool *fastpath_capable,
         bool *cache_hit);
   HdTexture texture_tracker_get_hd_texture(struct TextureTracker *self,
         HdTextureHandle index);
   void texture_tracker_endFrame(struct TextureTracker *self);
   void texture_tracker_on_queues_reset(struct TextureTracker *self);

void texture_tracker_set_cache_budgets(struct TextureTracker *self,
      size_t ram_bytes,
      size_t vram_bytes)
   {
      HdImageCache_set_budget(&self->hd_cache, ram_bytes);
      HdGpuCache_set_budget(&self->hd_gpu_cache, vram_bytes);
      /* Fused-page composites are a separate VRAM tier; bound them by the same VRAM
       * budget so they can't grow without limit (worst-case total HD VRAM ~= 2x the
       * cap: GPU cache + fused pages). */
      fused_pages_set_budget(&self->fused_pages, vram_bytes);
   }

   static Palette texture_tracker_get_palette(struct TextureTracker *self,
         Rect palette_rect);
   static uint32_t texture_tracker_get_palette_hash(struct TextureTracker *self,
         Rect palette_rect);
   static void texture_tracker_want_combo(struct TextureTracker *self,
         HdTextureId id, bool high_priority, bool pages);
   static void texture_tracker_dump_texture(struct TextureTracker *self,
         TextureUpload *upload,
         UsedMode *mode,
         DumpedMode dump_mode);
   static void texture_tracker_load_hd_texture(struct TextureTracker *self,
         uint32_t hash);
   static void texture_tracker_request_hd_texture(struct TextureTracker *self,
         TextureUpload *upload,
         uint32_t palette_hash);
   void texture_tracker_reload_textures_from_disk(struct TextureTracker *self);
   static void texture_tracker_dump_image(struct TextureTracker *self,
         TextureUpload *upload,
         UsedMode *mode,
         uint32_t palette_hash_override);
   /* ---- Page-aligned experiment + HD QoL forward declarations (PAGE_ALIGN.md) ---- */
   static void texture_tracker_mirror_store(struct TextureTracker *self, Rect rect, const uint16_t *vram);
   static void texture_tracker_mirror_blit(struct TextureTracker *self, Rect dst, Rect src);
   static void texture_tracker_mirror_fill(struct TextureTracker *self, Rect rect, uint16_t value);
   static void texture_tracker_invalidate_page_hashes(struct TextureTracker *self, Rect written);
   static uint32_t texture_tracker_hash_page(struct TextureTracker *self, Rect page_rect);
   static uint32_t texture_tracker_hash_page_cached(struct TextureTracker *self, Rect page_rect);
   static void texture_tracker_dump_page(struct TextureTracker *self, Rect page_rect, uint32_t page_hash, UsedMode *mode, uint32_t palette_hash);
   void texture_tracker_ensure_directories(struct TextureTracker *self, bool dump, bool replace);
   static void texture_tracker_sync_load_combo(struct TextureTracker *self, TextureUpload *upload, uint32_t palette_hash);
   static void texture_tracker_sync_load_page(struct TextureTracker *self, HdTextureId id);
   static HdTextureHandle texture_tracker_match_page(struct TextureTracker *self, Rect page_rect, uint32_t page_hash, uint32_t palette_hash);
static void texture_tracker_clear_palette_cache(struct TextureTracker *self,
      Rect rect)
   {
      self->cached_palette_hashes_count = 0; /* keep allocation for reuse */
   }
   static TextureUpload *texture_tracker_find_upload(struct TextureTracker *self,
         uint32_t hash);

static INLINE int cfg_is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static INLINE const char *cfg_skip_ws(const char *p)
{
    while (*p && cfg_is_space((unsigned char)*p)) p++;
    return p;
}

/* Parse one "(\d+|\*)" field with surrounding optional whitespace. On success
 * advances *pp past the field and returns 0, writing the value (-1 for '*');
 * returns -1 if the field doesn't match. */
static int cfg_parse_field(const char **pp, int *out)
{
    const char *p = cfg_skip_ws(*pp);
    if (*p == '*') {
        *out = -1;
        p++;
    } else if (*p >= '0' && *p <= '9') {
        int v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        *out = v;
    } else {
        return -1;
    }
    *pp = cfg_skip_ws(p);
    return 0;
}

/* Match one config line against:
 * ^\s*ignore\s+(\d+|\*)\s*,\s*(\d+|\*)\s*,\s*(\d+|\*)\s*,\s*(\d+|\*)\s*(?:#.*)?$
 * On match, fills *m and returns true. Hand-rolled matcher. */
static bool cfg_match_ignore(const char *line, RectMatch *m)
{
    const char *p = cfg_skip_ws(line);
    int i;
    int *fields[4];
    if (strncmp(p, "ignore", 6) != 0)
        return false;
    p += 6;
    /* \s+ : at least one whitespace after the keyword */
    if (!cfg_is_space((unsigned char)*p))
        return false;
    p = cfg_skip_ws(p);

    fields[0] = &m->x; fields[1] = &m->y; fields[2] = &m->w; fields[3] = &m->h;
    for (i = 0; i < 4; i++) {
        if (cfg_parse_field(&p, fields[i]) != 0)
            return false;
        if (i < 3) {
            if (*p != ',')
                return false;
            p++;
        }
    }
    /* optional trailing comment, then end of line */
    if (*p == '#')
        return true;
    return *p == '\0';
}

static int parse_config_file(const char *path, RectMatch *out, int max) {
    char line[1024];
    int count = 0;
    RFILE *in = filestream_open(path, RETRO_VFS_FILE_ACCESS_READ,
                                RETRO_VFS_FILE_ACCESS_HINT_NONE);
    if (!in)
        return 0;
    while (count < max && filestream_gets(in, line, sizeof(line))) {
        RectMatch m;
        if (cfg_match_ignore(line, &m))
            out[count++] = m;
    }
    filestream_close(in);
    return count;
}

struct RGBAImage {
    uint8_t* data;
    int width;
    int height;
};

/* ------------------------------------------------------------------
 * Image IO through libretro-common. Decoding goes through the
 * image_texture front end (which dispatches to rpng / rjpeg / rbmp /
 * rtga / rwebp / rdds by file type); encoding through rpng_encode.
 * ------------------------------------------------------------------ */

/* Extensions the replacement loader probes for, in priority order. PNG
 * first (the historical pack format and what the dumper writes), then
 * the remaining supported decoders. */
static const char * const tt_replacement_exts[] = {
    "png", "dds", "webp", "jpg", "jpeg", "bmp", "tga"
};
#define TT_REPLACEMENT_EXT_COUNT \
    (sizeof(tt_replacement_exts) / sizeof(tt_replacement_exts[0]))

/* Case-insensitive extension test against the supported set. ext points
 * just past the dot. */
static bool tt_is_replacement_ext(const char *ext) {
    size_t i;
    for (i = 0; i < TT_REPLACEMENT_EXT_COUNT; i++) {
        const char *a = ext;
        const char *b = tt_replacement_exts[i];
        while (*a && *b) {
            int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
            if (ca != *b)
                break;
            a++; b++;
        }
        if (*a == '\0' && *b == '\0')
            return true;
    }
    return false;
}

static void rgba_image_free(RGBAImage *img) {
    if (img->data != NULL) {
        free(img->data);
        img->data = NULL;
    }
}

/* Decode an image from disk into *img (straight RGBA8, R at byte 0 - the
 * byte order the GPU upload paths consume). On failure img->data is NULL.
 * The caller owns img->data and must release it with rgba_image_free. */
static void load_image(const char *path, RGBAImage *img) {
    struct texture_image tex;
    img->data          = NULL;
    tex.pixels         = NULL;
    tex.width          = 0;
    tex.height         = 0;
    tex.supports_rgba  = true; /* decode to RGBA byte order, not ARGB words */
    tex.compressed     = NULL;
    if (!image_texture_load(&tex, path))
        return;
    /* DDS can arrive as a GPU-native BCn payload with pixels == NULL;
     * force the CPU decode since the tracker's mip/alpha pipeline (and
     * the GL backend) want plain RGBA8. */
    if (!tex.pixels && !image_texture_realize_rgba(&tex)) {
        image_texture_free(&tex);
        return;
    }
    img->data   = (uint8_t *)tex.pixels;
    img->width  = (int)tex.width;
    img->height = (int)tex.height;
    tex.pixels  = NULL;          /* ownership moved to img */
    image_texture_free(&tex);    /* frees any compressed payload */
}

static bool write_image(const char *path,
      int width,
      int height,
      const void *data){
    /* rpng_save_image_argb consumes ARGB8888 words; the dump decode
     * produces RGBA bytes, so repack into a temporary. */
    const uint8_t *src = (const uint8_t *)data;
    size_t count = (size_t)width * (size_t)height;
    uint32_t *argb = (uint32_t *)malloc(count ? count * sizeof(uint32_t) : 1);
    size_t i;
    bool ok;
    if (!argb)
        return false;
    for (i = 0; i < count; i++) {
        const uint8_t *p = src + i * 4u;
        argb[i] = ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) |
                  ((uint32_t)p[1] << 8)  |  (uint32_t)p[2];
    }
    ok = rpng_save_image_argb(path, argb,
            (unsigned)width, (unsigned)height,
            (unsigned)width * (unsigned)sizeof(uint32_t));
    free(argb);
    return ok;
}


extern retro_input_state_t dbg_input_state_cb;

#include "libretro-common/include/retro_dirent.h"

/* Actually using the implementation in deps/zlib/crc32.c I think */
#include "scrc32.h"

extern char retro_cd_base_name[4096];
extern char retro_cd_base_directory[4096];
/* RetroArch System / Save directories (defined in libretro.c). Used by the HD
 * Texture Folder option to relocate the dump/replacement folders off the content
 * directory to a single central location. */
extern char retro_base_directory[4096];   /* System directory */
extern char retro_save_directory[4096];   /* Save directory */
#ifdef _WIN32
static char retro_slash = '\\';
#else
static char retro_slash = '/';
#endif

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

   /* HD Texture Folder mode: 0 = content dir (default), 1 = system, 2 = save. */
   static int texture_dir_mode = 0;

   /* Base directory for the texture dump/replacement folders, chosen by the HD
    * Texture Folder option. Falls back to the content directory if the selected
    * frontend directory is unset. Trailing separators are trimmed so the helpers
    * below append retro_slash uniformly. */
   static char *texture_root(char *out, size_t cap) {
      const char *dir = retro_cd_base_directory;       /* 0 = content (default) */
      size_t len;
      if (texture_dir_mode == 1 && retro_base_directory[0] != '\0')
         dir = retro_base_directory;                    /* 1 = system */
      else if (texture_dir_mode == 2 && retro_save_directory[0] != '\0')
         dir = retro_save_directory;                    /* 2 = save */
      strncpy(out, dir, cap - 1);
      out[cap - 1] = '\0';
      len = strlen(out);
      while (len > 0 && (out[len - 1] == '/' || out[len - 1] == '\\'))
         out[--len] = '\0';
      return out;
   }

   /* Create a single directory if it doesn't exist (the dump folders must
    * exist before rpng can write into them). Trailing slash trimmed first
    * since _mkdir dislikes it on Windows. */
   static void ensure_directory(const char *dir_in) {
      char dir[PATH_MAX_TT];
      size_t len;
      strncpy(dir, dir_in, sizeof(dir) - 1);
      dir[sizeof(dir) - 1] = '\0';
      len = strlen(dir);
      while (len > 0 && (dir[len - 1] == '/' || dir[len - 1] == '\\'))
         dir[--len] = '\0';
      if (len == 0)
         return;
#ifdef _WIN32
      _mkdir(dir);
#else
      mkdir(dir, 0755);
#endif
   }

   /* Path helpers write into a caller-provided buffer (PATH_MAX_TT bytes) and
    * return it C-style, no allocation. */

   static char *dump_path(char *out, size_t cap) {
      char root[PATH_MAX_TT];
      int n = snprintf(out, cap, "%s%c%s-texture-dump%c",
            texture_root(root, sizeof(root)), retro_slash, retro_cd_base_name, retro_slash);
      if (n < 0 || (size_t)n >= cap)
         out[cap - 1] = '\0';
      return out;
   }

   static char *replacements_path(char *out, size_t cap) {
      char root[PATH_MAX_TT];
      int n = snprintf(out, cap, "%s%c%s-texture-replacements%c",
            texture_root(root, sizeof(root)), retro_slash, retro_cd_base_name, retro_slash);
      if (n < 0 || (size_t)n >= cap)
         out[cap - 1] = '\0';
      return out;
   }

   static char *replacement_filename_from_hash(char *out,
         size_t cap,
         uint32_t hash,
         uint32_t palette_hash,
         const char *ext){
      char base[PATH_MAX_TT];
      int n;
      replacements_path(base, sizeof(base));
      n = snprintf(out, cap, "%s%x-%x.%s", base, (unsigned)hash, (unsigned)palette_hash, ext);
      if (n < 0 || (size_t)n >= cap)
         out[cap - 1] = '\0';
      return out;
   }

   /* Page-aligned experiment folders (kept separate so page packs and upload-rect
    * packs never collide - same filename != same texture; see PAGE_ALIGN.md). */
   static char *dump_pages_path(char *out, size_t cap) {
      char root[PATH_MAX_TT];
      int n = snprintf(out, cap, "%s%c%s-texture-dump-pages%c",
            texture_root(root, sizeof(root)), retro_slash, retro_cd_base_name, retro_slash);
      if (n < 0 || (size_t)n >= cap)
         out[cap - 1] = '\0';
      return out;
   }

   static char *replacements_pages_path(char *out, size_t cap) {
      char root[PATH_MAX_TT];
      int n = snprintf(out, cap, "%s%c%s-texture-replacements-pages%c",
            texture_root(root, sizeof(root)), retro_slash, retro_cd_base_name, retro_slash);
      if (n < 0 || (size_t)n >= cap)
         out[cap - 1] = '\0';
      return out;
   }

   /* Page-aligned experiment: filename of a page replacement (mirrors the
    * upload-rect helper, but in the -pages folder; page_hash takes the
    * texture-hash slot). */
   static char *replacement_pages_filename_from_hash(char *out,
         size_t cap,
         uint32_t page_hash,
         uint32_t palette_hash,
         const char *ext){
      char base[PATH_MAX_TT];
      int n;
      replacements_pages_path(base, sizeof(base));
      n = snprintf(out, cap, "%s%x-%x.%s", base, (unsigned)page_hash, (unsigned)palette_hash, ext);
      if (n < 0 || (size_t)n >= cap)
         out[cap - 1] = '\0';
      return out;
   }

   /* Resolve the on-disk replacement file for a combo, probing the
    * supported extensions in priority order. Writes the winning path
    * into out and returns true; on a miss, out holds the .png candidate
    * (for error messages) and the return is false. */
   static bool find_replacement_file(char *out,
         size_t cap,
         uint32_t hash,
         uint32_t palette_hash,
         bool pages){
      size_t i;
      for (i = 0; i < TT_REPLACEMENT_EXT_COUNT; i++) {
         if (pages)
            replacement_pages_filename_from_hash(out, cap, hash, palette_hash, tt_replacement_exts[i]);
         else
            replacement_filename_from_hash(out, cap, hash, palette_hash, tt_replacement_exts[i]);
         if (path_is_valid(out))
            return true;
      }
      if (pages)
         replacement_pages_filename_from_hash(out, cap, hash, palette_hash, "png");
      else
         replacement_filename_from_hash(out, cap, hash, palette_hash, "png");
      return false;
   }

static uint8_t *loaded_pixel(LoadedImage *image, int x, int y) {
      return &image->owned_data[(y * image->width + x) * 4];
   }

   static LoadedImage generate_mip(LoadedImage *higher) {
      /* Generate custom mipmaps in order to avoid transparent (0, 0, 0, 0) and
       * semi-transparent (r, g, b, a>=128) mixing to create some dark opaque
       * value (r, g, b, a<128). */

      LoadedImage result;
      int x, y;
      /* Assumes higher.width and higher.height are both divisible by 2 (and also therefore > 1) */
      loaded_image_init(&result);
      loaded_image_alloc(&result, higher->width / 2, higher->height / 2);
      for (y = 0; y < result.height; y++) {
         for (x = 0; x < result.width; x++) {
            uint8_t * dst;
            uint8_t *src00 = loaded_pixel(higher, x * 2 + 0, y * 2 + 0);
            uint8_t *src10 = loaded_pixel(higher, x * 2 + 1, y * 2 + 0);
            uint8_t *src01 = loaded_pixel(higher, x * 2 + 0, y * 2 + 1);
            uint8_t *src11 = loaded_pixel(higher, x * 2 + 1, y * 2 + 1);

            int numTransparent = 0;
            if (src00[0] == 0 && src00[1] == 0 && src00[2] == 0 && src00[3] == 0) numTransparent += 1;
            if (src10[0] == 0 && src10[1] == 0 && src10[2] == 0 && src10[3] == 0) numTransparent += 1;
            if (src01[0] == 0 && src01[1] == 0 && src01[2] == 0 && src01[3] == 0) numTransparent += 1;
            if (src11[0] == 0 && src11[1] == 0 && src11[2] == 0 && src11[3] == 0) numTransparent += 1;

            dst = loaded_pixel(&result, x, y);
            if (numTransparent > 2) {
               dst[0] = 0;
               dst[1] = 0;
               dst[2] = 0;
               dst[3] = 0;
            } else {
               int r = src00[0] + src10[0] + src01[0] + src11[0];
               int g = src00[1] + src10[1] + src01[1] + src11[1];
               int b = src00[2] + src10[2] + src01[2] + src11[2];
               int a = src00[3] + src10[3] + src01[3] + src11[3];

               int numNotTransparent = 4 - numTransparent;
               dst[0] = r / numNotTransparent;
               dst[1] = g / numNotTransparent;
               dst[2] = b / numNotTransparent;
               dst[3] = a / numNotTransparent;
            }
         }
      }
      return result;
   }

   static LoadedImage convert_tri_to_psx(uint8_t *image,
         int width,
         int height,
         int *alpha_flags){
      LoadedImage result;
      size_t i;
      loaded_image_init(&result);
      loaded_image_alloc(&result, width, height);
      (*alpha_flags) = 0;
      for (i = 0; i < result.owned_size; i += 4) {
         uint8_t *src = &image[i];
         uint8_t *dst = &result.owned_data[i];
         if (src[3] == 0) {
            /* Transparent */
            (*alpha_flags) |= ALPHA_FLAG_TRANSPARENT;
            dst[0] = 0;
            dst[1] = 0;
            dst[2] = 0;
            dst[3] = 0;
         } else if (src[3] == 255) {
            (*alpha_flags) |= ALPHA_FLAG_OPAQUE;
            if (src[0] == 0 && src[1] == 0 && src[2] == 0) {
               /* Opaque black */
               dst[0] = 1;
               dst[1] = 1;
               dst[2] = 1;
               dst[3] = 0;
            } else {
               /* Opaque */
               dst[0] = src[0];
               dst[1] = src[1];
               dst[2] = src[2];
               dst[3] = 0;
            }
         } else {
            (*alpha_flags) |= ALPHA_FLAG_SEMI_TRANSPARENT;
            if (src[0] == 0 && src[1] == 0 && src[2] == 0) {
               /* (0, 0, 0, 255) is a special reserved value */
               dst[0] = 1;
               dst[1] = 1;
               dst[2] = 1;
               dst[3] = 255;
            } else {
               /* Semi-transparent */
               dst[0] = src[0];
               dst[1] = src[1];
               dst[2] = src[2];
               dst[3] = 255;
            }
         }
      }
      return result;
   }

   static LoadedLevels prepare_texture(RGBAImage *image, int *alpha_flags) {
      LoadedLevels levels;
      int width = image->width;
      int height = image->height;
      LoadedImage base;
      loaded_levels_init(&levels);
      base = convert_tri_to_psx(image->data, width, height, alpha_flags);
      loaded_levels_push_move(&levels, &base);
      while (width % 2 == 0 && height % 2 == 0) {
         LoadedImage mip = generate_mip(&levels.levels[levels.count - 1]);
         loaded_levels_push_move(&levels, &mip);

         width /= 2;
         height /= 2;
      }
      return levels;
   }

   /* Worker-side dump decode: expand a snapshot of raw VRAM source words (req->src)
    * into RGBA with the tri-alpha convention, on the IO thread instead of the
    * render thread. Bit-identical to the old inline loops in dump_image/dump_page
    * (same math + order), so existing dumps still round-trip. Empty palette +
    * non-ABGR1555 = grayscale "missing". Returns a malloc'd buffer the caller frees;
    * *out_len receives its size. */
   static uint8_t *decode_dump_rgba(const IORequest *req, size_t *out_len) {
      size_t cap = (size_t)req->width * req->height * 4u;
      uint8_t *bytes = (uint8_t *)malloc(cap ? cap : 1);
      size_t bi = 0;
      bool is_abgr1555 = (req->dump_mode == (int)TextureMode_ABGR1555);
      bool has_pal = (req->palette != NULL && req->palette_len > 0);
      int ppp = req->ppp;
      int bpp = 16 / ppp;
      int mask = (1 << bpp) - 1;
      size_t wi;
      for (wi = 0; wi < req->src_len; wi++) {
         uint16_t word = req->src[wi];
         int p;
         for (p = 0; p < ppp; p++) {
            uint16_t subpixel = (word >> (p * bpp)) & mask;
            if (!is_abgr1555 && !has_pal) {
               /* Missing palette: grayscale of the raw index. */
               bytes[bi++] = (uint8_t)(255.0 * subpixel / mask);
               bytes[bi++] = (uint8_t)(255.0 * subpixel / mask);
               bytes[bi++] = (uint8_t)(255.0 * subpixel / mask);
               bytes[bi++] = (uint8_t)255;
            } else {
               uint16_t abgr1555 = is_abgr1555 ? subpixel : req->palette[subpixel];
               int r = ((abgr1555 >> 0) & 0x1f) * 255.0 / 31.0;
               int g = ((abgr1555 >> 5) & 0x1f) * 255.0 / 31.0;
               int b = ((abgr1555 >> 10) & 0x1f) * 255.0 / 31.0;
               int a = (abgr1555 >> 15) * 255.0;
               /* Convert psx alpha to the tri convention. */
               if (a == 0) {
                  if (r == 0 && g == 0 && b == 0) {
                     /* Transparent: already (0,0,0,0). */
                  } else {
                     a = 255; /* Opaque */
                  }
               } else {
                  a = 127; /* Semi-transparent */
               }
               bytes[bi++] = (uint8_t)r;
               bytes[bi++] = (uint8_t)g;
               bytes[bi++] = (uint8_t)b;
               bytes[bi++] = (uint8_t)a;
            }
         }
      }
      if (out_len)
         *out_len = bi;
      return bytes;
   }

   static void io_thread(void *user_data) {
      /* Pool worker. Each worker is handed the channel pointer with a reference
       * already taken on its behalf (at spawn); it releases that reference on
       * the way out. Whichever holder releases last frees the channel. */
      IOChannel *channel = (IOChannel *)user_data;
      TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread starting\n");

      while (true) {
         IORequest *request = NULL;
         {
            slock_lock(channel->lock);
            while (!io_channel_has_requests(channel) && !channel->done) {
               scond_wait(channel->cond, channel->lock);
            }
            if (channel->done) {
               /* Prompt shutdown; drop any unprocessed requests (matches the
                * previous single-thread behaviour). The channel teardown
                * frees whatever is still queued. */
               slock_unlock(channel->lock);
               break;
            }
            /* Take ONE request (high priority first) so work spreads across the
             * pool. Wake another worker if anything remains. */
            request = io_channel_pop_request(channel);
            if (io_channel_has_requests(channel)) {
               scond_signal(channel->cond);
            }
            slock_unlock(channel->lock);
         }

         /* The expensive part (PNG decode + mipmaps, or decode + PNG write) runs
          * WITHOUT the lock so workers process in parallel; only the queue access
          * and the response push are serialised. */
         if (request->kind == IORequestKind_Load) {
            uint32_t hash = request->hash;
            uint32_t palette_hash = request->palette_hash;

            char path[PATH_MAX_TT];
            RGBAImage image;
            find_replacement_file(path, sizeof(path), hash, palette_hash, request->pages);
            image.data = NULL;
            load_image(path, &image);
            if (image.data != NULL) {
               int alpha_flags_out = 0;
               LoadedLevels levels = prepare_texture(&image, &alpha_flags_out);
               IOResponse *response = (IOResponse *)malloc(sizeof(IOResponse));
               response->next         = NULL;
               response->hash         = hash;
               response->palette_hash = palette_hash;
               response->alpha_flags  = alpha_flags_out;
               response->pages        = request->pages;
               loaded_levels_init(&response->levels);
               loaded_levels_move(&response->levels, &levels);

               slock_lock(channel->lock);
               io_channel_push_response(channel, response);
               slock_unlock(channel->lock);

               rgba_image_free(&image);
            } else {
               TT_LOG(RETRO_LOG_ERROR, "failed to load: %s\n", path);
            }
         } else if (request->kind == IORequestKind_Dump) {
            /* Decode (palette->RGBA->tri-alpha) here on the worker, then encode+write,
             * so the burst stays off the render thread (it only snapshotted raw
             * src+CLUT). */
            size_t blen = 0;
            uint8_t *bytes = decode_dump_rgba(request, &blen);
            int success = write_image(request->path, request->width, request->height, bytes);
            if (success == 0) {
               TT_LOG(RETRO_LOG_ERROR, "failed to write to: %s\n", request->path);
            }
            free(bytes);
         }
         io_request_free(request);
      }
      io_channel_release(channel); /* drop this worker's reference */
      TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread ending\n");
   }

   static void io_channel_destroy(IOChannel *c) {
      /* Free any nodes still queued at shutdown. */
      IORequest *r = c->req_high_head;
      IOResponse *p = c->resp_head;
      while (r) { IORequest *n = r->next; io_request_free(r); r = n; }
      r = c->req_head;
      while (r) { IORequest *n = r->next; io_request_free(r); r = n; }
      while (p) { IOResponse *n = p->next; io_response_free(p); p = n; }
      slock_free(c->lock);
      scond_free(c->cond);
      free(c);
   }

   /* Number of parallel PNG-decode workers. Keeps first-appearance prefetch
    * bursts short without starving the emulation/render threads. */
   enum { NUM_IO_THREADS = 4 };

   static void io_thread_init(IOThread *t) {
      io_channel_rc_lock_init();
      t->channel = io_channel_new(); /* this IOThread holds one reference */
      { int i; for (i = 0; i < NUM_IO_THREADS; i++) {
         sthread_t * thread;
         /* Take a reference on the worker's behalf BEFORE it starts, so the
          * channel can't be freed out from under it; the worker releases on
          * exit. */
         io_channel_acquire(t->channel);
         thread = sthread_create(io_thread, t->channel);
         if (thread) {
            sthread_detach(thread);
         } else {
            io_channel_release(t->channel); /* thread failed to start; undo its ref */
         }
      } }
   }
   static void io_thread_deinit(IOThread *t) {
      slock_lock(t->channel->lock);
      t->channel->done = true;
      slock_unlock(t->channel->lock);
      scond_broadcast(t->channel->cond); /* wake ALL workers so they can exit */
      io_channel_release(t->channel); /* drop this IOThread's reference; the last */
                  /* worker to exit frees the channel */
      t->channel = NULL;
   }

   static void texture_tracker_dump_image(struct TextureTracker *self, TextureUpload *upload, UsedMode *mode, uint32_t palette_hash_override) {
      int bpp;
      char path[PATH_MAX_TT];
      int ppp;
      uint32_t hash = upload->hash;

      uint8_t *bytes;
      size_t   bytes_len;
      size_t   bi;
      size_t   img_count;

      /* from glsl/vram.h */
      int shift;
      switch (mode->mode) {
         case TextureMode_ABGR1555:
            shift = 0;
            break;
         case TextureMode_Palette8bpp:
            shift = 1;
            break;
         case TextureMode_Palette4bpp:
            shift = 2;
            break;
         case TextureMode_None:
         default:
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Tried to dump unused texture %x.\n", hash);
            return; /* Early out */
      }

      { size_t plen;
      dump_path(path, sizeof(path));
      plen = strlen(path);
      snprintf(path + plen, sizeof(path) - plen, "%x", (unsigned)hash);

      { uint16_t *palette = NULL;
      if (mode->mode == TextureMode_Palette4bpp || mode->mode == TextureMode_Palette8bpp) {
         Rect palette_rect = make_rect(mode->palette_offset_x, mode->palette_offset_y, mode->mode == TextureMode_Palette8bpp ? 256 : 16, 1);
         Palette p = texture_tracker_get_palette(self, palette_rect);
         if (p.data != NULL) {
            palette = p.data;
            plen = strlen(path);
            /* Filename hash from the caller (reduced range when enabled, else the
             * full-palette hash). p.data is still used below for the PNG decode. */
            snprintf(path + plen, sizeof(path) - plen, "-%x", (unsigned)palette_hash_override);
         }
      }

      if (palette != NULL) {
         TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping palette %i, %i.\n", mode->palette_offset_x, mode->palette_offset_y);
      } else if (mode->mode != TextureMode_ABGR1555) {
         plen = strlen(path);
         snprintf(path + plen, sizeof(path) - plen, "-missing");
         TT_LOG_VERBOSE(RETRO_LOG_INFO, "MISSING palette %i, %i.\n", mode->palette_offset_x, mode->palette_offset_y);
      } else {
         /* ABGR1555: direct colour, no palette. The loader looks up palette hash 0,
          * so tag the file -0 to round-trip (was <hash>.png, which never reloaded). */
         plen = strlen(path);
         snprintf(path + plen, sizeof(path) - plen, "-0");
      }

      ppp = 1 << shift;
      (void)bpp; (void)bytes; (void)bytes_len; (void)bi; (void)img_count;

      plen = strlen(path);
      snprintf(path + plen, sizeof(path) - plen, ".png");

      /* Snapshot the raw indexed words + CLUT (cheap copies); the IO worker
       * (decode_dump_rgba) expands palette->RGBA->tri-alpha off the render thread. */
      TT_LOG_VERBOSE(RETRO_LOG_INFO, "requesting dump: %s (%ux%u)\n", path,
            (unsigned)(upload->width * ppp), (unsigned)upload->height);
      {
         IORequest *dump = (IORequest *)malloc(sizeof(IORequest));
         dump->next = NULL;
         dump->kind = IORequestKind_Dump;
         snprintf(dump->path, sizeof(dump->path), "%s", path);
         dump->width  = upload->width * ppp;
         dump->height = upload->height;
         dump->pages  = false;
         dump->dump_mode = (int)mode->mode;
         dump->ppp    = ppp;
         dump->src_len = (size_t)upload->image_count;
         dump->src = (uint16_t *)malloc((dump->src_len ? dump->src_len : 1) * sizeof(uint16_t));
         memcpy(dump->src, upload->image, dump->src_len * sizeof(uint16_t));
         if (palette != NULL) {
            dump->palette_len = (size_t)(mode->mode == TextureMode_Palette8bpp ? 256 : 16);
            dump->palette = (uint16_t *)malloc(dump->palette_len * sizeof(uint16_t));
            memcpy(dump->palette, palette, dump->palette_len * sizeof(uint16_t));
         } else {
            dump->palette = NULL;
            dump->palette_len = 0;
         }

         slock_lock(self->iothread.channel->lock);
         io_channel_push_request(self->iothread.channel, dump); /* texture dumps = background */
         slock_unlock(self->iothread.channel->lock);
         scond_signal(self->iothread.channel->cond);
      }
      }
      }
   }

   static void read_texture_directory(HdKeySet *out, const char *path, bool pages) {
      RDIR *dir;
      hd_key_set_clear(out);
      dir = retro_opendir(path);
      if (dir != NULL) {
         while (retro_readdir(dir)) {
            /* https://stackoverflow.com/questions/13701657/control-whole-string-with-sscanf */
            uint32_t hash;
            uint32_t palette_hash;
            int chars_read;
            HdTextureId id;
            const char *name = retro_dirent_get_name(dir);
            /* <hash>-<palette>.<ext> where ext is any supported decoder
             * (png/dds/webp/jpg/jpeg/bmp/tga, case-insensitive). */
            if (sscanf(name, "%x-%x.%n", &hash, &palette_hash, &chars_read) != 2 ||
                  chars_read <= 0 ||
                  name[chars_read - 1] != '.' ||
                  !tt_is_replacement_ext(name + chars_read)
               ) {
               continue;
            }

            /* pages=true sets id.pages so hd_pack_key salts these away from the
             * upload-rect keyspace (they share the cache, separate known_files). */
            id.hash = hash; id.palette_hash = palette_hash; id.pages = pages;
            hd_key_set_insert(out, hd_pack_key(id));
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "file found: %s\n", name);
         }
         retro_closedir(dir);
      }
   }

   static void texture_tracker_init(struct TextureTracker *self)
   {
      char rpath[PATH_MAX_TT];
      char cfg[PATH_MAX_TT];
      int mi;
      /* former default member initializers */
      self->dump_enabled = false;
      self->hd_textures_enabled = false;
      self->eager_textures = true;
      self->frame = 0;
      self->dbg_responses_received = 0;
      self->dbg_responses_received_last = 0;
      self->dbg_gpu_uploads = 0;
      self->dbg_gpu_uploads_last = 0;
      self->dbg_attaches = 0;
      self->dbg_attaches_last = 0;
      self->frame_dump = NULL;
      self->frame_dump_need_comma = false;
      self->fastpath_enabled = true;
      self->default_hd_texture.data = NULL; /* plain-struct handle: explicit null */
      rect_tracker_init(&self->tracker); /* embedded RectTracker: explicit init (was default initialiser) */
      fused_pages_init(&self->fused_pages); /* embedded FusedPages: explicit init (was default initialiser) */
      HdImageCache_init(&self->hd_cache, HD_CACHE_RAM_BUDGET);
      HdGpuCache_init(&self->hd_gpu_cache, HD_CACHE_VRAM_BUDGET);
      handle_lru_cache_init(&self->handle_cache, 4);
      dbg_hotkey_init(&self->frame_dump_key, RETROK_LEFTBRACKET);
      dbg_hotkey_init(&self->hd_toggle_key, RETROK_RIGHTBRACKET);
      dbg_hotkey_init(&self->reload_key, RETROK_QUOTE);
      dbg_hotkey_init(&self->fastpath_key, RETROK_SEMICOLON);
      hd_key_set_init(&self->known_files);
      hd_key_set_init(&self->requested);
      hd_key_set_init(&self->pending_attach);
      self->cached_palette_hashes = NULL;
      self->cached_palette_hashes_count = 0;
      self->cached_palette_hashes_cap = 0;
      /* ---- page-aligned experiment + HD QoL members ---- */
      hd_key_set_init(&self->known_files_pages);
      hd_key_set_init(&self->pending_attach_pages);
      hd_key_set_init(&self->dumped_pages);
      self->dump_mode_rect        = true;
      self->dump_mode_pages       = false;
      self->replacement_mode_pages= false;
      self->replacement_fallback  = false;
      self->lazy_sync             = false;
      self->reduce_palette_range  = false;
      self->vram_mirror           = (uint16_t*)calloc((size_t)FB_WIDTH * FB_HEIGHT, sizeof(uint16_t));
      self->ensured_dump_dir[0]       = '\0';
      self->ensured_dump_pages_dir[0] = '\0';
      self->ensured_replace_dir[0]    = '\0';
      self->page_bindings         = NULL;
      self->page_bindings_count   = 0;
      self->page_bindings_cap     = 0;
      self->cached_page_hashes    = NULL;
      self->cached_page_hashes_count = 0;
      self->cached_page_hashes_cap   = 0;
      self->cached_page_bounds    = NULL;
      self->cached_page_bounds_count = 0;
      self->cached_page_bounds_cap   = 0;
      self->dbg_gpu_uploads_peak  = 0;
      self->dbg_page_binds        = 0;
      self->dbg_page_binds_last   = 0;
      self->dbg_page_miss         = 0;
      self->dbg_page_miss_last    = 0;
      self->dbg_page_hashes       = 0;
      self->dbg_page_hashes_last  = 0;
      read_texture_directory(&self->known_files, replacements_path(rpath, sizeof(rpath)), false);
      TT_LOG(RETRO_LOG_INFO, "num hd textures: %d\n", (int)self->known_files.count);
      read_texture_directory(&self->known_files_pages, replacements_pages_path(rpath, sizeof(rpath)), true);
      TT_LOG(RETRO_LOG_INFO, "num hd page textures: %d\n", (int)self->known_files_pages.count);

      /* Read in the dump config file */
      dump_path(cfg, sizeof(cfg));
      snprintf(cfg + strlen(cfg), sizeof(cfg) - strlen(cfg), "/dump.cfg");
      self->dump_ignore_count = parse_config_file(cfg, self->dump_ignore, DUMP_IGNORE_MAX);
      for (mi = 0; mi < self->dump_ignore_count; mi++) {
         RectMatch m = self->dump_ignore[mi];
         TT_LOG_VERBOSE(RETRO_LOG_INFO, "Ignoring %d,%d,%d,%d\n", m.x, m.y, m.w, m.h);
      }
      /* Spin up the IO worker pool last, once the self->tracker is fully built. */
      io_thread_init(&self->iothread);
   }

   static void texture_tracker_fini(struct TextureTracker *self)
   {
      ih_reset(&self->default_hd_texture); /* drop the default HD texture reference */
      HdImageCache_clear(&self->hd_cache);   /* frees decoded levels + arena */
      HdGpuCache_clear(&self->hd_gpu_cache); /* releases cached image refs + arena */
      hd_key_set_free(&self->known_files);
      hd_key_set_free(&self->requested);
      hd_key_set_free(&self->pending_attach);
      hd_key_set_free(&self->known_files_pages);
      hd_key_set_free(&self->pending_attach_pages);
      hd_key_set_free(&self->dumped_pages);
      free(self->vram_mirror);
      { int i; for (i = 0; i < self->page_bindings_count; i++) ih_reset(&self->page_bindings[i].texture); }
      free(self->page_bindings);
      free(self->cached_page_hashes);
      free(self->cached_page_bounds);
      free(self->cached_palette_hashes);
      fused_pages_deinit(&self->fused_pages); /* embedded FusedPages: explicit deinit (was default teardown) */
      rect_tracker_deinit(&self->tracker); /* embedded RectTracker: explicit deinit (was default teardown) */
      /* Signal and drop the IO worker pool last. Previously self->iothread was
       * a by-value member, so its teardown ran after this body; preserve that
       * ordering by deinitialising it here at the end. */
      io_thread_deinit(&self->iothread);
   }

static SRect toSRect(Rect rect) {
      return make_srect(rect.x, rect.y, rect.width, rect.height);
   }
static Rect fromSRect(SRect rect) {
      return make_rect(rect.x, rect.y, rect.width, rect.height);
   }

   /* fromSRect(s).contains(r) in one shot, so the by-value temporary the old
    * chained call relied on has a stable address for rect_contains. */
   static INLINE bool fromSRect_contains(SRect s, Rect r) {
      struct Rect fr = fromSRect(s);
      return rect_contains(&fr, &r);
   }

   static Palette texture_tracker_get_palette(struct TextureTracker *self, Rect palette_rect) {
      assert(palette_rect.height == 1);

      { static RectIndexSet overlap = { NULL, 0, 0 };
      rect_tracker_overlapping(&self->tracker, palette_rect, &overlap);
      { int oi; for (oi = 0; oi < overlap.count; oi++) {
         RectIndex index = overlap.items[oi];
         EnduringTextureRect *other = &self->tracker.textures.a[index]; /* TODO: The `other.alive` check is unnecessary because self->tracker.overlapping never returns dead indices */
         if (fromSRect_contains(other->texture_rect.vram_rect, palette_rect) && other->alive) {
            int y;
            int x;
            if (other->texture_rect.offset_x != 0 || other->texture_rect.offset_y != 0) {
               continue; /* TODO: handle offset subrects */
            }
            x = palette_rect.x - other->texture_rect.vram_rect.x;
            y = palette_rect.y - other->texture_rect.vram_rect.y;
            { int offset = y * other->texture_rect.vram_rect.width + x;
            uint16_t *data = other->texture_rect.upload->image + offset;
            uint32_t hash = crc32(0, (unsigned char*)data, palette_rect.width * sizeof(uint16_t));
            { Palette _p; _p.data = data; _p.hash = hash; return _p; }
            }
         }
      } }

      /* Fallback: the CLUT wasn't inside a tracked, zero-offset upload rect, so the
       * rect-tracker lookup above found nothing - this produced the bulk of the
       * "-missing" palette dumps and palette_hash==0 keys. Read the CLUT straight
       * from the CPU VRAM mirror instead; it is kept current by every VRAM mutation
       * hook, exactly as the hardware reads its CLUT from live VRAM. Columns are
       * masked so a CLUT near the right VRAM edge wraps around correctly. */
      if (self->vram_mirror != NULL) {
         unsigned n  = (unsigned)palette_rect.width;   /* 16 (4bpp) or 256 (8bpp) */
         unsigned py = (unsigned)palette_rect.y & (FB_HEIGHT - 1);
         unsigned i;
         uint32_t hash;
         if (n > 256u)
            n = 256u;
         for (i = 0; i < n; i++) {
            unsigned px = ((unsigned)palette_rect.x + i) & (FB_WIDTH - 1);
            self->palette_scratch[i] = self->vram_mirror[py * FB_WIDTH + px];
         }
         hash = crc32(0, (unsigned char*)self->palette_scratch, n * sizeof(uint16_t));
         { Palette _p; _p.data = self->palette_scratch; _p.hash = hash; return _p; }
      }

      { Palette _p; _p.data = NULL; _p.hash = 0; return _p; }
      }
   }

   static uint32_t texture_tracker_get_palette_hash(struct TextureTracker *self, Rect palette_rect) {
      Palette palette;
      int i;
      for (i = 0; i < self->cached_palette_hashes_count; i++) {
         if (rect_eq(&self->cached_palette_hashes[i].rect, &palette_rect)) {
            return self->cached_palette_hashes[i].hash;
         }
      }
      palette = texture_tracker_get_palette(self, palette_rect);
      if (palette.data != NULL) {
         if (self->cached_palette_hashes_count == self->cached_palette_hashes_cap) {
            int ncap = self->cached_palette_hashes_cap ? self->cached_palette_hashes_cap * 2 : 16;
            CachedPaletteHash *nh = (CachedPaletteHash *)realloc(self->cached_palette_hashes,
                  (size_t)ncap * sizeof(CachedPaletteHash));
            if (!nh)
               return palette.hash;
            self->cached_palette_hashes = nh;
            self->cached_palette_hashes_cap = ncap;
         }
         self->cached_palette_hashes[self->cached_palette_hashes_count].rect = palette_rect;
         self->cached_palette_hashes[self->cached_palette_hashes_count].hash = palette.hash;
         self->cached_palette_hashes_count++;
         return palette.hash;
      }
      return 0; /* TODO: better way to indicate no palette found? */
   }

   void texture_tracker_clearRegion(struct TextureTracker *self,
         Rect rect, uint16_t fill16){
      if (rect.width == 0 || rect.height == 0) {
         /* Some games do this, apparently. */
         return;
      }
      rect_tracker_clear(&self->tracker, make_srect(rect.x, rect.y, rect.width, rect.height));
      fused_pages_mark_dead(&self->fused_pages, rect);
      texture_tracker_mirror_fill(self, rect, fill16); /* keep the page mirror current */

      texture_tracker_clear_palette_cache(self, rect);
   }


   void texture_tracker_blit(struct TextureTracker *self,
         Rect dst,
         Rect src){
      rect_tracker_blit(&self->tracker, make_srect(dst.x, dst.y, dst.width, dst.height), make_srect(src.x, src.y, src.width, src.height));
      texture_tracker_mirror_blit(self, dst, src); /* keep the page mirror current */
      fused_pages_mark_dirty(&self->fused_pages, dst);
      fused_pages_rebuild_dirty(&self->fused_pages, &self->tracker);
      texture_tracker_clear_palette_cache(self, dst);
   }

   static uint32_t texture_tracker_dbgHashVram(struct TextureTracker *self,
         Rect rect,
         uint16_t *vram){
      unsigned x = rect.x,
          y = rect.y,
          w = rect.width,
          h = rect.height;
      size_t n = (size_t)w * (size_t)h;
      uint16_t *buf = (uint16_t *)malloc(n * sizeof(uint16_t));
      size_t bi = 0;
      uint32_t hash;
      { int j; for (j = y; j < (int)(y + h); j++) {
         { int i; for (i = x; i < (int)(x + w); i++) {
            buf[bi++] = vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))];
         } }
      } }
      hash = crc32(0, (unsigned char*)buf, rect.width * rect.height * sizeof(uint16_t));
      free(buf);
      return hash;
   }

   /* Geometry helpers return a result plus a validity flag.
    * Named structs make the boolean's meaning explicit. */
   struct SRectResult { SRect rect; bool valid; };
   struct TextureRectResult { TextureRect rect; bool valid; };

   static SRectResult intersect(SRect a, SRect b) {
      int al     = srect_left(&a),   ar = srect_right(&a),  at = srect_top(&a), ab = srect_bottom(&a);
      int bl     = srect_left(&b),   br = srect_right(&b),  bt = srect_top(&b), bb = srect_bottom(&b);
      int left   = (al > bl) ? al : bl;
      int right  = (ar < br) ? ar : br;
      int top    = (at > bt) ? at : bt;
      int bottom = (ab < bb) ? ab : bb;
      int width  = right - left;
      int height = bottom - top;
      if (width <= 0 || height <= 0)
      { SRectResult r = { make_srect(0, 0, 1, 1), false }; return r; }
      {
         SRectResult r = { make_srect(left, top, width, height), true };
         return r;
      }
   }

   static TextureRect subTexture(TextureRect original, SRect sub_vram_rect) {
      return make_texture_rect(
            original.upload,
            original.offset_x + srect_left(&sub_vram_rect) - srect_left(&original.vram_rect),
            original.offset_y + srect_top(&sub_vram_rect) - srect_top(&original.vram_rect),
            sub_vram_rect
            );
   }

   static TextureRectResult clip_texture_rect_to_vram(TextureRect *t,
         Rect vram_rect){
      SRectResult intersection = intersect(t->vram_rect, toSRect(vram_rect));
      if (intersection.valid) {
         TextureRectResult r = { subTexture(*t, intersection.rect), true };
         return r;
      }
      { TextureRectResult r = { make_texture_rect(NULL, 0, 0, make_srect(0, 0, 1, 1)), false };
      return r;
      }
   }

   void texture_tracker_notifyReadback(struct TextureTracker *self,
         Rect rect,
         uint16_t *vram){
      RestorableRect rr;
      uint32_t hash;
      /* These hacks are my workaround for the dialog self->frame texture restorable getting evicted by FMVs */
      if (rect.width == 96 && rect.height == 224 && rect.y == 0 && (rect.x % 96) == 0) {
         /* HACK: Looks like final fmv self->frame readback for cross fade, ignore */
         return;
      }
      if (rect.width == 64 && rect.height == 224 && rect.y == 0 && (rect.x % 64) == 0) {
         /* HACK: Looks like final fmv self->frame readback for cross fade, ignore */
         return;
      }

      hash = texture_tracker_dbgHashVram(self, rect, vram);

      texture_tracker_mirror_store(self, rect, vram); /* keep the page mirror current */

      { int i; for (i = 0; i < rrvec_size(&self->restorable_rects); ) {
         if (rect_intersects(&self->restorable_rects.items[i].rect, &rect))
            rrvec_erase_at(&self->restorable_rects, i);
         else
            i++;
      } }

      { static RectIndexSet overlap = { NULL, 0, 0 };

      OwnedRectVec to_restore;
      ownedrects_init(&to_restore); /* must be valid before ownedrects_push below (reads v.items/count/cap); left indeterminate otherwise -> garbage push at -O2/-O3 */
      rect_tracker_overlapping(&self->tracker, rect, &overlap);
      { int oi; for (oi = 0; oi < overlap.count; oi++) {
         RectIndex index = overlap.items[oi];
         EnduringTextureRect *e = &self->tracker.textures.a[index];
         if (e->alive) { /* TODO: This check is unnecessary because self->tracker.overlapping never returns dead indices */
                   /* Clip to the self->requested rect */
            TextureRectResult result = clip_texture_rect_to_vram(&e->texture_rect, rect);
            if (result.valid) {
               /* assert(rect.contains(fromSRect(result.rect.vram_rect))); */
               ownedrects_push(&to_restore, result.rect);
            }
         }
      } }

      restorablerect_init(&rr);
      rr.rect = rect;
      rr.hash = hash;
      ownedrects_move(&rr.to_restore, &to_restore);
      rrvec_push(&self->restorable_rects, &rr);
      restorablerect_destroy(&rr);
      }
   }

   void texture_tracker_upload(struct TextureTracker *self,
         Rect rect,
         uint16_t *vram){
      TextureUpload * upload;
      RestorableRect * restore;
      texture_tracker_clear_palette_cache(self, rect);

      if (rect.width == FB_WIDTH && rect.height == FB_HEIGHT) {
         /* probably loading a save state, this is the entirety of vram */
         rect_tracker_clear(&self->tracker, toSRect(rect));
         fused_pages_mark_dead(&self->fused_pages, rect);
         texture_tracker_mirror_store(self, rect, vram); /* refresh the whole mirror */
         return;
      }

      /* Would this ever happen? */
      if (rect.width == 0 || rect.height == 0) {
         return;
      }

      /* Keep the CPU VRAM mirror current with the uploaded bytes (page-aligned
       * experiment), unconditionally regardless of dedup/restore below. */
      texture_tracker_mirror_store(self, rect, vram);

      upload = NULL;
      { bool preexisting = false;
      {
         uint32_t hash;
         unsigned x = rect.x,
             y = rect.y,
             w = rect.width,
             h = rect.height;
         size_t img_n = (size_t)w * (size_t)h;
         uint16_t *img = (uint16_t *)malloc(img_n * sizeof(uint16_t));
         size_t vi = 0;
         int j, i;
         for (j = y; j < (int)(y + h); j++) {
            for (i = x; i < (int)(x + w); i++) {
               img[vi++] = vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))];
            }
         }
         hash = crc32(0, (unsigned char*)img, rect.width * rect.height * sizeof(uint16_t));
         /* TODO: check for hash collision, by checking if existing upload has
          * different dimensions. not sure how to recover if it does, but the
          * odds of a collision are probably much higher than the odds that both
          * textures would be in play simultaneously, so it'd probably be safe
          * to simply ignore the newest upload and clear instead. */
         upload = texture_tracker_find_upload(self, hash);    /* borrowed */
         if (upload == NULL) {
            upload = texture_upload_new();  /* owns +1 */
            upload->image = img;            /* transfer ownership */
            upload->image_count = (int)img_n;
            img = NULL;
            upload->width = rect.width;
            upload->height = rect.height;
            upload->hash = hash;
            upload->dumpable = true;
            /* Don't dump uploads specified by dump.cfg */
            { int ri; for (ri = 0; ri < self->dump_ignore_count; ri++) {
               if (rect_match_matches(&self->dump_ignore[ri], rect)) {
                  upload->dumpable = false;
                  break;
               }
            } }
         } else {
            preexisting = true;
            texture_upload_acquire(upload); /* take our own ref on the borrowed result */
         }
         free(img); /* NULL if ownership was transferred */
      }

      restore = NULL;
      {
      int _oi;
      for (_oi = 0; _oi < rrvec_size(&self->restorable_rects); _oi++) {
         RestorableRect *other = &self->restorable_rects.items[_oi];
         if (other->hash == upload->hash && rect_eq(&other->rect, &rect)) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "RESTORATION: %x\n", other->hash);
            restore = other;
            break;
         }
      }
      }
      if (restore != NULL) {
         int _ri;
         for (_ri = 0; _ri < ownedrects_size(&restore->to_restore); _ri++) {
            TextureRect *t = &restore->to_restore.v.items[_ri];
            rect_tracker_place(&self->tracker, *t); /* TODO: clip to other.rect */
         }
      } else {
         rect_tracker_upload(&self->tracker, toSRect(rect), upload);
      }
      fused_pages_mark_dirty(&self->fused_pages, rect);
      fused_pages_rebuild_dirty(&self->fused_pages, &self->tracker);

      /* HD texture caching method: - Lazy (self->eager_textures=false): nothing
       * is queued here; each (hash,palette) is loaded on demand when first
       * drawn (request_hd_texture). Leanest. - Eager
       * (self->eager_textures=true, the master-consistent default): on the
       * first upload of a hash, prefetch ALL of its known palette variants into
       * the cache. Routed through want_combo so it still respects the cache
       * (decode-once / dedup) and the VRAM/RAM budgets, unlike stock Beetle's
       * raw load_hd_texture. */
      if (self->eager_textures && self->hd_textures_enabled && !preexisting && !self->replacement_mode_pages) {
         int lo = hd_key_set_lower_bound(&self->known_files, (uint64_t)upload->hash << 32);
         int hi = hd_key_set_lower_bound(&self->known_files, ((uint64_t)upload->hash + 1) << 32);
         int ki;
         for (ki = lo; ki < hi; ki++) {
            HdTextureId combo;
            combo.hash = upload->hash;
            combo.palette_hash = (uint32_t)self->known_files.keys[ki];
            combo.pages = false;
            texture_tracker_want_combo(self, combo, false, false); /* eager prefetch = low priority */
         }
      }
      texture_upload_release(upload); /* drop the local ref; rects hold their own */
      }
   }

   static void texture_tracker_load_hd_texture(struct TextureTracker *self, uint32_t hash) {
      int lo = hd_key_set_lower_bound(&self->known_files, (uint64_t)hash << 32);
      int hi = hd_key_set_lower_bound(&self->known_files, ((uint64_t)hash + 1) << 32);
      if (lo != hi) {
         int ki;
         slock_lock(self->iothread.channel->lock);
         for (ki = lo; ki < hi; ki++) {
            uint32_t palette_hash = (uint32_t)self->known_files.keys[ki];
            IORequest *load = (IORequest *)malloc(sizeof(IORequest));
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "requesting texture: %x-%x\n", hash, palette_hash);
            load->next = NULL;
            load->kind = IORequestKind_Load;
            load->hash = hash;
            load->palette_hash = palette_hash;
            load->pages = false;
            load->src = NULL; load->palette = NULL; /* Load: no dump payload to free */
            io_channel_push_request(self->iothread.channel, load); /* savestate warm = background */
         }
         slock_unlock(self->iothread.channel->lock);
         scond_signal(self->iothread.channel->cond);
      }
   }

   /* Queue a disk load for one (hash,palette) combo, unless it's already
    * decoded (in the cache), already in flight, or known to have no file.
    * Combos with no file are inserted into `requested` as a permanent negative
    * cache. The IO thread only pushes a response on success, so a
    * failed/missing load stays in `requested` and is never retried (until a
    * reload clears it). */
   static void texture_tracker_want_combo(struct TextureTracker *self, HdTextureId id, bool high_priority, bool pages) {
      /* pages=true sources the file from the -pages folder and checks
       * known_files_pages; both feed the SAME 3-tier cache (id.pages namespaces
       * the shared requested/hd_cache/hd_gpu_cache via hd_pack_key's salt). */
      if (HdGpuCache_contains(&self->hd_gpu_cache, hd_pack_key(id)) || HdImageCache_contains(&self->hd_cache, hd_pack_key(id)))
         return; /* already resident in VRAM, or already decoded in RAM */
      if (!hd_key_set_insert(&self->requested, hd_pack_key(id)))
         return; /* already in flight, or negatively cached */
      if (!hd_key_set_contains(pages ? &self->known_files_pages : &self->known_files, hd_pack_key(id)))
         return; /* no file on disk */

      slock_lock(self->iothread.channel->lock);
      {
         IORequest *load = (IORequest *)malloc(sizeof(IORequest));
         load->next = NULL;
         load->kind = IORequestKind_Load;
         load->hash = id.hash;
         load->palette_hash = id.palette_hash;
         load->pages = pages;
         load->src = NULL; load->palette = NULL; /* Load: no dump payload to free */
         /* High priority = needed for an on-screen draw (jumps ahead of prefetch);
          * low priority = speculative prefetch that fills idle IO time. */
         if (high_priority)
            io_channel_push_request_high(self->iothread.channel, load);
         else
            io_channel_push_request(self->iothread.channel, load);
      }
      slock_unlock(self->iothread.channel->lock);
      scond_signal(self->iothread.channel->cond);
   }

   /* Cache-backed HD texture binding for a drawn (hash,palette): pure lazy.
    *
    * If the combo is in the GPU cache, bind it immediately (handle copy, used
    * this frame). If it's decoded in the CPU cache, schedule a GPU upload for
    * the next safe point (on_queues_reset). Otherwise queue a single disk load
    * for it. The 3-tier cache makes every re-draw free, so each combo costs at
    * most one decode on its very first appearance.
    *
    * (Cross-hash prefetch was tried and removed: with the cache, re-draws are
    * already free, so warming the whole palette hash-set up front mostly
    * decoded combos that were never drawn - thrashing the RAM cache and
    * clogging the IO queue ahead of the combos actually on screen, which made
    * pop-in worse.) */
   static void texture_tracker_request_hd_texture(struct TextureTracker *self, TextureUpload *upload, uint32_t palette_hash) {
      if (hd_tex_map_contains(&upload->textures, palette_hash))
         return; /* already attached to this upload */

      { HdTextureId current = { upload->hash, palette_hash };

      /* GPU-cache hit: the Vulkan image already exists, so binding it is just a
       * ref-counted handle copy (no Vulkan commands). Bind it IMMEDIATELY so
       * the CURRENT self->frame's draw uses the HD texture. Deferring to
       * on_queues_reset cost a 1-self->frame native flicker every time an
       * animation self->frame's upload was recreated (constant for sprites) -
       * i.e. persistent pop-in even when the image was fully cached. */
      CachedGpuImage *gpu = HdGpuCache_get(&self->hd_gpu_cache, hd_pack_key(current));
      if (gpu != NULL) {
         hd_tex_map_set(&upload->textures, palette_hash, gpu->image, gpu->alpha_flags);
         self->dbg_attaches++;
         return;
      }

      /* CPU-cache hit (decoded but not in VRAM): needs a GPU upload, which we
       * keep at the safe point - schedule it for on_queues_reset. */
      /* Lazy (synchronous): get this combo ready on the render thread NOW so the
       * current frame uses it (no pop-in), at the cost of a brief stall. Handles
       * both the CPU-cache hit (just upload) and the full miss (disk+decode+upload). */
      if (self->lazy_sync) {
         texture_tracker_sync_load_combo(self, upload, palette_hash);
         return;
      }

      if (HdImageCache_contains(&self->hd_cache, hd_pack_key(current)))
         hd_key_set_insert(&self->pending_attach, hd_pack_key(current));
      else
         texture_tracker_want_combo(self, current, true, false); /* on-demand: high priority */
      }
   }

   static void output_rect_json(RFILE *stream, Rect *rect) {
      filestream_printf(stream,
            "{ \"x\": %u,\"y\": %u,\"width\": %u,\"height\": %u}\n",
            rect->x, rect->y, rect->width, rect->height);
   }

   static void texture_tracker_dump_texture(struct TextureTracker *self, TextureUpload *upload, UsedMode *mode, DumpedMode dump_mode) {
      int dmi;
      bool already_dumped;
      if (!upload->dumpable) {
         return;
      }

      already_dumped = false;
      for (dmi = 0; dmi < upload->dumped_modes_count; dmi++) {
         if (dumpedmode_eq(&upload->dumped_modes[dmi], &dump_mode)) {
            already_dumped = true;
            break;
         }
      }
      if (!already_dumped) {
         if (upload->dumped_modes_count == upload->dumped_modes_cap) {
            int ncap = upload->dumped_modes_cap ? upload->dumped_modes_cap * 2 : 4;
            DumpedMode *nm = (DumpedMode *)realloc(upload->dumped_modes,
                  (size_t)ncap * sizeof(DumpedMode));
            if (!nm)
               return;
            upload->dumped_modes = nm;
            upload->dumped_modes_cap = ncap;
         }
         upload->dumped_modes[upload->dumped_modes_count++] = dump_mode;
         if (self->dump_enabled) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping %x\n", upload->hash);
            /* dump_mode.palette_hash is the effective hash (reduced range when the
             * option is on, else full) chosen by the caller: dedup key + filename. */
            texture_tracker_dump_image(self, upload, mode, dump_mode.palette_hash);
         }
      }
   }

   static HandleCacheResult handle_lru_cache_get(struct HandleLRUCache *self,
         Rect rect,
         uint32_t palette_hash){
      HandleCacheResult res;
      int i, j;
      for (i = 0; i < self->count; i++) {
         CacheEntry *entry = &self->entries[i];
         if (entry->key_palette_hash == palette_hash && rect_contains(&entry->rect, &rect)) {
            CacheEntry hit = *entry;
            for (j = i; j > 0; j--) {
               self->entries[j] = self->entries[j - 1];
            }
            self->entries[0] = hit;
            self->dbg_hits += 1;
            res.handle = hit.handle;
            res.found = true;
            return res;
         }
      }
      self->dbg_misses += 1;
      res.handle = hd_handle_make_none();
      res.found = false;
      return res;
   }
   static void handle_lru_cache_insert(struct HandleLRUCache *self,
         Rect rect,
         uint32_t palette_hash,
         HdTextureHandle handle){
      int j;
      CacheEntry e;
      e.rect = rect;
      e.key_palette_hash = palette_hash;
      e.handle = handle;
      /* If full, the entry at index max_size-1 (the LRU) is dropped by the
       * shift below not preserving it. Otherwise grow by one. */
      if (self->count < self->max_size)
         self->count++;
      for (j = self->count - 1; j > 0; j--)
         self->entries[j] = self->entries[j - 1];
      self->entries[0] = e;
   }
   static INLINE void handle_lru_cache_clear(struct HandleLRUCache *self) { self->count = 0; }

   /* Reduce Palette Range (see the "HD Reduce Palette Range" core option). Scans a
    * texture's raw index words to find the lowest/highest CLUT index it references,
    * so the palette hash can ignore unused CLUT entries (games often leave those as
    * garbage or rewrite them over time). Mirrors Duckstation's ReducePaletteBounds.
    * Returns false for non-palettised modes or empty input (caller uses full hash).
    * min/max inclusive. */
   static bool reduce_palette_bounds(const uint16_t *words, size_t word_count, int mode,
         unsigned *out_min, unsigned *out_max) {
      unsigned pal_min, pal_max;
      size_t i;
      if (mode == (int)TextureMode_Palette4bpp) {
         pal_min = 15; pal_max = 0;
         for (i = 0; i < word_count; i++) {
            uint16_t v = words[i];
            unsigned p0 = v & 0xf, p1 = (v >> 4) & 0xf, p2 = (v >> 8) & 0xf, p3 = (v >> 12) & 0xf;
            if (p0 < pal_min) pal_min = p0;
            if (p0 > pal_max) pal_max = p0;
            if (p1 < pal_min) pal_min = p1;
            if (p1 > pal_max) pal_max = p1;
            if (p2 < pal_min) pal_min = p2;
            if (p2 > pal_max) pal_max = p2;
            if (p3 < pal_min) pal_min = p3;
            if (p3 > pal_max) pal_max = p3;
         }
      } else if (mode == (int)TextureMode_Palette8bpp) {
         pal_min = 255; pal_max = 0;
         for (i = 0; i < word_count; i++) {
            uint16_t v = words[i];
            unsigned p0 = v & 0xff, p1 = (v >> 8) & 0xff;
            if (p0 < pal_min) pal_min = p0;
            if (p0 > pal_max) pal_max = p0;
            if (p1 < pal_min) pal_min = p1;
            if (p1 > pal_max) pal_max = p1;
         }
      } else {
         return false; /* direct colour / no palette */
      }
      if (word_count == 0 || pal_min > pal_max)
         return false;
      *out_min = pal_min;
      *out_max = pal_max;
      return true;
   }

   /* CRC32 over palette[min..max] inclusive. When min==0 and max==width-1 this
    * equals the full-palette CRC, so a texture using its whole CLUT yields an
    * identical hash/filename to the non-reduced path (packs stay interchangeable). */
   static uint32_t hash_partial_palette(const uint16_t *palette, unsigned min, unsigned max) {
      return crc32(0, (const unsigned char *)(palette + min), (size_t)(max - min + 1) * sizeof(uint16_t));
   }

   /* Effective palette hash for an upload-rect texture: reduced range (option on and
    * a valid range) else full_hash. The [min,max] scan is memoised on the upload. */
   static uint32_t texture_tracker_effective_palette_hash_upload(struct TextureTracker *self,
         TextureUpload *upload, int mode, const uint16_t *palette, uint32_t full_hash) {
      if (!self->reduce_palette_range || palette == NULL)
         return full_hash;
      if (upload->reduce_pal_mode != mode) {
         unsigned lo, hi;
         if (reduce_palette_bounds(upload->image, (size_t)upload->image_count, mode, &lo, &hi)) {
            upload->reduce_pal_min = (int)lo;
            upload->reduce_pal_max = (int)hi;
         } else {
            upload->reduce_pal_min = -1;
         }
         upload->reduce_pal_mode = mode;
      }
      if (upload->reduce_pal_min < 0)
         return full_hash;
      return hash_partial_palette(palette, (unsigned)upload->reduce_pal_min, (unsigned)upload->reduce_pal_max);
   }

   /* Page-aligned counterpart. The [min,max] scan over the page's index words (read
    * from vram_mirror) is memoised in cached_page_bounds keyed by (rect, page_hash). */
   static uint32_t texture_tracker_effective_palette_hash_page(struct TextureTracker *self,
         Rect page_rect, uint32_t page_hash, const uint16_t *palette, uint32_t full_hash) {
      int mode;
      int i;
      CachedPageBounds *slot = NULL;
      if (!self->reduce_palette_range || palette == NULL)
         return full_hash;
      /* Mode implied by page width: 64 = 4bpp, 128 = 8bpp (256 = direct colour). */
      if (page_rect.width == 64)       mode = (int)TextureMode_Palette4bpp;
      else if (page_rect.width == 128) mode = (int)TextureMode_Palette8bpp;
      else                             return full_hash;

      for (i = 0; i < self->cached_page_bounds_count; i++) {
         if (rect_eq(&self->cached_page_bounds[i].rect, &page_rect)) {
            slot = &self->cached_page_bounds[i];
            break;
         }
      }
      if (slot == NULL) {
         if (self->cached_page_bounds_count == self->cached_page_bounds_cap) {
            int ncap = self->cached_page_bounds_cap ? self->cached_page_bounds_cap * 2 : 16;
            CachedPageBounds *nb = (CachedPageBounds*)realloc(self->cached_page_bounds,
                  (size_t)ncap * sizeof(CachedPageBounds));
            if (!nb)
               return full_hash;
            self->cached_page_bounds = nb;
            self->cached_page_bounds_cap = ncap;
         }
         slot = &self->cached_page_bounds[self->cached_page_bounds_count++];
         slot->rect = page_rect;
         slot->hash = page_hash ^ 1u; /* guarantee a mismatch -> first compute */
         slot->pal_min = -1;
         slot->pal_max = -1;
      }

      if (slot->hash != page_hash) {
         size_t n = (size_t)page_rect.width * page_rect.height;
         uint16_t *words = (uint16_t*)malloc((n ? n : 1) * sizeof(uint16_t));
         unsigned lo, hi;
         unsigned j, x;
         size_t k = 0;
         if (!words)
            return full_hash;
         for (j = 0; j < page_rect.height; j++) {
            unsigned vy = (page_rect.y + j) & (FB_HEIGHT - 1);
            for (x = 0; x < page_rect.width; x++) {
               unsigned vx = (page_rect.x + x) & (FB_WIDTH - 1);
               words[k++] = self->vram_mirror[vy * FB_WIDTH + vx];
            }
         }
         if (reduce_palette_bounds(words, n, mode, &lo, &hi)) {
            slot->pal_min = (int)lo;
            slot->pal_max = (int)hi;
         } else {
            slot->pal_min = -1;
            slot->pal_max = -1;
         }
         slot->hash = page_hash;
         free(words);
      }

      if (slot->pal_min < 0)
         return full_hash;
      return hash_partial_palette(palette, (unsigned)slot->pal_min, (unsigned)slot->pal_max);
   }

   HdTextureHandle texture_tracker_get_hd_texture_index(struct TextureTracker *self,
         Rect rect,
         UsedMode *mode,
         unsigned int page_x,
         unsigned int page_y,
         bool *fastpath_capable_out,
         bool *cache_hit){
      Rect palette_rect;
      Rect result_rect;
      HdTextureHandle result;
      (*fastpath_capable_out) = false;
      palette_rect = make_rect(mode->palette_offset_x, mode->palette_offset_y, mode->mode == TextureMode_Palette8bpp ? 256 : 16, 1);

      /* TODO: I'm pretty sure this doesn't handle TextureMode_ABGR1555 */

      { uint32_t palette_hash = 0;
      /* Reduce Palette Range needs the gathered CLUT data (not just its hash) to
       * compute per-upload/per-page reduced hashes below. Copy it into a local
       * because get_palette may return a pointer into the volatile palette_scratch. */
      uint16_t pal_local[256];
      bool have_pal_data = false;
      (*cache_hit) = false;
      if (self->hd_textures_enabled || self->dump_enabled) {
         if (mode->mode == TextureMode_Palette8bpp || mode->mode == TextureMode_Palette4bpp) {
            if (self->reduce_palette_range) {
               Palette p = texture_tracker_get_palette(self, palette_rect);
               if (p.data != NULL) {
                  palette_hash = p.hash;
                  memcpy(pal_local, p.data, (size_t)palette_rect.width * sizeof(uint16_t));
                  have_pal_data = true;
               }
            } else {
               palette_hash = texture_tracker_get_palette_hash(self, palette_rect);
            }
         }
      }
      if (self->hd_textures_enabled && !self->replacement_mode_pages) {
         /* Check if the same texture as last time is used. (The handle cache stores
          * non-fused upload-rect handles; page mode resolves its own way below.) */
         HandleCacheResult cache_result = handle_lru_cache_get(&self->handle_cache, rect, palette_hash);
         (*cache_hit) = cache_result.found;
         if ((*cache_hit)) {
            /* cache_result.handle is currently always a non-fused, non-none,
             * index + palette_hash in the future it may be useful to cache
             * none, but there's currently no way to check if such a containing
             * rect is still alive (since HdTextureHandle's index would be -1) */
            EnduringTextureRect *tex = &self->tracker.textures.a[cache_result.handle.index]; /* Forgive me */
            if (tex->alive) {
               /* Index by the handle's own palette hash (may be a reduced-range hash),
                * not the draw's full palette_hash. */
               uint32_t hh = cache_result.handle.palette_hash;
               (*fastpath_capable_out) = self->fastpath_enabled && ((hd_tex_map_find(&tex->texture_rect.upload->textures, hh) ? hd_tex_map_find(&tex->texture_rect.upload->textures, hh)->alpha_flags : 0) & ALPHA_FLAG_TRANSPARENT) == 0;
               return cache_result.handle;
            }
         }
      }

      { static RectIndexSet overlap = { NULL, 0, 0 };
      /* A page-match-only draw needs neither the overlap set nor the upload-rect
       * dump loop, so skip the spatial query in that common case. */
      bool need_overlap = self->dump_enabled || (self->hd_textures_enabled && !self->replacement_mode_pages);
      if (need_overlap)
         rect_tracker_overlapping(&self->tracker, rect, &overlap);

      /* Dump texture (upload-rect). The two folders are independent, so "Both"
       * mode collects both pack types in a single playthrough. */
      if (self->dump_enabled && self->dump_mode_rect) {
         int oi; for (oi = 0; oi < overlap.count; oi++) {
            RectIndex index = overlap.items[oi];
            TextureRect *tex = rect_tracker_get_index(&self->tracker, index);
            DumpedMode _dm;
            uint32_t dump_phash;
            /* Reduce Palette Range: dump under the reduced hash (per-upload; falls
             * back to the full hash when disabled or no valid range). */
            dump_phash = have_pal_data
               ? texture_tracker_effective_palette_hash_upload(self, tex->upload, (int)mode->mode, pal_local, palette_hash)
               : palette_hash;
            _dm.mode = mode->mode;
            _dm.palette_hash = dump_phash;
            texture_tracker_dump_texture(self, tex->upload, mode, _dm);
         }
      }
      /* Dump texture (page-aligned): dump the WHOLE VRAM page once per
       * (page_hash, palette_hash). Require a real dumpable tracked upload to overlap. */
      if (self->dump_enabled && self->dump_mode_pages) {
         bool dumpable = false;
         int oi;
         for (oi = 0; oi < overlap.count; oi++) {
            if (rect_tracker_get_index(&self->tracker, overlap.items[oi])->upload->dumpable) {
               dumpable = true;
               break;
            }
         }
         if (dumpable && mode->mode != TextureMode_None) {
            unsigned width
               = mode->mode == TextureMode_Palette4bpp ? 64
               : mode->mode == TextureMode_Palette8bpp ? 128
               : 256;
            Rect page_rect = { 0, 0, 0, 0 };
            uint32_t page_hash;
            uint32_t page_phash;
            HdTextureId page_id;
            page_rect.x = page_x; page_rect.y = page_y; page_rect.width = width; page_rect.height = 256;
            page_hash = texture_tracker_hash_page_cached(self, page_rect);
            /* Reduce Palette Range (page path): dump under the page's reduced hash. */
            page_phash = have_pal_data
               ? texture_tracker_effective_palette_hash_page(self, page_rect, page_hash, pal_local, palette_hash)
               : palette_hash;
            page_id.hash = page_hash; page_id.palette_hash = page_phash; page_id.pages = true;
            if (!hd_key_set_contains(&self->dumped_pages, hd_pack_key(page_id))) {
               hd_key_set_insert(&self->dumped_pages, hd_pack_key(page_id));
               texture_tracker_dump_page(self, page_rect, page_hash, mode, page_phash);
            }
         }
      }
      if (self->frame_dump != NULL) {
         if (self->frame_dump_need_comma) {
            filestream_printf(self->frame_dump, ",");
         } else {
            self->frame_dump_need_comma = true;
         }
         filestream_printf(self->frame_dump, " { \"rect\": ");
         output_rect_json(self->frame_dump, &rect);
         filestream_printf(self->frame_dump,
               ", \"mode\": { \"mode\": %d, \"palette_x\": %u, \"palette_y\": %u} }\n",
               (int)mode->mode, mode->palette_offset_x, mode->palette_offset_y);
      }

      if (!self->hd_textures_enabled) {
         (*fastpath_capable_out) = false;
         return hd_handle_make_none();
      }

      /* Page-aligned replacement: match the whole VRAM page from the -pages pack
       * instead of per-upload rects. Bypasses the overlap/fuse path. */
      if (self->replacement_mode_pages) {
         unsigned width;
         Rect page_rect = { 0, 0, 0, 0 };
         uint32_t page_hash;
         uint32_t page_phash;
         HdTextureHandle page;
         (*fastpath_capable_out) = false;
         if (mode->mode == TextureMode_None)
            return hd_handle_make_none();
         width = mode->mode == TextureMode_Palette4bpp ? 64
               : mode->mode == TextureMode_Palette8bpp ? 128 : 256;
         page_rect.x = page_x; page_rect.y = page_y; page_rect.width = width; page_rect.height = 256;
         page_hash = texture_tracker_hash_page_cached(self, page_rect);
         /* Reduce Palette Range (page path): prefer the reduced hash only when a
          * reduced page file exists, else keep the full hash (backward compatible). */
         page_phash = palette_hash;
         if (have_pal_data) {
            uint32_t rh = texture_tracker_effective_palette_hash_page(self, page_rect, page_hash, pal_local, palette_hash);
            if (rh != palette_hash) {
               HdTextureId rid;
               rid.hash = page_hash; rid.palette_hash = rh; rid.pages = true;
               if (hd_key_set_contains(&self->known_files_pages, hd_pack_key(rid)))
                  page_phash = rh;
            }
         }
         page = texture_tracker_match_page(self, page_rect, page_hash, page_phash);
         if (!hd_handle_is_none(&page) || !self->replacement_fallback)
            return page;
         /* Direction B fallback: page-aligned found no match. Page mode skipped the
          * overlap query (need_overlap was false), so run it now for the upload-rect
          * overlap/fuse loop below. */
         rect_tracker_overlapping(&self->tracker, rect, &overlap);
      }

      result = hd_handle_make_none();

      { int oi; for (oi = 0; oi < overlap.count; oi++) {
         RectIndex index = overlap.items[oi];
         TextureRect *tex = rect_tracker_get_index(&self->tracker, index);
         uint32_t eff = palette_hash;
         HdTexEntry *overlapped_image;
         /* Reduce Palette Range: prefer this upload's reduced hash, but only if a
          * reduced file exists on disk - else keep the full hash so existing
          * full-palette packs still match with the option enabled. Memoised scan. */
         if (have_pal_data) {
            uint32_t rh = texture_tracker_effective_palette_hash_upload(self, tex->upload, (int)mode->mode, pal_local, palette_hash);
            if (rh != palette_hash) {
               HdTextureId rid;
               rid.hash = tex->upload->hash; rid.palette_hash = rh; rid.pages = false;
               if (hd_key_set_contains(&self->known_files, hd_pack_key(rid)))
                  eff = rh;
            }
         }
         overlapped_image = hd_tex_map_find(&tex->upload->textures, eff);
         if (overlapped_image == NULL) {
            /* Not bound to this upload yet. Bind it now: a GPU-cache hit binds
             * in-frame (handle copy, used by THIS draw - no 1-self->frame
             * native flicker), otherwise this schedules a decode / GPU upload
             * that lands on a later self->frame. */
            texture_tracker_request_hd_texture(self, tex->upload, eff);
            overlapped_image = hd_tex_map_find(&tex->upload->textures, eff);
         }
         if (overlapped_image != NULL) {
            if (hd_handle_is_none(&result)) {
               /* note that if tex->vram_rect contains rect, then it will be the only entry in overlap, so an early out would be pointless */
               result_rect = fromSRect(tex->vram_rect);
               (*fastpath_capable_out) = self->fastpath_enabled && fromSRect_contains(tex->vram_rect, rect) && (overlapped_image->alpha_flags & ALPHA_FLAG_TRANSPARENT) == 0;
               result = hd_handle_make(index, eff);
            } else {
               /* Multiple overlap, must fuse */
               unsigned int width
                  = mode->mode == TextureMode_Palette4bpp ? 64
                  : mode->mode == TextureMode_Palette8bpp ? 128
                  : 256;
               Rect page_rect = { page_x, page_y, width, 256 };
               (*fastpath_capable_out) = false;
               return fused_pages_get_or_make(&self->fused_pages, page_rect, palette_hash, &self->tracker);
            }
         }
      } }

      /* Cross-mode fallback (Direction A): upload-rect found no HD match. Try the
       * page-aligned (-pages) pack for this VRAM page before native. Gate on "no
       * upload-rect FILE exists for this draw" so a texture whose replacement is just
       * mid-load doesn't trigger a wasted page probe. Page handles are per-frame, so
       * they MUST bypass handle_cache - return directly. */
      if (self->replacement_fallback && !self->replacement_mode_pages && hd_handle_is_none(&result)
            && mode->mode != TextureMode_None) {
         bool upload_rect_has_file = false;
         int oi;
         for (oi = 0; oi < overlap.count; oi++) {
            TextureRect *tex = rect_tracker_get_index(&self->tracker, overlap.items[oi]);
            HdTextureId fid;
            fid.hash = tex->upload->hash; fid.palette_hash = palette_hash; fid.pages = false;
            if (hd_key_set_contains(&self->known_files, hd_pack_key(fid))) {
               upload_rect_has_file = true;
               break;
            }
            /* A reduced-range file counts too (don't trigger a wasted page probe). */
            if (have_pal_data) {
               uint32_t rh = texture_tracker_effective_palette_hash_upload(self, tex->upload, (int)mode->mode, pal_local, palette_hash);
               if (rh != palette_hash) {
                  HdTextureId rid;
                  rid.hash = tex->upload->hash; rid.palette_hash = rh; rid.pages = false;
                  if (hd_key_set_contains(&self->known_files, hd_pack_key(rid))) {
                     upload_rect_has_file = true;
                     break;
                  }
               }
            }
         }
         if (!upload_rect_has_file) {
            unsigned width = mode->mode == TextureMode_Palette4bpp ? 64
                  : mode->mode == TextureMode_Palette8bpp ? 128 : 256;
            Rect page_rect = { 0, 0, 0, 0 };
            uint32_t page_hash;
            uint32_t page_phash;
            HdTextureHandle page;
            page_rect.x = page_x; page_rect.y = page_y; page_rect.width = width; page_rect.height = 256;
            page_hash = texture_tracker_hash_page_cached(self, page_rect);
            page_phash = palette_hash;
            if (have_pal_data) {
               uint32_t rh = texture_tracker_effective_palette_hash_page(self, page_rect, page_hash, pal_local, palette_hash);
               if (rh != palette_hash) {
                  HdTextureId rid;
                  rid.hash = page_hash; rid.palette_hash = rh; rid.pages = true;
                  if (hd_key_set_contains(&self->known_files_pages, hd_pack_key(rid)))
                     page_phash = rh;
               }
            }
            page = texture_tracker_match_page(self, page_rect, page_hash, page_phash);
            if (!hd_handle_is_none(&page)) {
               (*fastpath_capable_out) = false;
               return page;
            }
         }
      }

      if (!hd_handle_is_none(&result))
         handle_lru_cache_insert(&self->handle_cache, result_rect, palette_hash, result);
      return result;
      }
      }
   }

   HdTexture texture_tracker_get_hd_texture(struct TextureTracker *self,
         HdTextureHandle handle)
   {
      if (handle.page) {
         /* Page-aligned replacement: resolve from this frame's page_bindings.
          * vram_rect is the page in VRAM words; the shader maps it to the full
          * image via (vram_rect, texel_rect={0,0,img_w,img_h}) - like the fused path. */
         if (handle.index < 0 || handle.index >= self->page_bindings_count
               || !ih_is_valid(&self->page_bindings[handle.index].texture)) {
            HdTexture _r;
            TT_LOG(RETRO_LOG_WARN, "stale page HdTextureHandle: %d\n", handle.index);
            _r.vram_rect = (SRect){0, 0, 1, 1};
            _r.texel_rect = (SRect){0, 0, (int)(tt_img_width(ih_get(&self->default_hd_texture))), (int)(tt_img_height(ih_get(&self->default_hd_texture)))};
            _r.texture = tt_ih_owned_copy(&self->default_hd_texture); /* owned +1 (released by the consumer) */
            return _r;
         }
         {
            ReplacedPage *rp = &self->page_bindings[handle.index];
            TTGpuImage *img = ih_get(&rp->texture);
            HdTexture _r;
            /* Reconstruct a counted handle from the cached image (add_reference + adopt). */
            tt_img_addref(img);
            _r.vram_rect = toSRect(rp->page_rect);
            _r.texel_rect = (SRect){0, 0, (int)tt_img_width(img), (int)tt_img_height(img)};
            _r.texture = img; /* +1 taken above */
            return _r;
         }
      }
      if (!handle.fused)
      {
         int scaleX;
         ImageHandle image;
         /* HdTextureHandle's are perhaps too tricky.  They assume that the
          * RectTracker's textures vector hasn't removed anything since the
          * handle was created. So it would seem all you need to do is, in
          * renderer reset_queue, call the rect-tracker dead-handle release.
          * Except you have to be very very careful that no handles outside of
          * the queues (ie. local) exist across a call to reset_queue.  That is,
          * the handle must go into the queue as soon as possible, otherwise
          * that hd texture might not work (previously it would segfault). */
         TextureRect *tex = rect_tracker_get_index(&self->tracker, handle.index);
         if (tex == NULL) {
            if (handle.index != -1) {
               TT_LOG(RETRO_LOG_WARN, "stale HdTextureHandle: %d, %x\n", handle.index, handle.palette_hash);
            }
            {
               HdTexture _r;
               _r.vram_rect = (SRect){0, 0, 1, 1};
               _r.texel_rect = (SRect){0, 0, (int)(tt_img_width(ih_get(&self->default_hd_texture))), (int)(tt_img_height(ih_get(&self->default_hd_texture)))};
               _r.texture = tt_ih_owned_copy(&self->default_hd_texture); /* owned +1 (released by the consumer) */
               return _r;
            }
         }
         { TextureUpload *upload = tex->upload;
         /* Use find rather than index, because if a stale HdTextureHandle was
          * provided this could segfault because indexing on a key that isn't
          * present would initialize a new one with a null pointer */
         HdTexEntry *iter = hd_tex_map_find(&upload->textures, handle.palette_hash);
         if (iter == NULL) {
            TT_LOG(RETRO_LOG_WARN, "stale HdTextureHandle: %d, %x\n", handle.index, handle.palette_hash);
            {
               HdTexture _r;
               _r.vram_rect = (SRect){0, 0, 1, 1};
               _r.texel_rect = (SRect){0, 0, (int)(tt_img_width(ih_get(&self->default_hd_texture))), (int)(tt_img_height(ih_get(&self->default_hd_texture)))};
               _r.texture = tt_ih_owned_copy(&self->default_hd_texture); /* owned +1 (released by the consumer) */
               return _r;
            }
         }
         /* Reconstruct a counted handle from the stored raw image
          * (add_reference then adopt), matching hd_gpu_image_handle. */
         tt_img_addref(iter->image);
         image = ih_make(iter->image);
         scaleX = tt_img_width(ih_get(&image)) / upload->width;
         { int scaleY = tt_img_height(ih_get(&image)) / upload->height;
         SRect texture_subrect = texture_rect_subrect(tex);
         {
            HdTexture _r;
            _r.vram_rect = tex->vram_rect;
            _r.texel_rect = (SRect){
               texture_subrect.x * scaleX,
               texture_subrect.y * scaleY,
               texture_subrect.width * scaleX,
               texture_subrect.height * scaleY
            };
            /* Move the owned reference built just above into the result; the
             * local 'image' is not ih_reset, so the +1 ref from
             * image_add_reference lives on in _r.texture; the retain here balances the
             * eventual release at teardown. */
            _r.texture = ih_get(&image);
            return _r;
         }
         }
         }
      }
      else
         return fused_pages_get_from_handle(&self->fused_pages, handle, &self->default_hd_texture);
   }

static bool is_power_of_two(int n) {
      /* https://stackoverflow.com/questions/108318/whats-the-simplest-way-to-test-whether-a-number-is-a-power-of-2-in-c */
      return n != 0 && (n & (n - 1)) == 0;
   }

   /* ===== Page-aligned experiment + Lazy(sync) function definitions (PAGE_ALIGN.md) ===== */

   /* CRC32 of a VRAM page rect read from the CPU mirror. Row-by-row incremental
    * CRC is bit-identical to CRCing one gathered buffer, so it matches -pages
    * dumps while avoiding a malloc+copy on the common (no x-wrap) path. */
   static uint32_t texture_tracker_hash_page(struct TextureTracker *self, Rect page_rect) {
      uint32_t crc = 0;
      self->dbg_page_hashes++;
      if (page_rect.x + page_rect.width <= FB_WIDTH) {
         unsigned j;
         for (j = 0; j < page_rect.height; j++) {
            unsigned vy = (page_rect.y + j) & (FB_HEIGHT - 1);
            const uint16_t *row = &self->vram_mirror[vy * FB_WIDTH + page_rect.x];
            crc = crc32(crc, (const unsigned char*)row, page_rect.width * sizeof(uint16_t));
         }
         return crc;
      }
      {  /* rare x-wrap: gather then CRC */
         uint16_t *vec = (uint16_t*)malloc((size_t)page_rect.width * page_rect.height * sizeof(uint16_t));
         unsigned j, i;
         size_t k = 0;
         for (j = 0; j < page_rect.height; j++) {
            unsigned vy = (page_rect.y + j) & (FB_HEIGHT - 1);
            for (i = 0; i < page_rect.width; i++) {
               unsigned vx = (page_rect.x + i) & (FB_WIDTH - 1);
               vec[k++] = self->vram_mirror[vy * FB_WIDTH + vx];
            }
         }
         crc = crc32(0, (unsigned char*)vec, (size_t)page_rect.width * page_rect.height * sizeof(uint16_t));
         free(vec);
         return crc;
      }
   }

   /* Memoized page hash: re-CRC only if the page was written since last hash AND
    * not already refreshed this frame (caps busy regions to one re-hash/page/frame). */
   static uint32_t texture_tracker_hash_page_cached(struct TextureTracker *self, Rect page_rect) {
      int i;
      for (i = 0; i < self->cached_page_hashes_count; i++) {
         CachedPageHash *c = &self->cached_page_hashes[i];
         if (rect_eq(&c->rect, &page_rect)) {
            if (c->dirty && c->hashed_frame != self->frame) {
               c->hash = texture_tracker_hash_page(self, page_rect);
               c->dirty = false;
               c->hashed_frame = self->frame;
            }
            return c->hash;
         }
      }
      {
         uint32_t h = texture_tracker_hash_page(self, page_rect);
         if (self->cached_page_hashes_count == self->cached_page_hashes_cap) {
            int ncap = self->cached_page_hashes_cap ? self->cached_page_hashes_cap * 2 : 16;
            self->cached_page_hashes = (CachedPageHash*)realloc(self->cached_page_hashes, (size_t)ncap * sizeof(CachedPageHash));
            self->cached_page_hashes_cap = ncap;
         }
         {
            CachedPageHash *c = &self->cached_page_hashes[self->cached_page_hashes_count++];
            c->rect = page_rect; c->hash = h; c->dirty = false; c->hashed_frame = self->frame;
         }
         return h;
      }
   }

   /* Drop (mark dirty) memoized page hashes whose page overlaps a written VRAM rect. */
   static void texture_tracker_invalidate_page_hashes(struct TextureTracker *self, Rect written) {
      int i;
      bool all;
      if (self->cached_page_hashes_count == 0)
         return;
      all = (written.x + written.width > FB_WIDTH || written.y + written.height > FB_HEIGHT);
      for (i = 0; i < self->cached_page_hashes_count; i++) {
         if (all || rect_intersects(&self->cached_page_hashes[i].rect, &written))
            self->cached_page_hashes[i].dirty = true;
      }
   }

   /* --- VRAM mirror helpers. All x/y wrap to match the live VRAM access pattern. --- */
   static void texture_tracker_mirror_store(struct TextureTracker *self, Rect rect, const uint16_t *vram) {
      unsigned j, i;
      texture_tracker_invalidate_page_hashes(self, rect);
      for (j = rect.y; j < rect.y + rect.height; j++) {
         unsigned my = j & (FB_HEIGHT - 1);
         for (i = rect.x; i < rect.x + rect.width; i++) {
            unsigned mx = i & (FB_WIDTH - 1);
            self->vram_mirror[my * FB_WIDTH + mx] = vram[my * FB_WIDTH + mx];
         }
      }
   }
   static void texture_tracker_mirror_blit(struct TextureTracker *self, Rect dst, Rect src) {
      uint16_t *tmp;
      unsigned j, i;
      texture_tracker_invalidate_page_hashes(self, dst);
      /* copy src to a temp first so overlapping src/dst can't read overwritten cells */
      tmp = (uint16_t*)malloc((size_t)src.width * src.height * sizeof(uint16_t));
      for (j = 0; j < src.height; j++) {
         unsigned sy = (src.y + j) & (FB_HEIGHT - 1);
         for (i = 0; i < src.width; i++) {
            unsigned sx = (src.x + i) & (FB_WIDTH - 1);
            tmp[j * src.width + i] = self->vram_mirror[sy * FB_WIDTH + sx];
         }
      }
      for (j = 0; j < dst.height; j++) {
         unsigned dy = (dst.y + j) & (FB_HEIGHT - 1);
         for (i = 0; i < dst.width; i++) {
            unsigned dx = (dst.x + i) & (FB_WIDTH - 1);
            self->vram_mirror[dy * FB_WIDTH + dx] = tmp[j * dst.width + i];
         }
      }
      free(tmp);
   }
   static void texture_tracker_mirror_fill(struct TextureTracker *self, Rect rect, uint16_t value) {
      unsigned j, i;
      texture_tracker_invalidate_page_hashes(self, rect);
      for (j = rect.y; j < rect.y + rect.height; j++) {
         unsigned my = j & (FB_HEIGHT - 1);
         for (i = rect.x; i < rect.x + rect.width; i++)
            self->vram_mirror[my * FB_WIDTH + (i & (FB_WIDTH - 1))] = value;
      }
   }

   /* Decode a FULL VRAM texture page from the mirror via the active palette and
    * queue a PNG to <cd>-texture-dump-pages/. Snapshots raw words; the IO worker
    * decodes off-thread (decode_dump_rgba). */
   static void texture_tracker_dump_page(struct TextureTracker *self, Rect page_rect, uint32_t page_hash, UsedMode *mode, uint32_t palette_hash) {
      int shift;
      int ppp;
      uint16_t *palette = NULL;
      char dir[PATH_MAX_TT];
      char path[PATH_MAX_TT];
      uint16_t *src;
      size_t src_len;
      unsigned j, i;
      size_t k;
      switch (mode->mode) {
         case TextureMode_ABGR1555:    shift = 0; break;
         case TextureMode_Palette8bpp: shift = 1; break;
         case TextureMode_Palette4bpp: shift = 2; break;
         case TextureMode_None:
         default: return;
      }
      if (mode->mode == TextureMode_Palette4bpp || mode->mode == TextureMode_Palette8bpp) {
         Rect palette_rect = make_rect(mode->palette_offset_x, mode->palette_offset_y, mode->mode == TextureMode_Palette8bpp ? 256 : 16, 1);
         Palette p = texture_tracker_get_palette(self, palette_rect);
         if (p.data != NULL)
            palette = p.data;
      }
      ppp = 1 << shift;
      src_len = (size_t)page_rect.width * page_rect.height;
      src = (uint16_t*)malloc((src_len ? src_len : 1) * sizeof(uint16_t));
      k = 0;
      for (j = 0; j < page_rect.height; j++) {
         unsigned vy = (page_rect.y + j) & (FB_HEIGHT - 1);
         for (i = 0; i < page_rect.width; i++) {
            unsigned vx = (page_rect.x + i) & (FB_WIDTH - 1);
            src[k++] = self->vram_mirror[vy * FB_WIDTH + vx];
         }
      }
      dump_pages_path(dir, sizeof(dir));
      {
         static bool pages_dir_made = false;
         if (!pages_dir_made) { ensure_directory(dir); pages_dir_made = true; }
      }
      if (mode->mode != TextureMode_ABGR1555 && palette == NULL)
         snprintf(path, sizeof(path), "%s%x-missing.png", dir, (unsigned)page_hash);
      else
         snprintf(path, sizeof(path), "%s%x-%x.png", dir, (unsigned)page_hash, (unsigned)palette_hash);
      {
         IORequest *dump = (IORequest *)malloc(sizeof(IORequest));
         dump->next = NULL;
         dump->kind = IORequestKind_Dump;
         snprintf(dump->path, sizeof(dump->path), "%s", path);
         dump->width  = (int)(page_rect.width * ppp);
         dump->height = (int)page_rect.height;
         dump->pages  = true;
         dump->dump_mode = (int)mode->mode;
         dump->ppp    = ppp;
         dump->src = src; dump->src_len = src_len;
         if (palette != NULL) {
            dump->palette_len = (size_t)(mode->mode == TextureMode_Palette8bpp ? 256 : 16);
            dump->palette = (uint16_t*)malloc(dump->palette_len * sizeof(uint16_t));
            memcpy(dump->palette, palette, dump->palette_len * sizeof(uint16_t));
         } else {
            dump->palette = NULL; dump->palette_len = 0;
         }
         slock_lock(self->iothread.channel->lock);
         io_channel_push_request(self->iothread.channel, dump);
         slock_unlock(self->iothread.channel->lock);
         scond_signal(self->iothread.channel->cond);
      }
   }

   /* Auto-create the dump/replacement folder(s) for the active features + modes at
    * the HD-Texture-Folder location. Only mkdirs when the target path changes. */
   void texture_tracker_ensure_directories(struct TextureTracker *self, bool dump, bool replace) {
      char buf[PATH_MAX_TT];
      if (retro_cd_base_name[0] == '\0')
         return; /* no game loaded yet - paths would be malformed */
      if (dump && self->dump_mode_rect) dump_path(buf, sizeof(buf)); else buf[0] = '\0';
      if (strcmp(buf, self->ensured_dump_dir) != 0) {
         if (buf[0]) ensure_directory(buf);
         snprintf(self->ensured_dump_dir, sizeof(self->ensured_dump_dir), "%s", buf);
      }
      if (dump && self->dump_mode_pages) dump_pages_path(buf, sizeof(buf)); else buf[0] = '\0';
      if (strcmp(buf, self->ensured_dump_pages_dir) != 0) {
         if (buf[0]) ensure_directory(buf);
         snprintf(self->ensured_dump_pages_dir, sizeof(self->ensured_dump_pages_dir), "%s", buf);
      }
      if (replace) {
         if (self->replacement_mode_pages) replacements_pages_path(buf, sizeof(buf));
         else                              replacements_path(buf, sizeof(buf));
      } else buf[0] = '\0';
      if (strcmp(buf, self->ensured_replace_dir) != 0) {
         if (buf[0]) ensure_directory(buf);
         snprintf(self->ensured_replace_dir, sizeof(self->ensured_replace_dir), "%s", buf);
      }
   }

   /* Lazy (synchronous) upload-rect load: disk+decode+GPU upload+bind inline. */
   static void texture_tracker_sync_load_combo(struct TextureTracker *self, TextureUpload *upload, uint32_t palette_hash) {
      HdTextureId id;
      CachedHdImage *cpu;
      ImageHandle texture;
      id.hash = upload->hash; id.palette_hash = palette_hash; id.pages = false;
      cpu = HdImageCache_get(&self->hd_cache, hd_pack_key(id));
      if (cpu == NULL) {
         char path[PATH_MAX_TT];
         RGBAImage image;
         int alpha_flags = 0;
         LoadedLevels levels;
         int width, height;
         if (hd_key_set_contains(&self->requested, hd_pack_key(id)))
            return;
         if (!hd_key_set_contains(&self->known_files, hd_pack_key(id))) {
            hd_key_set_insert(&self->requested, hd_pack_key(id));
            return;
         }
         find_replacement_file(path, sizeof(path), id.hash, id.palette_hash, false);
         image.data = NULL;
         load_image(path, &image);
         if (image.data == NULL) {
            TT_LOG(RETRO_LOG_ERROR, "sync load failed: %s\n", path);
            hd_key_set_insert(&self->requested, hd_pack_key(id));
            return;
         }
         levels = prepare_texture(&image, &alpha_flags);
         width  = levels.levels[0].width;
         height = levels.levels[0].height;
         if (!(width % upload->width == 0 && is_power_of_two(width / upload->width) &&
               height % upload->height == 0 && is_power_of_two(height / upload->height))) {
            TT_LOG(RETRO_LOG_WARN, "Dimension mismatch (sync) for %x-%x, original=%dx%d, replacement=%dx%d\n",
                   id.hash, id.palette_hash, upload->width, upload->height, width, height);
            loaded_levels_reset(&levels);
            rgba_image_free(&image);
            hd_key_set_insert(&self->requested, hd_pack_key(id));
            return;
         }
         hd_image_cache_put(&self->hd_cache, id, &levels, alpha_flags);
         rgba_image_free(&image);
         self->dbg_responses_received++;
         cpu = HdImageCache_get(&self->hd_cache, hd_pack_key(id));
      }
      texture = ih_make(tt_upload_levels(&cpu->levels));
      hd_gpu_cache_put(&self->hd_gpu_cache, id, texture, cpu->alpha_flags, cpu->bytes);
      self->dbg_gpu_uploads++;
      hd_tex_map_set(&upload->textures, palette_hash, ih_get(&texture), cpu->alpha_flags);
      ih_reset(&texture);
      self->dbg_attaches++;
      {
         int ti;
         for (ti = 0; ti < self->tracker.textures.count; ti++) {
            EnduringTextureRect *e = &self->tracker.textures.a[ti];
            if (e->alive && e->texture_rect.upload == upload)
               fused_pages_mark_dirty(&self->fused_pages, fromSRect(e->texture_rect.vram_rect));
         }
      }
   }

   /* Lazy-sync page load: disk+decode+GPU upload inline; lands in the shared cache
    * keyed by {page_hash, palette, pages=true}; no per-upload bind. */
   static void texture_tracker_sync_load_page(struct TextureTracker *self, HdTextureId id) {
      CachedHdImage *cpu;
      ImageHandle texture;
      if (HdGpuCache_contains(&self->hd_gpu_cache, hd_pack_key(id)))
         return;
      cpu = HdImageCache_get(&self->hd_cache, hd_pack_key(id));
      if (cpu == NULL) {
         char path[PATH_MAX_TT];
         RGBAImage image;
         int alpha_flags = 0;
         LoadedLevels levels;
         if (hd_key_set_contains(&self->requested, hd_pack_key(id)))
            return;
         if (!hd_key_set_contains(&self->known_files_pages, hd_pack_key(id))) {
            hd_key_set_insert(&self->requested, hd_pack_key(id));
            return;
         }
         find_replacement_file(path, sizeof(path), id.hash, id.palette_hash, true);
         image.data = NULL;
         load_image(path, &image);
         if (image.data == NULL) {
            TT_LOG(RETRO_LOG_ERROR, "sync page load failed: %s\n", path);
            hd_key_set_insert(&self->requested, hd_pack_key(id));
            return;
         }
         levels = prepare_texture(&image, &alpha_flags);
         hd_image_cache_put(&self->hd_cache, id, &levels, alpha_flags);
         rgba_image_free(&image);
         self->dbg_responses_received++;
         cpu = HdImageCache_get(&self->hd_cache, hd_pack_key(id));
      }
      texture = ih_make(tt_upload_levels(&cpu->levels));
      hd_gpu_cache_put(&self->hd_gpu_cache, id, texture, cpu->alpha_flags, cpu->bytes);
      ih_reset(&texture);
      self->dbg_gpu_uploads++;
   }

   /* Draw-time page resolve: GPU-cache hit -> bind this frame; else load per the
    * caching method (lazy_sync inline, else async high-priority) and native this frame. */
   static HdTextureHandle texture_tracker_match_page(struct TextureTracker *self, Rect page_rect, uint32_t page_hash, uint32_t palette_hash) {
      HdTextureId id;
      CachedGpuImage *gpu;
      id.hash = page_hash; id.palette_hash = palette_hash; id.pages = true;
      gpu = HdGpuCache_get(&self->hd_gpu_cache, hd_pack_key(id));
      if (gpu == NULL) {
         if (self->lazy_sync) {
            texture_tracker_sync_load_page(self, id);
            gpu = HdGpuCache_get(&self->hd_gpu_cache, hd_pack_key(id));
         } else if (HdImageCache_contains(&self->hd_cache, hd_pack_key(id))) {
            /* decoded; GPU upload at on_queues_reset. Store the BASE (unsalted) key
             * so the page attach pass can unpack hash/palette. */
            hd_key_set_insert(&self->pending_attach_pages, ((uint64_t)id.hash << 32) | (uint64_t)id.palette_hash);
         } else {
            texture_tracker_want_combo(self, id, true, true); /* on-demand page disk load (high priority) */
         }
      }
      if (gpu == NULL) {
         self->dbg_page_miss++;
         return hd_handle_make_none(); /* native this frame */
      }
      if (self->page_bindings_count == self->page_bindings_cap) {
         int ncap = self->page_bindings_cap ? self->page_bindings_cap * 2 : 16;
         self->page_bindings = (ReplacedPage*)realloc(self->page_bindings, (size_t)ncap * sizeof(ReplacedPage));
         self->page_bindings_cap = ncap;
      }
      {
         ReplacedPage *rp = &self->page_bindings[self->page_bindings_count];
         rp->page_rect = page_rect;
         rp->texture = hd_gpu_image_handle(gpu); /* +1 counted handle owned by page_bindings */
         rp->alpha_flags = gpu->alpha_flags;
      }
      self->dbg_page_binds++;
      {
         int idx = self->page_bindings_count;
         self->page_bindings_count++;
         return hd_handle_make_page((RectIndex)idx, palette_hash);
      }
   }

   /* Release every per-upload HD binding (upload->textures). Each entry holds an
    * image_add_reference on a GPU-cache image, so persistent bindings would pin VRAM
    * outside the budget. Clearing them per frame (below) makes the budgeted GPU cache
    * the sole persistent VRAM owner: the draw path re-binds on-screen combos each
    * frame via request_hd_texture -> HdGpuCache_get (which touches the LRU), so drawn
    * textures stay hot and off-screen ones age out and free their VRAM. */
   static void texture_tracker_clear_all_upload_bindings(struct TextureTracker *self) {
      int _ti;
      int _rri;
      for (_ti = 0; _ti < self->tracker.textures.count; _ti++)
         hd_tex_map_clear(&self->tracker.textures.a[_ti].texture_rect.upload->textures);
      for (_rri = 0; _rri < rrvec_size(&self->restorable_rects); _rri++) {
         RestorableRect *restorable = &self->restorable_rects.items[_rri];
         int _tri;
         for (_tri = 0; _tri < ownedrects_size(&restorable->to_restore); _tri++)
            hd_tex_map_clear(&restorable->to_restore.v.items[_tri].upload->textures);
      }
   }

   /* Drop ALL resident HD state and free its VRAM (GPU + RAM caches, in-flight/pending
    * loads, page bindings, per-upload bindings, fused pages). Used when HD replacement
    * is turned off so VRAM is reclaimed immediately (does NOT re-scan folders). */
   void texture_tracker_flush_hd_state(struct TextureTracker *self) {
      int i;
      Rect _mdr;
      texture_tracker_clear_all_upload_bindings(self);
      HdGpuCache_clear(&self->hd_gpu_cache);
      HdImageCache_clear(&self->hd_cache);
      hd_key_set_clear(&self->requested);
      hd_key_set_clear(&self->pending_attach);
      hd_key_set_clear(&self->pending_attach_pages);
      /* The per-draw handle cache memoizes (rect, mode) -> handle; with the
       * upload bindings cleared above those handles now resolve to the 1x1
       * default texture. Without this clear, re-enabling Replace Textures
       * kept serving the stale memoized handles instead of re-running the
       * load/attach path, leaving placeholder textures on screen. */
      handle_lru_cache_clear(&self->handle_cache);
      for (i = 0; i < self->page_bindings_count; i++)
         ih_reset(&self->page_bindings[i].texture);
      self->page_bindings_count = 0;
      _mdr.x = 0; _mdr.y = 0; _mdr.width = FB_WIDTH; _mdr.height = FB_HEIGHT;
      fused_pages_mark_dead(&self->fused_pages, _mdr);
      fused_pages_remove_dead(&self->fused_pages); /* actually free the fused-page VRAM now */
   }

   /* TEMPORARY: */
   void texture_tracker_on_queues_reset(struct TextureTracker *self) {
      uint64_t up0 = self->dbg_gpu_uploads; /* GPU uploads done by THIS reset (per-frame burst detector) */
      handle_lru_cache_clear(&self->handle_cache);
      /* page handles don't survive a queue reset (same contract as handle_cache) */
      { int i; for (i = 0; i < self->page_bindings_count; i++) ih_reset(&self->page_bindings[i].texture); }
      self->page_bindings_count = 0;
      /* NOTE: upload->textures bindings are intentionally NOT cleared here. They hold
       * a shared (ref-counted) copy of the cache's image - not separate VRAM - so
       * clearing them per frame frees no VRAM, adds heavy re-attach churn, and breaks
       * handle resolution across a mid-frame queue flush (stale-handle floods). They
       * are released only on eviction/reload/flush. */
      rect_tracker_releaseDeadHandles(&self->tracker); /* This is called from reset_queue, so as of now no HdTextureHandle's exist */

      /* Poll HD uploads */

      slock_lock(self->iothread.channel->lock);
      { IOResponse *responses = io_channel_take_responses(self->iothread.channel); /* steal the list */
      slock_unlock(self->iothread.channel->lock);

      /* Move freshly decoded images into the cache (decode-once); mark them for
       * attach. The cache owns them regardless of whether their hash is
       * resident. Each response node is freed after its levels are moved into
       * the cache. */
      {
         IOResponse *response = responses;
         while (response != NULL) {
            IOResponse *rnext = response->next;
            HdTextureId id;
            self->dbg_responses_received++;
            id.hash = response->hash;
            id.palette_hash = response->palette_hash;
            id.pages = response->pages;
            hd_key_set_erase(&self->requested, hd_pack_key(id)); /* no longer in flight; now cached */
            hd_image_cache_put(&self->hd_cache, id, &response->levels, response->alpha_flags);
            if (response->pages)
               /* page combo: store the BASE (unsalted) key so the page attach pass can unpack it */
               hd_key_set_insert(&self->pending_attach_pages, ((uint64_t)id.hash << 32) | (uint64_t)id.palette_hash);
            else
               hd_key_set_insert(&self->pending_attach, hd_pack_key(id));
            io_response_free(response); /* levels already moved out (now empty) */
            response = rnext;
         }
      }

      /* Attach pass: for every wanted combo whose base hash is currently
       * resident, bind an HD image to it. Prefer the GPU cache (a ref-counted
       * handle copy - no upload); otherwise build the image from the CPU cache
       * and store it in the GPU cache. Combos whose hash isn't resident yet
       * stay cached (NOT discarded) and attach on a later self->frame. */
      {
         int pi;
         for (pi = 0; pi < self->pending_attach.count; pi++) {
            int height;
            int width;
            HdTextureId id;
            id.hash = (uint32_t)(self->pending_attach.keys[pi] >> 32);
            id.palette_hash = (uint32_t)self->pending_attach.keys[pi];
            id.pages = false;
            { TextureUpload *upload = texture_tracker_find_upload(self, id.hash); /* borrowed */
            if (upload == NULL)
               continue; /* not resident yet; kept in cache */
            if (hd_tex_map_contains(&upload->textures, id.palette_hash))
               continue; /* already attached */

            /* Tier 1: ready-to-bind GPU image - just copy the handle. */
            { CachedGpuImage *gpu = HdGpuCache_get(&self->hd_gpu_cache, hd_pack_key(id));
            if (gpu != NULL) {
               hd_tex_map_set(&upload->textures, id.palette_hash, gpu->image, gpu->alpha_flags);
               self->dbg_attaches++;
               { int _ti; for (_ti = 0; _ti < self->tracker.textures.count; _ti++)
               {
                  EnduringTextureRect *e = &self->tracker.textures.a[_ti];
                  if (e->alive && e->texture_rect.upload == upload)
                     fused_pages_mark_dirty(&self->fused_pages, fromSRect(e->texture_rect.vram_rect));
               } }
               continue;
            }

            /* Tier 2: decoded CPU levels - upload to GPU, then cache the image. */
            { CachedHdImage *cached = HdImageCache_get(&self->hd_cache, hd_pack_key(id));
            if (cached == NULL)
               continue; /* evicted from both caches; will be re-self->requested on draw */

            width = cached->levels.levels[0].width;
            height = cached->levels.levels[0].height;
            if (width  % upload->width  == 0 && is_power_of_two(width  / upload->width) &&
                  height % upload->height == 0 && is_power_of_two(height / upload->height))
            {
               ImageHandle texture = ih_make(tt_upload_levels(&cached->levels));
               hd_gpu_cache_put(&self->hd_gpu_cache, id, texture, cached->alpha_flags, cached->bytes);
               hd_tex_map_set(&upload->textures, id.palette_hash, ih_get(&texture), cached->alpha_flags);
               /* Both caches above retain their own reference; drop the
                * upload_texture producer reference held by this local. */
               ih_reset(&texture);
               self->dbg_gpu_uploads++;
               self->dbg_attaches++;
               { int _ti; for (_ti = 0; _ti < self->tracker.textures.count; _ti++) {
                  EnduringTextureRect *e = &self->tracker.textures.a[_ti];
                  if (e->alive && e->texture_rect.upload == upload)
                     fused_pages_mark_dirty(&self->fused_pages, fromSRect(e->texture_rect.vram_rect));
               } }
            } else {
               TT_LOG(RETRO_LOG_WARN, "Dimension mismatch for %x-%x, original=%dx%d, replacement=%dx%d\n",
                     id.hash, id.palette_hash, upload->width, upload->height, width, height);
               HdImageCache_erase(&self->hd_cache, hd_pack_key(id)); /* don't keep a bad-sized image around */
               hd_key_set_insert(&self->requested, hd_pack_key(id)); /* negatively cache so we don't reload + re-warn every self->frame */
            }
            }
            }
            }
         }
      }
      hd_key_set_clear(&self->pending_attach);

      /* Page attach pass: page combos have no TextureUpload to bind to - they're
       * resolved at draw time from the GPU cache. So this only promotes decoded CPU
       * levels into the GPU cache; match_page binds from there on a later frame. (No
       * dimension check: the page binding maps vram_rect to the image proportionally.)
       * pending_attach_pages stores BASE keys; rebuild the salted id to hit the cache. */
      {
         int pi;
         for (pi = 0; pi < self->pending_attach_pages.count; pi++) {
            HdTextureId id;
            CachedHdImage *cached;
            id.hash = (uint32_t)(self->pending_attach_pages.keys[pi] >> 32);
            id.palette_hash = (uint32_t)self->pending_attach_pages.keys[pi];
            id.pages = true;
            if (HdGpuCache_contains(&self->hd_gpu_cache, hd_pack_key(id)))
               continue; /* already resident */
            cached = HdImageCache_get(&self->hd_cache, hd_pack_key(id));
            if (cached == NULL)
               continue; /* evicted before upload; re-requested on next draw */
            {
               ImageHandle texture = ih_make(tt_upload_levels(&cached->levels));
               hd_gpu_cache_put(&self->hd_gpu_cache, id, texture, cached->alpha_flags, cached->bytes);
               ih_reset(&texture); /* gpu cache holds the surviving ref */
               self->dbg_gpu_uploads++;
            }
         }
      }
      hd_key_set_clear(&self->pending_attach_pages);

      fused_pages_rebuild_dirty(&self->fused_pages, &self->tracker);
      fused_pages_evict(&self->fused_pages);      /* LRU-evict to budget (marks dead) */
      fused_pages_remove_dead(&self->fused_pages); /* free the marked-dead pages' VRAM */

      {
         uint64_t this_reset_uploads = self->dbg_gpu_uploads - up0;
         if (this_reset_uploads > self->dbg_gpu_uploads_peak)
            self->dbg_gpu_uploads_peak = this_reset_uploads;
      }
      }
   }
   static TextureUpload *texture_tracker_find_upload(struct TextureTracker *self, uint32_t hash) {
      TextureUpload *upload = rect_tracker_find_upload(&self->tracker, hash); /* borrowed */
      int _ri;

      if (upload != NULL)
         return upload;

      /* backup search, in case it's restorable but currently missing from the rect tracker */
      for (_ri = 0; _ri < rrvec_size(&self->restorable_rects); _ri++)
      {
         RestorableRect *entry = &self->restorable_rects.items[_ri];
         size_t _ti;
         for (_ti = 0; _ti < ownedrects_size(&entry->to_restore); _ti++)
         {
            TextureRect *t = &entry->to_restore.v.items[_ti];
            if (hash == t->upload->hash)
               return t->upload;
         }
      }

      return NULL;
   }

   /* Defined in libretro.c; declared here so endFrame() can post an OSD toast on the
    * ']' HD-replacement toggle (the cbs header is #included far below this point). */
   extern retro_environment_t environ_cb;

   void texture_tracker_endFrame(struct TextureTracker *self) {
      self->frame += 1;

      if (self->frame % 300 == 0)
      {
         TT_LOG_VERBOSE(RETRO_LOG_INFO, "hit ratio: %f (%ld, %ld)\n", (double)(self->handle_cache.dbg_hits) / (self->handle_cache.dbg_hits + self->handle_cache.dbg_misses), self->handle_cache.dbg_hits, self->handle_cache.dbg_misses);
         self->handle_cache.dbg_hits = 0;
         self->handle_cache.dbg_misses = 0;
         TT_LOG(RETRO_LOG_INFO, "[hdcache] last 300f: %llu decodes, %llu gpu-uploads (peak %llu/frame), %llu attaches\n",
               (unsigned long long)(self->dbg_responses_received - self->dbg_responses_received_last),
               (unsigned long long)(self->dbg_gpu_uploads - self->dbg_gpu_uploads_last),
               (unsigned long long)self->dbg_gpu_uploads_peak,
               (unsigned long long)(self->dbg_attaches - self->dbg_attaches_last));
         TT_LOG(RETRO_LOG_INFO, "[hdcache] mode=%s ; replace=%s ; ram %zu/%zu MB (%zu entries) ; vram %zu/%zu MB (%zu entries)\n",
               self->eager_textures ? "eager" : (self->lazy_sync ? "lazy-sync" : "lazy"),
               self->replacement_mode_pages ? "page" : "upload-rect",
               HdImageCache_size_bytes(&self->hd_cache) / (1024 * 1024), HdImageCache_budget(&self->hd_cache) / (1024 * 1024), HdImageCache_count(&self->hd_cache),
               HdGpuCache_size_bytes(&self->hd_gpu_cache) / (1024 * 1024), HdGpuCache_budget(&self->hd_gpu_cache) / (1024 * 1024), HdGpuCache_count(&self->hd_gpu_cache));
         {  /* Fused-page composites are a separate VRAM tier NOT counted in the vram figure above. */
            int64_t _fused_bytes = 0;
            int _fi;
            for (_fi = 0; _fi < fused_page_vec_size(&self->fused_pages.pages); _fi++)
               _fused_bytes += page_bytes(&fused_page_vec_at(&self->fused_pages.pages, _fi)->fusion);
            TT_LOG(RETRO_LOG_INFO, "[hdcache] fused: %d pages, %.1f MiB\n",
                  fused_page_vec_size(&self->fused_pages.pages), _fused_bytes / 1048576.0);
         }
         if (self->replacement_mode_pages || self->replacement_fallback) {
            TT_LOG(RETRO_LOG_INFO, "[hdcache] pages(%s): %llu binds, %llu native-misses, %llu page-CRCs (last 300f); memo %d entries\n",
                  self->replacement_mode_pages ? "mode" : "fallback",
                  (unsigned long long)(self->dbg_page_binds - self->dbg_page_binds_last),
                  (unsigned long long)(self->dbg_page_miss - self->dbg_page_miss_last),
                  (unsigned long long)(self->dbg_page_hashes - self->dbg_page_hashes_last),
                  self->cached_page_hashes_count);
         }
         self->dbg_responses_received_last = self->dbg_responses_received;
         self->dbg_gpu_uploads_last = self->dbg_gpu_uploads;
         self->dbg_gpu_uploads_peak = 0;
         self->dbg_attaches_last = self->dbg_attaches;
         self->dbg_page_binds_last = self->dbg_page_binds;
         self->dbg_page_miss_last = self->dbg_page_miss;
         self->dbg_page_hashes_last = self->dbg_page_hashes;
      }

      if (self->frame_dump != NULL) {
         filestream_printf(self->frame_dump, "]}\n");
         filestream_close(self->frame_dump);
         self->frame_dump = NULL;
      }

      if (dbg_input_state_cb != 0)
      {
         if (dbg_hotkey_query(&self->frame_dump_key))
         {
            char fdpath[PATH_MAX_TT];
            dump_path(fdpath, sizeof(fdpath));
            snprintf(fdpath + strlen(fdpath), sizeof(fdpath) - strlen(fdpath), "test_dump.json");
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Left bracket!\n");
            self->frame_dump = filestream_open(fdpath, RETRO_VFS_FILE_ACCESS_WRITE,
                  RETRO_VFS_FILE_ACCESS_HINT_NONE);
            self->frame_dump_need_comma = false;
            if (self->frame_dump != NULL)
            {
               bool need_comma = false;
               int _eti;
               filestream_printf(self->frame_dump, "{ \"initial\": [\n");
               for (_eti = 0; _eti < self->tracker.textures.count; _eti++)
               {
                  EnduringTextureRect *etexture = &self->tracker.textures.a[_eti];
                  Rect rect;
                  TextureRect *texture;
                  if (!etexture->alive) continue;
                  texture = &etexture->texture_rect;
                  if (need_comma)
                     filestream_printf(self->frame_dump, ",");
                  else
                     need_comma = true;
                  filestream_printf(self->frame_dump, " { \"rect\": ");
                  rect = fromSRect(texture->vram_rect);
                  output_rect_json(self->frame_dump, &rect);
                  filestream_printf(self->frame_dump, ", \"hash\": \"%x\" }\n", texture->upload->hash);
               }
               filestream_printf(self->frame_dump, "], \"events\": [\n");
            }
         }

         if (dbg_hotkey_query(&self->hd_toggle_key)) {
            /* Snapshot HD VRAM-cache stats BEFORE any flush, so toggling OFF reports
             * what was actually resident. Shown ON-SCREEN (bypasses RetroArch's core
             * log-level filtering) and doubles as a build-freshness check: if you see
             * this new "cache .../... MB" format, the VRAM-fix build is definitely live. */
            size_t vram_used   = HdGpuCache_size_bytes(&self->hd_gpu_cache) / (1024 * 1024);
            size_t vram_budget = HdGpuCache_budget(&self->hd_gpu_cache) / (1024 * 1024);
            size_t vram_count  = HdGpuCache_count(&self->hd_gpu_cache);
            int    fused_count = fused_page_vec_size(&self->fused_pages.pages);
            self->hd_textures_enabled = !self->hd_textures_enabled;
            if (!self->hd_textures_enabled)
               texture_tracker_flush_hd_state(self); /* free HD VRAM immediately when turned off */
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Toggling hd textures: %s\n", self->hd_textures_enabled ? "on" : "off");
            if (environ_cb) {
               static char hd_msgbuf[256];
               struct retro_message msg;
               snprintf(hd_msgbuf, sizeof(hd_msgbuf),
                     "HD %s | cache %lu/%lu MB, %lu tex, %d fused",
                     self->hd_textures_enabled ? "ON" : "OFF",
                     (unsigned long)vram_used, (unsigned long)vram_budget,
                     (unsigned long)vram_count, fused_count);
               msg.msg    = hd_msgbuf;
               msg.frames = 300; /* ~5s at 60fps */
               environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
            }
         }

         if (dbg_hotkey_query(&self->reload_key)) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Reloading hd textures from disk\n");
            texture_tracker_reload_textures_from_disk(self);
         }

         if (dbg_hotkey_query(&self->fastpath_key)) {
            self->fastpath_enabled = !self->fastpath_enabled;
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Toggling fastpath %s\n", self->fastpath_enabled ? "ON" : "OFF");
         }
      }
   }

   void texture_tracker_reload_textures_from_disk(struct TextureTracker *self) {
      char rpath[PATH_MAX_TT];
      char cfg[PATH_MAX_TT];
      /* Reload the directory listing */
      read_texture_directory(&self->known_files, replacements_path(rpath, sizeof(rpath)), false);
      TT_LOG_VERBOSE(RETRO_LOG_INFO, "Found %d hd textures\n", (int)self->known_files.count);

      /* Page-aligned experiment: reload the -pages listing too. */
      read_texture_directory(&self->known_files_pages, replacements_pages_path(rpath, sizeof(rpath)), true);

      /* Re-read the dump-ignore list from the (possibly relocated) dump folder. */
      dump_path(cfg, sizeof(cfg));
      snprintf(cfg + strlen(cfg), sizeof(cfg) - strlen(cfg), "/dump.cfg");
      self->dump_ignore_count = parse_config_file(cfg, self->dump_ignore, DUMP_IGNORE_MAX);

      /* Drop all cached / loaded HD state so edited files on disk take effect. */
      HdGpuCache_clear(&self->hd_gpu_cache);
      HdImageCache_clear(&self->hd_cache);
      hd_key_set_clear(&self->requested);
      hd_key_set_clear(&self->pending_attach);
      hd_key_set_clear(&self->pending_attach_pages);
      self->cached_page_hashes_count = 0;
      self->cached_page_bounds_count = 0;
      { int _ti; for (_ti = 0; _ti < self->tracker.textures.count; _ti++) {
         EnduringTextureRect *texture = &self->tracker.textures.a[_ti];
         hd_tex_map_clear(&texture->texture_rect.upload->textures);
      } }
      {
      int _rri;
      for (_rri = 0; _rri < rrvec_size(&self->restorable_rects); _rri++)
      {
         RestorableRect *restorable = &self->restorable_rects.items[_rri];
         {
            int _tri;
            for (_tri = 0; _tri < ownedrects_size(&restorable->to_restore); _tri++) {
               TextureRect *tr = &restorable->to_restore.v.items[_tri];
               hd_tex_map_clear(&tr->upload->textures);
            }
         }
      }
      }

      /* Delete fused textures */
      {
         Rect _mdr;
         _mdr.x = 0;
         _mdr.y = 0;
         _mdr.width = FB_WIDTH;
         _mdr.height = FB_HEIGHT;
         fused_pages_mark_dead(&self->fused_pages, _mdr);
      }

      /* Draws will lazily re-request and the cache repopulates. */
   }

   /* RectTracker */

   static bool intersects(SRect a, SRect b) {
      return !(
            srect_left(&a) >= srect_right(&b) ||
            srect_left(&b) >= srect_right(&a) ||
            srect_top(&a) >= srect_bottom(&b) ||
            srect_top(&b) >= srect_bottom(&a)
         );
   }

   static SRect bounds(int left, int right, int top, int bottom) {
      return make_srect(left, top, right - left, bottom - top);
   }

   static void split(SRect original,
         SRect remove,
         SRect *results,
         unsigned *count)
   {
      SRect intersection;
      SRectResult intersectionResult = intersect(original, remove);
      if (!intersectionResult.valid)
      {
         results[(*count)++] = original;
         return;
      }

      intersection = intersectionResult.rect;

      /* Top rect */
      if (srect_top(&intersection) > srect_top(&original)) {
         results[(*count)++] = bounds(
               srect_left(&original),
               srect_right(&original),
               srect_top(&original),
               srect_top(&intersection)
               );
      }

      /* Bottom rect */
      if (srect_bottom(&intersection) < srect_bottom(&original)) {
         results[(*count)++] = bounds(
               srect_left(&original),
               srect_right(&original),
               srect_bottom(&intersection),
               srect_bottom(&original)
               );
      }

      /* Left rect */
      if (srect_left(&intersection) > srect_left(&original)) {
         results[(*count)++] = bounds(
               srect_left(&original),
               srect_left(&intersection),
               srect_top(&intersection),
               srect_bottom(&intersection)
               );
      }

      /* Right rect */
      if (srect_right(&intersection) < srect_right(&original)) {
         results[(*count)++] = bounds(
               srect_right(&intersection),
               srect_right(&original),
               srect_top(&intersection),
               srect_bottom(&intersection)
               );
      }
   }

   static void rect_tracker_upload(struct RectTracker *self,
         SRect rect,
         TextureUpload *upload){
      TextureRect texture = make_texture_rect(upload, 0, 0, rect);
      rect_tracker_place(self, texture);
      self->lookup_grid_dirty = true;
   }

   static SRect moved(SRect rect, int dx, int dy) {
      return make_srect(rect.x + dx, rect.y + dy, rect.width, rect.height);
   }

   static void rect_tracker_blit(struct RectTracker *self,
         SRect dst,
         SRect src){
      TextureRectVec to_place = { NULL, 0, 0 };
      int moveX = dst.x - src.x;
      int moveY = dst.y - src.y;
      int _ti;
      for (_ti = 0; _ti < self->textures.count; _ti++) {
         EnduringTextureRect *eold = &self->textures.a[_ti];
         if (eold->alive) {
            TextureRect *old = &eold->texture_rect;
            SRectResult intersection = intersect(old->vram_rect, src);
            if (intersection.valid) {
               TextureRect sub = subTexture(*old, intersection.rect);
               TextureRect subMoved = make_texture_rect(sub.upload, sub.offset_x, sub.offset_y, moved(sub.vram_rect, moveX, moveY));
               TextureRectVec_push(&to_place, &subMoved);
            }
         }
      }
      rect_tracker_clear_rect(self, &dst);
      {
         int _i;
         for (_i = 0; _i < TextureRectVec_size(&to_place); _i++)
            rect_tracker_place(self, *TextureRectVec_at(&to_place, _i));
      }
      TextureRectVec_free_storage(&to_place);
      self->lookup_grid_dirty = true;
   }

   static void rect_tracker_releaseDeadHandles(struct RectTracker *self)
   {
      enduring_arr_compact(&self->textures);
      self->lookup_grid_dirty = true;
   }

   static RectIndexSet *rect_tracker_overlapping(struct RectTracker *self,
         Rect uvrect,
         RectIndexSet *results)
   {
      SRect rect;
      if (self->lookup_grid_dirty)
         rect_tracker_rebuild_lookup_grid(self);

      /* TODO: remove this when renderer/build_attribs doesn't
       * have an unnecessary - 1 */
      if (uvrect.width == 0)
         uvrect.width = 1;

      rect = toSRect(uvrect);

      rect_index_set_clear(results);
      lookup_grid_get(&self->lookup_grid, rect, results);
      return results;
   }

   static TextureRect *rect_tracker_get_index(struct RectTracker *self,
         RectIndex index)
   {
      if (index < 0 || index >= self->textures.count)
         return NULL;
      return &self->textures.a[index].texture_rect;
   }

   static void rect_tracker_clear_rect(struct RectTracker *self, SRect *rect) {
      SRect splits[4];
      unsigned splits_count = 0;
      int ti, ni;

      TextureRectVec newTextures = { NULL, 0, 0 };
      for (ti = 0; ti < self->textures.count; ti++) {
         EnduringTextureRect *eold = &self->textures.a[ti];
         if (eold->alive) {
            TextureRect *old = &eold->texture_rect;

            splits_count = 0;
            split(old->vram_rect, *rect, splits, &splits_count);
            /* The rect didn't split, do nothing */
            if (splits_count == 1 && srect_eq(&splits[0], &old->vram_rect)) { }
            else
            {
               /* The rect split, mark this texture as dead and push its splits to be added */
               unsigned i;
               eold->alive = false;
               for (i = 0; i < splits_count; i++)
                  {
                     TextureRect _tr = subTexture(*old, splits[i]);
                     TextureRectVec_push(&newTextures, &_tr);
                  }
            }
         }
      }
      for (ni = 0; ni < newTextures.count; ni++)
         enduring_arr_push(&self->textures, newTextures.items[ni], true);
      TextureRectVec_free_storage(&newTextures);
   }
   static void rect_tracker_place(struct RectTracker *self,
         TextureRect texture){
      rect_tracker_clear_rect(self, &texture.vram_rect);
      enduring_arr_push(&self->textures, texture, true);
   }

   static void rect_tracker_rebuild_lookup_grid(struct RectTracker *self) {
      int i;
      lookup_grid_clear(&self->lookup_grid);
      for (i = 0; i < self->textures.count; i++)
      {
         if (self->textures.a[i].alive)
            lookup_grid_insert(&self->lookup_grid, self->textures.a[i].texture_rect.vram_rect, i);
      }
      self->lookup_grid_dirty = false;
   }

   static TextureUpload *rect_tracker_find_upload(struct RectTracker *self,
         uint32_t hash){
      int i;
      for (i = 0; i < self->textures.count; i++)
      {
         EnduringTextureRect *eold = &self->textures.a[i];
         if (eold->texture_rect.upload->hash == hash)
            return eold->texture_rect.upload;
      }
      return NULL;
   }

static int clamp(int x, int low, int high)
   {
      if (x < low)  x = low;
      if (x > high) x = high;
      return x;
   }

   struct CellBounds
   {
      int lowX;
      int highX; /* exclusive */
      int lowY;
      int highY; /* exclusive */
   };

   static CellBounds cellBounds(SRect vram) {
      {
         CellBounds _cb;
         _cb.lowX  = clamp(srect_left(&vram) / LOOKUP_CELL_WIDTH, 0, LOOKUP_GRID_COLUMNS);
         _cb.highX = clamp(ceil(srect_right(&vram) / (float)(LOOKUP_CELL_WIDTH)), 0, LOOKUP_GRID_COLUMNS);
         _cb.lowY  = clamp(srect_top(&vram) / LOOKUP_CELL_HEIGHT, 0, LOOKUP_GRID_ROWS);
         _cb.highY = clamp(ceil(srect_bottom(&vram) / (float)(LOOKUP_CELL_HEIGHT)), 0, LOOKUP_GRID_ROWS);
         return _cb;
      }
   }

   static void lookup_grid_init(LookupGrid *g) {
      int i;
      for (i = 0; i < LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS; i++) {
         g->cells[i].entries = NULL;
         g->cells[i].count = 0;
         g->cells[i].cap = 0;
      }
   }

   static void lookup_grid_deinit(LookupGrid *g) {
      int i;
      for (i = 0; i < LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS; i++)
         free(g->cells[i].entries);
   }

   static void lookup_grid_insert(LookupGrid *self, SRect r, RectIndex index)
   {
      CellBounds c = cellBounds(r);
      int x, y;
      for (x = c.lowX; x < c.highX; x++) {
         for (y = c.lowY; y < c.highY; y++) {
            Cell *cell = &self->cells[y * LOOKUP_GRID_COLUMNS + x];
            if (cell->count == cell->cap) {
               int ncap = cell->cap ? cell->cap * 2 : 8;
               LookupEntry *ne = (LookupEntry *)realloc(cell->entries, (size_t)ncap * sizeof(LookupEntry));
               if (!ne)
                  return;
               cell->entries = ne;
               cell->cap = ncap;
            }
            cell->entries[cell->count].rect = r;
            cell->entries[cell->count].index = index;
            cell->count++;
         }
      }
   }

   static void lookup_grid_get(LookupGrid *self, SRect r, RectIndexSet *results)
   {
      CellBounds c = cellBounds(r);
      int x, y;
      for (x = c.lowX; x < c.highX; x++)
      {
         for (y = c.lowY; y < c.highY; y++)
         {
            Cell *cell = &self->cells[y * LOOKUP_GRID_COLUMNS + x];
            int e;
            for (e = 0; e < cell->count; e++)
            {
               if (intersects(cell->entries[e].rect, r))
                  rect_index_set_insert(results, cell->entries[e].index);
            }
         }
      }
   }
   static void lookup_grid_clear(LookupGrid *self)
   {
      int i;
      for (i = 0; i < LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS; i++)
         self->cells[i].count = 0; /* keep allocation for reuse */
   }

   /* FusedPages */

static int64_t page_bytes(FusionRects *fusion)
   {
      return fusion->scaleX * fusion->scaleY * fusion->vram_rect.width * fusion->vram_rect.height * 4;
   }

   static void fused_pages_dbg_print_info(struct FusedPages *self) {
      int64_t num_bytes = 0;
      int _i;
      for (_i = 0; _i < fused_page_vec_size(&self->pages); _i++)
         num_bytes += page_bytes(&fused_page_vec_at(&self->pages, _i)->fusion);
      TT_LOG_VERBOSE(RETRO_LOG_INFO, "Fused Pages: %lu, Bytes: %ld (%.1f MiB)\n", (unsigned long)fused_page_vec_size(&self->pages), num_bytes, num_bytes / 1048576.0);
   }

   static bool srect_gt(const struct SRect *a, const struct SRect *b)
   {
      if (a->x != b->x)
         return a->x > b->x;
      if (a->y != b->y)
         return a->y > b->y;
      if (a->width != b->width)
         return a->width > b->width;
      return a->height > b->height;
   }

   static bool texture_rect_sort_gt(const struct TextureRect *a,
         const struct TextureRect *b){
      /* Compare .upload by pointer */
      if (a->upload != b->upload)
         return a->upload > b->upload;
      if (!srect_eq(&a->vram_rect, &b->vram_rect))
         return srect_gt(&a->vram_rect, &b->vram_rect);
      {
         SRect _sa = texture_rect_subrect(a);
         SRect _sb = texture_rect_subrect(b);
         return srect_gt(&_sa, &_sb);
      }
   }

   /* qsort comparator: descending order, matching texture_rect_sort_gt. Equal
    * elements (fully identical under the predicate) are interchangeable, so the
    * unstable order among them does not affect the canonical-form comparison
    * this sort exists to enable. */
   static int texture_rect_qsort_cmp(const void *pa, const void *pb) {
      const TextureRect *a = (const TextureRect *)(pa);
      const TextureRect *b = (const TextureRect *)(pb);
      if (texture_rect_sort_gt(a, b))
         return -1;
      if (texture_rect_sort_gt(b, a))
         return 1;
      return 0;
   }

   static void fusion_rects(struct FusionRects *out,
         Rect full_page_rect,
         uint32_t palette_hash,
         struct RectTracker *tracker){
      int _ei;
      struct FusionRects *f = out;
      fusionrects_init(f);

      for (_ei = 0; _ei < tracker->textures.count; _ei++) {
         EnduringTextureRect *e = &tracker->textures.a[_ei];
         SRectResult intersection;
         if (!e->alive)
            continue;
         intersection = intersect(toSRect(full_page_rect), e->texture_rect.vram_rect);
         if (intersection.valid) {
            TextureUpload *upload = e->texture_rect.upload;
            HdTexEntry *hd_texture = hd_tex_map_find(&upload->textures, palette_hash);
            if (hd_texture != NULL) {
               Rect r;
               /* Clip to the destination texture (important, otherwise it might blit out of bounds which may have wrought havoc upon my sanity) */
               TextureRect clipped = subTexture(e->texture_rect, intersection.rect);
               unsigned hd_scale_x = tt_img_width(hd_texture->image) / upload->width;
               unsigned hd_scale_y = tt_img_height(hd_texture->image) / upload->height;
               f->scaleX = max_(f->scaleX, hd_scale_x);
               f->scaleY = max_(f->scaleY, hd_scale_y);
               r = fromSRect(clipped.vram_rect);
               if (f->vram_rect.width == 0)
                  f->vram_rect = r;
               else
                  rect_extend_bounding_box(&f->vram_rect, &r);
               ownedrects_push(&f->rects, clipped);
            }
         }
      }

      /* Sort rects so that the vector itself can be compared */
      qsort(TextureRectVec_data(&f->rects.v), TextureRectVec_size(&f->rects.v), sizeof(TextureRect), texture_rect_qsort_cmp);

   }

   static void rebuild_page(FusedPage *page,
         struct RectTracker *tracker){
      int texture_width;
      TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilding page for %x, %d,%d %dx%d\n",
            page->palette,
            page->fusion.vram_rect.x,
            page->fusion.vram_rect.y,
            page->fusion.vram_rect.width,
            page->fusion.vram_rect.height
               );

      page->dirty = false;

      {
         FusionRects fusion;
         fusion_rects(&fusion, page->full_page_rect, page->palette, tracker);
         if (fusionrects_eq(&page->fusion, &fusion)) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: no change\n");
            fusionrects_destroy(&fusion);
            return;
         }
         fusionrects_move(&page->fusion, &fusion);
         fusionrects_destroy(&fusion);
      }

      if (ownedrects_size(&page->fusion.rects) == 0) {
         page->dead = true;
         ih_reset(&page->texture);
         TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: page is now dead\n");
         return;
      }

      texture_width = page->fusion.vram_rect.width * page->fusion.scaleX;
      { int texture_height = page->fusion.vram_rect.height * page->fusion.scaleY;

      int mip_levels = log2(min_(page->fusion.scaleX, page->fusion.scaleY)) + 1;

      /* page_begin returns a cleared (0,0,0,1-sentinel) transfer target,
       * reusing the existing image when its dimensions still match (same
       * handle back, no reference change) or creating a fresh one (+1 ref
       * owned by us; the stale one is released below). The sentinel clear
       * will not bleed into neighbors in the mipmaps, because the mipmaps
       * are only used down to the original resolution, and hd textures are
       * aligned to that original resolution's texels. */
      {
         TTGpuImage *cur = ih_get(&page->texture);
         TTGpuImage *target = tt_backend.page_begin(tt_backend.ctx, cur,
               texture_width, texture_height, mip_levels);
         if (target != cur)
            ih_move(&page->texture, ih_make(target));
      }

      /* Second pass to blit all the existing textures into the new texture */
      {
      int _fri;
      for (_fri = 0; _fri < ownedrects_size(&page->fusion.rects); _fri++) {
         TTGpuImage * image;
         TextureRect *tex = &page->fusion.rects.v.items[_fri];
         TextureUpload *upload = tex->upload;

         HdTexEntry *hd_texture = hd_tex_map_find(&upload->textures, page->palette);
         /* That's odd */
         if (hd_texture == NULL)
            continue;

         image = hd_texture->image;

         { int srcWidth = tt_img_width(image);
         int srcHeight = tt_img_height(image);

         int sx = srcWidth / upload->width;
         int sy = srcHeight / upload->height;

         int rx = page->fusion.scaleX / sx;
         int ry = page->fusion.scaleY / sy;

         SRect subrect = texture_rect_subrect(tex);

         /* Blit into every mipmap level down to base vram. The backend
          * replays the level-0 rects across the page's mip chain (source
          * level = max(0, dstLevel - full_res_levels), source rect shifted
          * per source level, destination rect halved per destination
          * level) exactly like the old inline Vulkan loop. TODO: this
          * isn't a great way to do this, will probably be blurrier than it
          * could be if src and dst aspect ratios are different. */
         {
            TTPageBlit blit;
            blit.dst_x = (tex->vram_rect.x - (int)(page->fusion.vram_rect.x)) * (int)(page->fusion.scaleX);
            blit.dst_y = (tex->vram_rect.y - (int)(page->fusion.vram_rect.y)) * (int)(page->fusion.scaleY);
            blit.dst_w = tex->vram_rect.width * (int)(page->fusion.scaleX);
            blit.dst_h = tex->vram_rect.height * (int)(page->fusion.scaleY);
            blit.src_x = sx * subrect.x;
            blit.src_y = sy * subrect.y;
            blit.src_w = sx * subrect.width;
            blit.src_h = sy * subrect.height;
            blit.full_res_levels = log2(max_(rx, ry)) + 1;
            tt_backend.page_blit(tt_backend.ctx, ih_get(&page->texture), image, &blit);
         }
         }
      }
      }

      /* Make the fused texture readable by shaders */
      tt_backend.page_end(tt_backend.ctx, ih_get(&page->texture));

      TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: page now %ux%u, %ld bytes (%.1f MiB)\n",
            page->fusion.vram_rect.width * page->fusion.scaleX, page->fusion.vram_rect.height * page->fusion.scaleY,
            page_bytes(&page->fusion), page_bytes(&page->fusion) / 1048576.0
               );
      }
   }

   static HdTexture fused_pages_get_from_handle(struct FusedPages *self,
         HdTextureHandle handle,
         ImageHandle *default_hd_texture){
      FusedPage *page;
      if (handle.index < 0 || handle.index >= fused_page_vec_size(&self->pages)) {
         HdTexture _r;
         TT_LOG(RETRO_LOG_WARN, "BAD fused index!\n");
         _r.vram_rect = (SRect){0, 0, 1, 1};
         _r.texel_rect = (SRect){0, 0, (int)(tt_img_width(ih_get(default_hd_texture))), (int)(tt_img_height(ih_get(default_hd_texture)))};
         _r.texture = tt_ih_owned_copy(default_hd_texture); /* owned +1 (released by the consumer) */
         return _r;
      }
      page = fused_page_vec_at(&self->pages, handle.index);
      if (!ih_is_valid(&page->texture)) {
         HdTexture _r;
         TT_LOG(RETRO_LOG_WARN, "Missing fused texture!\n");
         _r.vram_rect = (SRect){0, 0, 1, 1};
         _r.texel_rect = (SRect){0, 0, (int)(tt_img_width(ih_get(default_hd_texture))), (int)(tt_img_height(ih_get(default_hd_texture)))};
         _r.texture = tt_ih_owned_copy(default_hd_texture); /* owned +1 (released by the consumer) */
         return _r;
      }
      {
         HdTexture _r;
         _r.vram_rect = toSRect(page->fusion.vram_rect);
         _r.texel_rect = (SRect){ 0, 0, (int)(tt_img_width(ih_get(&page->texture))), (int)(tt_img_height(ih_get(&page->texture))) };
         _r.texture = tt_ih_owned_copy(&page->texture); /* owned +1 (released by the consumer) */
         return _r;
      }
   }

   static HdTextureHandle fused_pages_get_or_make(struct FusedPages *self,
         Rect page_rect,
         uint32_t palette,
         struct RectTracker *tracker){
      int x;
      FusedPage page;
      for (x = 0; x < fused_page_vec_size(&self->pages); x++)
      {
         FusedPage *p = fused_page_vec_at(&self->pages, x);
         /* return page */
         if (!p->dead && p->palette == palette && rect_eq(&p->full_page_rect, &page_rect)) {
            p->last_used = ++self->tick; /* touch LRU */
            return hd_handle_make_fused(x);
         }
      }

      /* Make a new fused page */
      TT_LOG_VERBOSE(RETRO_LOG_INFO, "Creating new fused page for palette %x\n", palette);

      /* `page` is a raw stack FusedPage: its handle/heap-owning members MUST be
       * initialised before rebuild_page reads them. rebuild_page checks
       * ih_is_valid(&page->texture) (so texture must be a NULL handle, not garbage)
       * and compares page->fusion via fusionrects_eq (so fusion.rects must be a
       * valid empty vec). Leaving them uninitialised reads as zero at -O0 (safe) but
       * garbage at -O3 (texture.data=0xffffffff -> ih_is_valid true -> deref crash). */
      page.texture = ih_make(NULL);
      fp_init_raw(&page); /* fusion.rects -> empty */
      page.fusion.vram_rect = make_rect(0, 0, 0, 0);
      page.fusion.scaleX = 0;
      page.fusion.scaleY = 0;
      page.dead = false;
      page.dirty = false;
      page.full_page_rect = page_rect;
      page.palette = palette;
      rebuild_page(&page, tracker);
      page.last_used = ++self->tick;
      page.bytes = ih_is_valid(&page.texture)
         ? (size_t)tt_img_width(ih_get(&page.texture)) * (size_t)tt_img_height(ih_get(&page.texture)) * 4u
         : 0;
      fused_page_vec_push(&self->pages, &page);
      return hd_handle_make_fused(fused_page_vec_size(&self->pages) - 1);
   }
   static void fused_pages_mark_dirty(struct FusedPages *self, Rect rect) {
      int _i;
      for (_i = 0; _i < fused_page_vec_size(&self->pages); _i++)
      {
         FusedPage *page = fused_page_vec_at(&self->pages, _i);
         if (!page->dead && rect_intersects(&page->full_page_rect, &rect))
            page->dirty = true;
      }
   }
   static void fused_pages_mark_dead(struct FusedPages *self, Rect rect) {
      int _i;
      for (_i = 0; _i < fused_page_vec_size(&self->pages); _i++)
      {
         FusedPage *page = fused_page_vec_at(&self->pages, _i);
         if (!page->dead && rect_intersects(&page->full_page_rect, &rect))
            page->dead = true;
      }
   }
   static void fused_pages_rebuild_dirty(struct FusedPages *self,
         struct RectTracker *tracker){
      bool changed = false;
      int _i;
      for (_i = 0; _i < fused_page_vec_size(&self->pages); _i++) {
         FusedPage *page = fused_page_vec_at(&self->pages, _i);
         if (!page->dead && page->dirty) {
            rebuild_page(page, tracker);
            changed = true;
         }
      }
      if (changed)
         fused_pages_dbg_print_info(self);
   }
   static void fused_pages_remove_dead(struct FusedPages *self) {
      int retained = 0;
      int i;
      for (i = 0; i < fused_page_vec_size(&self->pages); i++) {
         if (!fused_page_vec_at(&self->pages, i)->dead) {
            if (retained != i) {
               FusedPage *dst = fused_page_vec_at(&self->pages, retained);
               FusedPage *src = fused_page_vec_at(&self->pages, i);
               fp_destroy(dst);
               fp_copy(dst, src);
            }
            retained++;
         }
      }
      fused_page_vec_truncate(&self->pages, retained);
   }
   /* LRU-evict live fused pages (mark them dead) until the total footprint is within
    * budget. Run at the on_queues_reset safe point, immediately before remove_dead
    * compacts and frees the marked pages. budget 0 = unlimited (disabled). */
   static void fused_pages_evict(struct FusedPages *self) {
      size_t total = 0;
      int i;
      if (self->budget_bytes == 0)
         return;
      for (i = 0; i < fused_page_vec_size(&self->pages); i++) {
         FusedPage *p = fused_page_vec_at(&self->pages, i);
         if (!p->dead)
            total += p->bytes;
      }
      while (total > self->budget_bytes) {
         int victim = -1;
         uint64_t oldest = 0;
         for (i = 0; i < fused_page_vec_size(&self->pages); i++) {
            FusedPage *p = fused_page_vec_at(&self->pages, i);
            if (p->dead)
               continue;
            if (victim < 0 || p->last_used < oldest) {
               oldest = p->last_used;
               victim = i;
            }
         }
         if (victim < 0)
            break; /* nothing left to evict */
         { FusedPage *v = fused_page_vec_at(&self->pages, victim);
           v->dead = true;
           total -= v->bytes; }
      }
   }


   /* ========================================
    * Save State */

   /* Allocate a new upload (refcount 1) that is a deep copy of to_copy with the
    * HD textures map cleared. Replaces the old by-value copy initialiser flow. */
   static TextureUpload *texture_upload_new_copy_without_handles(const TextureUpload *to_copy) {
      TextureUpload *copy = texture_upload_new();
      texture_upload_copy_contents(copy, to_copy);
      hd_tex_map_clear(&copy->textures);
      return copy;
   }

   /* Transient hash -> upload-pointer lookup used only while loading a save
    * state. Non-owning (the pointers are owned by the +1 ref taken in
    * load_state and dropped at the end), keys are unique and inserted once, so
    * a flat POD array with a linear scan replaces the old
    * a uint32_t -> TextureUpload* map. */
   struct UploadPtrEntry { uint32_t key; TextureUpload *val; };
   POD_VEC_DECLARE(UploadPtrVec, UploadPtrEntry);

   static TextureRectSaveState to_save_state(const TextureRect *t,
         UploadOwningMap *uploads){
      uint32_t hash = t->upload->hash;
      if (!uploadmap_contains(uploads, hash))
         uploadmap_insert(uploads, hash, texture_upload_new_copy_without_handles(t->upload));
      { TextureRectSaveState _ss; _ss.upload_hash = t->upload->hash; _ss.offset_x = t->offset_x;
        _ss.offset_y = t->offset_y; _ss.vram_rect = t->vram_rect; return _ss; }
   }

   static TextureRect from_save_state(const TextureRectSaveState *t,
         UploadPtrVec *uploads){
      TextureUpload *found = NULL;
      { int i; for (i = 0; i < uploads->count; i++) {
         if (uploads->items[i].key == t->upload_hash) {
            found = uploads->items[i].val;
            break;
         }
      } }
      if (!found) {
         TT_LOG(RETRO_LOG_ERROR, "SaveState upload missing!\n");
      }
      { TextureRect _tr; _tr.upload = found; _tr.offset_x = t->offset_x; _tr.offset_y = t->offset_y;
        _tr.vram_rect = t->vram_rect; return _tr; }
   }

   void texture_tracker_save_state(struct TextureTracker *self,
         TextureTrackerSaveState *out)
   {
      TextureTrackerSaveState state;
      tts_init(&state);

      { int _ti; for (_ti = 0; _ti < self->tracker.textures.count; _ti++)
      {
         EnduringTextureRect *r = &self->tracker.textures.a[_ti];
         if (r->alive)
         {
            TextureRectSaveState _ss = to_save_state(&r->texture_rect, &state.uploads);
            TextureRectSaveStateVec_push(&state.rects, &_ss);
         }
      } }
      {
      int _sri;
      for (_sri = 0; _sri < rrvec_size(&self->restorable_rects); _sri++)
      {
         RestorableRect *r = &self->restorable_rects.items[_sri];
         RestorableRectSaveState saved;
         rrss_init(&saved);
         saved.hash = r->hash;
         saved.rect = r->rect;
         {
         int _rti;
         for (_rti = 0; _rti < ownedrects_size(&r->to_restore); _rti++)
         {
            TextureRect *t = &r->to_restore.v.items[_rti];
            TextureRectSaveState _ss = to_save_state(t, &state.uploads);
            TextureRectSaveStateVec_push(&saved.to_restore, &_ss);
         }
         }
         RestorableRectSaveStateVec_push_move(&state.restorable, &saved);
      }
      }

      tts_move(out, &state);
      tts_destroy(&state);
   }


   void texture_tracker_load_state(struct TextureTracker *self,
         const TextureTrackerSaveState *state)
   {
      UploadPtrVec uploads = { NULL, 0, 0 };
      { int e; for (e = 0; e < state->uploads.count; e++) {
         TextureUpload *ptr = texture_upload_new(); /* owns +1 */
         texture_upload_copy_contents(ptr, state->uploads.items[e].val); /* deep-copy contents (refcount untouched) */
         { UploadPtrEntry pe = { state->uploads.items[e].key, ptr };
         UploadPtrVec_push(&uploads, &pe);
         }
      } }

      {
         Rect _crr;
         _crr.x = 0;
         _crr.y = 0;
         _crr.width = FB_WIDTH;
         _crr.height = FB_HEIGHT;
         texture_tracker_clearRegion(self, _crr, 0);
      }
      enduring_arr_clear(&self->tracker.textures); /* load_state should only be called right after creating this TextureTracker, so this ought to be empty already anyway */
      {
         int _i;
         for (_i = 0; _i < TextureRectSaveStateVec_size(&state->rects); _i++)
            rect_tracker_place(&self->tracker, from_save_state(TextureRectSaveStateVec_at((struct TextureRectSaveStateVec *)&state->rects, _i), &uploads));
      }
      rrvec_clear(&self->restorable_rects);
      {
         int _i;
         for (_i = 0; _i < RestorableRectSaveStateVec_size(&state->restorable); _i++)
         {
            int _j;
            RestorableRectSaveState *r = RestorableRectSaveStateVec_at((struct RestorableRectSaveStateVec *)&state->restorable, _i);
            RestorableRect loaded;
            restorablerect_init(&loaded);
            loaded.hash = r->hash;
            loaded.rect = r->rect;
            for (_j = 0; _j < TextureRectSaveStateVec_size(&r->to_restore); _j++)
               ownedrects_push(&loaded.to_restore, from_save_state(TextureRectSaveStateVec_at(&r->to_restore, _j), &uploads));
            rrvec_push(&self->restorable_rects, &loaded);
            restorablerect_destroy(&loaded);
         }
      }
      /* Need to reload the hd textures, too */
      {
         int e;
         for (e = 0; e < state->uploads.count; e++)
            texture_tracker_load_hd_texture(self, state->uploads.items[e].key);
      }
      /* Drop the map's construction refs; the placed/restorable TextureRects
       * now hold their own references to each upload. */
      {
         int i;
         for (i = 0; i < uploads.count; i++)
            texture_upload_release(uploads.items[i].val);
      }
      UploadPtrVec_free_storage(&uploads);
   }
   /* End of Save State
    * ======================================== */
   static bool dbg_hotkey_query(struct DbgHotkey *self)
   {
      uint16_t state = dbg_input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, self->key);
      bool is_key_down = state != 0;
      bool just_pressed = is_key_down && !self->was_key_down;
      self->was_key_down = is_key_down;
      return just_pressed;
   }

/* ============================================================
 * Public lifecycle + configuration adapters (rhi_tt.h). The
 * tracker itself is heap-allocated and opaque to the renderers;
 * the backend vtable is process-wide (one active RHI renderer at
 * a time - see the shim block at the top of this file).
 * ============================================================ */

TextureTracker *texture_tracker_new(const TTGpuBackend *backend,
      TTGpuImage *default_hd_texture)
{
   TextureTracker *self;

   tt_backend     = *backend;
   tt_backend_set = true;

   self = (TextureTracker *)calloc(1, sizeof(*self));
   if (!self)
      return NULL;
   /* init assumes zeroed storage for a few members (it historically ran
    * inside a calloc'd Renderer), hence calloc above rather than malloc. */
   texture_tracker_init(self);

   if (default_hd_texture) {
      tt_img_addref(default_hd_texture);
      ih_move(&self->default_hd_texture, ih_make(default_hd_texture));
   } else {
      /* Build the 1x1 transparent fallback through the backend (the old
       * set_texture_uploader body). */
      LoadedLevels default_levels;
      LoadedImage default_image;
      loaded_levels_init(&default_levels);
      loaded_image_init(&default_image);
      loaded_image_alloc(&default_image, 1, 1);
      default_image.owned_data[0] = 0;
      default_image.owned_data[1] = 0;
      default_image.owned_data[2] = 0;
      default_image.owned_data[3] = 0;
      loaded_levels_push_move(&default_levels, &default_image);
      ih_move(&self->default_hd_texture, ih_make(tt_upload_levels(&default_levels)));
      loaded_levels_reset(&default_levels); /* upload copied the pixels */
   }
   return self;
}

void texture_tracker_free(TextureTracker *self)
{
   if (!self)
      return;
   texture_tracker_fini(self);
   free(self);
   tt_backend_set = false;
}

void texture_tracker_set_config(TextureTracker *self,
      const TextureTrackerConfig *cfg)
{
   /* Replace Textures is edge-applied: the in-game ']' toggle (see
    * texture_tracker_endFrame) flips hd_textures_enabled directly, and this
    * setter runs every frame from the option-refresh path - re-stamping the
    * menu value unconditionally would immediately undo the hotkey. Only a
    * CHANGED menu value re-applies (and re-syncs), matching the old inline
    * apply logic in the Vulkan renderer. */
   static int replace_textures_applied = -1; /* -1 = force the first apply */

   self->dump_enabled           = cfg->dump_enabled;
   self->eager_textures         = cfg->eager_textures;
   self->lazy_sync              = cfg->lazy_sync;
   self->dump_mode_rect         = cfg->dump_mode_rect;
   self->dump_mode_pages        = cfg->dump_mode_pages;
   self->replacement_mode_pages = cfg->replacement_mode_pages;
   self->replacement_fallback   = cfg->replacement_fallback;
   self->reduce_palette_range   = cfg->reduce_palette_range;

   if ((int)cfg->hd_textures_enabled != replace_textures_applied) {
      self->hd_textures_enabled = cfg->hd_textures_enabled;
      if (!cfg->hd_textures_enabled)
         texture_tracker_flush_hd_state(self); /* free HD VRAM immediately when turned off */
      replace_textures_applied = cfg->hd_textures_enabled;
   }
}

void texture_tracker_set_texture_dir_mode(int mode)
{
   texture_dir_mode = mode;
}

TextureTrackerSaveState *tts_new(void)
{
   TextureTrackerSaveState *s =
      (TextureTrackerSaveState *)malloc(sizeof(*s));
   if (s)
      tts_init(s);
   return s;
}

void tts_free(TextureTrackerSaveState *s)
{
   if (!s)
      return;
   tts_destroy(s);
   free(s);
}
