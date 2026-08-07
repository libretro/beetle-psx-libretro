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
*	pgxp_gte.c
*	PGXP - Parallel/Precision Geometry Xform Pipeline
*
*	Created on: 12 Mar 2016
*      Author: iCatButler
***************************************************************************/

#include <string.h>
#include <math.h>

#include "pgxp_gte.h"
#include "pgxp_main.h"
#include "pgxp_value.h"
#include "pgxp_mem.h"
#include "pgxp_cpu.h"
#include "pgxp_gpu.h"


/* GTE registers */
PGXP_value GTE_data_reg_mem[32];
PGXP_value GTE_ctrl_reg_mem[32];


PGXP_value* GTE_data_reg = GTE_data_reg_mem;
PGXP_value* GTE_ctrl_reg = GTE_ctrl_reg_mem;

void PGXP_InitGTE()
{
	memset(GTE_data_reg_mem, 0, sizeof(GTE_data_reg_mem));
	memset(GTE_ctrl_reg_mem, 0, sizeof(GTE_ctrl_reg_mem));
}

/* Instruction register decoding */
#define op(_instr)		(_instr >> 26)			/* The op part of the instruction register */
#define func(_instr)	((_instr) & 0x3F)		/* The funct part of the instruction register */
#define sa(_instr)		((_instr >>  6) & 0x1F) /* The sa part of the instruction register */
#define rd(_instr)		((_instr >> 11) & 0x1F)	/* The rd part of the instruction register */
#define rt(_instr)		((_instr >> 16) & 0x1F)	/* The rt part of the instruction register */
#define rs(_instr)		((_instr >> 21) & 0x1F)	/* The rs part of the instruction register */
#define imm(_instr)		(_instr & 0xFFFF)		/* The immediate part of the instruction register */

#define SX0 (GTE_data_reg[ 12 ].x)
#define SY0 (GTE_data_reg[ 12 ].y)
#define SX1 (GTE_data_reg[ 13 ].x)
#define SY1 (GTE_data_reg[ 13 ].y)
#define SX2 (GTE_data_reg[ 14 ].x)
#define SY2 (GTE_data_reg[ 14 ].y)

#define SXY0 (GTE_data_reg[ 12 ])
#define SXY1 (GTE_data_reg[ 13 ])
#define SXY2 (GTE_data_reg[ 14 ])
#define SXYP (GTE_data_reg[ 15 ])

void PGXP_pushSXYZ2f(float _x, float _y, float _z, uint32_t _v)
{
	static uint32_t uCount = 0;
	low_value temp;
	/* push values down FIFO */
	SXY0 = SXY1;
	SXY1 = SXY2;
	
	SXY2.x		= _x;
	SXY2.y		= _y;
	SXY2.z		= (PGXP_GetModes() & PGXP_TEXTURE_CORRECTION) ? _z : 1.f;
	SXY2.value	= _v;
	SXY2.flags	= VALID_ALL;
	SXY2.count	= uCount++;

	/* cache value in GPU plugin */
	temp.word = _v;
	if(PGXP_GetModes() & PGXP_VERTEX_CACHE)
		PGXP_CacheVertex(temp.x, temp.y, &SXY2);
	else
		PGXP_CacheVertex(0, 0, NULL);

#ifdef GTE_LOG
	GTE_LOG("PGXP_PUSH (%f, %f) %u %u|", SXY2.x, SXY2.y, SXY2.flags, SXY2.count);
#endif
}

void PGXP_pushSXYZ2s(int64_t _x, int64_t _y, int64_t _z, uint32_t v)
{
	float fx = (float)(_x) / (float)(1 << 16);
	float fy = (float)(_y) / (float)(1 << 16);
	float fz = (float)(_z);

	PGXP_pushSXYZ2f(fx, fy, fz, v);
}

/* Shadow of the GTE ColorFIFO push (gte.c MAC_to_RGB_FIFO).  x/y/z carry
 * the pre-saturation MACn/16 channel values in 8-bit scale; value is the
 * packed architectural RGB|CD word written to DR[22].  The registers 20-22
 * shadows already ride the generic MFC2/SWC2/memory transport, so filling
 * them here is all that is needed for a display-list color word stored via
 * `swc2 $22` to arrive at the GPU-side command buffer with its precise
 * payload attached. */
/* Depth-cue sidecar ring; see pgxp_gte.h. 4096 entries is over two
 * seconds of colour pushes at the GTE's realistic rate, and the exact
 * count compare on lookup makes aliasing a refusal, never a wrong
 * answer. */
#define PGXP_FOG_RING_SIZE 4096u

typedef struct
{
	float    pre[3];
	float    fc[3];
	float    t;        /* IR0/4096 clamped to [0,1]; < 0 means no cue */
	uint32_t count;
} pgxp_fog_entry;

static pgxp_fog_entry pgxp_fog_ring[PGXP_FOG_RING_SIZE];
static pgxp_fog_entry pgxp_fog_pending;
static int            pgxp_fog_pending_set = 0;

void PGXP_GTE_SetFogContext(float pre_r, float pre_g, float pre_b,
		float fc_r, float fc_g, float fc_b, float t)
{
	pgxp_fog_pending.pre[0] = pre_r;
	pgxp_fog_pending.pre[1] = pre_g;
	pgxp_fog_pending.pre[2] = pre_b;
	pgxp_fog_pending.fc[0]  = fc_r;
	pgxp_fog_pending.fc[1]  = fc_g;
	pgxp_fog_pending.fc[2]  = fc_b;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	pgxp_fog_pending.t      = t;
	pgxp_fog_pending_set    = 1;
}

int PGXP_GTE_GetFogByCount(uint32_t count, float out_pre[3],
		float out_fc[3], float *out_t)
{
	const pgxp_fog_entry *e = &pgxp_fog_ring[count % PGXP_FOG_RING_SIZE];
	if (e->count != count || e->t < 0.0f)
		return 0;
	out_pre[0] = e->pre[0]; out_pre[1] = e->pre[1]; out_pre[2] = e->pre[2];
	out_fc[0]  = e->fc[0];  out_fc[1]  = e->fc[1];  out_fc[2]  = e->fc[2];
	*out_t     = e->t;
	return 1;
}

void PGXP_pushRGBf(float _r, float _g, float _b, uint32_t _v)
{
	static uint32_t uCount = 0;
	pgxp_fog_entry *slot = &pgxp_fog_ring[uCount % PGXP_FOG_RING_SIZE];

	if (pgxp_fog_pending_set)
	{
		*slot = pgxp_fog_pending;
		pgxp_fog_pending_set = 0;
	}
	else
		slot->t = -1.0f;
	slot->count = uCount;

	GTE_data_reg[20] = GTE_data_reg[21];
	GTE_data_reg[21] = GTE_data_reg[22];

	GTE_data_reg[22].x      = _r;
	GTE_data_reg[22].y      = _g;
	GTE_data_reg[22].z      = _b;
	GTE_data_reg[22].value  = _v;
	GTE_data_reg[22].flags  = VALID_ALL;
	GTE_data_reg[22].count  = uCount++;
	GTE_data_reg[22].gFlags = 0;
	GTE_data_reg[22].lFlags = 0;
	GTE_data_reg[22].hFlags = 0;
}

#define VX(n) (psxRegs.CP2D.p[ n << 1 ].sw.l)
#define VY(n) (psxRegs.CP2D.p[ n << 1 ].sw.h)
#define VZ(n) (psxRegs.CP2D.p[ (n << 1) + 1 ].sw.l)

int PGXP_NCLIP_valid(uint32_t sxy0, uint32_t sxy1, uint32_t sxy2)
{
	Validate(&SXY0, sxy0);
	Validate(&SXY1, sxy1);
	Validate(&SXY2, sxy2);
	if (((SXY0.flags & SXY1.flags & SXY2.flags & VALID_01) == VALID_01))/* && Config.PGXP_GTE && (Config.PGXP_Mode > 0)) */
		return 1;
	return 0;
}

double PGXP_NCLIP()
{
	/* Factored cross product - same algebra as the native integer NCLIP
	 * (x0*(y1-y2) + x1*(y2-y0) + x2*(y0-y1)) - accumulated in double.
	 * The previous form summed six separate float products; for the thin /
	 * near-degenerate triangles typical of PSX geometry that suffers
	 * catastrophic cancellation (observed error up to ~1.4 in the int32
	 * result written to MAC0).  Double + the factored form is accurate to
	 * the true cross product of the (float) shadow coords. */
	double nclip = (double)SX0 * ((double)SY1 - (double)SY2)
	             + (double)SX1 * ((double)SY2 - (double)SY0)
	             + (double)SX2 * ((double)SY0 - (double)SY1);
	double nclipAbs = fabs(nclip);

	/* Preserve a tiny but non-zero orientation through the caller's
	 * truncation to int32 (a genuine near-degenerate result would otherwise
	 * read as 0 and flip the culling decision).  Kept with identical intent;
	 * simply hit far less often now the cancellation error is gone. */
	if ((0.1 < nclipAbs) && (nclipAbs < 1.0))
		nclip += (nclip < 0.0 ? -1.0 : 1.0);

	return nclip;
}

static void PGXP_MTC2_int(PGXP_value value, uint32_t reg)
{
	switch(reg)
	{
		case 15:
			/* push FIFO */
			SXY0 = SXY1;
			SXY1 = SXY2;
			SXY2 = value;
			SXYP = SXY2;
			break;

		case 31:
			return;
	}

	GTE_data_reg[reg] = value;
}

/* ============================================================
 * Data transfer tracking
 * ============================================================ */

void MFC2(int reg) {
	psx_value val;
	val.d = GTE_data_reg[reg].value;
	switch (reg) {
	case 1:
	case 3:
	case 5:
	case 8:
	case 9:
	case 10:
	case 11:
		GTE_data_reg[reg].value = (int32_t)val.sw.l;
		GTE_data_reg[reg].y = 0.f;
		break;

	case 7:
	case 16:
	case 17:
	case 18:
	case 19:
		GTE_data_reg[reg].value = (uint32_t)val.w.l;
		GTE_data_reg[reg].y = 0.f;
		break;

	case 15:
		GTE_data_reg[reg] = SXY2;
		break;

	case 28:
	case 29:
		break;
	}
}

void PGXP_GTE_MFC2(uint32_t instr, uint32_t rtVal, uint32_t rdVal)
{
	/* CPU[Rt] = GTE_D[Rd] */
	Validate(&GTE_data_reg[rd(instr)], rdVal);
	CPU_reg[rt(instr)] = GTE_data_reg[rd(instr)];
	CPU_reg[rt(instr)].value = rtVal;
}

void PGXP_GTE_MTC2(uint32_t instr, uint32_t rdVal, uint32_t rtVal)
{
	/* GTE_D[Rd] = CPU[Rt] */
	Validate(&CPU_reg[rt(instr)], rtVal);
	PGXP_MTC2_int(CPU_reg[rt(instr)], rd(instr));
	GTE_data_reg[rd(instr)].value = rdVal;
}

void PGXP_GTE_CFC2(uint32_t instr, uint32_t rtVal, uint32_t rdVal)
{
	/* CPU[Rt] = GTE_C[Rd] */
	Validate(&GTE_ctrl_reg[rd(instr)], rdVal);
	CPU_reg[rt(instr)] = GTE_ctrl_reg[rd(instr)];
	CPU_reg[rt(instr)].value = rtVal;
}

void PGXP_GTE_CTC2(uint32_t instr, uint32_t rdVal, uint32_t rtVal)
{
	/* GTE_C[Rd] = CPU[Rt] */
	Validate(&CPU_reg[rt(instr)], rtVal);
	GTE_ctrl_reg[rd(instr)] = CPU_reg[rt(instr)];
	GTE_ctrl_reg[rd(instr)].value = rdVal;
}

/* ============================================================
 * Memory Access
 * ============================================================ */
void	PGXP_GTE_LWC2(uint32_t instr, uint32_t rtVal, uint32_t addr)
{
	/* GTE_D[Rt] = Mem[addr] */
	PGXP_value val;
	ValidateAndCopyMem(&val, addr, rtVal);
	PGXP_MTC2_int(val, rt(instr));
}

void	PGXP_GTE_SWC2(uint32_t instr, uint32_t rtVal, uint32_t addr)
{
	/*  Mem[addr] = GTE_D[Rt] */
	Validate(&GTE_data_reg[rt(instr)], rtVal);
	WriteMem(&GTE_data_reg[rt(instr)], addr);
}
