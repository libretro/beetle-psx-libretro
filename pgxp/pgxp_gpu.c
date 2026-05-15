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
#include "pgxp_main.h"
#include "pgxp_mem.h"
#include "pgxp_value.h"

#include <stdlib.h>
#include <assert.h>

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
 * ============================================================ */
typedef struct
{
	float   x;
	float   y;
	float   z;
	uint8_t gFlags;	/* 0: empty.  1: valid this session.  5 was used by
	                 * the (currently disabled) "ambiguous" branch. */
	/* 3 bytes of tail padding bring this to 16 bytes naturally. */
} PGXP_cache_entry;

const uint32_t mode_init = 0;
const uint32_t mode_write = 1;
const uint32_t mode_read = 2;
const uint32_t mode_fail = 3;

#define VERTEX_CACHE_DIM	(0x800 * 2)
#define VERTEX_CACHE_SIZE	(VERTEX_CACHE_DIM * VERTEX_CACHE_DIM)

static PGXP_cache_entry *vertexCache = NULL;

uint32_t cacheMode = 0;

/* Allocate the vertex cache on first use.  Returns 1 on success, 0 on
 * allocation failure (in which case the cache stays NULL and callers
 * fall back to mode_fail). */
static int VertexCacheEnsureAllocated(void)
{
	if (vertexCache)
		return 1;
	vertexCache = (PGXP_cache_entry*)calloc(VERTEX_CACHE_SIZE, sizeof(PGXP_cache_entry));
	return vertexCache ? 1 : 0;
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

		/* First vertex of write session (frame?) */
		cacheMode = mode_write;
	}

	if (sx >= -0x800 && sx <= 0x7ff &&
		sy >= -0x800 && sy <= 0x7ff)
	{
		pOldVertex = &vertexCache[(sy + 0x800) * VERTEX_CACHE_DIM + (sx + 0x800)];

		/* Write vertex into cache */
		pOldVertex->x      = pNewVertex->x;
		pOldVertex->y      = pNewVertex->y;
		pOldVertex->z      = pNewVertex->z;
		pOldVertex->gFlags = 1;
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
		/* Return pointer to cache entry */
		return &vertexCache[(sy + 0x800) * VERTEX_CACHE_DIM + (sx + 0x800)];
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
		 * struct (just x/y/z/gFlags) than the FIFO/CB, so we use a
		 * separate local rather than aliasing `vert`. */
		PGXP_cache_entry* cache_vert = PGXP_GetCachedVertex(psxX, psxY);
		if ((cache_vert) && (cache_vert->gFlags == 1))
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
