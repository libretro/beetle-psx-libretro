#ifndef MDFN_SETTINGS_H
#define MDFN_SETTINGS_H

#include <string>

#if defined(WANT_PCE_FAST_EMU)
extern int setting_pce_fast_nospritelimit;
extern int setting_pce_fast_cddavolume;
extern int setting_pce_fast_adpcmvolume;
extern int setting_pce_fast_cdpsgvolume;
extern uint32_t setting_pce_fast_cdspeed;
extern uint32_t setting_pce_keepaspect;
#elif defined(WANT_PSX_EMU)
extern uint32_t setting_psx_multitap_port_1;
extern uint32_t setting_psx_multitap_port_2;
extern uint32_t setting_psx_analog_toggle;
extern uint32_t setting_psx_fastboot;
#elif defined(WANT_NGP_EMU)
extern uint32_t setting_ngp_language;
#elif defined(WANT_GBA_EMU)
extern uint32_t setting_gba_hle;
#elif defined(WANT_VB_EMU)
extern uint32_t setting_vb_lcolor;
extern uint32_t setting_vb_rcolor;
extern uint32_t setting_vb_anaglyph_preset;
#endif

bool MDFN_LoadSettings(const char *path, const char *section = NULL, bool override = false);
bool MDFN_MergeSettings(const void*);
bool MDFN_MergeSettings(const std::vector<void> &);
bool MDFN_SaveSettings(const char *path);

void MDFN_KillSettings(void);	// Free any resources acquired.

// This should assert() or something if the setting isn't found, since it would
// be a totally tubular error!
uint64 MDFN_GetSettingUI(const char *name);
int64 MDFN_GetSettingI(const char *name);
double MDFN_GetSettingF(const char *name);
bool MDFN_GetSettingB(const char *name);
std::string MDFN_GetSettingS(const char *name);
#endif
