#ifndef _MEDNAFEN_H
#define _MEDNAFEN_H

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include "mednafen-types.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

#ifdef __cplusplus
extern "C" {
#endif

void MDFN_LoadGameCheats(void *override);
void MDFN_FlushGameCheats(int nosave);

#ifdef __cplusplus
}
#endif

#endif
