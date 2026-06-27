#include <string.h>
#include <math.h>

#include "pgxp_cpu.h"
#include "pgxp_value.h"
#include "pgxp_mem.h"


#include "limits.h"

/* CPU registers */
PGXP_value CPU_reg_mem[34];
/*PGXP_value CPU_Hi, CPU_Lo; */
PGXP_value CP0_reg_mem[32];

PGXP_value* CPU_reg = CPU_reg_mem;
PGXP_value* CP0_reg = CP0_reg_mem;

/* Instruction register decoding */
#define op(_instr)		(_instr >> 26)			/* The op part of the instruction register */
#define func(_instr)	((_instr) & 0x3F)		/* The funct part of the instruction register */
#define sa(_instr)		((_instr >>  6) & 0x1F) /* The sa part of the instruction register */
#define rd(_instr)		((_instr >> 11) & 0x1F)	/* The rd part of the instruction register */
#define rt(_instr)		((_instr >> 16) & 0x1F)	/* The rt part of the instruction register */
#define rs(_instr)		((_instr >> 21) & 0x1F)	/* The rs part of the instruction register */
#define imm(_instr)		(_instr & 0xFFFF)		/* The immediate part of the instruction register */

void PGXP_InitCPU()
{
	memset(CPU_reg_mem, 0, sizeof(CPU_reg_mem));
	memset(CP0_reg_mem, 0, sizeof(CP0_reg_mem));
}

/* invalidate register (invalid 8 bit read) */
void InvalidLoad(uint32_t addr, uint32_t code, uint32_t value)
{
	uint32_t reg = ((code >> 16) & 0x1F); /* The rt part of the instruction register */
	PGXP_value* pD = NULL;
	PGXP_value p;

	p.x = p.y = -1337; /* default values */

					   /*p.valid = 0;
					    *p.count = value; */
	pD = ReadMem(addr);

	if (pD)
	{
		p.count = addr;
		p = *pD;
	}
	else
	{
		p.count = value;
	}

	p.flags = 0;

	/* invalidate register */
	CPU_reg[reg] = p;
}

/* invalidate memory address (invalid 8 bit write) */
void InvalidStore(uint32_t addr, uint32_t code, uint32_t value)
{
	uint32_t reg = ((code >> 16) & 0x1F); /* The rt part of the instruction register */
	PGXP_value* pD = NULL;
	PGXP_value p;

	pD = ReadMem(addr);

	p.x = p.y = -2337;

	if (pD)
		p = *pD;

	p.flags = 0;
	p.count = (reg * 1000) + value;

	/* invalidate memory */
	WriteMem(&p, addr);
}

/* ============================================================
 * Arithmetic with immediate value
 * ============================================================ */
void PGXP_CPU_ADDI(uint32_t instr, uint32_t rtVal, uint32_t rsVal)
{
	/* Rt = Rs + Imm (signed) */
	psx_value tempImm;
	PGXP_value ret;
	float of;

	Validate(&CPU_reg[rs(instr)], rsVal);
	ret = CPU_reg[rs(instr)];
	tempImm.d = imm(instr);
	tempImm.sd = (int32_t)(int16_t)tempImm.w.l;	/* sign extend low 16 -> 32 */

	ret.x = f16Unsign(ret.x);
	ret.x += tempImm.w.l;

	/* carry on over/underflow */
	of = (ret.x > USHRT_MAX) ? 1.f : (ret.x < 0) ? -1.f : 0.f;
	ret.x = f16Sign(ret.x);
	ret.y += tempImm.sw.h + of;

	/* truncate on overflow/underflow */
	ret.y += (ret.y > SHRT_MAX) ? -(USHRT_MAX + 1) : (ret.y < SHRT_MIN) ? USHRT_MAX + 1 : 0.f;

	CPU_reg[rt(instr)] = ret;
	CPU_reg[rt(instr)].value = rtVal;
}

void PGXP_CPU_ADDIU(uint32_t instr, uint32_t rtVal, uint32_t rsVal)
{
	/* Rt = Rs + Imm (signed) (unsafe?) */
	PGXP_CPU_ADDI(instr, rtVal, rsVal);
}

void PGXP_CPU_ANDI(uint32_t instr, uint32_t rtVal, uint32_t rsVal)
{
	/* Rt = Rs & Imm */
	psx_value vRt;
	PGXP_value ret;

	Validate(&CPU_reg[rs(instr)], rsVal);
	ret = CPU_reg[rs(instr)];

	vRt.d = rtVal;

	ret.y = 0.f;	/* remove upper 16-bits */

	switch (imm(instr))
	{
	case 0:
		/* if 0 then x == 0 */
		ret.x = 0.f;
		break;
	case 0xFFFF:
		/* if saturated then x == x */
		break;
	default:
		/* otherwise x is low precision value */
		ret.x = vRt.sw.l;
		ret.flags |= VALID_0;
	}

	ret.flags |= VALID_1;

	CPU_reg[rt(instr)] = ret;
	CPU_reg[rt(instr)].value = rtVal;
}

void PGXP_CPU_ORI(uint32_t instr, uint32_t rtVal, uint32_t rsVal)
{
	/* Rt = Rs | Imm */
	psx_value vRt;
	PGXP_value ret;

	Validate(&CPU_reg[rs(instr)], rsVal);
	ret = CPU_reg[rs(instr)];

	vRt.d = rtVal;

	switch (imm(instr))
	{
	case 0:
		/* if 0 then x == x */
		break;
	default:
		/* otherwise x is low precision value */
		ret.x = vRt.sw.l;
		ret.flags |= VALID_0;
	}

	ret.value = rtVal;
	CPU_reg[rt(instr)] = ret;
}

void PGXP_CPU_XORI(uint32_t instr, uint32_t rtVal, uint32_t rsVal)
{
	/* Rt = Rs ^ Imm */
	psx_value vRt;
	PGXP_value ret;

	Validate(&CPU_reg[rs(instr)], rsVal);
	ret = CPU_reg[rs(instr)];

	vRt.d = rtVal;

	switch (imm(instr))
	{
	case 0:
		/* if 0 then x == x */
		break;
	default:
		/* otherwise x is low precision value */
		ret.x = vRt.sw.l;
		ret.flags |= VALID_0;
	}

	ret.value = rtVal;
	CPU_reg[rt(instr)] = ret;
}

void PGXP_CPU_SLTI(uint32_t instr, uint32_t rtVal, uint32_t rsVal)
{
	/* Rt = Rs < Imm (signed) */
	psx_value tempImm;
	PGXP_value ret;

	Validate(&CPU_reg[rs(instr)], rsVal);
	ret = CPU_reg[rs(instr)];

	tempImm.w.h = imm(instr);
	ret.y		= 0.f;
	ret.x		= (CPU_reg[rs(instr)].x < tempImm.sw.h) ? 1.f : 0.f;
	ret.flags	|= VALID_1;
	ret.value	= rtVal;

	CPU_reg[rt(instr)] = ret;
}

void PGXP_CPU_SLTIU(uint32_t instr, uint32_t rtVal, uint32_t rsVal)
{
	/* Rt = Rs < Imm (Unsigned) */
	psx_value tempImm;
	PGXP_value ret;

	Validate(&CPU_reg[rs(instr)], rsVal);
	ret = CPU_reg[rs(instr)];

	tempImm.w.h	= imm(instr);
	ret.y		= 0.f;
	ret.x		= (f16Unsign(CPU_reg[rs(instr)].x) < tempImm.w.h) ? 1.f : 0.f;
	ret.flags	|= VALID_1;
	ret.value	= rtVal;

	CPU_reg[rt(instr)] = ret;
}

/* ============================================================
 * Load Upper
 * ============================================================ */
void PGXP_CPU_LUI(uint32_t instr, uint32_t rtVal)
{
	/*Rt = Imm << 16 */
	CPU_reg[rt(instr)] = PGXP_value_zero;
	CPU_reg[rt(instr)].y = (float)(int16_t)imm(instr);
	CPU_reg[rt(instr)].hFlags = VALID_HALF;
	CPU_reg[rt(instr)].value = rtVal;
	CPU_reg[rt(instr)].flags = VALID_01;
}

/* ============================================================
 * Register Arithmetic
 * ============================================================ */

void PGXP_CPU_ADD(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs + Rt (signed) */
	PGXP_value ret;
	float of;

	Validate(&CPU_reg[rs(instr)], rsVal);
	Validate(&CPU_reg[rt(instr)], rtVal);

	/* iCB: Only require one valid input */
	if (((CPU_reg[rt(instr)].flags & VALID_01) != VALID_01) != ((CPU_reg[rs(instr)].flags & VALID_01) != VALID_01))
	{
		MakeValid(&CPU_reg[rs(instr)], rsVal);
		MakeValid(&CPU_reg[rt(instr)], rtVal);
	}

	ret = CPU_reg[rs(instr)];

	ret.x = f16Unsign(ret.x);
	ret.x += f16Unsign(CPU_reg[rt(instr)].x);

	/* carry on over/underflow */
	of = (ret.x > USHRT_MAX) ? 1.f : (ret.x < 0) ? -1.f : 0.f;
	ret.x = f16Sign(ret.x);
	ret.y += CPU_reg[rt(instr)].y + of;

	/* truncate on overflow/underflow */
	ret.y += (ret.y > SHRT_MAX) ? -(USHRT_MAX + 1) : (ret.y < SHRT_MIN) ? USHRT_MAX + 1 : 0.f;

	/* TODO: decide which "z/w" component to use */

	ret.halfFlags[0] &= CPU_reg[rt(instr)].halfFlags[0];
	ret.gFlags |= CPU_reg[rt(instr)].gFlags;
	ret.lFlags |= CPU_reg[rt(instr)].lFlags;
	ret.hFlags |= CPU_reg[rt(instr)].hFlags;

	ret.value = rdVal;

	CPU_reg[rd(instr)] = ret;
}

void PGXP_CPU_ADDU(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs + Rt (signed) (unsafe?) */
	PGXP_CPU_ADD(instr, rdVal, rsVal, rtVal);
}

void PGXP_CPU_SUB(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs - Rt (signed) */
	PGXP_value ret;
	float of;

	Validate(&CPU_reg[rs(instr)], rsVal);
	Validate(&CPU_reg[rt(instr)], rtVal);

	/* iCB: Only require one valid input */
	if (((CPU_reg[rt(instr)].flags & VALID_01) != VALID_01) != ((CPU_reg[rs(instr)].flags & VALID_01) != VALID_01))
	{
		MakeValid(&CPU_reg[rs(instr)], rsVal);
		MakeValid(&CPU_reg[rt(instr)], rtVal);
	}

	ret = CPU_reg[rs(instr)];

	ret.x = f16Unsign(ret.x);
	ret.x -= f16Unsign(CPU_reg[rt(instr)].x);

	/* carry on over/underflow */
	of = (ret.x > USHRT_MAX) ? 1.f : (ret.x < 0) ? -1.f : 0.f;
	ret.x = f16Sign(ret.x);
	ret.y -= CPU_reg[rt(instr)].y - of;

	/* truncate on overflow/underflow */
	ret.y += (ret.y > SHRT_MAX) ? -(USHRT_MAX + 1) : (ret.y < SHRT_MIN) ? USHRT_MAX + 1 : 0.f;

	ret.halfFlags[0] &= CPU_reg[rt(instr)].halfFlags[0];
	ret.gFlags |= CPU_reg[rt(instr)].gFlags;
	ret.lFlags |= CPU_reg[rt(instr)].lFlags;
	ret.hFlags |= CPU_reg[rt(instr)].hFlags;

	ret.value = rdVal;

	CPU_reg[rd(instr)] = ret;
}

void PGXP_CPU_SUBU(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs - Rt (signed) (unsafe?) */
	PGXP_CPU_SUB(instr, rdVal, rsVal, rtVal);
}

void PGXP_CPU_AND(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs & Rt */
	psx_value vald, vals, valt;
	PGXP_value ret;

	Validate(&CPU_reg[rs(instr)], rsVal);
	Validate(&CPU_reg[rt(instr)], rtVal);

	/* iCB: Only require one valid input */
	if (((CPU_reg[rt(instr)].flags & VALID_01) != VALID_01) != ((CPU_reg[rs(instr)].flags & VALID_01) != VALID_01))
	{
		MakeValid(&CPU_reg[rs(instr)], rsVal);
		MakeValid(&CPU_reg[rt(instr)], rtVal);
	}

	vald.d = rdVal;
	vals.d = rsVal;
	valt.d = rtVal;

	/*	CPU_reg[rd(instr)].valid = CPU_reg[rs(instr)].valid && CPU_reg[rt(instr)].valid; */
	ret.flags = VALID_01;

	if (vald.w.l == 0)
	{
		ret.x = 0.f;
		ret.lFlags = VALID_HALF;
	}
	else if (vald.w.l == vals.w.l)
	{
		ret.x = CPU_reg[rs(instr)].x;
		ret.lFlags = CPU_reg[rs(instr)].lFlags;
		ret.compFlags[0] = CPU_reg[rs(instr)].compFlags[0];
	}
	else if (vald.w.l == valt.w.l)
	{
		ret.x = CPU_reg[rt(instr)].x;
		ret.lFlags = CPU_reg[rt(instr)].lFlags;
		ret.compFlags[0] = CPU_reg[rt(instr)].compFlags[0];
	}
	else
	{
		ret.x = (float)vald.sw.l;
		ret.compFlags[0] = VALID;
		ret.lFlags = 0;
	}

	if (vald.w.h == 0)
	{
		ret.y = 0.f;
		ret.hFlags = VALID_HALF;
	}
	else if (vald.w.h == vals.w.h)
	{
		ret.y = CPU_reg[rs(instr)].y;
		ret.hFlags = CPU_reg[rs(instr)].hFlags;
		ret.compFlags[1] &= CPU_reg[rs(instr)].compFlags[1];
	}
	else if (vald.w.h == valt.w.h)
	{
		ret.y = CPU_reg[rt(instr)].y;
		ret.hFlags = CPU_reg[rt(instr)].hFlags;
		ret.compFlags[1] &= CPU_reg[rt(instr)].compFlags[1];
	}
	else
	{
		ret.y = (float)vald.sw.h;
		ret.compFlags[1] = VALID;
		ret.hFlags = 0;
	}

	/* iCB Hack: Force validity if even one half is valid
	 *if ((ret.hFlags & VALID_HALF) || (ret.lFlags & VALID_HALF))
	 *	ret.valid = 1;
	 * /iCB Hack */

	/* Get a valid W */
	if ((CPU_reg[rs(instr)].flags & VALID_2) == VALID_2)
	{
		ret.z = CPU_reg[rs(instr)].z;
		ret.compFlags[2] = CPU_reg[rs(instr)].compFlags[2];
	}
	else if((CPU_reg[rt(instr)].flags & VALID_2) == VALID_2)
	{
		ret.z = CPU_reg[rt(instr)].z;
		ret.compFlags[2] = CPU_reg[rt(instr)].compFlags[2];
	}

	ret.value = rdVal;
	CPU_reg[rd(instr)] = ret;
}

void PGXP_CPU_OR(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs | Rt */
	PGXP_CPU_AND(instr, rdVal, rsVal, rtVal);
}

void PGXP_CPU_XOR(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs ^ Rt */
	PGXP_CPU_AND(instr, rdVal, rsVal, rtVal);
}

void PGXP_CPU_NOR(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs NOR Rt */
	PGXP_CPU_AND(instr, rdVal, rsVal, rtVal);
}

void PGXP_CPU_SLT(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs < Rt (signed) */
	PGXP_value ret;
	Validate(&CPU_reg[rs(instr)], rsVal);
	Validate(&CPU_reg[rt(instr)], rtVal);

	/* iCB: Only require one valid input */
	if (((CPU_reg[rt(instr)].flags & VALID_01) != VALID_01) != ((CPU_reg[rs(instr)].flags & VALID_01) != VALID_01))
	{
		MakeValid(&CPU_reg[rs(instr)], rsVal);
		MakeValid(&CPU_reg[rt(instr)], rtVal);
	}

	ret = CPU_reg[rs(instr)];
	ret.y = 0.f;
	ret.compFlags[1] = VALID;

	ret.x = (CPU_reg[rs(instr)].y < CPU_reg[rt(instr)].y) ? 1.f : (f16Unsign(CPU_reg[rs(instr)].x) < f16Unsign(CPU_reg[rt(instr)].x)) ? 1.f : 0.f;

	ret.value = rdVal;
	CPU_reg[rd(instr)] = ret;
}

void PGXP_CPU_SLTU(uint32_t instr, uint32_t rdVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Rd = Rs < Rt (unsigned) */
	PGXP_value ret;
	Validate(&CPU_reg[rs(instr)], rsVal);
	Validate(&CPU_reg[rt(instr)], rtVal);

	/* iCB: Only require one valid input */
	if (((CPU_reg[rt(instr)].flags & VALID_01) != VALID_01) != ((CPU_reg[rs(instr)].flags & VALID_01) != VALID_01))
	{
		MakeValid(&CPU_reg[rs(instr)], rsVal);
		MakeValid(&CPU_reg[rt(instr)], rtVal);
	}

	ret = CPU_reg[rs(instr)];
	ret.y = 0.f;
	ret.compFlags[1] = VALID;

	ret.x = (f16Unsign(CPU_reg[rs(instr)].y) < f16Unsign(CPU_reg[rt(instr)].y)) ? 1.f : (f16Unsign(CPU_reg[rs(instr)].x) < f16Unsign(CPU_reg[rt(instr)].x)) ? 1.f : 0.f;

	ret.value = rdVal;
	CPU_reg[rd(instr)] = ret;
}

/* ============================================================
 * Register mult/div
 * ============================================================ */

void PGXP_CPU_MULT(uint32_t instr, uint32_t hiVal, uint32_t loVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Hi/Lo = Rs * Rt (signed) */
	double xx, xy, yx, yy;
	double lx = 0, ly = 0, hx = 0, hy = 0;

	Validate(&CPU_reg[rs(instr)], rsVal);
	Validate(&CPU_reg[rt(instr)], rtVal);

	/* iCB: Only require one valid input */
	if (((CPU_reg[rt(instr)].flags & VALID_01) != VALID_01) != ((CPU_reg[rs(instr)].flags & VALID_01) != VALID_01))
	{
		MakeValid(&CPU_reg[rs(instr)], rsVal);
		MakeValid(&CPU_reg[rt(instr)], rtVal);
	}

	CPU_Lo = CPU_Hi = CPU_reg[rs(instr)];

	CPU_Lo.halfFlags[0] = CPU_Hi.halfFlags[0] = (CPU_reg[rs(instr)].halfFlags[0] & CPU_reg[rt(instr)].halfFlags[0]);

	/* Multiply out components */
	xx = f16Unsign(CPU_reg[rs(instr)].x) * f16Unsign(CPU_reg[rt(instr)].x);
	xy = f16Unsign(CPU_reg[rs(instr)].x) * (CPU_reg[rt(instr)].y);
	yx = (CPU_reg[rs(instr)].y) * f16Unsign(CPU_reg[rt(instr)].x);
	yy = (CPU_reg[rs(instr)].y) * (CPU_reg[rt(instr)].y);

	/* Split values into outputs */
	lx = xx;

	ly = f16Overflow(xx);
	ly += xy + yx;

	hx = f16Overflow(ly);
	hx += yy;

	hy = f16Overflow(hx);

	CPU_Lo.x = f16Sign(lx);
	CPU_Lo.y = f16Sign(ly);
	CPU_Hi.x = f16Sign(hx);
	CPU_Hi.y = f16Sign(hy);

	CPU_Lo.value = loVal;
	CPU_Hi.value = hiVal;
}

void PGXP_CPU_MULTU(uint32_t instr, uint32_t hiVal, uint32_t loVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Hi/Lo = Rs * Rt (unsigned) */
	double xx, xy, yx, yy;
	double lx = 0, ly = 0, hx = 0, hy = 0;

	Validate(&CPU_reg[rs(instr)], rsVal);
	Validate(&CPU_reg[rt(instr)], rtVal);

	/* iCB: Only require one valid input */
	if (((CPU_reg[rt(instr)].flags & VALID_01) != VALID_01) != ((CPU_reg[rs(instr)].flags & VALID_01) != VALID_01))
	{
		MakeValid(&CPU_reg[rs(instr)], rsVal);
		MakeValid(&CPU_reg[rt(instr)], rtVal);
	}

	CPU_Lo = CPU_Hi = CPU_reg[rs(instr)];

	CPU_Lo.halfFlags[0] = CPU_Hi.halfFlags[0] = (CPU_reg[rs(instr)].halfFlags[0] & CPU_reg[rt(instr)].halfFlags[0]);

	/* Multiply out components */
	xx = f16Unsign(CPU_reg[rs(instr)].x) * f16Unsign(CPU_reg[rt(instr)].x);
	xy = f16Unsign(CPU_reg[rs(instr)].x) * f16Unsign(CPU_reg[rt(instr)].y);
	yx = f16Unsign(CPU_reg[rs(instr)].y) * f16Unsign(CPU_reg[rt(instr)].x);
	yy = f16Unsign(CPU_reg[rs(instr)].y) * f16Unsign(CPU_reg[rt(instr)].y);

	/* Split values into outputs */
	lx = xx;

	ly = f16Overflow(xx);
	ly += xy + yx;

	hx = f16Overflow(ly);
	hx += yy;

	hy = f16Overflow(hx);

	CPU_Lo.x = f16Sign(lx);
	CPU_Lo.y = f16Sign(ly);
	CPU_Hi.x = f16Sign(hx);
	CPU_Hi.y = f16Sign(hy);

	CPU_Lo.value = loVal;
	CPU_Hi.value = hiVal;
}

void PGXP_CPU_DIV(uint32_t instr, uint32_t hiVal, uint32_t loVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Lo = Rs / Rt (signed)
	 * Hi = Rs % Rt (signed) */
	double vs, vt, lo, hi;

	Validate(&CPU_reg[rs(instr)], rsVal);
	Validate(&CPU_reg[rt(instr)], rtVal);

	/* iCB: Only require one valid input */
	if (((CPU_reg[rt(instr)].flags & VALID_01) != VALID_01) != ((CPU_reg[rs(instr)].flags & VALID_01) != VALID_01))
	{
		MakeValid(&CPU_reg[rs(instr)], rsVal);
		MakeValid(&CPU_reg[rt(instr)], rtVal);
	}

	CPU_Lo = CPU_Hi = CPU_reg[rs(instr)];

	CPU_Lo.halfFlags[0] = CPU_Hi.halfFlags[0] = (CPU_reg[rs(instr)].halfFlags[0] & CPU_reg[rt(instr)].halfFlags[0]);

	vs = f16Unsign(CPU_reg[rs(instr)].x) + (CPU_reg[rs(instr)].y) * (double)(1 << 16);
	vt = f16Unsign(CPU_reg[rt(instr)].x) + (CPU_reg[rt(instr)].y) * (double)(1 << 16);

	lo = vs / vt;
	CPU_Lo.y = f16Sign(f16Overflow(lo));
	CPU_Lo.x = f16Sign(lo);

	hi = fmod(vs, vt);
	CPU_Hi.y = f16Sign(f16Overflow(hi));
	CPU_Hi.x = f16Sign(hi);

	CPU_Lo.value = loVal;
	CPU_Hi.value = hiVal;
}

void PGXP_CPU_DIVU(uint32_t instr, uint32_t hiVal, uint32_t loVal, uint32_t rsVal, uint32_t rtVal)
{
	/* Lo = Rs / Rt (unsigned)
	 * Hi = Rs % Rt (unsigned) */
	double vs, vt, lo, hi;

	Validate(&CPU_reg[rs(instr)], rsVal);
	Validate(&CPU_reg[rt(instr)], rtVal);

	/* iCB: Only require one valid input */
	if (((CPU_reg[rt(instr)].flags & VALID_01) != VALID_01) != ((CPU_reg[rs(instr)].flags & VALID_01) != VALID_01))
	{
		MakeValid(&CPU_reg[rs(instr)], rsVal);
		MakeValid(&CPU_reg[rt(instr)], rtVal);
	}

	CPU_Lo = CPU_Hi = CPU_reg[rs(instr)];

	CPU_Lo.halfFlags[0] = CPU_Hi.halfFlags[0] = (CPU_reg[rs(instr)].halfFlags[0] & CPU_reg[rt(instr)].halfFlags[0]);

	vs = f16Unsign(CPU_reg[rs(instr)].x) + f16Unsign(CPU_reg[rs(instr)].y) * (double)(1 << 16);
	vt = f16Unsign(CPU_reg[rt(instr)].x) + f16Unsign(CPU_reg[rt(instr)].y) * (double)(1 << 16);

	lo = vs / vt;
	CPU_Lo.y = f16Sign(f16Overflow(lo));
	CPU_Lo.x = f16Sign(lo);

	hi = fmod(vs, vt);
	CPU_Hi.y = f16Sign(f16Overflow(hi));
	CPU_Hi.x = f16Sign(hi);

	CPU_Lo.value = loVal;
	CPU_Hi.value = hiVal;
}

/* ============================================================
 * Shift operations (sa)
 * ============================================================ */
void PGXP_CPU_SLL(uint32_t instr, uint32_t rdVal, uint32_t rtVal)
{
	/* Rd = Rt << Sa */
	PGXP_value ret;
	uint32_t sh = sa(instr);
#if 1
	double x, y;
#else
	double x, y;
	psx_value iX, iY, dX, dY;
#endif

	Validate(&CPU_reg[rt(instr)], rtVal);
	
	ret = CPU_reg[rt(instr)];

	/* TODO: Shift flags */
#if 1 
	x = f16Unsign(CPU_reg[rt(instr)].x);
	y = f16Unsign(CPU_reg[rt(instr)].y);
	if (sh >= 32)
	{
		x = 0.f;
		y = 0.f;
	}
	else if (sh == 16)
	{
		y = f16Sign(x);
		x = 0.f;
	}
	else if (sh >= 16)
	{
		y = x * (1 << (sh - 16));
		y = f16Sign(y);
		x = 0.f;
	}
	else
	{
		x = x * (1 << sh);
		y = y * (1 << sh);
		y += f16Overflow(x);
		x = f16Sign(x);
		y = f16Sign(y);
	}
#else
	x = CPU_reg[rt(instr)].x;
	y = f16Unsign(CPU_reg[rt(instr)].y);

	iX.d = rtVal;
	iY.d = rtVal;

	iX.w.h = 0;		/* remove Y */
	iY.w.l = 0;		/* remove X */

					/* Shift test values */
	dX.d = iX.d << sh;
	dY.d = iY.d << sh;


	if ((dY.sw.h == 0) || (dY.sw.h == -1))
		y = dY.sw.h;
	else
		y = y * (1 << sh);

	if (dX.sw.h != 0.f)
	{
		if (sh == 16)
		{
			y = x;
		}
		else if (sh < 16)
		{
			y += f16Unsign(x) / (1 << (16 - sh));
		}
		else
		{
			y += x * (1 << (sh - 16));
		}
	}

	/* if there's anything left of X write it in */
	if (dX.w.l != 0.f)
		x = x * (1 << sh);
	else
		x = 0;

	x = f16Sign(x);
	y = f16Sign(y);

#endif

	ret.x = x;
	ret.y = y;

	ret.value = rdVal;
	CPU_reg[rd(instr)] = ret;
}

void PGXP_CPU_SRL(uint32_t instr, uint32_t rdVal, uint32_t rtVal)
{
	/* Rd = Rt >> Sa */
	PGXP_value ret;
	uint32_t sh = sa(instr);
	double x, y;
#if 0
	psx_value valt;
	uint16_t mask;
#else
	psx_value iX, iY, dX, dY;
#endif

	Validate(&CPU_reg[rt(instr)], rtVal);

	ret = CPU_reg[rt(instr)];

#if 0
	x = f16Unsign(CPU_reg[rt(instr)].x);
	y = f16Unsign(CPU_reg[rt(instr)].y);
	if (sh >= 32)
	{
		x = y = 0.f;
	}
	else if (sh >= 16)
	{
		x = y / (1 << (sh - 16));
		x = f16Sign(x);
		y = (y < 0) ? -1.f : 0.f;	/* sign extend */
	}
	else
	{
		x = x / (1 << sh);

		/* check for potential sign extension in overflow */
		valt.d = rtVal;
		mask = 0xFFFF >> (16 - sh);
		if ((valt.w.h & mask) == mask)
			x += mask << (16 - sh);
		else if ((valt.w.h & mask) == 0)
			x = x;
		else
			x += y * (1 << (16 - sh));/*f16Overflow(y); */

		y = y / (1 << sh);
		x = f16Sign(x);
		y = f16Sign(y);
	}
#else
	x = CPU_reg[rt(instr)].x;
	y = f16Unsign(CPU_reg[rt(instr)].y);

	iX.d = rtVal;
	iY.d = rtVal;

	iX.sd = (int32_t)(int16_t)iX.sw.l;	/* sign-extend low 16 to 32, removing Y */
	iY.sw.l = iX.sw.h;				/* overwrite x with sign(x) */

									/* Shift test values */
	dX.sd = iX.sd >> sh;
	dY.d = iY.d >> sh;

	if (dX.sw.l != iX.sw.h)
		x = x / (1u << sh);
	else
		x = dX.sw.l;	/* only sign bits left */

	if (dY.sw.l != iX.sw.h)
	{
		if (sh == 16)
		{
			x = y;
		}
		else if (sh < 16)
		{
			x += y * (1 << (16 - sh));
			if (CPU_reg[rt(instr)].x < 0)
				x += 1 << (16 - sh);
		}
		else
		{
			x += y / (1 << (sh - 16));
		}
	}

	if ((dY.sw.h == 0) || (dY.sw.h == -1))
		y = dY.sw.h;
	else
		y = y / (1u << sh);

	x = f16Sign(x);
	y = f16Sign(y);

#endif
	ret.x = x;
	ret.y = y;

	ret.value = rdVal;
	CPU_reg[rd(instr)] = ret;
}

void PGXP_CPU_SRA(uint32_t instr, uint32_t rdVal, uint32_t rtVal)
{
	/* Rd = Rt >> Sa */
	PGXP_value ret;
	uint32_t sh = sa(instr);
	double x, y;
#if 0
	psx_value valt;
	uint16_t mask;
#else
	psx_value iX, iY, dX, dY;
#endif

	Validate(&CPU_reg[rt(instr)], rtVal);
	ret = CPU_reg[rt(instr)];

#if 0
	x = f16Unsign(CPU_reg[rt(instr)].x);
	y = (CPU_reg[rt(instr)].y);
	if (sh >= 32)
	{
		/* sign extend */
		x = y = (y < 0) ? -1.f : 0.f;
	}
	else if (sh >= 16)
	{
		x = y / (1 << (sh - 16));
		x = f16Sign(x);
		y = (y < 0) ? -1.f : 0.f;	/* sign extend */
	}
	else
	{
		x = x / (1 << sh);
		
		/* check for potential sign extension in overflow */
		valt.d = rtVal;
		mask = 0xFFFF >> (16 - sh);
		if ((valt.w.h & mask) == mask)
			x += mask << (16 - sh);
		else if ((valt.w.h & mask) == 0)
			x = x;
		else
			x += y * (1 << (16 - sh));/*f16Overflow(y); */

		y = y / (1 << sh);
		x = f16Sign(x);
		y = f16Sign(y);
	}

#else
	x = CPU_reg[rt(instr)].x;
	y = CPU_reg[rt(instr)].y;

	iX.d = rtVal;
	iY.d = rtVal;

	iX.sd = (int32_t)(int16_t)iX.sw.l;	/* sign-extend low 16 to 32, removing Y */
	iY.sw.l = iX.sw.h;				/* overwrite x with sign(x) */

									/* Shift test values */
	dX.sd = iX.sd >> sh;
	dY.sd = iY.sd >> sh;

	if (dX.sw.l != iX.sw.h)
		x = x / (1u << sh);
	else
		x = dX.sw.l;	/* only sign bits left */

	if (dY.sw.l != iX.sw.h)
	{
		if (sh == 16)
		{
			x = y;
		}
		else if (sh < 16)
		{
			x += y * (1 << (16 - sh));
			if (CPU_reg[rt(instr)].x < 0)
				x += 1 << (16 - sh);
		}
		else
		{
			x += y / (1 << (sh - 16));
		}
	}

	if ((dY.sw.h == 0) || (dY.sw.h == -1))
		y = dY.sw.h;
	else
		y = y / (1u << sh);

	x = f16Sign(x);
	y = f16Sign(y);

#endif

	ret.x = x;
	ret.y = y;

	ret.value = rdVal;
	CPU_reg[rd(instr)] = ret;
}

/* ============================================================
 * Shift operations variable
 * ============================================================ */
void PGXP_CPU_SLLV(uint32_t instr, uint32_t rdVal, uint32_t rtVal, uint32_t rsVal)
{
	/* Rd = Rt << Rs */
	PGXP_value ret;
	uint32_t sh = rsVal & 0x1F;
#if 1
	double x, y;
#else
	double x, y;
	psx_value iX, iY, dX, dY;
#endif

	Validate(&CPU_reg[rt(instr)], rtVal);
	Validate(&CPU_reg[rs(instr)], rsVal);

	ret = CPU_reg[rt(instr)];

#if 1
	x = f16Unsign(CPU_reg[rt(instr)].x);
	y = f16Unsign(CPU_reg[rt(instr)].y);
	if (sh >= 32)
	{
		x = 0.f;
		y = 0.f;
	}
	else if (sh == 16)
	{
		y = f16Sign(x);
		x = 0.f;
	}
	else if (sh >= 16)
	{
		y = x * (1 << (sh - 16));
		y = f16Sign(y);
		x = 0.f;
	}
	else
	{
		x = x * (1 << sh);
		y = y * (1 << sh);
		y += f16Overflow(x);
		x = f16Sign(x);
		y = f16Sign(y);
	}
#else
	x = CPU_reg[rt(instr)].x;
	y = f16Unsign(CPU_reg[rt(instr)].y);

	iX.d = rtVal;
	iY.d = rtVal;

	iX.w.h = 0;		/* remove Y */
	iY.w.l = 0;		/* remove X */

					/* Shift test values */
	dX.d = iX.d << sh;
	dY.d = iY.d << sh;


	if ((dY.sw.h == 0) || (dY.sw.h == -1))
		y = dY.sw.h;
	else
		y = y * (1 << sh);

	if (dX.sw.h != 0.f)
	{
		if (sh == 16)
		{
			y = x;
		}
		else if (sh < 16)
		{
			y += f16Unsign(x) / (1 << (16 - sh));
		}
		else
		{
			y += x * (1 << (sh - 16));
		}
	}

	/* if there's anything left of X write it in */
	if (dX.w.l != 0.f)
		x = x * (1 << sh);
	else
		x = 0;

	x = f16Sign(x);
	y = f16Sign(y);

#endif
	ret.x = x;
	ret.y = y;

	ret.value = rdVal;
	CPU_reg[rd(instr)] = ret;
}

void PGXP_CPU_SRLV(uint32_t instr, uint32_t rdVal, uint32_t rtVal, uint32_t rsVal)
{
	/* Rd = Rt >> Sa */
	PGXP_value ret;
	uint32_t sh = rsVal & 0x1F;
	double x, y;
#if 0
	psx_value valt;
	uint16_t mask;
#else
	psx_value iX, iY, dX, dY;
#endif

	Validate(&CPU_reg[rt(instr)], rtVal);
	Validate(&CPU_reg[rs(instr)], rsVal);

	ret = CPU_reg[rt(instr)];

#if 0
	x = f16Unsign(CPU_reg[rt(instr)].x);
	y = f16Unsign(CPU_reg[rt(instr)].y);
	if (sh >= 32)
	{
		x = y = 0.f;
	}
	else if (sh >= 16)
	{
		x = y / (1 << (sh - 16));
		x = f16Sign(x);
		y = (y < 0) ? -1.f : 0.f;	/* sign extend */
	}
	else
	{
		x = x / (1 << sh);
		
		/* check for potential sign extension in overflow */
		valt.d = rtVal;
		mask = 0xFFFF >> (16 - sh);
		if ((valt.w.h & mask) == mask)
			x += mask << (16 - sh);
		else if ((valt.w.h & mask) == 0)
			x = x;
		else
			x += y * (1 << (16 - sh));/*f16Overflow(y); */

		y = y / (1 << sh);
		x = f16Sign(x);
		y = f16Sign(y);
	}

#else
	x = CPU_reg[rt(instr)].x;
	y = f16Unsign(CPU_reg[rt(instr)].y);

	iX.d = rtVal;
	iY.d = rtVal;

	iX.sd = (int32_t)(int16_t)iX.sw.l;	/* sign-extend low 16 to 32, removing Y */
	iY.sw.l = iX.sw.h;				/* overwrite x with sign(x) */

									/* Shift test values */
	dX.sd = iX.sd >> sh;
	dY.d = iY.d >> sh;

	if (dX.sw.l != iX.sw.h)
		x = x / (1u << sh);
	else
		x = dX.sw.l;	/* only sign bits left */

	if (dY.sw.l != iX.sw.h)
	{
		if (sh == 16)
		{
			x = y;
		}
		else if (sh < 16)
		{
			x += y * (1 << (16 - sh));
			if (CPU_reg[rt(instr)].x < 0)
				x += 1 << (16 - sh);
		}
		else
		{
			x += y / (1 << (sh - 16));
		}
	}

	if ((dY.sw.h == 0) || (dY.sw.h == -1))
		y = dY.sw.h;
	else
		y = y / (1u << sh);

	x = f16Sign(x);
	y = f16Sign(y);

#endif

	ret.x = x;
	ret.y = y;

	ret.value = rdVal;
	CPU_reg[rd(instr)] = ret;
}

void PGXP_CPU_SRAV(uint32_t instr, uint32_t rdVal, uint32_t rtVal, uint32_t rsVal)
{
	/* Rd = Rt >> Sa */
	PGXP_value ret;
	uint32_t sh = rsVal & 0x1F;
	double x, y;
#if 0
	psx_value valt;
	uint16_t mask;
#else
	psx_value iX, iY, dX, dY;
#endif

	Validate(&CPU_reg[rt(instr)], rtVal);
	Validate(&CPU_reg[rs(instr)], rsVal);

	ret = CPU_reg[rt(instr)];
#if 0
	x = f16Unsign(CPU_reg[rt(instr)].x);
	y = f16Unsign(CPU_reg[rt(instr)].y);
	if (sh >= 32)
	{
		x = y = 0.f;
	}
	else if (sh >= 16)
	{
		x = y / (1 << (sh - 16));
		x = f16Sign(x);
		y = (y < 0) ? -1.f : 0.f;	/* sign extend */
	}
	else
	{
		x = x / (1 << sh);
		
		/* check for potential sign extension in overflow */
		valt.d = rtVal;
		mask = 0xFFFF >> (16 - sh);
		if ((valt.w.h & mask) == mask)
			x += mask << (16 - sh);
		else if ((valt.w.h & mask) == 0)
			x = x;
		else
			x += y * (1 << (16 - sh));/*f16Overflow(y); */

		y = y / (1 << sh);
		x = f16Sign(x);
		y = f16Sign(y);
	}

#else
	x = CPU_reg[rt(instr)].x;
	y = CPU_reg[rt(instr)].y;

	iX.d = rtVal;
	iY.d = rtVal;

	iX.sd = (int32_t)(int16_t)iX.sw.l;	/* sign-extend low 16 to 32, removing Y */
	iY.sw.l = iX.sw.h;				/* overwrite x with sign(x) */

									/* Shift test values */
	dX.sd = iX.sd >> sh;
	dY.sd = iY.sd >> sh;

	if (dX.sw.l != iX.sw.h)
		x = x / (1u << sh);
	else
		x = dX.sw.l;	/* only sign bits left */

	if (dY.sw.l != iX.sw.h)
	{
		if (sh == 16)
		{
			x = y;
		}
		else if (sh < 16)
		{
			x += y * (1 << (16 - sh));
			if (CPU_reg[rt(instr)].x < 0)
				x += 1 << (16 - sh);
		}
		else
		{
			x += y / (1 << (sh - 16));
		}
	}

	if ((dY.sw.h == 0) || (dY.sw.h == -1))
		y = dY.sw.h;
	else
		y = y / (1u << sh);

	x = f16Sign(x);
	y = f16Sign(y);

#endif

	ret.x = x;
	ret.y = y;

	ret.value = rdVal;
	CPU_reg[rd(instr)] = ret;
}

/* ============================================================
 * Move registers
 * ============================================================ */
void PGXP_CPU_MFHI(uint32_t instr, uint32_t rdVal, uint32_t hiVal)
{
	/* Rd = Hi */
	Validate(&CPU_Hi, hiVal);

	CPU_reg[rd(instr)] = CPU_Hi;
}

void PGXP_CPU_MTHI(uint32_t instr, uint32_t hiVal, uint32_t rdVal)
{
	/* Hi = Rd */
	Validate(&CPU_reg[rd(instr)], rdVal);

	CPU_Hi = CPU_reg[rd(instr)];
}

void PGXP_CPU_MFLO(uint32_t instr, uint32_t rdVal, uint32_t loVal)
{
	/* Rd = Lo */
	Validate(&CPU_Lo, loVal);

	CPU_reg[rd(instr)] = CPU_Lo;
}

void PGXP_CPU_MTLO(uint32_t instr, uint32_t loVal, uint32_t rdVal)
{
	/* Lo = Rd */
	Validate(&CPU_reg[rd(instr)], rdVal);

	CPU_Lo = CPU_reg[rd(instr)];
}

/* ============================================================
 * Memory Access
 * ============================================================ */

/* Load 32-bit word */
void PGXP_CPU_LWL(uint32_t instr, uint32_t rtVal, uint32_t addr)
{
	/* Rt = Mem[Rs + Im] */
	PGXP_CPU_LW(instr, rtVal, addr);
}

void PGXP_CPU_LW(uint32_t instr, uint32_t rtVal, uint32_t addr)
{
	/* Rt = Mem[Rs + Im] */
	ValidateAndCopyMem(&CPU_reg[rt(instr)], addr, rtVal);
}

void PGXP_CPU_LWR(uint32_t instr, uint32_t rtVal, uint32_t addr)
{
	/* Rt = Mem[Rs + Im] */
	PGXP_CPU_LW(instr, rtVal, addr);
}

/* Load 16-bit */
void PGXP_CPU_LH(uint32_t instr, uint16_t rtVal, uint32_t addr)
{
	/* Rt = Mem[Rs + Im] (sign extended) */
	psx_value val;
	val.sd = (int32_t)(int16_t)rtVal;
	ValidateAndCopyMem16(&CPU_reg[rt(instr)], addr, val.d, 1);
}

void PGXP_CPU_LHU(uint32_t instr, uint16_t rtVal, uint32_t addr)
{
	/* Rt = Mem[Rs + Im] (zero extended) */
	psx_value val;
	val.d = rtVal;
	val.w.h = 0;
	ValidateAndCopyMem16(&CPU_reg[rt(instr)], addr, val.d, 0);
}

/* Load 8-bit */
void PGXP_CPU_LB(uint32_t instr, uint8_t rtVal, uint32_t addr)
{
	InvalidLoad(addr, instr, 116);
}

void PGXP_CPU_LBU(uint32_t instr, uint8_t rtVal, uint32_t addr)
{
	InvalidLoad(addr, instr, 116);
}

/* Store 32-bit word */
void PGXP_CPU_SWL(uint32_t instr, uint32_t rtVal, uint32_t addr)
{
	/* Mem[Rs + Im] = Rt */
	PGXP_CPU_SW(instr, rtVal, addr);
}

void PGXP_CPU_SW(uint32_t instr, uint32_t rtVal, uint32_t addr)
{
	/* Mem[Rs + Im] = Rt */
	Validate(&CPU_reg[rt(instr)], rtVal);
	WriteMem(&CPU_reg[rt(instr)], addr);
}

void PGXP_CPU_SWR(uint32_t instr, uint32_t rtVal, uint32_t addr)
{
	/* Mem[Rs + Im] = Rt */
	PGXP_CPU_SW(instr, rtVal, addr);
}

/* Store 16-bit */
void PGXP_CPU_SH(uint32_t instr, uint16_t rtVal, uint32_t addr)
{
	/* validate and copy half value */
	MaskValidate(&CPU_reg[rt(instr)], rtVal, 0xFFFF, VALID_0);
	WriteMem16(&CPU_reg[rt(instr)], addr);
}

/* Store 8-bit */
void PGXP_CPU_SB(uint32_t instr, uint8_t rtVal, uint32_t addr)
{
	InvalidStore(addr, instr, 208);
}

/* ============================================================
 * Data transfer tracking
 * ============================================================ */
void PGXP_CP0_MFC0(uint32_t instr, uint32_t rtVal, uint32_t rdVal)
{
	/* CPU[Rt] = CP0[Rd] */
	Validate(&CP0_reg[rd(instr)], rdVal);
	CPU_reg[rt(instr)] = CP0_reg[rd(instr)];
	CPU_reg[rt(instr)].value = rtVal;
}

void PGXP_CP0_MTC0(uint32_t instr, uint32_t rdVal, uint32_t rtVal)
{
	/* CP0[Rd] = CPU[Rt] */
	Validate(&CPU_reg[rt(instr)], rtVal);
	CP0_reg[rd(instr)] = CPU_reg[rt(instr)];
	CP0_reg[rd(instr)].value = rdVal;
}

void PGXP_CP0_CFC0(uint32_t instr, uint32_t rtVal, uint32_t rdVal)
{
	/* CPU[Rt] = CP0[Rd] */
	Validate(&CP0_reg[rd(instr)], rdVal);
	CPU_reg[rt(instr)] = CP0_reg[rd(instr)];
	CPU_reg[rt(instr)].value = rtVal;
}

void PGXP_CP0_CTC0(uint32_t instr, uint32_t rdVal, uint32_t rtVal)
{
	/* CP0[Rd] = CPU[Rt] */
	Validate(&CPU_reg[rt(instr)], rtVal);
	CP0_reg[rd(instr)] = CPU_reg[rt(instr)];
	CP0_reg[rd(instr)].value = rdVal;
}

void PGXP_CP0_RFE(uint32_t instr)
{}

/* ------------------------------------------------------------------------- *
 * Unified PGXP CPU-tracking dispatch.
 *
 * The 44 PGXP_CPU_* trackers above are hooked one-by-one from the mednafen
 * interpreter's per-instruction handlers (cpu.c BEGIN_OPF bodies).  The
 * lightrec recompiler does not run those handlers, so under DYNAREC_EXECUTE
 * the CPU-side tracking is never invoked.  To let recompiled code drive the
 * same tracking, the JIT needs (a) a way to ask, at block-compile time,
 * whether a given instruction is tracked, and (b) a single entry point to
 * call at run time that routes to the correct tracker.
 *
 * PGXP_CPU_Tracks(instr) answers (a) -- pure function of the opcode, so the
 * recompiler can decide whether to emit a tracking call site at all.
 *
 * PGXP_CPU_Dispatch() answers (b).  It is passed the canonical post-execution
 * values the recompiler already has in hand (the destination/result values and
 * the source operand values, plus the effective address for loads/stores) and
 * forwards them to the matching tracker with that tracker's exact argument
 * marshalling -- including the narrowed result widths for sub-word loads and
 * the operand ordering quirks of the shift-variable and HI/LO moves.  Values
 * the selected tracker does not consume are ignored, so the recompiler may
 * pass 0 for them.
 *
 * This is a thin, behaviour-preserving routing layer: for any given
 * instruction it performs exactly the same tracker call (same args, same
 * order) that the interpreter performs, so the resulting CPU_reg[] precision
 * state is identical regardless of which backend drove it.
 * ------------------------------------------------------------------------- */

/* Does this instruction have a PGXP CPU-mode tracker?  Pure opcode predicate. */
int PGXP_CPU_Tracks(uint32_t instr)
{
   switch (op(instr))
   {
      case 0x00: /* SPECIAL */
         switch (func(instr))
         {
            case 0x00: /* SLL  */ case 0x02: /* SRL  */ case 0x03: /* SRA  */
            case 0x04: /* SLLV */ case 0x06: /* SRLV */ case 0x07: /* SRAV */
            case 0x10: /* MFHI */ case 0x11: /* MTHI */
            case 0x12: /* MFLO */ case 0x13: /* MTLO */
            case 0x18: /* MULT */ case 0x19: /* MULTU */
            case 0x1A: /* DIV  */ case 0x1B: /* DIVU */
            case 0x20: /* ADD  */ case 0x21: /* ADDU */
            case 0x22: /* SUB  */ case 0x23: /* SUBU */
            case 0x24: /* AND  */ case 0x25: /* OR   */
            case 0x26: /* XOR  */ case 0x27: /* NOR  */
            case 0x2A: /* SLT  */ case 0x2B: /* SLTU */
               return 1;
            default:
               return 0;
         }
      case 0x08: /* ADDI  */ case 0x09: /* ADDIU */
      case 0x0A: /* SLTI  */ case 0x0B: /* SLTIU */
      case 0x0C: /* ANDI  */ case 0x0D: /* ORI   */
      case 0x0E: /* XORI  */ case 0x0F: /* LUI   */
      case 0x20: /* LB    */ case 0x21: /* LH    */
      case 0x22: /* LWL   */ case 0x23: /* LW    */
      case 0x24: /* LBU   */ case 0x25: /* LHU   */
      case 0x26: /* LWR   */
      case 0x28: /* SB    */ case 0x29: /* SH    */
      case 0x2A: /* SWL   */ case 0x2B: /* SW    */
      case 0x2E: /* SWR   */
         return 1;
      default:
         return 0;
   }
}

/* Route one tracked instruction to its PGXP_CPU_* tracker.
 *   rdVal : result written to Rd (R-type) or Rt (I-type / loads)
 *   rsVal : Rs source value      (also Rt source for stores)
 *   rtVal : Rt source value
 *   hiVal,loVal : HI/LO results (mult/div) or HI/LO sources (mfhi/mflo/...)
 *   addr  : effective memory address (loads/stores)
 * The recompiler passes the values it has; unused ones are ignored. */
void PGXP_CPU_Dispatch(uint32_t instr,
                       uint32_t rdVal, uint32_t rsVal, uint32_t rtVal,
                       uint32_t hiVal, uint32_t loVal, uint32_t addr)
{
   switch (op(instr))
   {
      case 0x00: /* SPECIAL */
         switch (func(instr))
         {
            case 0x00: PGXP_CPU_SLL  (instr, rdVal, rtVal);               break;
            case 0x02: PGXP_CPU_SRL  (instr, rdVal, rtVal);               break;
            case 0x03: PGXP_CPU_SRA  (instr, rdVal, rtVal);               break;
            case 0x04: PGXP_CPU_SLLV (instr, rdVal, rtVal, rsVal);        break;
            case 0x06: PGXP_CPU_SRLV (instr, rdVal, rtVal, rsVal);        break;
            case 0x07: PGXP_CPU_SRAV (instr, rdVal, rtVal, rsVal);        break;
            case 0x10: PGXP_CPU_MFHI (instr, rdVal, hiVal);               break;
            case 0x11: PGXP_CPU_MTHI (instr, hiVal, rsVal);               break;
            case 0x12: PGXP_CPU_MFLO (instr, rdVal, loVal);               break;
            case 0x13: PGXP_CPU_MTLO (instr, loVal, rsVal);               break;
            case 0x18: PGXP_CPU_MULT (instr, hiVal, loVal, rsVal, rtVal); break;
            case 0x19: PGXP_CPU_MULTU(instr, hiVal, loVal, rsVal, rtVal); break;
            case 0x1A: PGXP_CPU_DIV  (instr, hiVal, loVal, rsVal, rtVal); break;
            case 0x1B: PGXP_CPU_DIVU (instr, hiVal, loVal, rsVal, rtVal); break;
            case 0x20: PGXP_CPU_ADD  (instr, rdVal, rsVal, rtVal);        break;
            case 0x21: PGXP_CPU_ADDU (instr, rdVal, rsVal, rtVal);        break;
            case 0x22: PGXP_CPU_SUB  (instr, rdVal, rsVal, rtVal);        break;
            case 0x23: PGXP_CPU_SUBU (instr, rdVal, rsVal, rtVal);        break;
            case 0x24: PGXP_CPU_AND  (instr, rdVal, rsVal, rtVal);        break;
            case 0x25: PGXP_CPU_OR   (instr, rdVal, rsVal, rtVal);        break;
            case 0x26: PGXP_CPU_XOR  (instr, rdVal, rsVal, rtVal);        break;
            case 0x27: PGXP_CPU_NOR  (instr, rdVal, rsVal, rtVal);        break;
            case 0x2A: PGXP_CPU_SLT  (instr, rdVal, rsVal, rtVal);        break;
            case 0x2B: PGXP_CPU_SLTU (instr, rdVal, rsVal, rtVal);        break;
            default: break;
         }
         break;
      /* I-type: rdVal carries the Rt result, rsVal the Rs source. */
      case 0x08: PGXP_CPU_ADDI (instr, rdVal, rsVal); break;
      case 0x09: PGXP_CPU_ADDIU(instr, rdVal, rsVal); break;
      case 0x0A: PGXP_CPU_SLTI (instr, rdVal, rsVal); break;
      case 0x0B: PGXP_CPU_SLTIU(instr, rdVal, rsVal); break;
      case 0x0C: PGXP_CPU_ANDI (instr, rdVal, rsVal); break;
      case 0x0D: PGXP_CPU_ORI  (instr, rdVal, rsVal); break;
      case 0x0E: PGXP_CPU_XORI (instr, rdVal, rsVal); break;
      case 0x0F: PGXP_CPU_LUI  (instr, rdVal);        break;
      /* Loads: rdVal carries the Rt result (narrowed for sub-word). */
      case 0x20: PGXP_CPU_LB (instr, (uint8_t) rdVal, addr); break;
      case 0x21: PGXP_CPU_LH (instr, (uint16_t)rdVal, addr); break;
      case 0x22: PGXP_CPU_LWL(instr, rdVal, addr);           break;
      case 0x23: PGXP_CPU_LW (instr, rdVal, addr);           break;
      case 0x24: PGXP_CPU_LBU(instr, (uint8_t) rdVal, addr); break;
      case 0x25: PGXP_CPU_LHU(instr, (uint16_t)rdVal, addr); break;
      case 0x26: PGXP_CPU_LWR(instr, rdVal, addr);           break;
      /* Stores: rtVal carries the Rt source being written out. */
      case 0x28: PGXP_CPU_SB (instr, (uint8_t) rtVal, addr); break;
      case 0x29: PGXP_CPU_SH (instr, (uint16_t)rtVal, addr); break;
      case 0x2A: PGXP_CPU_SWL(instr, rtVal, addr);           break;
      case 0x2B: PGXP_CPU_SW (instr, rtVal, addr);           break;
      case 0x2E: PGXP_CPU_SWR(instr, rtVal, addr);           break;
      default: break;
   }
}
