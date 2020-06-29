#ifndef __MDFN_MEDNAFEN_DRIVER_H
#define __MDFN_MEDNAFEN_DRIVER_H

#include <libretro.h>

#include <stdio.h>
#include <vector>
#include <string>

#include "settings-common.h"

extern std::vector<MDFNGI *>MDFNSystems;

// Call this function as early as possible, even before MDFNI_Initialize()
bool MDFNI_InitializeModule(void);

/* Sets the base directory(save states, snapshots, etc. are saved in directories
   below this directory. */
void MDFNI_SetBaseDirectory(const char *dir);

/* Closes currently loaded game */
void MDFNI_CloseGame(void);

void MDFND_DispMessage(
      unsigned priority, enum retro_log_level level,
      enum retro_message_target target, enum retro_message_type type,
      const char *msg);

void MDFN_DispMessage(
      unsigned priority, enum retro_log_level level,
      enum retro_message_target target, enum retro_message_type type,
      const char *format, ...);

uint32 MDFNI_CRC32(uint32 crc, uint8 *buf, uint32 len);

#endif
