#ifndef __MDFN_PSX_DMA_H
#define __MDFN_PSX_DMA_H

#include <stdint.h>

#include "../state.h"

int32_t DMA_Update(const int32_t timestamp);
void DMA_Write(const int32_t timestamp, uint32_t A, uint32_t V);

/* The event handler's path for the virtual DMA event: DMA's own
 * bookkeeping for the span (exact over any span while idle), none of
 * the per-device work. */
int32_t DMA_VirtualAdvance(const int32_t event_time);

/* Bring the GPU's state to the last point DMA's cadence ran and make
 * DMA's event real (PSX_WakeVirtual's second half). */
void DMA_WakeVirtual(void);

/* A write that gives the DMA controller or the MDEC something to do:
 * resets the idle hysteresis. */
void DMA_NoteActivity(void);
uint32_t DMA_Read(const int32_t timestamp, uint32_t A);

void DMA_ResetTS(void);

void DMA_Power(void);

int DMA_StateAction(StateMem *sm, int load, int data_only);

#endif
