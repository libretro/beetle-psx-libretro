#ifndef _L_EC_H
#define _L_EC_H

#include <stdint.h>
#include "galois.h"

#ifdef __cplusplus
extern "C" {
#endif

#define N_P_VECTORS   86      /* 43 16bit p vectors */
#define P_VECTOR_SIZE 26      /* using RS(26,24) ECC */

#define N_Q_VECTORS   52      /* 26 16bit q vectors */
#define Q_VECTOR_SIZE 45      /* using RS(45,43) ECC */

#define P_PADDING 229         /* padding values for */
#define Q_PADDING 210         /* shortened RS code  */

int PToByteIndex(int, int);
int QToByteIndex(int, int);
void ByteIndexToP(int, int*, int*);
void ByteIndexToQ(int, int*, int*);

void GetPVector(unsigned char*, unsigned char*, int);
void SetPVector(unsigned char*, unsigned char*, int);
void FillPVector(unsigned char*, unsigned char, int);
void AndPVector(unsigned char*, unsigned char, int);
void OrPVector(unsigned char*, unsigned char, int);

void GetQVector(unsigned char*, unsigned char*, int);
void SetQVector(unsigned char*, unsigned char*, int);
void FillQVector(unsigned char*, unsigned char, int);
void AndQVector(unsigned char*, unsigned char, int);
void OrQVector(unsigned char*, unsigned char, int);

int DecodePQ(ReedSolomonTables*, unsigned char*, int, int*, int);

int CountC2Errors(unsigned char*);

#ifdef __cplusplus
}
#endif

#endif
