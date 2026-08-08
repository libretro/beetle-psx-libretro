/***************************************************************************
*   Copyright (C) 2016 by iCatButler                                      *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
***************************************************************************/

/**************************************************************************
*	pgxp_gpu.c
*	PGXP - Parallel/Precision Geometry Xform Pipeline
*
*	Created on: 25 Mar 2016
*      Author: iCatButler
***************************************************************************/
#include "pgxp_gpu.h"
#include "pgxp_gte.h"
#include "pgxp_main.h"
#include "pgxp_mem.h"
#include "pgxp_value.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include <libretro.h>

extern retro_log_printf_t log_cb;

/* ============================================================
 * Partial FIFO and Command Buffer implementation
 * ============================================================ */

PGXP_value FIFO[32];
PGXP_value CB[16];

void PGXP_WriteFIFO(PGXP_value* pV, uint32_t pos)
{
	assert(pos < 32);
	FIFO[pos] = *pV;
}

PGXP_value* PGXP_ReadFIFO(uint32_t pos)
{
	assert(pos < 32);
	return &FIFO[pos];
}

void PGXP_WriteCB(PGXP_value* pV, uint32_t pos)
{
	assert(pos < 16);
	CB[pos] = *pV;
}

PGXP_value* PGXP_ReadCB(uint32_t pos)
{
	assert(pos < 16);
	return &CB[pos];
}


uint32_t PGXP_tDebug = 0;
/* ============================================================
 * Blade_Arma's Vertex Cache (CatBlade?)
 *
 * vertexCache is a 4096x4096 grid indexed by the 11-bit signed
 * vertex (sx, sy) screen coordinates.  At 28 bytes per PGXP_value
 * that grid would be 448 MB if we stored full PGXP_values; the
 * read path however only uses x, y, z, and a tiny session-validity
 * flag, so the cache is stored as a packed 16-byte
 * PGXP_cache_entry instead - shaving off ~192 MB of heap when the
 * cache is allocated.  The full PGXP_value's count / value / flags
 * / lFlags / hFlags fields are not relevant for cache reads; they
 * matter only on the FIFO/CB side, which uses PGXP_value directly.
 *
 * The buffer is allocated lazily via calloc() the first time it's
 * actually needed and only when the PGXP_VERTEX_CACHE mode bit is
 * set.  Most users do not enable the vertex cache, and previously
 * the buffer was static BSS that still consumed 448 MB of address
 * space (and got faulted in on the first memset) regardless of
 * whether the feature was on.
 *
 * The cache is freed in PGXP_Shutdown() (called from retro_deinit)
 * so we don't leak across libretro dlopen/dlclose cycles.
 *
 * False positives.  This is the defect the core option warns about
 * ("false positives when querying the cache may produce graphical
 * glitches. It is currently recommended to leave this option
 * disabled"), and it had two distinct sources, both of which returned
 * a confidently wrong vertex rather than declining:
 *
 *   1. Staleness.  The old gFlags was set to 1 on write and never
 *      cleared by anything - not per frame, not per session, not on
 *      savestate load.  "Valid this session" was aspirational; an
 *      entry written hundreds of frames ago stayed valid forever, so
 *      a lookup could be answered with the precise position of
 *      geometry that had long since moved or been discarded.
 *   2. Collision.  The key is the *integer* screen position, so two
 *      distinct vertices in the same frame that project to the same
 *      pixel share a cell.  The second write silently overwrote the
 *      first and the reader had no way to tell.  The gFlags == 5
 *      "ambiguous" encoding the original author left behind was the
 *      intended guard for this, but the branch that would have set it
 *      was disabled, so nothing ever produced that value.
 *
 * Both are now refused rather than answered.  A refusal costs nothing:
 * PGXP_GetVertex simply falls through to the native PSX vertex, which
 * is precisely what the (recommended, default) cache-disabled setting
 * yields.  Over-refusal therefore degrades to the current default,
 * while under-refusal is the glitch source - so the policy here is
 * deliberately conservative.
 *
 * Determinism note: the generation counter is render-side state and is
 * not serialized, exactly as the cache contents were not serialized
 * before.  This is not a regression, and it is arguably a small
 * improvement: after a savestate load the game's next transform batch
 * opens a new generation, which retires every pre-load entry instead
 * of leaving it live.
 * ============================================================ */
typedef struct
{
	float    x;
	float    y;
	float    z;
	/* Validity tag, replacing the old uint8_t gFlags plus its 3 bytes of
	 * tail padding.  The struct stays 16 bytes, so the 256 MB allocation
	 * does not grow.
	 *
	 *   0                     : empty, never written
	 *   bit 0                 : ambiguous - two different vertices landed
	 *                           on this cell in the same write session
	 *   bits 1..31            : generation in which the cell was written
	 *
	 * Generations start at 1, so a zeroed (calloc'd) buffer reads as
	 * entirely empty without a separate init pass. */
	uint32_t tag;
} PGXP_cache_entry;

#define PGXP_TAG_MAKE(gen)   (((uint32_t)(gen) << 1))
#define PGXP_TAG_GEN(tag)    ((tag) >> 1)
#define PGXP_TAG_AMBIGUOUS   (1u)

const uint32_t mode_init = 0;
const uint32_t mode_write = 1;
const uint32_t mode_read = 2;
const uint32_t mode_fail = 3;

#define VERTEX_CACHE_DIM	(0x800 * 2)
#define VERTEX_CACHE_SIZE	(VERTEX_CACHE_DIM * VERTEX_CACHE_DIM)

static PGXP_cache_entry *vertexCache = NULL;

uint32_t cacheMode = 0;

/* Current write generation.  Bumped when a write session opens (i.e. on
 * every read -> write transition), which is what retires the previous
 * session's entries.
 *
 * It is only ever reset to 1 alongside a fresh calloc, never on its own:
 * resetting the counter while the buffer still holds tags from earlier
 * generations would make those stale entries validate again, which is
 * exactly the bug being fixed.  31 bits at one bump per frame is over a
 * year of continuous play, so the wrap is not reachable in practice; the
 * saturating guard below makes it a permanent refusal rather than a
 * wrap-to-collision if it ever were. */
static uint32_t cacheGen = 1;

/* Instrumentation, mirroring the PGXP_GetColorStats idiom.  Indices:
 *   0 writes                    3 read attempts
 *   1 writes marked ambiguous   4 read hits
 *   2 writes retiring an older  5 reads refused: stale generation
 *     generation                6 reads refused: ambiguous cell
 *
 * Behind PGXP_DIAG and compiled out otherwise; see pgxp_gte.h.  These
 * shipped unconditional in c478571d, which was wrong for the same reason
 * the transform counters were: [0] and [3] are per-vertex on a live
 * feature path, and a counter nobody reads in a shipping build has no
 * business costing anything there. */
#if PGXP_DIAG
static uint32_t vcache_stats[7];
#define VCACHE_STAT(i) (vcache_stats[i]++)

#else
#define VCACHE_STAT(i) ((void)0)
#endif

void PGXP_GetVertexCacheStats(uint32_t stats[7])
{
	unsigned i;
	for (i = 0; i < 7; i++)
#if PGXP_DIAG
		stats[i] = vcache_stats[i];
#else
		/* Callable in every build so consumers need no ifdef of their
		 * own; a non-PGXP_DIAG build simply has nothing to report. */
		stats[i] = 0;
#endif
}

/* Allocate the vertex cache on first use.  Returns 1 on success, 0 on
 * allocation failure (in which case the cache stays NULL and callers
 * fall back to mode_fail). */
static int VertexCacheEnsureAllocated(void)
{
	if (vertexCache)
		return 1;
	vertexCache = (PGXP_cache_entry*)calloc(VERTEX_CACHE_SIZE, sizeof(PGXP_cache_entry));
	if (!vertexCache)
		return 0;

	/* The buffer is zeroed, so every tag reads as empty and restarting
	 * the generation counter here cannot resurrect anything. */
	cacheGen = 1;
#if PGXP_DIAG
	memset(vcache_stats, 0, sizeof(vcache_stats));
#endif
	return 1;
}

/* Free the heap-allocated vertex cache.  Safe to call when the
 * cache was never allocated (e.g. PGXP_VERTEX_CACHE was never on).
 * Called from PGXP_Shutdown (retro_deinit) and from the mode-toggle
 * path in pgxp_main.c when the vertex cache bit is cleared. */
void PGXP_FreeVertexCache(void)
{
	if (vertexCache)
	{
		free(vertexCache);
		vertexCache = NULL;
	}
	cacheMode = mode_init;
}

void PGXP_CacheVertex(int16_t sx, int16_t sy, const PGXP_value* _pVertex)
{
	const PGXP_value*	pNewVertex = (const PGXP_value*)_pVertex;
	PGXP_cache_entry*	pOldVertex = NULL;

	if (!pNewVertex)
	{
		cacheMode = mode_fail;
		return;
	}

	if (cacheMode != mode_write)
	{
		/* Make sure the cache buffer is allocated before we start
		 * writing vertices into it.  This covers two cases:
		 *
		 *   1. First-ever use after PGXP_Init (cacheMode == mode_init,
		 *      vertexCache == NULL).
		 *   2. Re-enable after the user toggled PGXP_VERTEX_CACHE off
		 *      and then on again - PGXP_FreeVertexCache freed the
		 *      buffer, and during the off interval pgxp_gte.c was
		 *      calling us with a NULL vertex which set cacheMode to
		 *      mode_fail.  Now that the user toggled it back on we
		 *      get a real vertex again, and we need to re-allocate.
		 *
		 * If allocation fails (e.g. on an embedded target with tight
		 * memory) cacheMode goes to mode_fail and we bail out
		 * cleanly. */
		if (!VertexCacheEnsureAllocated())
		{
			cacheMode = mode_fail;
			return;
		}

		/* First vertex of write session (frame?).
		 *
		 * Opening a generation here is what retires the previous
		 * session's entries.  Note the asymmetry with a naive
		 * "clear the cache each frame": a game that transforms its
		 * geometry once and then re-draws the same display list for
		 * many frames issues no writes in those frames, so no
		 * generation opens and its entries stay legitimately live.
		 * Only a fresh transform batch retires the old one. */
		if (cacheGen < 0x7FFFFFFFu)
			cacheGen++;
		cacheMode = mode_write;
	}

	if (sx >= -0x800 && sx <= 0x7ff &&
		sy >= -0x800 && sy <= 0x7ff)
	{
		uint32_t tag = PGXP_TAG_MAKE(cacheGen);

		pOldVertex = &vertexCache[(sy + 0x800) * VERTEX_CACHE_DIM + (sx + 0x800)];

		VCACHE_STAT(0);

		if (PGXP_TAG_GEN(pOldVertex->tag) == cacheGen)
		{
			/* Something already claimed this cell this generation.
			 * If it is bit-identical the game simply transformed the
			 * same vertex twice (RTPT overlap, a redundant RTPS) and
			 * there is nothing ambiguous about it - the transform is
			 * deterministic, so equal inputs give equal bits and an
			 * exact compare is the right test.  Otherwise two
			 * distinct vertices project to the same pixel and the
			 * cell can no longer answer for either of them. */
			if (pOldVertex->x != pNewVertex->x ||
			    pOldVertex->y != pNewVertex->y ||
			    pOldVertex->z != pNewVertex->z)
			{
				pOldVertex->tag |= PGXP_TAG_AMBIGUOUS;
				VCACHE_STAT(1);
				return;
			}
			/* Identical rewrite: nothing to do, and in particular do
			 * not clear an ambiguous mark already set this
			 * generation. */
			return;
		}

		if (pOldVertex->tag != 0)
			VCACHE_STAT(2);

		/* Write vertex into cache */
		pOldVertex->x   = pNewVertex->x;
		pOldVertex->y   = pNewVertex->y;
		pOldVertex->z   = pNewVertex->z;
		pOldVertex->tag = tag;
	}
}

static PGXP_cache_entry* PGXP_GetCachedVertex(int16_t sx, int16_t sy)
{
	if (cacheMode != mode_read)
	{
		if (cacheMode == mode_fail)
			return NULL;

		/* Initialise cache on first use */
		if (cacheMode == mode_init)
		{
			if (!VertexCacheEnsureAllocated())
			{
				cacheMode = mode_fail;
				return NULL;
			}
		}

		/* First vertex of read session (frame?) */
		cacheMode = mode_read;
	}

	if (sx >= -0x800 && sx <= 0x7ff &&
		sy >= -0x800 && sy <= 0x7ff)
	{
		PGXP_cache_entry *e = &vertexCache[(sy + 0x800) * VERTEX_CACHE_DIM + (sx + 0x800)];

		VCACHE_STAT(3);

		/* The accept decision lives here rather than at the call site.
		 * Previously the caller had to remember to test gFlags == 1
		 * itself, which is the kind of check that gets dropped when a
		 * second consumer is added; returning NULL for anything we
		 * will not stand behind makes the refusal unskippable. */
		if (e->tag == 0)
			return NULL;                       /* never written */

		if (PGXP_TAG_GEN(e->tag) != cacheGen)
		{
			VCACHE_STAT(5);                 /* retired generation */
			return NULL;
		}

		if (e->tag & PGXP_TAG_AMBIGUOUS)
		{
			VCACHE_STAT(6);                 /* pixel claimed twice */
			return NULL;
		}

		VCACHE_STAT(4);
		return e;
	}

	return NULL;
}


/* ============================================================
 * PGXP Implementation
 * ============================================================ */

const uint32_t primStrideTable[] = { 1, 2, 1, 2, 2, 3, 2, 3, 0 };
const uint32_t primCountTable[] = { 3, 3, 4, 4, 3, 3, 4, 4, 0 };

PGXP_value*	PGXP_Mem = NULL;	/* pointer to parallel memory */
uint32_t	currentAddr = 0;	/* address of current DMA */

uint32_t	numVertices = 0;	/* iCB: Used for glVertex3fv fix */
uint32_t	vertexIdx = 0;

/* Set current DMA address and pointer to parallel memory */
void GPUpgxpMemory(uint32_t addr, uint8_t* pVRAM)
{
	PGXP_Mem = (PGXP_value*)(pVRAM);
	currentAddr = addr;
}

/* Set current DMA address */
void PGXP_SetAddress(uint32_t addr)
{
	currentAddr = addr;
}

/* Get single parallel vertex value */
/* Same treatment as the vertex cache counters above: measurement only,
 * so it is compiled out of a shipping build.  Pre-dates PGXP_DIAG; folded
 * in here so the tree has one convention for instrumentation rather than
 * two.  tools/pgxp_color builds with -DPGXP_DIAG=1, which is what its
 * range harness needs to read these back. */
#if PGXP_DIAG
static uint32_t color_stats[4]; /* attempts, hits, value miss, quant miss */
#define COLOR_STAT(i) (color_stats[i]++)
#else
#define COLOR_STAT(i) ((void)0)
#endif

/* Over-range measurement.
 *
 * The hit rate answers "can the precise colour be recovered". It does not
 * answer "is there anything above white to recover", and for the HDR
 * renderer slice that second question is the one that decides the feature:
 * a high hit rate over content whose lighting never exceeds the Color FIFO
 * ceiling buys exactly nothing, because every recovered value requantizes to
 * the byte the architectural path already had.
 *
 * Only accepted words are counted -- a shadow that was refused cannot be
 * used no matter how bright it is. Buckets are on the peak channel relative
 * to the 255 ceiling, so bucket 0 is a colour that would clip slightly and
 * bucket 3 is one the GTE computed at more than twice white. */
static uint32_t color_over;          /* accepted words with any channel > 255 */
static uint32_t color_over_bucket[4];/* (1,1.25] (1.25,1.5] (1.5,2] (2,inf) */
static float    color_peak;          /* largest channel seen, 8-bit scale */

static void color_note_range(float r, float g, float b)
{
   float peak = r;
   float ratio;

   if (g > peak) peak = g;
   if (b > peak) peak = b;

   if (peak > color_peak)
      color_peak = peak;

   if (!(peak > 255.0f))
      return;

   color_over++;
   ratio = peak / 255.0f;
   if (ratio <= 1.25f)      color_over_bucket[0]++;
   else if (ratio <= 1.5f)  color_over_bucket[1]++;
   else if (ratio <= 2.0f)  color_over_bucket[2]++;
   else                     color_over_bucket[3]++;
}

/* Requantize a shadow channel exactly as gte.c's MAC_to_RGB_FIFO did:
 * MACn >> 4 is an arithmetic shift (floor), then Lm_C saturates to
 * 0..0xFF.  The shadow holds MACn/16.0f, so floor+saturate here inverts
 * it bit-exactly wherever the result is not saturated (the float is
 * exact below 2^24, see the push site), and saturation absorbs any
 * conversion rounding above that.  Returns -1 for non-finite input so
 * a corrupt shadow can never match. */
static int32_t pgxp_requant8(float f)
{
	int32_t q;
	if (!(f >= -2147483648.0f && f < 2147483648.0f))
		return -1;
	q = (int32_t)floor((double)f);
	if (q < 0)
		return 0;
	if (q > 0xFF)
		return 0xFF;
	return q;
}

int PGXP_GetColor(const uint32_t offset, const uint32_t* addr, float* out_rgb)
{
	PGXP_value* col = PGXP_ReadCB(offset);
	uint32_t word   = *addr;
	int ok          = 0;

	COLOR_STAT(0);

	if (col && ((col->flags & VALID_012) == VALID_012) &&
	    (((col->value ^ word) & 0x00FFFFFFu) == 0))
	{
		/* Byte 3 is the GP0 command code and is excluded from the value
		 * match: the swc2-$22 idiom carries it through the GTE CD field
		 * (full-word match), while the mfc2+or idiom rewrites it on the
		 * CPU.  The requantization test below is the actual guarantee
		 * either way. */
		if (pgxp_requant8(col->x) == (int32_t)(word & 0xFF) &&
		    pgxp_requant8(col->y) == (int32_t)((word >> 8) & 0xFF) &&
		    pgxp_requant8(col->z) == (int32_t)((word >> 16) & 0xFF))
		{
			if (out_rgb)
			{
				out_rgb[0] = col->x;
				out_rgb[1] = col->y;
				out_rgb[2] = col->z;
			}
			/* Recorded whether or not the caller wanted the value: the
			 * measurement slice passes NULL and this is the number it
			 * exists to collect. */
			color_note_range(col->x, col->y, col->z);
			COLOR_STAT(1);
			ok = 1;
		}
		else
			COLOR_STAT(3);
	}
	else
		COLOR_STAT(2);

	/* The measurement scaffolding that used to print a cumulative
	 * [PGXP color] line here every ~1M colour words is gone: the
	 * renderer slice it was the go/no-go for shipped long ago, and the
	 * durable API is PGXP_GetColorStats / PGXP_GetColorRangeStats,
	 * which the offline range harness still consumes. */

	return ok;
}

int PGXP_GetFog(const uint32_t offset, const uint32_t* addr,
		float out_pre[3], float out_fc[3], float* out_t)
{
	PGXP_value* col = PGXP_ReadCB(offset);
	float       rgb[3];

	/* The colour accept is the safety gate; run it first (it also keeps the
	 * hit statistics honest -- a fog probe is a colour probe). */
	if (!PGXP_GetColor(offset, addr, rgb))
		return 0;

	if (!col)
		return 0;

	if (!PGXP_GTE_GetFogByCount(col->count, out_pre, out_fc, out_t))
		return 0;

	return 1;
}

void PGXP_GetColorRangeStats(uint32_t *over, uint32_t buckets[4], float *peak)
{
	if (over)
		*over = color_over;
	if (buckets)
	{
		buckets[0] = color_over_bucket[0];
		buckets[1] = color_over_bucket[1];
		buckets[2] = color_over_bucket[2];
		buckets[3] = color_over_bucket[3];
	}
	if (peak)
		*peak = color_peak;
}

void PGXP_GetColorStats(uint32_t stats[4])
{
#if PGXP_DIAG
	stats[0] = color_stats[0];
	stats[1] = color_stats[1];
	stats[2] = color_stats[2];
	stats[3] = color_stats[3];
#else
	/* Kept callable so consumers need no ifdef of their own; a
	 * non-PGXP_DIAG build simply has nothing to report. */
	stats[0] = stats[1] = stats[2] = stats[3] = 0;
#endif
}

int PGXP_GetVertex(const uint32_t offset, const uint32_t* addr, OGLVertex* pOutput, int xOffs, int yOffs)
{
	PGXP_value* vert = PGXP_ReadCB(offset);          /* pointer to vertex */

	/* The GP0 vertex word packs sy in the high 16 bits and sx in
	 * the low 16 bits.  Unpack with shifts on the u32 rather than
	 * aliasing addr through `short*` (which is strict-aliasing UB
	 * since addr's effective type is `unsigned int`). */
	uint32_t psxWord = *addr;
	int16_t psxX = (int16_t)(psxWord & 0xFFFF);
	int16_t psxY = (int16_t)(psxWord >> 16);

	if (vert && ((vert->flags & VALID_01) == VALID_01) && (vert->value == psxWord))
	{
		/* There is a value here with valid X and Y coordinates */
		pOutput->x = (vert->x + xOffs);
		pOutput->y = (vert->y + yOffs);
		pOutput->z = 0.95f;
		pOutput->w = vert->z;
		pOutput->valid_w = 1;

		if ((vert->flags & VALID_2) != VALID_2)
		{
			/* This value does not have a valid W coordinate */
			pOutput->valid_w = 0;
		}
	}
	else
	{
		/* Look in cache for valid vertex.  The cache holds a smaller
		 * struct (just x/y/z/tag) than the FIFO/CB, so we use a
		 * separate local rather than aliasing `vert`.  A non-NULL
		 * return is already fully validated - current generation,
		 * unambiguous - so there is no flag test to repeat here. */
		PGXP_cache_entry* cache_vert = PGXP_GetCachedVertex(psxX, psxY);
		if (cache_vert)
		{
			/* a value is found, it is from the current session and is unambiguous (there was only one value recorded at that position) */
			pOutput->x = cache_vert->x + xOffs;
			pOutput->y = cache_vert->y + yOffs;
			pOutput->z = 0.95f;
			pOutput->w = cache_vert->z;
			pOutput->valid_w = 0;	/* iCB: Getting the wrong w component causes too great an error when using perspective correction so disable it */
		}
		else
		{
			/* no valid value can be found anywhere, use the native PSX
			 * data.  The original `((psxData[0] + xOffs) << 5) >> 5`
			 * was a clamp-to-11-bit-signed-and-sign-extend; the
			 * left-shift was UB on signed when the value crossed the
			 * sign bit (compiler with -fwrapv tolerates it, but the
			 * left-shift exception isn't covered by that flag).
			 * Replace with an explicit mask-and-sign-extend. */
			int32_t sx = psxX + xOffs;
			int32_t sy = psxY + yOffs;
			sx &= 0x07FFFFFF; if (sx & 0x04000000) sx |= ~0x07FFFFFF;
			sy &= 0x07FFFFFF; if (sy & 0x04000000) sy |= ~0x07FFFFFF;
			pOutput->x = (float)sx;
			pOutput->y = (float)sy;
			pOutput->valid_w = 0;
		}
	}

	/* clear upper 5 bits in x and y - same 27-bit signed clamp as
	 * above, but applied in the 16.16 fixed-point domain (i.e. 11
	 * integer bits with 16 fractional) so we keep sub-pixel
	 * precision from the PGXP path.  Original code did
	 * `(int)x << 5 >> 5` which is the same UB.  Mask-and-sign-
	 * extend via unsigned avoids it.  The (int32_t)float cast can
	 * still be UB if the float is out of int32 range; for our
	 * normal inputs (vert->x + xOffs in -2048..2046 -> * 65536 is
	 * < 2^28, well within int32) it's fine. */
	{
		float x = pOutput->x * (1 << 16);
		float y = pOutput->y * (1 << 16);
		int32_t ix = (int32_t)x;
		int32_t iy = (int32_t)y;
		ix &= 0x07FFFFFF; if (ix & 0x04000000) ix |= ~0x07FFFFFF;
		iy &= 0x07FFFFFF; if (iy & 0x04000000) iy |= ~0x07FFFFFF;
		pOutput->x = (float)ix / (1 << 16);
		pOutput->y = (float)iy / (1 << 16);
	}

	return 1;
}
