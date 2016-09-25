#/***************************************************************************
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
*	pgxp_main.h
*	PGXP - Parallel/Precision Geometry Xform Pipeline
*
*	Created on: 24 Sep 2016
*      Author: iCatButler
***************************************************************************/

#ifndef _PGXP_MAIN_H_
#define _PGXP_MAIN_H_

#define PGXP_MODE_NONE 0

#define PGXP_MODE_MEMORY (1 << 0)
#define PGXP_MODE_CPU (1 << 1)
#define PGXP_MODE_GTE (1 << 2)

#define PGXP_VERTEX_CACHE (1 << 4)
#define PGXP_TEXTURE_CORRECTION (1 << 5)

#ifdef __cplusplus
extern "C" {
#endif

#include "pgxp_types.h"

	void	PGXP_Init();	// initialise memory

	void	PGXP_SetModes(u32 modes);
	u32		PGXP_GetModes();
	void	PGXP_EnableModes(u32 modes);
	void	PGXP_DisableModes(u32 modes);

#ifdef __cplusplus
}//extern "C"
#endif


#endif//_PGXP_MAIN_H_