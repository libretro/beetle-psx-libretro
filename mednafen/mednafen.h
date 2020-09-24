#ifndef _MEDNAFEN_H
#define _MEDNAFEN_H

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include "mednafen-types.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

void MDFND_DispMessage(
      unsigned priority, enum retro_log_level level,
      enum retro_message_target target, enum retro_message_type type,
      const char *msg);

#ifdef __cplusplus
extern "C" {
#endif
void MDFN_DispMessage(
      unsigned priority, enum retro_log_level level,
      enum retro_message_target target, enum retro_message_type type,
      const char *format, ...);
#ifdef __cplusplus
}
#endif

void MDFN_LoadGameCheats(void *override);
void MDFN_FlushGameCheats(int nosave);

#endif
