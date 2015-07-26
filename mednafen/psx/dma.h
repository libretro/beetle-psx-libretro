#ifndef __MDFN_PSX_DMA_H
#define __MDFN_PSX_DMA_H

int32_t DMA_Update(const int32_t timestamp);
void DMA_Write(const int32_t timestamp, uint32_t A, uint32_t V);
uint32_t DMA_Read(const int32_t timestamp, uint32_t A);

void DMA_ResetTS(void);

void DMA_Power(void);

void DMA_Init(void);
void DMA_Kill(void);

int DMA_StateAction(StateMem *sm, int load, int data_only);

#endif
