#ifndef __MDFN_PSX_MDEC_H
#define __MDFN_PSX_MDEC_H

namespace MDFN_IEN_PSX
{

void MDEC_DMAWrite(uint32_t V);

uint32_t MDEC_DMARead(void);

void MDEC_Write(const pscpu_timestamp_t timestamp, uint32_t A, uint32_t V);
uint32_t MDEC_Read(const pscpu_timestamp_t timestamp, uint32_t A);


void MDEC_Power(void);

bool MDEC_DMACanWrite(void);
bool MDEC_DMACanRead(void);
void MDEC_Run(int32 clocks);

int MDEC_StateAction(StateMem *sm, int load, int data_only);
}

#endif
