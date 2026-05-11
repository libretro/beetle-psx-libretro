/* Copyright (c) 2017-2018 Hans-Kristian Arntzen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

#include "libretro.h"
namespace Granite
{
extern retro_log_printf_t libretro_log;
}
#define LOGE(...) do { if (::Granite::libretro_log) ::Granite::libretro_log(RETRO_LOG_ERROR, __VA_ARGS__); } while(0)
#define LOGI(...) do { if (::Granite::libretro_log) ::Granite::libretro_log(RETRO_LOG_INFO, __VA_ARGS__); } while(0)

#ifdef _MSC_VER
#include <intrin.h>
#endif

#ifdef __GNUC__
#define leading_zeroes(x) ((x) == 0 ? 32 : __builtin_clz(x))
#define trailing_zeroes(x) ((x) == 0 ? 32 : __builtin_ctz(x))
#define trailing_ones(x) __builtin_ctz(~(x))
#elif defined(_MSC_VER)
static inline uint32_t util_clz(uint32_t x)
{
	unsigned long result;
	if (_BitScanReverse(&result, x))
		return 31 - result;
	return 32;
}

static inline uint32_t util_ctz(uint32_t x)
{
	unsigned long result;
	if (_BitScanForward(&result, x))
		return result;
	return 32;
}

#define leading_zeroes(x) util_clz(x)
#define trailing_zeroes(x) util_ctz(x)
#define trailing_ones(x) util_ctz(~(x))
#else
#error "Implement me."
#endif

/* Iterate over each set bit in a uint32_t mask. Inside the body, BIT_VAR holds
 * the bit index. C-style: just expand into a for loop, no captures or lambdas. */
#define FOR_EACH_BIT(value, bit_var)                                          \
	for (uint32_t _fe_v = (uint32_t)(value), bit_var = trailing_zeroes(_fe_v); \
	     _fe_v;                                                                \
	     _fe_v &= _fe_v - 1u, bit_var = trailing_zeroes(_fe_v))

/* Iterate over each contiguous run of 1-bits in a uint32_t mask. BASE_VAR is
 * the bit index of the run's first 1, RANGE_VAR is the run length. */
#define FOR_EACH_BIT_RANGE(value, base_var, range_var)                            \
	for (uint32_t _fe_v = (uint32_t)(value),                                       \
	              base_var = trailing_zeroes(_fe_v),                               \
	              range_var = (_fe_v ? trailing_ones(_fe_v >> base_var) : 0u);     \
	     _fe_v;                                                                    \
	     _fe_v &= ~((1u << (base_var + range_var)) - 1u),                          \
	     base_var = trailing_zeroes(_fe_v),                                        \
	     range_var = (_fe_v ? trailing_ones(_fe_v >> base_var) : 0u))
