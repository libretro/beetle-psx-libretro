/* mdec_idct_test: the MDEC's IDCT (mednafen/psx/mdec_idct.h) against the
 * plain sums, pass by pass as the hardware takes them.
 *
 * Blocks of random coefficients at every density, from all zeros to
 * full, at the coefficients' limits, and rows of zeros between rows of
 * values, through the default matrix the decoder loads, random matrices
 * and matrices at their limits.  Every output byte must match.
 *
 * Usage: mdec_idct_test [blocks]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../mednafen/psx/mdec_idct.h"

static uint32_t rng = 0x9E3779B9u;

static uint32_t next_rand(void)
{
   rng ^= rng << 13;
   rng ^= rng >> 17;
   rng ^= rng << 5;
   return rng;
}

/* The decoder's limits: coefficients clamp to -0x4000..0x3FFF and the
 * matrix holds the scale table's 16-bit values shifted down 3 */
static int16_t rand_coeff(void)
{
   switch (next_rand() % 8)
   {
      case 0: return -0x4000;
      case 1: return 0x3FFF;
      case 2: return (int16_t)((int32_t)(next_rand() % 0x8000) - 0x4000);
      default: return (int16_t)((int32_t)(next_rand() % 513) - 256);
   }
}

static int16_t rand_matrix(void)
{
   switch (next_rand() % 6)
   {
      case 0: return (int16_t)(-0x8000 >> 3);
      case 1: return (int16_t)(0x7FFF >> 3);
      default: return (int16_t)((int16_t)(next_rand() & 0xFFFF) >> 3);
   }
}

/* The passes as plain sums */
static void reference(const int16_t *m, const int16_t *in, int8_t *out)
{
   int16_t  t[64];
   unsigned col, x, u;
   for (col = 0; col < 8; col++)
      for (x = 0; x < 8; x++)
      {
         int32_t sum = 0;
         for (u = 0; u < 8; u++)
            sum += in[col * 8 + u] * m[x * 8 + u];
         t[x * 8 + col] = (int16_t)((sum + 0x4000) >> 15);
      }
   for (col = 0; col < 8; col++)
      for (x = 0; x < 8; x++)
      {
         int32_t sum = 0, v;
         for (u = 0; u < 8; u++)
            sum += t[col * 8 + u] * m[x * 8 + u];
         v = (sum + 0x4000) >> 15;
         v = (v & 0x100) ? (v | ~0x1FF) : (v & 0x1FF);
         out[col * 8 + x] = (int8_t)(v < -128 ? -128 : v > 127 ? 127 : v);
      }
}

int main(int argc, char **argv)
{
   /* cos(k pi / 16) for k = 0..7, cos(0) as cos(pi / 4), in 1.15 */
   static const int16_t scale[8] = {
      0x5A82, 0x7D8A, 0x7641, 0x6A6D, 0x5A82, 0x471C, 0x30FB, 0x18F8 };
   unsigned long blocks = argc > 1 ? strtoul(argv[1], NULL, 10) : 400000;
   unsigned long b;
   int16_t       m[64];
   union { int16_t v[64]; mdec_idct_matrix align; } in;
   int8_t        got[64], want[64];
   mdec_idct_matrix p;
   unsigned      i, x, u;

   for (b = 0; b < blocks; b++)
   {
      unsigned density;
      if (b % 1000 == 0)
      {
         /* Every third matrix the cosine table games load: row x holds
          * cos((2u + 1) x pi / 16), scaled; the others random */
         for (x = 0; x < 8; x++)
            for (u = 0; u < 8; u++)
            {
               unsigned a = (x * (2 * u + 1)) % 32;
               int32_t  c;
               if (a > 16)
                  a = 32 - a;
               c = x ? (a > 8 ? -(a == 16 ? scale[0] : scale[16 - a])
                              : (a == 8 ? 0 : scale[a]))
                     : scale[0];
               if (a == 8)
                  c = 0;
               m[x * 8 + u] = (b / 1000) % 3 == 0
                  ? (int16_t)(c >> 3) : rand_matrix();
            }
         mdec_idct_prepare(&p, m);
      }
      /* From all zeros to full, rows of zeros among them */
      density = next_rand() % 66;
      for (i = 0; i < 64; i++)
         in.v[i] = (next_rand() % 64) < density ? rand_coeff() : 0;
      if (next_rand() % 4 == 0)
         memset(&in.v[(next_rand() % 8) * 8], 0, 16);
      reference(m, in.v, want);
      mdec_idct(&p, in.v, got);
      if (memcmp(got, want, 64))
      {
         printf("[mdec_idct] FAIL: block %lu, byte", b);
         for (i = 0; i < 64; i++)
            if (got[i] != want[i])
            {
               printf(" %u: %d, not %d\n", i, got[i], want[i]);
               break;
            }
         return 1;
      }
   }
   printf("[mdec_idct] %lu blocks\n", blocks);
   printf("[mdec_idct] OK\n");
   return 0;
}
