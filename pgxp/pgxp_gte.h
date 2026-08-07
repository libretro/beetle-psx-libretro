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
*	pgxp_gte.h
*	PGXP - Parallel/Precision Geometry Xform Pipeline
*
*	Created on: 12 Mar 2016
*      Author: iCatButler
***************************************************************************/

#ifndef _PGXP_GTE_H_
#define _PGXP_GTE_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "pgxp_types.h"

extern PGXP_value* GTE_data_reg;
extern PGXP_value* GTE_ctrl_reg;

void PGXP_InitGTE();

// -- GTE functions
// Transforms
void	PGXP_pushSXYZ2f(float _x, float _y, float _z, uint32_t _v);
void	PGXP_pushSXYZ2s(int64_t _x, int64_t _y, int64_t _z, uint32_t v);
void	PGXP_pushRGBf(float _r, float _g, float _b, uint32_t _v);

	/* Depth-cue sidecar. The colour FIFO shadow carries the POST-fog value
	 * (MAC_to_RGB_FIFO runs after the interpolation), so linear-light fog
	 * at the renderer needs three more quantities per colour word: the
	 * pre-cue colour, the far colour FC, and the blend factor IR0/4096.
	 * They cannot ride the PGXP_value itself without growing every tracked
	 * word in RAM, but the value's `count` field already travels through
	 * memory -- so the sidecar lives in a ring keyed by count, written at
	 * push time and looked up at the GPU end, with an exact count compare
	 * rejecting stale slots.
	 *
	 * A fog op calls PGXP_GTE_SetFogContext with 8-bit-scale floats before
	 * MAC_to_RGB_FIFO; the next pushRGBf consumes it (one-shot). Pushes
	 * without a pending context record "no cue". */
	void	PGXP_GTE_SetFogContext(float pre_r, float pre_g, float pre_b,
			float fc_r, float fc_g, float fc_b, float t);

	/* Sidecar lookup by the colour shadow's count. Returns 1 and fills the
	 * outputs only if the slot still belongs to `count` AND it recorded a
	 * depth cue; 0 for stale slots and cue-less pushes. */
	int	PGXP_GTE_GetFogByCount(uint32_t count, float out_pre[3],
			float out_fc[3], float *out_t);

	/* Refuse every stored cue. Called on init and savestate load: the push
	 * counter restarts there while counts inside RAM shadows survive, and a
	 * recycled slot must not satisfy a pre-load association. */
	void	PGXP_GTE_InvalidateFogRing(void);

void	PGXP_RTPS(uint32_t _n, uint32_t _v);

int		PGXP_NCLIP_valid(uint32_t sxy0, uint32_t sxy1, uint32_t sxy2);
double	PGXP_NCLIP();

// Data transfer tracking
void	PGXP_GTE_MFC2(uint32_t instr, uint32_t rtVal, uint32_t rdVal);		// copy GTE data reg to GPR reg (MFC2)
void	PGXP_GTE_MTC2(uint32_t instr, uint32_t rdVal, uint32_t rtVal);		// copy GPR reg to GTE data reg (MTC2)
void	PGXP_GTE_CFC2(uint32_t instr, uint32_t rtVal, uint32_t rdVal);		// copy GTE ctrl reg to GPR reg (CFC2)
void	PGXP_GTE_CTC2(uint32_t instr, uint32_t rdVal, uint32_t rtVal);		// copy GPR reg to GTE ctrl reg (CTC2)
// Memory Access
void	PGXP_GTE_LWC2(uint32_t instr, uint32_t rtVal, uint32_t addr);	// copy memory to GTE reg
void	PGXP_GTE_SWC2(uint32_t instr, uint32_t rtVal, uint32_t addr);	// copy GTE reg to memory

#ifdef __cplusplus
}
#endif

#endif /* _PGXP_GTE_H_ */
