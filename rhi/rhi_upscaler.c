/* SPDX-License-Identifier: MIT */
/* The geometry side of a frontend temporal upscaler; see rhi_upscaler.h */

#include "rhi_upscaler.h"

#include <stdlib.h>
#include <string.h>

/* Drawing areas drawn into at once (a game's double buffer and a few
 * more) */
#define UPSCALER_RECORDS   4
#define UPSCALER_MAX_PRIMS 65536
/* Further than this, in native pixels, nothing on screen is the same
 * thing a frame later */
#define UPSCALER_FARTHEST  96.0f
/* How far from its own ordinal a primitive's match is looked for: one
 * culled or added earlier in the frame shifts the rest */
#define UPSCALER_ORDINAL_WINDOW 8
/* What a primitive is nearer than the one drawn before it by */
#define UPSCALER_DEPTH_STEP (1.0f / 65536.0f)
/* PGXP's view-space z this far is depth 0.5 */
#define UPSCALER_DEPTH_HALF 512.0f

#define U64(hi, lo) (((uint64_t)(hi) << 32) | (uint64_t)(lo))

typedef struct upscaler_prim
{
   uint64_t signature;
   /* The signature less what changes on the same thing from frame to
    * frame (texture coordinates, a flat colour): matched by when
    * nothing matches the signature */
   uint64_t family;
   uint32_t ordinal;
   uint32_t family_ordinal;
   uint32_t flags;
   unsigned count;
   float x[4], y[4], w[4];
} upscaler_prim_t;

/* Open addressing; a value of 0 is an empty slot */
typedef struct upscaler_table
{
   uint64_t *keys;
   uint32_t *values;
   uint32_t mask;
   uint32_t count;
} upscaler_table_t;

typedef struct upscaler_record
{
   upscaler_prim_t *prims;
   unsigned num_prims;
   unsigned cap_prims;
   /* signature -> primitives drawn under it so far, and the same for
    * the family */
   upscaler_table_t ordinals;
   upscaler_table_t family_ordinals;
   uint64_t serial;
   unsigned x0, y0, x1, y1;
   float jitter_x, jitter_y;
   /* What its primitives are relative to: VRAM until shown, then the
    * display's top left */
   int origin_x, origin_y;
   bool used;
   bool shown;
} upscaler_record_t;

struct psx_upscaler
{
   upscaler_record_t records[UPSCALER_RECORDS];
   upscaler_record_t *drawing;
   unsigned area_x0, area_y0, area_x1, area_y1;

   /* The frame shown before, relative to its display's top left, and
    * (signature, ordinal) -> its primitive */
   upscaler_prim_t *prev;
   unsigned num_prev;
   unsigned cap_prev;
   upscaler_table_t prev_table;
   /* (family, family ordinal) -> its primitive */
   upscaler_table_t prev_family;
   /* (signature, corners) -> its primitive */
   upscaler_table_t prev_exact;
   /* Each primitive's match in prev while a frame is resolved, and
    * which of prev are matched, so a family match takes none a
    * signature match has */
   const upscaler_prim_t **matches;
   unsigned cap_matches;
   unsigned char *claimed;
   unsigned cap_claimed;

   psx_upscaler_vertex_t *vertices;
   unsigned cap_vertices;
   psx_upscaler_draw_t *draws;
   unsigned cap_draws;

   uint64_t serial;
   uint64_t shown_serial;
   unsigned jitter_phase;
   unsigned jitter_index;
   bool active;
   bool reset_pending;
};

static bool upscaler_grow(void **ptr, unsigned *cap, unsigned need, size_t size)
{
   unsigned new_cap;
   void *p;
   if (need <= *cap)
      return true;
   new_cap = *cap ? *cap : 64;
   while (new_cap < need)
      new_cap *= 2;
   p = realloc(*ptr, (size_t)new_cap * size);
   if (!p)
      return false;
   *ptr = p;
   *cap = new_cap;
   return true;
}

static uint32_t upscaler_slot(uint64_t key, uint32_t mask)
{
   return (uint32_t)((key * U64(0x9e3779b9, 0x7f4a7c15)) >> 40) & mask;
}

static void upscaler_table_clear(upscaler_table_t *t)
{
   if (t->values)
      memset(t->values, 0, ((size_t)t->mask + 1) * sizeof(*t->values));
   t->count = 0;
}

static void upscaler_table_free(upscaler_table_t *t)
{
   free(t->keys);
   free(t->values);
   memset(t, 0, sizeof(*t));
}

/* The value stored under @key, inserting 0 for it if it is new;
 * NULL when out of memory */
static uint32_t *upscaler_table_get(upscaler_table_t *t, uint64_t key)
{
   uint32_t i;
   if (!t->values || (t->count + 1) * 2 > t->mask + 1)
   {
      uint32_t old_size = t->values ? t->mask + 1 : 0;
      uint32_t new_size = old_size ? old_size * 2 : 256;
      uint64_t *keys    = (uint64_t*)malloc(new_size * sizeof(*keys));
      uint32_t *values  = (uint32_t*)calloc(new_size, sizeof(*values));
      if (!keys || !values)
      {
         free(keys);
         free(values);
         return NULL;
      }
      for (i = 0; i < old_size; i++)
      {
         uint32_t j;
         if (!t->values[i])
            continue;
         j = upscaler_slot(t->keys[i], new_size - 1);
         while (values[j])
            j = (j + 1) & (new_size - 1);
         keys[j]   = t->keys[i];
         values[j] = t->values[i];
      }
      free(t->keys);
      free(t->values);
      t->keys   = keys;
      t->values = values;
      t->mask   = new_size - 1;
   }
   i = upscaler_slot(key, t->mask);
   while (t->values[i])
   {
      if (t->keys[i] == key)
         return &t->values[i];
      i = (i + 1) & t->mask;
   }
   t->keys[i] = key;
   t->count++;
   return &t->values[i];
}

static uint32_t upscaler_table_find(const upscaler_table_t *t, uint64_t key)
{
   uint32_t i;
   if (!t->values)
      return 0;
   i = upscaler_slot(key, t->mask);
   while (t->values[i])
   {
      if (t->keys[i] == key)
         return t->values[i];
      i = (i + 1) & t->mask;
   }
   return 0;
}

static uint64_t upscaler_prim_key(uint64_t signature, uint32_t ordinal)
{
   return signature ^ ((uint64_t)ordinal * U64(0xff51afd7, 0xed558ccd));
}

static uint64_t upscaler_family_key(uint64_t family, uint32_t ordinal)
{
   return (family ^ U64(0xc2b2ae3d, 0x27d4eb4f))
      ^ ((uint64_t)ordinal * U64(0xff51afd7, 0xed558ccd));
}

static uint64_t upscaler_corner_word(float v)
{
   uint32_t w;
   memcpy(&w, &v, sizeof(w));
   return w;
}

static uint64_t upscaler_exact_key(const upscaler_prim_t *p)
{
   unsigned i;
   uint64_t h = p->signature ^ p->count;
   for (i = 0; i < p->count; i++)
   {
      h = (h ^ upscaler_corner_word(p->x[i])) * U64(0x00000100, 0x000001b3);
      h = (h ^ upscaler_corner_word(p->y[i])) * U64(0x00000100, 0x000001b3);
   }
   return h;
}

static bool upscaler_fits(const psx_upscaler_range_t *range,
      unsigned w, unsigned h, unsigned factor)
{
   w *= factor;
   h *= factor;
   return w >= range->min_width && w <= range->max_width
      && h >= range->min_height && h <= range->max_height;
}

unsigned psx_upscaler_factor(const psx_upscaler_range_t *range,
      unsigned w, unsigned h, unsigned max_factor, unsigned current,
      unsigned current_w, unsigned current_h)
{
   unsigned best = 0;
   unsigned best_distance = 0;
   unsigned factor;

   if (current && !upscaler_fits(range, w, h, current))
      current = 0;
   if (current && w == current_w && h == current_h)
      return current;
   for (factor = 1; factor <= max_factor && factor <= 16; factor *= 2)
   {
      unsigned width = w * factor;
      unsigned distance;
      if (!upscaler_fits(range, w, h, factor))
         continue;
      distance = width > range->want_width ? width - range->want_width
                                           : range->want_width - width;
      if (!best || distance <= best_distance)
      {
         best          = factor;
         best_distance = distance;
      }
   }
   return best > current ? best : current;
}

uint64_t psx_upscaler_hash_init(void)
{
   return U64(0xcbf29ce4, 0x84222325);
}

uint64_t psx_upscaler_hash(uint64_t h, uint32_t word)
{
   unsigned i;
   for (i = 0; i < 4; i++)
   {
      h ^= (word >> (i * 8)) & 0xff;
      h *= U64(0x00000100, 0x000001b3);
   }
   return h;
}

/* Halton (2, 3), the sequence DLSS's own samples use */
static float upscaler_halton(unsigned index, unsigned base)
{
   float r = 0.0f;
   float f = 1.0f;
   for (index++; index > 0; index /= base)
   {
      f /= (float)base;
      r += f * (float)(index % base);
   }
   return r;
}

psx_upscaler_t *psx_upscaler_new(void)
{
   psx_upscaler_t *u = (psx_upscaler_t*)calloc(1, sizeof(*u));
   if (!u)
      return NULL;
   u->jitter_phase  = 8;
   u->reset_pending = true;
   u->area_x1       = 1023;
   u->area_y1       = 511;
   return u;
}

void psx_upscaler_free(psx_upscaler_t *u)
{
   unsigned i;
   if (!u)
      return;
   for (i = 0; i < UPSCALER_RECORDS; i++)
   {
      free(u->records[i].prims);
      upscaler_table_free(&u->records[i].ordinals);
      upscaler_table_free(&u->records[i].family_ordinals);
   }
   free(u->prev);
   free(u->matches);
   free(u->claimed);
   upscaler_table_free(&u->prev_table);
   upscaler_table_free(&u->prev_family);
   upscaler_table_free(&u->prev_exact);
   free(u->vertices);
   free(u->draws);
   free(u);
}

static void upscaler_forget(psx_upscaler_t *u)
{
   unsigned i;
   for (i = 0; i < UPSCALER_RECORDS; i++)
   {
      u->records[i].used      = false;
      u->records[i].num_prims = 0;
   }
   u->num_prev      = 0;
   upscaler_table_clear(&u->prev_table);
   upscaler_table_clear(&u->prev_family);
   upscaler_table_clear(&u->prev_exact);
   u->shown_serial  = 0;
   u->jitter_index  = 0;
   u->drawing       = NULL;
   u->reset_pending = true;
}

void psx_upscaler_configure(psx_upscaler_t *u, bool enable,
      unsigned jitter_phase, bool reset)
{
   if (enable != u->active || reset)
      upscaler_forget(u);
   u->active       = enable;
   u->jitter_phase = jitter_phase ? jitter_phase : 1;
}

bool psx_upscaler_is_active(const psx_upscaler_t *u)
{
   return u->active;
}

void psx_upscaler_set_draw_area(psx_upscaler_t *u, unsigned x0,
      unsigned y0, unsigned x1, unsigned y1)
{
   if (!u)
      return;
   u->area_x0 = x0;
   u->area_y0 = y0;
   u->area_x1 = x1;
   u->area_y1 = y1;
   u->drawing = NULL;
}

/* The record drawn into the drawing area, in progress; NULL when the
 * next primitive starts one */
static upscaler_record_t *upscaler_area_record(psx_upscaler_t *u)
{
   unsigned i;
   if (u->drawing)
      return u->drawing;
   for (i = 0; i < UPSCALER_RECORDS; i++)
   {
      upscaler_record_t *r = &u->records[i];
      if (r->used && !r->shown && r->x0 == u->area_x0 && r->y0 == u->area_y0
            && r->x1 == u->area_x1 && r->y1 == u->area_y1)
         return r;
   }
   return NULL;
}

/* The jitter the next primitive is drawn with: its record's, else the
 * one the record it starts takes */
static void upscaler_frame_jitter(psx_upscaler_t *u,
      float *jitter_x, float *jitter_y)
{
   unsigned j;
   const upscaler_record_t *rec = upscaler_area_record(u);
   if (rec)
   {
      *jitter_x = rec->jitter_x;
      *jitter_y = rec->jitter_y;
      return;
   }
   j         = u->jitter_index % u->jitter_phase;
   *jitter_x = upscaler_halton(j, 2) - 0.5f;
   *jitter_y = upscaler_halton(j, 3) - 0.5f;
}

void psx_upscaler_jitter(psx_upscaler_t *u, float *jitter_x,
      float *jitter_y)
{
   *jitter_x = 0.0f;
   *jitter_y = 0.0f;
   if (u && u->active)
      upscaler_frame_jitter(u, jitter_x, jitter_y);
}

/* The record of the frame being drawn into the drawing area; a frame
 * starts with its first primitive after the area was last shown */
static upscaler_record_t *upscaler_record_for_draw(psx_upscaler_t *u)
{
   unsigned i;
   upscaler_record_t *rec = NULL;

   if (u->drawing)
      return u->drawing;

   for (i = 0; i < UPSCALER_RECORDS && !rec; i++)
   {
      upscaler_record_t *r = &u->records[i];
      if (r->used && r->x0 == u->area_x0 && r->y0 == u->area_y0
            && r->x1 == u->area_x1 && r->y1 == u->area_y1)
         rec = r;
   }
   if (!rec)
   {
      /* an unused record, else the one shown longest ago */
      for (i = 0; i < UPSCALER_RECORDS; i++)
      {
         upscaler_record_t *r = &u->records[i];
         if (!r->used)
         {
            rec = r;
            break;
         }
         if (!rec || r->serial < rec->serial)
            rec = r;
      }
      rec->used = false;
   }

   if (!rec->used || rec->shown)
   {
      unsigned j     = u->jitter_index % u->jitter_phase;
      rec->jitter_x  = upscaler_halton(j, 2) - 0.5f;
      rec->jitter_y  = upscaler_halton(j, 3) - 0.5f;
      rec->num_prims = 0;
      upscaler_table_clear(&rec->ordinals);
      upscaler_table_clear(&rec->family_ordinals);
      rec->serial    = ++u->serial;
      rec->x0        = u->area_x0;
      rec->y0        = u->area_y0;
      rec->x1        = u->area_x1;
      rec->y1        = u->area_y1;
      rec->origin_x  = 0;
      rec->origin_y  = 0;
      rec->used      = true;
      rec->shown     = false;
   }

   u->drawing = rec;
   return rec;
}

void psx_upscaler_primitive(psx_upscaler_t *u,
      const psx_upscaler_point_t *points, unsigned count,
      unsigned flags, uint64_t signature, uint64_t family)
{
   upscaler_record_t *rec;
   upscaler_prim_t *p;
   uint32_t *ordinal;
   uint32_t *family_ordinal;
   unsigned i;

   if (!u || !u->active || count < 3 || count > 4)
      return;
   rec = upscaler_record_for_draw(u);
   if (rec->num_prims >= UPSCALER_MAX_PRIMS
         || !upscaler_grow((void**)&rec->prims, &rec->cap_prims,
            rec->num_prims + 1, sizeof(*rec->prims)))
      return;
   if (!(ordinal = upscaler_table_get(&rec->ordinals, signature)))
      return;
   if (!(family_ordinal = upscaler_table_get(&rec->family_ordinals, family)))
      return;
   p                 = &rec->prims[rec->num_prims++];
   p->signature      = signature;
   p->family         = family;
   p->ordinal        = (*ordinal)++;
   p->family_ordinal = (*family_ordinal)++;
   p->flags     = flags;
   p->count     = count;
   for (i = 0; i < count; i++)
   {
      p->x[i] = points[i].x;
      p->y[i] = points[i].y;
      p->w[i] = points[i].w;
   }
}

/* Of the primitives of the frame before drawn under @key (a signature
 * or a family) with an ordinal near @ordinal, the nearest to @p that
 * is not claimed; NULL when none is near enough */
static const upscaler_prim_t *upscaler_match_near(const psx_upscaler_t *u,
      const upscaler_prim_t *p, const upscaler_table_t *table,
      uint64_t key, uint32_t ordinal, bool by_family)
{
   int k;
   const upscaler_prim_t *best = NULL;
   float best_cost             = 0.0f;
   const float far2            = UPSCALER_FARTHEST * UPSCALER_FARTHEST;

   for (k = -UPSCALER_ORDINAL_WINDOW; k <= UPSCALER_ORDINAL_WINDOW; k++)
   {
      unsigned j;
      uint32_t index;
      const upscaler_prim_t *q;
      float cost = 0.0f;
      long o     = (long)ordinal + k;
      if (o < 0)
         continue;
      index = upscaler_table_find(table, by_family
            ? upscaler_family_key(key, (uint32_t)o)
            : upscaler_prim_key(key, (uint32_t)o));
      if (!index)
         continue;
      q = &u->prev[index - 1];
      if (by_family
            ? (q->family != key || q->family_ordinal != (uint32_t)o)
            : (q->signature != key || q->ordinal != (uint32_t)o))
         continue;
      if (q->count != p->count || (u->claimed && u->claimed[index - 1]))
         continue;
      for (j = 0; j < p->count; j++)
      {
         float dx = q->x[j] - p->x[j];
         float dy = q->y[j] - p->y[j];
         float d2 = dx * dx + dy * dy;
         if (d2 > far2)
            break;
         cost += d2;
      }
      /* (the same ordinal wins a tie) */
      if (j == p->count && (!best || cost < best_cost))
      {
         best      = q;
         best_cost = cost;
      }
   }
   return best;
}

/* The primitive of the frame before that @p (relative to its display)
 * is: the same one drawn in the same place, else of those drawn under
 * its signature with an ordinal near its own, the nearest; NULL when
 * none is near enough (upscaler_match_family then) */
static const upscaler_prim_t *upscaler_match(const psx_upscaler_t *u,
      const upscaler_prim_t *p)
{
   uint32_t exact = upscaler_table_find(&u->prev_exact, upscaler_exact_key(p));

   /* Where the same primitive was drawn the frame before, it stood
    * still, however many drawn under its signature came and went before
    * it */
   if (exact)
   {
      const upscaler_prim_t *q = &u->prev[exact - 1];
      if (     q->signature == p->signature && q->count == p->count
            && !memcmp(q->x, p->x, sizeof(p->x))
            && !memcmp(q->y, p->y, sizeof(p->y)))
         return q;
   }
   return upscaler_match_near(u, p, &u->prev_table, p->signature, p->ordinal, false);
}

/* For a primitive nothing matched: the one of its family (the same
 * texture, blending and so on, with other texture coordinates or
 * colour: an animated texture, a fade) with an ordinal near its own,
 * among those no primitive matched */
static const upscaler_prim_t *upscaler_match_family(const psx_upscaler_t *u,
      const upscaler_prim_t *p)
{
   return upscaler_match_near(u, p, &u->prev_family, p->family, p->family_ordinal, true);
}

/* The pass's triangles for @rec, each primitive relative to the
 * display's top left (@x, @y) */
static void upscaler_build(psx_upscaler_t *u, upscaler_record_t *rec,
      int x, int y, unsigned scale, bool still, psx_upscaler_geometry_t *out)
{
   static const unsigned char polygon[6] = { 0, 1, 2, 1, 2, 3 };
   static const unsigned char sprite[6]  = { 0, 1, 2, 1, 3, 2 };
   unsigned i, j;
   unsigned num_vertices = 0;
   unsigned num_draws    = 0;
   float s               = (float)scale;

   if (     !upscaler_grow((void**)&u->vertices, &u->cap_vertices,
               rec->num_prims * 6, sizeof(*u->vertices))
         || !upscaler_grow((void**)&u->draws, &u->cap_draws,
               rec->num_prims, sizeof(*u->draws))
         || !upscaler_grow((void**)&u->matches, &u->cap_matches,
               rec->num_prims, sizeof(*u->matches)))
      return;
   if (u->num_prev && !upscaler_grow((void**)&u->claimed, &u->cap_claimed,
            u->num_prev, sizeof(*u->claimed)))
      return;
   if (u->num_prev)
      memset(u->claimed, 0, u->num_prev);

   /* Each primitive relative to the display, matched by its signature
    * first; what that leaves is matched by family among what that left */
   for (i = 0; i < rec->num_prims; i++)
   {
      upscaler_prim_t *p = &rec->prims[i];
      const upscaler_prim_t *q;
      for (j = 0; j < p->count; j++)
      {
         p->x[j] -= (float)(x - rec->origin_x);
         p->y[j] -= (float)(y - rec->origin_y);
      }
      q = still ? NULL : upscaler_match(u, p);
      u->matches[i] = q;
      if (q)
         u->claimed[q - u->prev] = 1;
   }
   if (!still)
      for (i = 0; i < rec->num_prims; i++)
      {
         const upscaler_prim_t *q;
         if (u->matches[i])
            continue;
         q = upscaler_match_family(u, &rec->prims[i]);
         u->matches[i] = q;
         if (q)
            u->claimed[q - u->prev] = 1;
      }
   for (i = 0; i < rec->num_prims; i++)
   {
      upscaler_prim_t *p = &rec->prims[i];
      const upscaler_prim_t *q = u->matches[i];
      psx_upscaler_vertex_t *v = &u->vertices[num_vertices];
      const unsigned char *order = (p->flags & PSX_UPSCALER_SPRITE) ? sprite : polygon;
      unsigned count = p->count == 4 ? 6 : 3;
      unsigned flags = p->flags & PSX_UPSCALER_TRANSLUCENT;
      float depth    = 1.0f - (float)(i + 1) * UPSCALER_DEPTH_STEP;

      for (j = 0; j < count; j++)
      {
         unsigned c    = order[j];
         v[j].x        = p->x[c] * s + out->jitter_x;
         v[j].y        = p->y[c] * s + out->jitter_y;
         v[j].z        = depth < 0.0f ? 0.0f : depth;
         /* PGXP's depth where the primitive has it: a w of 1 is none */
         v[j].depth    = p->w[c] > 1.0f ? p->w[c] / (p->w[c] + UPSCALER_DEPTH_HALF) : v[j].z;
         v[j].motion_x = q ? (p->x[c] - q->x[c]) * s : 0.0f;
         v[j].motion_y = q ? (p->y[c] - q->y[c]) * s : 0.0f;
      }
      if (num_draws && u->draws[num_draws - 1].flags == flags)
         u->draws[num_draws - 1].count += count;
      else
      {
         u->draws[num_draws].first = num_vertices;
         u->draws[num_draws].count = count;
         u->draws[num_draws].flags = flags;
         num_draws++;
      }
      num_vertices += count;
   }
   rec->origin_x     = x;
   rec->origin_y     = y;
   out->vertices     = u->vertices;
   out->draws        = u->draws;
   out->num_vertices = num_vertices;
   out->num_draws    = num_draws;
}

/* Keeps @rec, built relative to its display's top left, as the frame
 * shown */
static void upscaler_keep(psx_upscaler_t *u, const upscaler_record_t *rec)
{
   unsigned i;
   u->num_prev = 0;
   upscaler_table_clear(&u->prev_table);
   upscaler_table_clear(&u->prev_family);
   upscaler_table_clear(&u->prev_exact);
   if (!upscaler_grow((void**)&u->prev, &u->cap_prev,
            rec->num_prims, sizeof(*u->prev)))
      return;
   for (i = 0; i < rec->num_prims; i++)
   {
      uint32_t *slot;
      u->prev[i] = rec->prims[i];
      if (!(slot = upscaler_table_get(&u->prev_table,
               upscaler_prim_key(u->prev[i].signature, u->prev[i].ordinal))))
         break;
      *slot = i + 1;
      if (!(slot = upscaler_table_get(&u->prev_family,
               upscaler_family_key(u->prev[i].family, u->prev[i].family_ordinal))))
         break;
      *slot = i + 1;
      if (!(slot = upscaler_table_get(&u->prev_exact, upscaler_exact_key(&u->prev[i]))))
         break;
      if (!*slot)
         *slot = i + 1;
   }
   u->num_prev = rec->num_prims;
}

void psx_upscaler_resolve(psx_upscaler_t *u, int x, int y,
      unsigned width, unsigned height, unsigned scale,
      psx_upscaler_geometry_t *out)
{
   unsigned i;
   upscaler_record_t *rec = NULL;

   memset(out, 0, sizeof(*out));
   if (!u)
      return;

   /* the frame drawn last into an area the display shows */
   for (i = 0; i < UPSCALER_RECORDS; i++)
   {
      upscaler_record_t *r = &u->records[i];
      if (     r->used
            && (int)r->x1 >= x && (int)r->x0 < x + (int)width
            && (int)r->y1 >= y && (int)r->y0 < y + (int)height
            && (!rec || r->serial > rec->serial))
         rec = r;
   }

   if (rec)
   {
      /* shown again, unchanged: it stands still */
      bool still    = rec->serial == u->shown_serial;
      out->jitter_x = rec->jitter_x;
      out->jitter_y = rec->jitter_y;
      upscaler_build(u, rec, x, y, scale, still, out);
      if (!still)
      {
         upscaler_keep(u, rec);
         u->shown_serial = rec->serial;
         u->jitter_index++;
      }
      rec->shown = true;
   }

   u->drawing       = NULL;
   out->reset       = u->reset_pending;
   u->reset_pending = false;
}
