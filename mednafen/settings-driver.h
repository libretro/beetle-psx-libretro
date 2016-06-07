#ifndef _MDFN_SETTINGS_DRIVER_H
#define _MDFN_SETTINGS_DRIVER_H

#include <boolean.h>

#include "settings-common.h"

bool MDFNI_SetSetting(const char *name, const char *value, bool NetplayOverride = false);
bool MDFNI_SetSettingB(const char *name, bool value);
bool MDFNI_SetSettingUI(const char *name, uint64_t value);

bool MDFNI_DumpSettingsDef(const char *path);

#endif
