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
/* Drop the screen-XY FIFO; used on an off-to-on PGXP transition. */
void	PGXP_InvalidateVertexFIFO(void);

void	PGXP_GTE_LWC2(uint32_t instr, uint32_t rtVal, uint32_t addr);	// copy memory to GTE reg
void	PGXP_GTE_SWC2(uint32_t instr, uint32_t rtVal, uint32_t addr);	// copy GTE reg to memory

/* --- Transform-range instrumentation ---------------------------------
 *
 * OFF BY DEFAULT AND COMPILED OUT ENTIRELY. Build with -DPGXP_DIAG=1 to
 * enable. A default build must contain none of this: the counters below
 * sit in pgxp_precise_z(), which is called from RTPS/RTPT once per
 * transformed vertex, and that is not a place to spend cycles on a
 * diagnostic. A 64-bit increment is not free on the 32-bit ARM, MIPS and
 * PowerPC targets this core ships to - it is an add/add-with-carry pair
 * plus two stores - and a store to a static in a hot inline constrains
 * reordering on every target. Measuring it as noise on one x86-64 desktop
 * does not generalise, and is not the bar for the GTE inner loop.
 *
 * With PGXP_DIAG unset the object code of gte.c is byte-identical to a
 * tree without this patch; that identity is verified rather than
 * asserted, by disassembly comparison.
 *
 * What it measures, when enabled: pgxp_precise_z() clamps the exact
 * view-space Z to the same 0xFFFF ceiling the architectural SZ3
 * saturates at. Keeping that ceiling was a deliberate, documented
 * choice, but whether it is worth lifting is an empirical question about
 * real content that no offline harness can answer - it depends on how
 * much geometry sits past saturation and how far past it goes.
 *
 * Not thread safe by design. The GTE runs on the emulation thread; a
 * torn read costs a wrong digit in a log line, which is not worth a
 * locked instruction even in a diagnostic build. */
#if PGXP_DIAG
extern uint64_t pgxp_z_total;      /* vertices through pgxp_precise_z    */
extern uint64_t pgxp_z_ceiling;    /* ...of those, clamped at 0xFFFF     */
extern double   pgxp_z_ceiling_max;/* largest pre-clamp Z seen, else 0.0 */

/* stats[0] total, [1] ceiling hits, [2] largest pre-clamp Z (rounded). */
void	PGXP_GetTransformStats(uint64_t stats[3]);

/* Field diagnostics within a PGXP_DIAG build, same contract as
 * BEETLE_GL_DIAG in rhi_lib_gl.c: enabled by setting BEETLE_PGXP_DIAG in
 * the environment, one getenv on first use, silent otherwise. Dumps the
 * transform counters plus the vertex cache counters, which had no reader
 * until now. Called from the periodic tick in gte.c. */
void	PGXP_DiagDump(void);
#endif /* PGXP_DIAG */

#ifdef __cplusplus
}
#endif

#endif /* _PGXP_GTE_H_ */
