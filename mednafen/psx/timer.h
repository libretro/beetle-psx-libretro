#ifndef __MDFN_PSX_TIMER_H
#define __MDFN_PSX_TIMER_H

#include <stdint.h>

enum
{
   TIMER_GSREG_COUNTER0 = 0x00,
   TIMER_GSREG_MODE0,
   TIMER_GSREG_TARGET0,

   TIMER_GSREG_COUNTER1 = 0x10,
   TIMER_GSREG_MODE1,
   TIMER_GSREG_TARGET1,

   TIMER_GSREG_COUNTER2 = 0x20,
   TIMER_GSREG_MODE2,
   TIMER_GSREG_TARGET2
};

uint32_t TIMER_GetRegister(unsigned int which, char *special, const uint32_t special_len);
void TIMER_SetRegister(unsigned int which, uint32_t value);


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

#endif
