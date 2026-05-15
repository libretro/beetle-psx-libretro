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
*	pgxp_gpu.h
*	PGXP - Parallel/Precision Geometry Xform Pipeline
*
*	Created on: 25 Mar 2016
*      Author: iCatButler
***************************************************************************/

#ifndef _PGXP_GPU_H_
#define _PGXP_GPU_H_

#ifdef __cplusplus
extern "C" {
#endif

	#include "pgxp_types.h"

	typedef struct
	{
		float	x;
		float	y;
		float	z;
		float	w;
		uint8_t valid_w;
	} OGLVertex;

	void		PGXP_WriteFIFO(PGXP_value* pV, uint32_t pos);
	PGXP_value*	PGXP_ReadFIFO(uint32_t pos);
	void		PGXP_WriteCB(PGXP_value* pV, uint32_t pos);
	PGXP_value*	PGXP_ReadCB(uint32_t pos);

	void	PGXP_CacheVertex(int16_t sx, int16_t sy, const PGXP_value* _pVertex);

	void	PGXP_FreeVertexCache(void);

	void	PGXP_SetAddress(uint32_t addr);
	int		PGXP_GetVertices(const uint32_t* addr, void* pOutput, int xOffs, int yOffs);
	int		PGXP_GetVertex(const uint32_t offset, const uint32_t* addr, OGLVertex* pOutput, int xOffs, int yOffs);

#ifdef __cplusplus
}
#endif

#endif /* _PGXP_GPU_H_ */
