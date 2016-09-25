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
#include "pgxp_mem.h"
#include "pgxp_value.h"

#include <math.h>
#include <assert.h>

/////////////////////////////////
//// Partial FIFO and Command Buffer implementation
/////////////////////////////////

PGXP_value FIFO[32];
PGXP_value CB[16];

void PGXP_WriteFIFO(PGXP_value* pV, u32 pos)
{
	assert(pos < 32);
	FIFO[pos] = *pV;
}

PGXP_value* PGXP_ReadFIFO(u32 pos)
{
	assert(pos < 32);
	return &FIFO[pos];
}

void PGXP_WriteCB(PGXP_value* pV, u32 pos)
{
	assert(pos < 16);
	CB[pos] = *pV;
}

PGXP_value* PGXP_ReadCB(u32 pos)
{
	assert(pos < 16);
	return &CB[pos];
}


unsigned int PGXP_tDebug = 0;
/////////////////////////////////
//// Blade_Arma's Vertex Cache (CatBlade?)
/////////////////////////////////
const unsigned int mode_init = 0;
const unsigned int mode_write = 1;
const unsigned int mode_read = 2;
const unsigned int mode_fail = 3;

PGXP_value vertexCache[0x800 * 2][0x800 * 2];

unsigned int baseID = 0;
unsigned int lastID = 0;
unsigned int cacheMode = 0;

unsigned int IsSessionID(unsigned int vertID)
{
	// No wrapping
	if (lastID >= baseID)
		return (vertID >= baseID);

	// If vertID is >= baseID it is pre-wrap and in session
	if (vertID >= baseID)
		return 1;

	// vertID is < baseID, If it is <= lastID it is post-wrap and in session
	if (vertID <= lastID)
		return 1;

	return 0;
}

void PGXP_CacheVertex(short sx, short sy, const PGXP_value* _pVertex)
{
	const PGXP_value*	pNewVertex = (const PGXP_value*)_pVertex;
	PGXP_value*		pOldVertex = NULL;

	if (!pNewVertex)
	{
		cacheMode = mode_fail;
		return;
	}

	//if (bGteAccuracy)
	{
		if (cacheMode != mode_write)
		{
			// Initialise cache on first use
			if (cacheMode == mode_init)
				memset(vertexCache, 0x00, sizeof(vertexCache));

			// First vertex of write session (frame?)
			cacheMode = mode_write;
			baseID = pNewVertex->count;
		}

		lastID = pNewVertex->count;

		if (sx >= -0x800 && sx <= 0x7ff &&
			sy >= -0x800 && sy <= 0x7ff)
		{
			pOldVertex = &vertexCache[sy + 0x800][sx + 0x800];

			// To avoid ambiguity there can only be one valid entry per-session
			if (0)//(IsSessionID(pOldVertex->count) && (pOldVertex->value == pNewVertex->value))
			{
				// check to ensure this isn't identical
				if ((fabsf(pOldVertex->x - pNewVertex->x) > 0.1f) ||
					(fabsf(pOldVertex->y - pNewVertex->y) > 0.1f) ||
					(fabsf(pOldVertex->z - pNewVertex->z) > 0.1f))
				{
					*pOldVertex = *pNewVertex;
					pOldVertex->gFlags = 5;
					return;
				}
			}

			// Write vertex into cache
			*pOldVertex = *pNewVertex;
			pOldVertex->gFlags = 1;
		}
	}
}

PGXP_value* PGXP_GetCachedVertex(short sx, short sy)
{
	//if (bGteAccuracy)
	{
		if (cacheMode != mode_read)
		{
			if (cacheMode == mode_fail)
				return NULL;

			// Initialise cache on first use
			if (cacheMode == mode_init)
				memset(vertexCache, 0x00, sizeof(vertexCache));

			// First vertex of read session (frame?)
			cacheMode = mode_read;
		}

		if (sx >= -0x800 && sx <= 0x7ff &&
			sy >= -0x800 && sy <= 0x7ff)
		{
			// Return pointer to cache entry
			return &vertexCache[sy + 0x800][sx + 0x800];
		}
	}

	return NULL;
}


/////////////////////////////////
//// PGXP Implementation
/////////////////////////////////

const unsigned int primStrideTable[] = { 1, 2, 1, 2, 2, 3, 2, 3, 0 };
const unsigned int primCountTable[] = { 3, 3, 4, 4, 3, 3, 4, 4, 0 };

PGXP_value*	PGXP_Mem = NULL;	// pointer to parallel memory
unsigned int	currentAddr = 0;	// address of current DMA

unsigned int	numVertices = 0;	// iCB: Used for glVertex3fv fix
unsigned int	vertexIdx = 0;

// Set current DMA address and pointer to parallel memory
void GPUpgxpMemory(unsigned int addr, unsigned char* pVRAM)
{
	PGXP_Mem = (PGXP_value*)(pVRAM);
	currentAddr = addr;
}

// Set current DMA address
void PGXP_SetAddress(unsigned int addr)
{
	currentAddr = addr;
}

//// Wrap glVertex3fv/glVertex4fv
//void PGXP_glVertexfv(GLfloat* pV)
//{
//	// If there are PGXP vertices expected
//	if (1)//(vertexIdx < numVertices)
//	{
//	float temp[4];
//	memcpy(temp, pV, sizeof(float) * 4);
//
//		//pre-multiply each element by w (to negate perspective divide)
//		for (unsigned int i = 0; i < 3; i++)
//			temp[i] *= temp[3];
//
//		//pass complete vertex to OpenGL
//		glVertex4fv(temp);
//		vertexIdx++;
//
//		//pV[3] = 1.f;
//	}
//	else
//	{
//		glVertex3fv(pV);
//	}
//}

#ifndef min
#   define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// Get parallel vertex values
int PGXP_GetVertices(const unsigned int* addr, void* pOutput, int xOffs, int yOffs)
{
	PGXP_Mem = (PGXP_value*)PGXP_GetMem();

	unsigned int	primCmd		= ((*addr >> 24) & 0xff);			// primitive command
	unsigned int	primIdx		= min((primCmd - 0x20) >> 2, 8);	// index to primitive lookup
	OGLVertex*		pVertex		= (OGLVertex*)pOutput;				// pointer to output vertices
	unsigned int	stride		= primStrideTable[primIdx];			// stride between vertices
	unsigned int	count		= primCountTable[primIdx];			// number of vertices
	PGXP_value*	primStart	= NULL;								// pointer to first vertex
	char			invalidVert	= 0;								// Number of vertices without valid PGXP values

	short*			pPrimData	= ((short*)addr) + 2;				// primitive data for cache lookups
	PGXP_value*	pCacheVert	= NULL;

	// Reset vertex count
	numVertices = count;
	vertexIdx = 0;

	// if PGXP is enabled
	if (PGXP_Mem != NULL)
	{
		// Offset to start of primitive
		primStart = &PGXP_Mem[currentAddr + 1];

		// Find any invalid vertices
		for (unsigned i = 0; i < count; ++i)
		{
			if (!((primStart[stride * i].flags & VALID_012) == VALID_012))
				invalidVert++;
		}
	}
	else
		invalidVert = count;

	for (unsigned i = 0; i < count; ++i)
	{
		if (primStart && ((primStart[stride * i].flags & VALID_01) == VALID_01) && (primStart[stride * i].value == *(unsigned int*)(&pPrimData[stride * i * 2])))
		{
			pVertex[i].x = (primStart[stride * i].x + xOffs);
			pVertex[i].y = (primStart[stride * i].y + yOffs);
			pVertex[i].z = 0.95f;
			pVertex[i].w = primStart[stride * i].z;
			pVertex[i].PGXP_flag = 1;

			if ((primStart[stride * i].flags & VALID_2) != VALID_2)
			{
				pVertex[i].PGXP_flag = 6;
		//		__Log("GPPV No W: v:%x (%d, %d) pgxp(%f, %f)|\n", (currentAddr + 1 + (i * stride)) * 4, pPrimData[stride * i * 2], pPrimData[(stride * i * 2) + 1], primStart[stride * i].x, primStart[stride * i].y);
			}

			// Log incorrect vertices
			//if (PGXP_tDebug && 
			//	(fabs((float)pPrimData[stride * i * 2] - primStart[stride * i].x) > debug_tolerance) ||
			//	(fabs((float)pPrimData[(stride * i * 2) + 1] - primStart[stride * i].y) > debug_tolerance))
			//	__Log("GPPV: v:%x (%d, %d) pgxp(%f, %f)|\n", (currentAddr + 1 + (i * stride)) * 4, pPrimData[stride * i * 2], pPrimData[(stride * i * 2) + 1], primStart[stride * i].x, primStart[stride * i].y);
		}
		else
		{
			// Default to low precision vertex data
			//if (primStart  && ((primStart[stride * i].flags & VALID_01) == VALID_01) && primStart[stride * i].value != *(unsigned int*)(&pPrimData[stride * i * 2]))
			//	pVertex[i].PGXP_flag = 6;
			//else
				pVertex[i].PGXP_flag = 2;

				//pVertex[i].x = (pPrimData[stride * i * 2] + xOffs);
				//pVertex[i].y = (pPrimData[(stride * i * 2) + 1] + yOffs);

			// Look in cache for valid vertex
			pCacheVert = PGXP_GetCachedVertex(pPrimData[stride * i * 2], pPrimData[(stride * i * 2) + 1]);
			if (pCacheVert)
			{
				if (IsSessionID(pCacheVert->count))
				{
					if (pCacheVert->gFlags == 1)
					{
						pVertex[i].x = (pCacheVert->x + xOffs);
						pVertex[i].y = (pCacheVert->y + yOffs);
						pVertex[i].z = 0.95f;
						pVertex[i].w = pCacheVert->z;
						pVertex[i].PGXP_flag = 3;
						// reduce number of invalid vertices
						invalidVert--;
					}
					else if(pCacheVert->gFlags > 1)
						pVertex[i].PGXP_flag = 4;
				}
			}

			// Log unprocessed vertices
			//if(PGXP_tDebug)
			//	__Log("GPPV: v:%x (%d, %d)|\n", (currentAddr + 1 + (i * stride))*4, pPrimData[stride * i * 2], pPrimData[(stride * i * 2) + 1]);
		}

		//pVertex[i].x = ((pPrimData[stride * i * 2] + xOffs) << 5) >> 5;
		//pVertex[i].y = ((pPrimData[(stride * i * 2) + 1] + yOffs) << 5) >> 5;
	}

	// If there are any invalid vertices set all w values to 1
	// iCB: Could use plane equation to find w for single invalid vertex in a quad
	if (invalidVert > 0)
		for (unsigned i = 0; i < count; ++i)
			pVertex[i].w = 1;

	return 1;
}

// Get single parallel vertex value
int PGXP_GetVertex(const unsigned int offset, const unsigned int* addr, OGLVertex* pOutput, int xOffs, int yOffs)
{
	PGXP_value*		vert = PGXP_ReadCB(offset);			// pointer to vertex
	short*			psxData = ((short*)addr);			// primitive data for cache lookups
	PGXP_value*		pCacheVert = NULL;


		if (vert && ((vert->flags & VALID_01) == VALID_01) && (vert->value == *(unsigned int*)(addr)))
		{
			pOutput->x = (vert->x + xOffs);
			pOutput->y = (vert->y + yOffs);
			pOutput->z = 0.95f;
			pOutput->w = vert->z;
			pOutput->PGXP_flag = 1;

			if ((vert->flags & VALID_2) != VALID_2)
			{
				pOutput->PGXP_flag = 6;
			}
		}
		else
		{
			pOutput->PGXP_flag = 2;

			pOutput->x = ((psxData[0] + xOffs) << 5) >> 5;
			pOutput->y = ((psxData[1] + yOffs) << 5) >> 5;

			// Look in cache for valid vertex
			pCacheVert = PGXP_GetCachedVertex(psxData[0], psxData[1]);
			if (pCacheVert)
			{
			//	if (IsSessionID(pCacheVert->count))
				{
					if (pCacheVert->gFlags == 1)
					{
						pOutput->x = (pCacheVert->x + xOffs);
						pOutput->y = (pCacheVert->y + yOffs);
						pOutput->z = 0.95f;
						pOutput->w = pCacheVert->z;
						pOutput->PGXP_flag = 3;
					}
					else if (pCacheVert->gFlags > 1)
						pOutput->PGXP_flag = 4;
				}
			}
		}

	return 1;
}