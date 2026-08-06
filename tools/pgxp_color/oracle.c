/* PGXP precise-colour oracle check.
 *
 * Drives the *real* PGXP_GetColor() (pgxp/pgxp_gpu.c, linked in) against a
 * replica of the GTE ColorFIFO write, and asserts the two agree on every
 * MAC value it is fed. No emulator, no GPU, no content.
 *
 * The property under test is the one the renderer slice depends on:
 *
 *   for every MAC triple, requantizing the float shadow that
 *   MAC_to_RGB_FIFO pushed reproduces the architectural RGB bytes exactly
 *
 * If that holds, a shadow accepted by PGXP_GetColor cannot be a colour the
 * game altered: any alteration changes at least one byte, and any byte
 * change fails the requantization test. If it fails anywhere, the accept
 * rule is unsound and precise colours could be applied to a primitive the
 * game meant to be a different colour.
 *
 * Build and run: make -C tools/pgxp_color check
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../pgxp/pgxp_gpu.h"
#include "../../pgxp/pgxp_types.h"
#include "../../pgxp/pgxp_value.h"

/* --- Replica of the GTE side -------------------------------------------
 * gte.c: RGB_FIFO(2) = Lm_C(0, MAC(1) >> 4) | (Lm_C(1, MAC(2) >> 4) << 8)
 *                    | (Lm_C(2, MAC(3) >> 4) << 16) | (RGB_CD << 24);
 * with Lm_C saturating to 0..0xFF. MAC is int32 and >>4 is arithmetic
 * (floor) under the compiler flags the core builds with (-fwrapv, gcc/clang
 * arithmetic right shift). Mirrored rather than included because Lm_C is a
 * static INLINE that also mutates GTE FLAGS.
 */
static uint8_t lm_c_replica(int32_t v)
{
   if (v < 0)
      return 0;
   if (v > 0xFF)
      return 0xFF;
   return (uint8_t)v;
}

static uint32_t gte_pack(int32_t mac1, int32_t mac2, int32_t mac3, uint8_t cd)
{
   return  (uint32_t)lm_c_replica(mac1 >> 4)
        | ((uint32_t)lm_c_replica(mac2 >> 4) << 8)
        | ((uint32_t)lm_c_replica(mac3 >> 4) << 16)
        | ((uint32_t)cd << 24);
}

/* --- Replica of the push in MAC_to_RGB_FIFO ---------------------------- */
static void push_shadow(uint32_t cb_slot, int32_t mac1, int32_t mac2,
                        int32_t mac3, uint32_t packed)
{
   PGXP_value v;
   memset(&v, 0, sizeof(v));
   v.x     = (float)mac1 / 16.0f;
   v.y     = (float)mac2 / 16.0f;
   v.z     = (float)mac3 / 16.0f;
   v.value = packed;
   v.flags = VALID_ALL;
   PGXP_WriteCB(&v, cb_slot);
}

static uint64_t checked, accepted, rejected, mismatches;

/* One trial: push the shadow, ask the real PGXP_GetColor, verify. */
static void trial(int32_t mac1, int32_t mac2, int32_t mac3, uint8_t cd)
{
   uint32_t packed = gte_pack(mac1, mac2, mac3, cd);
   float    rgb[3];
   int      hit;

   push_shadow(0, mac1, mac2, mac3, packed);
   hit = PGXP_GetColor(0, &packed, rgb);
   checked++;

   if (!hit)
   {
      /* An unaltered colour must always be accepted: a rejection here is a
       * silent loss of the whole feature, not a safety failure, but it
       * means the float shadow did not survive its own round trip. */
      rejected++;
      if (mismatches < 10)
         fprintf(stderr,
               "REJECT  mac=(%d,%d,%d) packed=%08x\n",
               mac1, mac2, mac3, packed);
      mismatches++;
      return;
   }

   accepted++;

   /* And the value handed back must be the pre-saturation value, in 8-bit
    * scale, not the quantized byte. Only meaningful where the GTE did not
    * saturate; where it did, the shadow is legitimately outside 0..255. */
   {
      float want1 = (float)mac1 / 16.0f;
      float want2 = (float)mac2 / 16.0f;
      float want3 = (float)mac3 / 16.0f;
      if (rgb[0] != want1 || rgb[1] != want2 || rgb[2] != want3)
      {
         if (mismatches < 10)
            fprintf(stderr,
                  "PAYLOAD mac=(%d,%d,%d) got=(%f,%f,%f) want=(%f,%f,%f)\n",
                  mac1, mac2, mac3, rgb[0], rgb[1], rgb[2],
                  want1, want2, want3);
         mismatches++;
      }
   }
}

/* A colour the game altered after the GTE produced it must be refused. */
static void trial_altered(int32_t mac1, int32_t mac2, int32_t mac3,
                          uint32_t altered_word)
{
   uint32_t packed = gte_pack(mac1, mac2, mac3, 0);
   float    rgb[3];

   push_shadow(0, mac1, mac2, mac3, packed);
   checked++;
   if (PGXP_GetColor(0, &altered_word, rgb))
   {
      /* Accepting is only sound if the altered word quantizes identically,
       * i.e. the shadow is still within half an LSB of the truth. */
      uint32_t requant = gte_pack(mac1, mac2, mac3, 0) & 0x00FFFFFFu;
      if ((altered_word & 0x00FFFFFFu) != requant)
      {
         if (mismatches < 10)
            fprintf(stderr,
                  "FALSE-ACCEPT mac=(%d,%d,%d) altered=%08x\n",
                  mac1, mac2, mac3, altered_word);
         mismatches++;
      }
      accepted++;
   }
   else
      rejected++;
}

static uint32_t rng_state = 0x2545F491u;
static uint32_t rng(void)
{
   rng_state ^= rng_state << 13;
   rng_state ^= rng_state >> 17;
   rng_state ^= rng_state << 5;
   return rng_state;
}

int main(void)
{
   int32_t  a, b, c;
   uint32_t i;

   /* 1. Exhaustive over the whole non-saturating domain plus a margin on
    *    both sides: MAC>>4 in [-16, 271] covers every value that produces a
    *    distinct byte, both clamps, and the boundaries between them. All
    *    16 sub-LSB fractional positions of each are visited. */
   printf("[1] exhaustive: MAC in [-256, 4351] on all three channels "
          "(sub-LSB stride 1)\n");
   for (a = -256; a <= 4351; a++)
      trial(a, a, a, 0x00);
   /* channel independence: sweep one against fixed extremes on the others */
   for (a = -256; a <= 4351; a++)
   {
      trial(a, -4096, 65536, 0x7F);
      trial(65536, a, -4096, 0xFF);
      trial(-4096, 65536, a, 0x80);
   }

   /* 2. The saturating tails, including the extremes of int32 where the
    *    float conversion is lossy. The claim is that lossy conversion is
    *    harmless because both sides saturate. */
   printf("[2] saturating tails and int32 extremes\n");
   trial(INT32_MAX, INT32_MAX, INT32_MAX, 0);
   trial(INT32_MIN, INT32_MIN, INT32_MIN, 0);
   trial(INT32_MAX, INT32_MIN, 0, 0xAB);
   for (i = 0; i < 24; i++)
   {
      int32_t big = (int32_t)(1u << (i + 8));
      trial(big, -big, big - 1, (uint8_t)i);
      trial(INT32_MAX - (int32_t)i, INT32_MIN + (int32_t)i, big, 0);
   }

   /* 3. Uniform random over the full int32 range. */
   printf("[3] random full int32 domain, 3,000,000 triples\n");
   for (i = 0; i < 3000000u; i++)
   {
      a = (int32_t)rng();
      b = (int32_t)rng();
      c = (int32_t)rng();
      trial(a, b, c, (uint8_t)rng());
   }

   /* 4. Altered colours: the game reads the colour back and modifies it.
    *    Every one of these must be refused unless it requantizes the same. */
   printf("[4] altered-colour refusal, 500,000 perturbations\n");
   for (i = 0; i < 500000u; i++)
   {
      uint32_t packed, altered;
      a = (int32_t)(rng() % 4096u);      /* stay in the non-saturating band */
      b = (int32_t)(rng() % 4096u);
      c = (int32_t)(rng() % 4096u);
      packed  = gte_pack(a, b, c, 0);
      /* flip one bit somewhere in the 24 colour bits */
      altered = packed ^ (1u << (rng() % 24u));
      trial_altered(a, b, c, altered);
   }

   /* 5. The command-code byte must not defeat the match: games OR the GP0
    *    opcode into byte 3, and the CD field carries a different value. */
   printf("[5] command-code byte independence\n");
   for (i = 0; i < 100000u; i++)
   {
      uint32_t packed, with_cmd;
      a = (int32_t)(rng() % 4096u);
      b = (int32_t)(rng() % 4096u);
      c = (int32_t)(rng() % 4096u);
      packed   = gte_pack(a, b, c, (uint8_t)rng());
      with_cmd = (packed & 0x00FFFFFFu) | ((uint32_t)(0x20 + (rng() % 0x40u)) << 24);
      push_shadow(0, a, b, c, packed);
      checked++;
      if (PGXP_GetColor(0, &with_cmd, NULL))
         accepted++;
      else
      {
         if (mismatches < 10)
            fprintf(stderr, "CMD-BYTE REJECT packed=%08x with_cmd=%08x\n",
                  packed, with_cmd);
         mismatches++;
         rejected++;
      }
   }

   printf("\nchecked=%llu accepted=%llu rejected=%llu mismatches=%llu\n",
         (unsigned long long)checked, (unsigned long long)accepted,
         (unsigned long long)rejected, (unsigned long long)mismatches);

   if (mismatches)
   {
      printf("FAIL\n");
      return 1;
   }
   printf("PASS\n");
   return 0;
}
