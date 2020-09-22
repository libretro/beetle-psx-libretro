#ifndef _MEDNAFEN_H
#define _MEDNAFEN_H

#include <libretro.h>
#include "mednafen-types.h"
#include <stdlib.h>
#include <string.h>

#define _(String) (String)

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

/* This breaks on OSX for some reason */
#if 0
#if __cplusplus <= 199711L
# define HAS_CXX11
#endif
#endif

#define GET_FDATA_PTR(fp) (fp->data)
#define GET_FSIZE_PTR(fp) (fp->size)
#define GET_FEXTS_PTR(fp) (fp->ext)

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
