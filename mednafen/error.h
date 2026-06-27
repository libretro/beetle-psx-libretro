#ifndef __MDFN_ERROR_H
#define __MDFN_ERROR_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

void MDFN_Error(int errno_code, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
