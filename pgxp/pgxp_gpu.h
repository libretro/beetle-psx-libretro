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

	/* Look up the precise (pre-ColorFIFO-saturation) color for the GP0
	 * color word at `offset` in the command buffer.  Self-validating:
	 * accepts the shadow only if its low 24 bits match the architectural
	 * word AND each float channel requantizes (floor + saturate, exactly
	 * as the GTE did) to the corresponding byte - so a false accept is
	 * within half an LSB of truth by construction, and any game-side
	 * color math degrades to the architectural bytes, never to a glitch.
	 * On success writes out_rgb[0..2] in 8-bit scale (may exceed 255.0
	 * for over-range lighting; caller decides clamping policy) when
	 * out_rgb is non-NULL, and returns 1.  Returns 0 on fallback.
	 * Maintains hit-rate statistics; see PGXP_GetColorStats. */
	int		PGXP_GetColor(const uint32_t offset, const uint32_t* addr, float* out_rgb);

	/* Depth-cue recovery for the GP0 color word at `offset`. Succeeds only
	 * when PGXP_GetColor would accept the word (same shadow, same
	 * requantization guarantee -- the POST-fog value must reproduce the
	 * architectural bytes exactly) AND the push recorded a depth cue whose
	 * sidecar slot still belongs to this word's count. Outputs are
	 * 8-bit-scale floats: the pre-cue colour (may exceed 255), the far
	 * colour, and the blend factor in [0,1]. A refusal means the renderer
	 * uses the architectural post-fog colour, which is today's picture. */
	int		PGXP_GetFog(const uint32_t offset, const uint32_t* addr,
			float out_pre[3], float out_fc[3], float* out_t);

	/* stats[0]=attempts, [1]=hits, [2]=shadow/value misses, [3]=requantize misses */
	void	PGXP_GetColorStats(uint32_t stats[4]);

	/* Over-range statistics, over accepted words only.
	 *
	 * The hit rate says whether the precise colour can be recovered. This
	 * says whether recovering it would change anything: a colour whose
	 * channels all sit at or below 255 requantizes to the byte the
	 * architectural path already had, so a wide framebuffer gains nothing
	 * from it. For an HDR renderer slice, over-white frequency is the
	 * deciding number and hit rate is only a precondition.
	 *
	 * buckets are on the peak channel relative to the 255 ceiling:
	 * [0] (1,1.25]  [1] (1.25,1.5]  [2] (1.5,2]  [3] >2
	 * peak is the largest single channel seen, in 8-bit scale. */
	void	PGXP_GetColorRangeStats(uint32_t *over, uint32_t buckets[4],
			float *peak);

#ifdef __cplusplus
}
#endif

#endif /* _PGXP_GPU_H_ */
