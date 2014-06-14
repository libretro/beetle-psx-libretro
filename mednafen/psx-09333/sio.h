#ifndef __MDFN_PSX_SIO_H
#define __MDFN_PSX_SIO_H

namespace MDFN_IEN_PSX
{

void SIO_Write(pscpu_timestamp_t timestamp, uint32_t A, uint32_t V);
uint32_t SIO_Read(pscpu_timestamp_t timestamp, uint32_t A);
void SIO_Power(void);

}

#endif
