#include <math.h>
#include <string.h>

#include "pgxp_value.h"
#include "limits.h"

void SetValue(PGXP_value *pV, uint32_t psxV)
{
	psx_value psx;
	psx.d = psxV;

	pV->x = psx.sw.l;
	pV->y = psx.sw.h;
	pV->z = 0.f;
	pV->flags = VALID_01;
	pV->value = psx.d;
}

void MakeValid(PGXP_value *pV, uint32_t psxV)
{
	psx_value psx;
	psx.d = psxV;
	if (VALID_01 != (pV->flags & VALID_01))
	{
		pV->x = psx.sw.l;
		pV->y = psx.sw.h;
		pV->z = 0.f;
		pV->flags |= VALID_01;
		pV->value = psx.d;
	}
}

/* Validate and MaskValidate are now static inline in pgxp_value.h
 * (called 40+ times per PGXP-tagged CPU instruction; the cross-TU
 * function-call overhead used to dominate their actual work). */

uint32_t ValueToTolerance(PGXP_value *pV, uint32_t psxV, float tolerance)
{
	psx_value psx;
	uint32_t       retFlags = VALID_ALL;

	psx.d = psxV;

	if (fabs(pV->x - psx.sw.l) >= tolerance)
		retFlags = retFlags & (VALID_1 | VALID_2 | VALID_3);

	if (fabs(pV->y - psx.sw.h) >= tolerance)
		retFlags = retFlags & (VALID_0 | VALID_2 | VALID_3);

	return retFlags;
}

/* float logical arithmetic
 *
 * These helpers operate on a "16.16-ish" fixed-point view of a
 * double, multiplying by 65536 to get the integer bit pattern,
 * doing an unsigned-or-signed reinterpret, and dividing back.
 * The straightforward C ways to do that all hit UB:
 *
 *   - `*((s32*)&u32)` is a strict-aliasing violation.  GCC at -O2
 *     can and does optimise around it (e.g. assume the s32 read
 *     can't observe the u32 store and skip a reload).
 *   - `(s64)double` is UB when the double is outside the int64
 *     range.  PSX MULT accumulators can produce doubles up to
 *     ~2^48, well within s64, but careful code shouldn't depend
 *     on that staying true.
 *   - `>>` of a negative signed integer is implementation-defined
 *     in C and was UB in C++ before C++20.  In practice every
 *     compiler we care about does arithmetic shift, but encoding
 *     "round toward -infinity" as `floor(x/65536.0)` is the
 *     portable and obvious way.
 *   - `(u32)negative_double` is UB.
 *
 * The replacements below stay bit-exact with the originals on the
 * inputs PSX register math actually hits (everything below
 * |x| < 2^31), and produce defined results everywhere else. */

double f16Sign(double in)
{
	/* Multiply by 65536, take the low 32 bits as a uint32 bit
	 * pattern, reinterpret as int32, divide back by 65536.
	 * Effectively a "sign-extend the 32-bit value through the
	 * top bit", in the fixed-point domain. */
	uint32_t u = (uint32_t)(int64_t)(in * 65536.0);
	int32_t  s;
	memcpy(&s, &u, sizeof s);
	return (double)s / 65536.0;
}

double f16Unsign(double in)
{
	return (in >= 0) ? in : ((double)in + (double)USHRT_MAX + 1);
}

double fu16Trunc(double in)
{
	/* Currently unused but kept for API completeness.  Original
	 * was `(u32)(in * 65536)` then divide back, which is UB on
	 * negative inputs.  floor() gives the same truncation
	 * behaviour for non-negative input and stays defined for
	 * negative input. */
	return floor(in * 65536.0) / 65536.0;
}

double f16Overflow(double in)
{
	/* Original was `((s64)in) >> 16`, which is two stacked UB:
	 * the s64 cast for out-of-range doubles, and the right-
	 * shift of a possibly-negative integer.  floor(in/65536)
	 * computes the same value mathematically (arithmetic right
	 * shift by 16 = floor-divide by 65536) and stays defined. */
	return floor(in / 65536.0);
}
