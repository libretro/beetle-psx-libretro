#ifndef __MDFN_PSX_PSX_MEM_H
#define __MDFN_PSX_PSX_MEM_H

#include "../masmem.h"

/* MainRAM (2 MB), BIOSROM (512 KB), ScratchRAM (1 KB) singletons.
 * Defined in libretro.c, used by every memory-touching TU.  C TUs
 * use the MASMEM_* helpers from masmem.h directly; the C-accessor
 * forwarders that lived here during the cpu.c -> .c conversion are
 * gone now that every consumer is in C.
 *
 * PIOMem (64 KB) is also a MultiAccessSizeMem instance but only
 * libretro.c touches it; no extern needed here. */

extern MultiAccessSizeMem *MainRAM;
extern MultiAccessSizeMem *BIOSROM;
extern MultiAccessSizeMem *ScratchRAM;

#endif
