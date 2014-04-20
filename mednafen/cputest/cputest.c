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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

static int flags, checked = 0;


void cputest_force_flags(int arg)
{
    flags   = arg;
    checked = 1;
}

int cputest_get_flags(void)
{
    if (checked)
        return flags;

//    if (ARCH_ARM) flags = ff_get_cpu_flags_arm();
#if ARCH_POWERPC
    flags = ff_get_cpu_flags_ppc();
#endif

#if ARCH_X86
    flags = ff_get_cpu_flags_x86();
#endif

    checked = 1;
    return flags;
}
