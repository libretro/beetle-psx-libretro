#ifndef __MDFN_PSX_SIO_H
#define __MDFN_PSX_SIO_H

#include <stdint.h>

void SIO_Write(int32_t timestamp, uint32_t A, uint32_t V);
uint32_t SIO_Read(int32_t timestamp, uint32_t A);
void SIO_Power(void);

int SIO_StateAction(void *data, int load, int data_only);

#endif
