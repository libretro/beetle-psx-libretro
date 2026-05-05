#ifndef __MDFN_PSX_TIMER_H
#define __MDFN_PSX_TIMER_H

#include <stdint.h>
#include <boolean.h>

#include "../mednafen-types.h"

#ifdef __cplusplus
extern "C" {
#endif

void MDFN_FASTCALL TIMER_Write(const int32_t timestamp, uint32_t A, uint16_t V);
uint16_t MDFN_FASTCALL TIMER_Read(const int32_t timestamp, uint32_t A);

void MDFN_FASTCALL TIMER_AddDotClocks(uint32_t count);
void TIMER_ClockHRetrace(void);
void MDFN_FASTCALL TIMER_SetHRetrace(bool status);
void MDFN_FASTCALL TIMER_SetVBlank(bool status);

int32_t MDFN_FASTCALL TIMER_Update(const int32_t);
void TIMER_ResetTS(void);

void TIMER_Power(void) MDFN_COLD;
int TIMER_StateAction(void *sm, const unsigned load, const bool data_only);

#ifdef __cplusplus
}
#endif

#endif
