#ifndef __MDFN_PSX_DEBUG_H
#define __MDFN_PSX_DEBUG_H

#ifdef WANT_DEBUGGER

extern DebuggerInfoStruct PSX_DBGInfo;

bool DBG_Init(void);

void DBG_Break(void);

void DBG_GPUScanlineHook(unsigned scanline);

#endif

#endif
