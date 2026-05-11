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
#include <stddef.h>

namespace Util
{
using Hash = uint64_t;

class Hasher
{
public:
	Hasher(Hash h)
		: h(h)
	{
	}

	Hasher() = default;

	inline void data(const uint32_t *p, size_t bytes)
	{
		size_t count = bytes / sizeof(uint32_t);
		for (size_t i = 0; i < count; i++)
			h = (h * 0x100000001b3ull) ^ p[i];
	}

	inline void u32(uint32_t value)
	{
		h = (h * 0x100000001b3ull) ^ value;
	}

	inline void u64(uint64_t value)
	{
		u32(value & 0xffffffffu);
		u32(value >> 32);
	}

	inline Hash get() const
	{
		return h;
	}

private:
	Hash h = 0xcbf29ce484222325ull;
};
}
