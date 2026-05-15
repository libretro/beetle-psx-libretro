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
#define PGXP_NCLIP_IMPL (1 << 6)

#ifdef __cplusplus
extern "C" {
#endif

#include "pgxp_types.h"

	/* The mode bitfield is hot-path: 100+ readers across cpu.c,
	 * gte.c, gpu.c.  Expose the underlying global directly and
	 * inline the getter so callers don't pay for a cross-TU
	 * function call on every read.  All writes still go through
	 * the apply_modes() path in pgxp_main.c (which handles the
	 * vertex-cache free side effect when the cache bit drops). */
	extern uint32_t gMode;

	void	PGXP_Init(void);	/* initialise memory */
	void	PGXP_Shutdown(void);	/* free heap-allocated buffers */

	void	PGXP_SetModes(uint32_t modes);
	static inline uint32_t PGXP_GetModes(void) { return gMode; }
	void	PGXP_EnableModes(uint32_t modes);
	void	PGXP_DisableModes(uint32_t modes);

        static inline int PGXP_enabled(void) {
          return (PGXP_GetModes() & (PGXP_MODE_MEMORY | PGXP_VERTEX_CACHE)) != 0;
        }

        /* True if the user has enabled perspective-correct texturing.
         * Used by the software polygon rasteriser to decide whether to
         * take the perspective UV path (added in the libretro fork);
         * the GL / Vulkan backends consult it in their own vertex push
         * code. */
        static inline int PGXP_texture_correction_enabled(void) {
          return (PGXP_GetModes() & PGXP_TEXTURE_CORRECTION) != 0;
        }

#ifdef __cplusplus
}//extern "C"
#endif


#endif//_PGXP_MAIN_H_
