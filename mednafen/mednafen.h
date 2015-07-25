#ifndef _MEDNAFEN_H
#define _MEDNAFEN_H

#include "mednafen-types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define _(String) (String)

#include "math_ops.h"
#include "git.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

#define GET_FDATA_PTR(fp) (fp->data)
#define GET_FSIZE_PTR(fp) (fp->size)
#define GET_FEXTS_PTR(fp) (fp->ext)
#define gzopen(a, b) fopen(a, b)
#define gzread(a, b, c) fread(b, c, 1, a)
#define gzclose(a) fclose(a)
#define gzgetc(a) fgetc(a)
#define gzseek(a,b,c) fseek(a,b,c)

extern MDFNGI *MDFNGameInfo;

#include "settings.h"

void MDFN_DispMessage(const char *format, ...);

void MDFN_LoadGameCheats(void *override);
void MDFN_FlushGameCheats(int nosave);

#include "mednafen-driver.h"

#include "mednafen-endian.h"

#endif
