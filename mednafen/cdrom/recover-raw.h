#ifndef _RECOVER_RAW_H
#define _RECOVER_RAW_H

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CD_RAW_SECTOR_SIZE 2352  
#define CD_RAW_C2_SECTOR_SIZE (2352+294)  /* main channel plus C2 vector */

int CheckEDC(const unsigned char *a, bool b);
int CheckMSF(unsigned char *a, int b);


int ValidateRawSector(unsigned char *frame, bool xaMode);
bool Init_LEC_Correct(void);
void Kill_LEC_Correct(void);

#ifdef __cplusplus
}
#endif

#endif
