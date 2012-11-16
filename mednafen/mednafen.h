#ifndef _MEDNAFEN_H
#define _MEDNAFEN_H

#include "mednafen-types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _(String) (String)

#include "math_ops.h"
#include "git.h"

extern MDFNGI *MDFNGameInfo;

#include "settings.h"

void MDFN_PrintError(const char *format, ...);
void MDFN_printf(const char *format, ...);
void MDFN_DispMessage(const char *format, ...);

void MDFN_DebugPrintReal(const char *file, const int line, const char *format, ...);

#define gettext_noop(format, ...) (format)
#define MDFN_DebugPrint(format, ...) MDFN_DebugPrintReal(__FILE__, __LINE__, format, ## __VA_ARGS__)

void MDFN_LoadGameCheats(FILE *override);
void MDFN_FlushGameCheats(int nosave);
void MDFN_DoSimpleCommand(int cmd);
void MDFN_QSimpleCommand(int cmd);

void MDFN_MidSync(EmulateSpecStruct *espec);

#include "state.h"
int MDFN_RawInputStateAction(StateMem *sm, int load, int data_only);

#include "mednafen-driver.h"

#include "mednafen-endian.h"
#include "mednafen-memory.h"

#endif
