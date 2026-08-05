/* Verifies the LTI simplification of the biquad parallel scan.
 *
 * Reference: the serial recurrence s[n] = A*s[n-1] + B[n].
 * Model A:   the current shader - Hillis-Steele carrying (A,B) pairs.
 * Model B:   proposed - carry B only, and reconstruct the A factor.
 *
 * The claim under test: at Hillis-Steele round with offset `step`, every lane
 * that composes (lane >= step) holds a block-count of exactly min(lane+1,step)
 * = step, so its accumulated A is A^(PER_THREAD*step) for ALL such lanes. A is
 * therefore uniform across the workgroup at every round, needs no shared
 * memory, and is obtained by squaring a register-resident matrix each round.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define LANES      256
#define PER_THREAD 12
#define N          (LANES * PER_THREAD)

typedef struct { double m[2][2]; } mat2;
typedef struct { double v[2];    } vec2;

static mat2 mmul(mat2 a, mat2 b)
{
   mat2 r;
   int i, j;
   for (i = 0; i < 2; i++)
      for (j = 0; j < 2; j++)
         r.m[i][j] = a.m[i][0]*b.m[0][j] + a.m[i][1]*b.m[1][j];
   return r;
}
static vec2 mvec(mat2 a, vec2 b)
{
   vec2 r;
   r.v[0] = a.m[0][0]*b.v[0] + a.m[0][1]*b.v[1];
   r.v[1] = a.m[1][0]*b.v[0] + a.m[1][1]*b.v[1];
   return r;
}
static vec2 vadd(vec2 a, vec2 b) { vec2 r; r.v[0]=a.v[0]+b.v[0]; r.v[1]=a.v[1]+b.v[1]; return r; }

static double f[N];      /* feed-forward FIR output */
static double ref[N];

int main(void)
{
   /* Same design the shader uses: zeros r=0.99, poles r=0.85, on fsc. */
   const double w  = 2.0 * 3.14159265358979323846 / 15.0;
   const double fq = 0.99, iq = 0.85;
   double b0 = 1.0, b1 = -2.0*fq*cos(w), b2 = fq*fq;
   double a1 = -2.0*iq*cos(w), a2 = iq*iq;   /* y[n] = f[n] - a1 y[n-1] - a2 y[n-2] */
   mat2 A;
   int i, step, lane;
   double maxA = 0.0, maxB = 0.0;

   A.m[0][0] = -a1; A.m[0][1] = -a2;
   A.m[1][0] = 1.0; A.m[1][1] = 0.0;

   srand(12345);
   {
      double x[N + 2];
      for (i = 0; i < N + 2; i++)
         x[i] = (double)rand() / RAND_MAX * 2.0 - 1.0;
      for (i = 0; i < N; i++)
         f[i] = b0*x[i+2] + b1*x[i+1] + b2*x[i];
   }

   /* ---- serial reference ---- */
   {
      double y1 = 0.0, y2 = 0.0;
      for (i = 0; i < N; i++)
      {
         double y = f[i] - a1*y1 - a2*y2;
         ref[i] = y; y2 = y1; y1 = y;
      }
   }

   /* ---- shared per-lane block pass (identical in both models) ---- */
   static vec2 Bpart[LANES][PER_THREAD];
   static mat2 accA_A[LANES]; static vec2 accB_A[LANES];   /* model A */
   static vec2 accB_B[LANES];                              /* model B */
   mat2 blockA = A;
   for (i = 1; i < PER_THREAD; i++) blockA = mmul(A, blockA);

   for (lane = 0; lane < LANES; lane++)
   {
      vec2 acc = {{0.0, 0.0}};
      int k;
      for (k = 0; k < PER_THREAD; k++)
      {
         vec2 add = {{f[lane*PER_THREAD + k], 0.0}};
         acc = vadd(mvec(A, acc), add);
         Bpart[lane][k] = acc;
      }
      accA_A[lane] = blockA;
      accB_A[lane] = acc;
      accB_B[lane] = acc;
   }

   /* ---- model A: carry (A,B) through shared memory ---- */
   for (step = 1; step < LANES; step <<= 1)
   {
      static mat2 sA[LANES]; static vec2 sB[LANES];
      memcpy(sA, accA_A, sizeof(sA));
      memcpy(sB, accB_A, sizeof(sB));
      for (lane = 0; lane < LANES; lane++)
         if (lane >= step)
         {
            accB_A[lane] = vadd(mvec(accA_A[lane], sB[lane-step]), accB_A[lane]);
            accA_A[lane] = mmul(accA_A[lane], sA[lane-step]);
         }
   }

   /* ---- model B: carry B only; A is uniform = blockA^step, by squaring ---- */
   {
      mat2 M = blockA;      /* A^(PER_THREAD * step), step = 1 */
      for (step = 1; step < LANES; step <<= 1)
      {
         static vec2 sB[LANES];
         memcpy(sB, accB_B, sizeof(sB));
         for (lane = 0; lane < LANES; lane++)
            if (lane >= step)
               accB_B[lane] = vadd(mvec(M, sB[lane-step]), accB_B[lane]);
         M = mmul(M, M);   /* step doubles -> exponent doubles */
      }
   }

   /* ---- compare both models against the serial reference ---- */
   for (lane = 0; lane < LANES; lane++)
   {
      vec2 preA = {{0.0,0.0}}, preB = {{0.0,0.0}};
      mat2 Apow;
      int k;
      if (lane > 0) { preA = accB_A[lane-1]; preB = accB_B[lane-1]; }
      Apow.m[0][0]=1; Apow.m[0][1]=0; Apow.m[1][0]=0; Apow.m[1][1]=1;
      for (k = 0; k < PER_THREAD; k++)
      {
         double ya, yb, r;
         Apow = mmul(A, Apow);
         ya = vadd(mvec(Apow, preA), Bpart[lane][k]).v[0];
         yb = vadd(mvec(Apow, preB), Bpart[lane][k]).v[0];
         r  = ref[lane*PER_THREAD + k];
         if (fabs(ya - r) > maxA) maxA = fabs(ya - r);
         if (fabs(yb - r) > maxB) maxB = fabs(yb - r);
      }
   }

   printf("model A (current, carries A+B) max abs err vs serial: %.3e\n", maxA);
   printf("model B (proposed, carries B)  max abs err vs serial: %.3e\n", maxB);
   printf("%s\n", (maxB < 1e-9 && maxA < 1e-9) ? "EQUIVALENT" : "MISMATCH");
   return (maxB < 1e-9 && maxA < 1e-9) ? 0 : 1;
}
