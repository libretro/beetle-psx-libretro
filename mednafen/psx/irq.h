#ifndef __MDFN_PSX_IRQ_H
#define __MDFN_PSX_IRQ_H

#include <stdint.h>

enum
{
 IRQ_VBLANK     = 0,
 IRQ_GPU        = 1,
 IRQ_CD         = 2,
 IRQ_DMA        = 3,
 IRQ_TIMER_0    = 4,
 IRQ_TIMER_1    = 5,
 IRQ_TIMER_2    = 6,
 IRQ_SIO        = 7,
 IRQ_SPU        = 9,
 IRQ_PIO        = 10
};

enum
{
 IRQ_GSREG_ASSERTED  = 0,
 IRQ_GSREG_STATUS    = 1,
 IRQ_GSREG_MASK      = 2
};

void IRQ_Power(void);
void IRQ_Assert(int which, bool asserted);

void IRQ_Write(uint32_t A, uint32_t V);
uint32_t IRQ_Read(uint32_t A);

uint32_t IRQ_GetRegister(unsigned int which, char *special, const uint32_t special_len);
void IRQ_SetRegister(unsigned int which, uint32_t value);

int IRQ_StateAction(void *data, int load, int data_only);

#endif
