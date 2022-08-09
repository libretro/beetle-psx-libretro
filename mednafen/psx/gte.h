#ifndef __MDFN_PSX_GTE_H
#define __MDFN_PSX_GTE_H

#include "../state.h"

void GTE_SwitchRegisters(bool use_regs, uint32_t *regs);

void GTE_Init(void);
void GTE_Power(void);
int GTE_StateAction(StateMem *sm, int load, int data_only);

int32 GTE_Instruction(uint32_t instr);

void GTE_WriteCR(unsigned int which, uint32_t value);
void GTE_WriteDR(unsigned int which, uint32_t value);

uint32_t GTE_ReadCR(unsigned int which);
uint32_t GTE_ReadDR(unsigned int which);

#endif
