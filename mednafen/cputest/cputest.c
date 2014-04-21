/*
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "cputest.h"
#include "../libretro.h"

extern retro_get_cpu_features_t perf_get_cpu_features_cb;

static int flags, checked = 0;

void cputest_force_flags(int arg)
{
}

int cputest_get_flags(void)
{
   unsigned cpu = 0;
   flags = 0;
   if (perf_get_cpu_features_cb)
      cpu = perf_get_cpu_features_cb();

   if (cpu & RETRO_SIMD_MMX)
      flags |= CPUTEST_FLAG_MMX;
   if (cpu & RETRO_SIMD_SSE)
      flags |= CPUTEST_FLAG_SSE;
   if (cpu & RETRO_SIMD_SSE2)
      flags |= CPUTEST_FLAG_SSE2;
   if (cpu & RETRO_SIMD_SSE3)
      flags |= CPUTEST_FLAG_SSE3;
   if (cpu & RETRO_SIMD_SSSE3)
      flags |= CPUTEST_FLAG_SSSE3;
   if (cpu & RETRO_SIMD_SSE4)
      flags |= CPUTEST_FLAG_SSE4;
   if (cpu & RETRO_SIMD_SSE42)
      flags |= CPUTEST_FLAG_SSE42;
   if (cpu & RETRO_SIMD_AVX)
      flags |= CPUTEST_FLAG_AVX;
   if (cpu & RETRO_SIMD_VMX)
      flags |= CPUTEST_FLAG_ALTIVEC;

    checked = 1;
    return flags;
}
