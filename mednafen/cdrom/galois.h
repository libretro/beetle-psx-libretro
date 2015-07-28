#ifndef _GALOIS_H
#define _GALOIS_H

#include <stdint.h>
#include <retro_inline.h>

#ifdef __cplusplus
extern "C" {
#endif

/***
 *** galois.c
 ***
 * This is currently the hardcoded GF(2**8).
 * int32_t gives abundant space for the GF.
 * Squeezing it down to uint8 won't probably gain much,
 * so we implement this defensively here.
 *
 * Note that some performance critical stuff needs to
 * be #included from galois-inlines.h
 */  

/* Galois field parameters for 8bit symbol Reed-Solomon code */

#define GF_SYMBOLSIZE 8
#define GF_FIELDSIZE (1<<GF_SYMBOLSIZE)
#define GF_FIELDMAX (GF_FIELDSIZE-1)
#define GF_ALPHA0 GF_FIELDMAX

/* Lookup tables for Galois field arithmetic */

typedef struct _GaloisTables
{  int32_t gfGenerator;  /* GF generator polynomial */ 
   int32_t *indexOf;     /* log */
   int32_t *alphaTo;     /* inverse log */
   int32_t *encAlphaTo; /* inverse log optimized for encoder */
} GaloisTables;

/* Lookup and working tables for the ReedSolomon codecs */

typedef struct _ReedSolomonTables
{
   GaloisTables *gfTables;/* from above */
   int32_t *gpoly;        /* RS code generator polynomial */
   int32_t fcr;           /* first consecutive root of RS generator polynomial */
   int32_t primElem;      /* primitive field element */
   int32_t nroots;        /* degree of RS generator polynomial */
   int32_t ndata;         /* data bytes per ecc block */
} ReedSolomonTables;

/*
 * The following routine is performance critical.
 */

static INLINE int mod_fieldmax(int x)
{
   while (x >= GF_FIELDMAX) 
   {
      x -= GF_FIELDMAX;
      x = (x >> GF_SYMBOLSIZE) + (x & GF_FIELDMAX);
   }

   return x;
}

GaloisTables* CreateGaloisTables(int32_t a);
void FreeGaloisTables(GaloisTables *a);

ReedSolomonTables *CreateReedSolomonTables(GaloisTables *a, int32_t b, int32_t c, int d);
void FreeReedSolomonTables(ReedSolomonTables *a);

#ifdef __cplusplus
}
#endif

#endif
