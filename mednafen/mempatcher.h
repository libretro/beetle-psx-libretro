#ifndef __MDFN_MEMPATCHER_H
#define __MDFN_MEMPATCHER_H

#include <stdint.h>
#include <boolean.h>

#include "mempatcher-driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __SUBCHEAT
{
   uint32_t addr;
   uint8_t  value;
   int      compare; /* < 0 on no compare */
} SUBCHEAT;

bool MDFNMP_Init(uint32_t ps, uint32_t numpages);
void MDFNMP_AddRAM(uint32_t size, uint32_t address, uint8_t *RAM);
void MDFNMP_Kill(void);

void MDFNMP_InstallReadPatches(void);
void MDFNMP_RemoveReadPatches(void);

void MDFNMP_ApplyPeriodicCheats(void);
void MDFNMP_RegSearchable(uint32_t addr, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
