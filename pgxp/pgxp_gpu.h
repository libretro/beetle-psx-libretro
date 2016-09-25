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
		unsigned int PGXP_flag;
	} OGLVertex;

	//struct OGLVertexTag;
	//typedef struct OGLVertexTag OGLVertex;

	struct PGXP_value_Tag;
	typedef struct PGXP_value_Tag PGXP_value;

	void		PGXP_WriteFIFO(PGXP_value* pV, u32 pos);
	PGXP_value*	PGXP_ReadFIFO(u32 pos);
	void		PGXP_WriteCB(PGXP_value* pV, u32 pos);
	PGXP_value*	PGXP_ReadCB(u32 pos);

	void	PGXP_CacheVertex(short sx, short sy, const PGXP_value* _pVertex);

	void	PGXP_SetAddress(unsigned int addr);
	int		PGXP_GetVertices(const unsigned int* addr, void* pOutput, int xOffs, int yOffs);
	int		PGXP_GetVertex(const unsigned int offset, const unsigned int* addr, OGLVertex* pOutput, int xOffs, int yOffs);
	//void	PGXP_glVertexfv(GLfloat* pVertex);

#ifdef __cplusplus
}
#endif

#endif // _PGXP_GPU_H_
