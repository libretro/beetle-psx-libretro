#ifndef __MDFN_VIDEO_H
#define __MDFN_VIDEO_H

#include <libretro.h>

#include "video/surface.h"

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

#endif
