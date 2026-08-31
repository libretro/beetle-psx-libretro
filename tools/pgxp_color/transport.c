/* PGXP precise-colour transport check.
 *
 * The oracle check (oracle.c) proves that an accepted shadow is correct.
 * It injects the shadow straight into the command buffer with
 * PGXP_WriteCB, so it says nothing about whether a colour actually
 * *survives* the journey from the GTE to a GP0 packet. That journey is
 * this file's subject, and it is the half that decides whether the
 * hit-rate measurement can be believed: a near-zero hit rate in content
 * means "games recolour" only if transport is known to work.
 *
 * Everything here drives the real PGXP functions - GTE register hooks,
 * CPU register/memory tracking, the GPU FIFO -> command-buffer copy - in
 * the same order gte.c / pgxp_cpu.c / gpu.c call them. Nothing is
 * mirrored except the two MIPS instruction words, which are assembled by
 * hand.
 *
 * Build and run: make -C tools/pgxp_color check
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "../../pgxp/pgxp_gpu.h"
#include "../../pgxp/pgxp_gte.h"
#include "../../pgxp/pgxp_cpu.h"
#include "../../pgxp/pgxp_mem.h"
#include "../../pgxp/pgxp_main.h"
#include "../../pgxp/pgxp_types.h"
#include "../../pgxp/pgxp_value.h"

/* Replica of gte.c's ColorFIFO pack; see oracle.c for why it is mirrored
 * rather than included. */
static uint8_t lm_c_replica(int32_t v)
{
   if (v < 0)
      return 0;
   if (v > 0xFF)
      return 0xFF;
   return (uint8_t)v;
}

static uint32_t gte_pack(int32_t m1, int32_t m2, int32_t m3, uint8_t cd)
{
   return  (uint32_t)lm_c_replica(m1 >> 4)
        | ((uint32_t)lm_c_replica(m2 >> 4) << 8)
        | ((uint32_t)lm_c_replica(m3 >> 4) << 16)
        | ((uint32_t)cd << 24);
}

/* MIPS instruction assembly, only the fields the PGXP hooks decode. */
#define INSTR_RT(rt)          (((uint32_t)(rt) & 0x1F) << 16)
#define INSTR_RD(rd)          (((uint32_t)(rd) & 0x1F) << 11)
#define INSTR_RS(rs)          (((uint32_t)(rs) & 0x1F) << 21)

/* Scratch addresses in tracked RAM. */
#define LIST_ADDR   0x80100000u
#define LIST_ADDR2  0x80100004u

static int failures;

static void fail(const char* what, uint32_t got, uint32_t want)
{
   fprintf(stderr, "FAIL %-34s got=%08x want=%08x\n", what, got, want);
   failures++;
}

/* Push a colour through the GTE side exactly as MAC_to_RGB_FIFO does. */
static uint32_t gte_produce(int32_t m1, int32_t m2, int32_t m3, uint8_t cd)
{
   uint32_t packed = gte_pack(m1, m2, m3, cd);
   PGXP_pushRGBf((float)m1 / 16.0f, (float)m2 / 16.0f,
                 (float)m3 / 16.0f, packed);
   return packed;
}

/* The GPU side: gpu.c writes each command word into the blitter FIFO
 * paired with the tracked value read from the address it came from, then
 * ProcessFIFO copies the FIFO slots into the command buffer. */
static void gpu_deliver(const uint32_t* addrs, const uint32_t* words,
                        unsigned n)
{
   unsigned i;
   for (i = 0; i < n; i++)
      PGXP_WriteFIFO(ReadMem(addrs[i]), i);   /* GPU_WriteCB */
   for (i = 0; i < n; i++)
      PGXP_WriteCB(PGXP_ReadFIFO(i), i);      /* ProcessFIFO */
   (void)words;
}

/* ---------------------------------------------------------------------
 * Idiom A: swc2 $22 -> display list. The GTE's CD byte already holds the
 * GP0 command code, so the whole word goes straight out.
 */
static void test_swc2_direct(void)
{
   int32_t  m1 = 0x0A37, m2 = 0x1B04, m3 = 0x0055;
   uint32_t packed, addr = LIST_ADDR;
   uint32_t instr = INSTR_RT(22);
   float    rgb[3];

   packed = gte_produce(m1, m2, m3, 0x30);   /* 0x30 = shaded triangle */

   /* swc2 $22, 0(rs) : Mem[addr] = GTE_D[22] */
   PGXP_GTE_SWC2(instr, packed, addr);

   gpu_deliver(&addr, &packed, 1);

   if (!PGXP_GetColor(0, &packed, rgb))
   {
      fail("swc2 direct: not accepted", 0, 1);
      return;
   }
   if (rgb[0] != (float)m1 / 16.0f ||
       rgb[1] != (float)m2 / 16.0f ||
       rgb[2] != (float)m3 / 16.0f)
      fail("swc2 direct: wrong payload", 0, 0);
}

/* ---------------------------------------------------------------------
 * Idiom B: mfc2 into a GPR, ori the GP0 opcode into byte 3, sw to the
 * list. This is the path the accept rule's 24-bit compare exists for.
 */
static void test_mfc2_ori_sw(void)
{
   int32_t  m1 = 0x0100, m2 = 0x0F80, m3 = 0x0C21;
   uint32_t packed, with_cmd, addr = LIST_ADDR2;
   float    rgb[3];

   packed = gte_produce(m1, m2, m3, 0x00);

   /* mfc2 $t0, $22 : CPU[8] = GTE_D[22] */
   PGXP_GTE_MFC2(INSTR_RT(8) | INSTR_RD(22), packed, packed);

   /* ori $t0, $t0, ... - the opcode byte is above the 16-bit immediate,
    * so real code uses lui+or; model it as an OR of two GPRs, which is
    * the same tracking path (PGXP_CPU_OR). */
   with_cmd = (packed & 0x00FFFFFFu) | (0x38u << 24);
   PGXP_CPU_OR(INSTR_RD(8) | INSTR_RS(8) | INSTR_RT(9),
               with_cmd, packed, 0x38u << 24);

   /* sw $t0, 0(rs) */
   PGXP_CPU_SW(INSTR_RT(8), with_cmd, addr);

   gpu_deliver(&addr, &with_cmd, 1);

   if (!PGXP_GetColor(0, &with_cmd, rgb))
   {
      /* Not a hard failure: PGXP's bitwise-op tracking may legitimately
       * invalidate here (see pgxp_cpu.c INVALID_BITWISE_OP). Report it,
       * because it bounds what the hit rate can be in content that uses
       * this idiom. */
      printf("    note: mfc2+or+sw not tracked through the bitwise op "
             "(expected if PGXP invalidates on OR)\n");
      return;
   }
   if (rgb[0] != (float)m1 / 16.0f ||
       rgb[1] != (float)m2 / 16.0f ||
       rgb[2] != (float)m3 / 16.0f)
      fail("mfc2+or+sw: wrong payload", 0, 0);
   else
      printf("    note: mfc2+or+sw survives transport\n");
}

/* ---------------------------------------------------------------------
 * A multi-word packet: three gouraud vertices, colour words interleaved
 * with vertex words, checked at their real command-buffer offsets.
 */
static void test_gouraud_packet(void)
{
   /* GP0(0x30): colour0, xy0, colour1, xy1, colour2, xy2 */
   uint32_t addrs[6], words[6];
   int32_t  macs[3][3] = { { 0x0333, 0x0444, 0x0555 },
                           { 0x0666, 0x0777, 0x0888 },
                           { 0x0999, 0x0AAA, 0x0BBB } };
   unsigned v;
   float    rgb[3];

   for (v = 0; v < 3; v++)
   {
      uint32_t caddr = 0x80101000u + v * 8u;
      uint32_t vaddr = caddr + 4u;
      uint32_t packed = gte_produce(macs[v][0], macs[v][1], macs[v][2],
                                    v == 0 ? 0x30 : 0x00);
      PGXP_GTE_SWC2(INSTR_RT(22), packed, caddr);

      addrs[v * 2]     = caddr;
      words[v * 2]     = packed;
      addrs[v * 2 + 1] = vaddr;
      words[v * 2 + 1] = 0x00400040u;   /* an untracked vertex word */
   }

   gpu_deliver(addrs, words, 6);

   for (v = 0; v < 3; v++)
   {
      if (!PGXP_GetColor(v * 2, &words[v * 2], rgb))
      {
         fail("gouraud packet: colour not accepted", v, 1);
         continue;
      }
      if (rgb[0] != (float)macs[v][0] / 16.0f ||
          rgb[1] != (float)macs[v][1] / 16.0f ||
          rgb[2] != (float)macs[v][2] / 16.0f)
         fail("gouraud packet: wrong payload", v, 0);
   }

   /* The vertex slots must not be mistaken for colours. */
   if (PGXP_GetColor(1, &words[1], rgb))
      fail("gouraud packet: vertex word accepted as colour", 1, 0);
}

/* ---------------------------------------------------------------------
 * FIFO ordering: three colours pushed through the GTE in sequence must
 * arrive at their own slots, not shuffled. Catches an off-by-one in the
 * DR[20..22] shadow shift.
 */
static void test_fifo_order(void)
{
   uint32_t addrs[3], words[3];
   unsigned i;
   float    rgb[3];
   int32_t  base[3] = { 0x0110, 0x0220, 0x0330 };

   for (i = 0; i < 3; i++)
   {
      addrs[i] = 0x80102000u + i * 4u;
      words[i] = gte_produce(base[i], base[i] + 1, base[i] + 2, 0x20);
      PGXP_GTE_SWC2(INSTR_RT(22), words[i], addrs[i]);
   }

   gpu_deliver(addrs, words, 3);

   for (i = 0; i < 3; i++)
   {
      if (!PGXP_GetColor(i, &words[i], rgb))
      {
         fail("fifo order: not accepted", i, 1);
         continue;
      }
      if (rgb[0] != (float)base[i] / 16.0f)
         fail("fifo order: colours shuffled", i, 0);
   }
}

/* ---------------------------------------------------------------------
 * Negative control: a colour word the game composed on the CPU without
 * the GTE must NOT be accepted with a stale shadow attached.
 */
static void test_untracked_colour(void)
{
   uint32_t addr = 0x80103000u;
   uint32_t stale, cpu_word;
   float    rgb[3];

   /* Produce something through the GTE and land it at `addr`... */
   stale = gte_produce(0x0500, 0x0500, 0x0500, 0x30);
   PGXP_GTE_SWC2(INSTR_RT(22), stale, addr);

   /* ...then have the CPU overwrite that list slot with a different
    * colour. PGXP_CPU_SW with an untracked register must clear the
    * tracking, so the stale shadow cannot be reused. */
   cpu_word = 0x30204060u;
   PGXP_CPU_SW(INSTR_RT(15), cpu_word, addr);   /* $15 never tracked */

   gpu_deliver(&addr, &cpu_word, 1);

   if (PGXP_GetColor(0, &cpu_word, rgb))
      fail("untracked colour accepted from stale shadow", 0, 0);
}

static void test_nclip_magnitude(void)
{
   if (PGXP_NCLIP_preserve_magnitude(1234, -1) != -1234)
      fail("NCLIP positive orientation replacement", 0, 1);
   if (PGXP_NCLIP_preserve_magnitude(-4321, 1) != 4321)
      fail("NCLIP negative orientation replacement", 0, 1);
   if (PGXP_NCLIP_preserve_magnitude(1234, 1) != 1234)
      fail("NCLIP changed matching magnitude", 0, 1);
   if (PGXP_NCLIP_preserve_magnitude(1234, 0) != 1234)
      fail("NCLIP replaced native result with zero", 0, 1);
   if (PGXP_NCLIP_preserve_magnitude(0, -1) != -1)
      fail("NCLIP lost orientation at native zero", 0, 1);
   if (PGXP_NCLIP_preserve_magnitude(0, 1) != 1)
      fail("NCLIP lost positive orientation at native zero", 0, 1);
   if (PGXP_NCLIP_preserve_magnitude(INT32_MIN, -1) != INT32_MIN)
      fail("NCLIP changed matching minimum", 0, 1);
   if (PGXP_NCLIP_preserve_magnitude(INT32_MIN, 1) != INT32_MAX)
      fail("NCLIP overflowed reversed minimum", 0, 1);

   SetValue(&GTE_data_reg[12], 0);
   SetValue(&GTE_data_reg[13], 0);
   SetValue(&GTE_data_reg[14], 0);
   if (PGXP_NCLIP_valid(0, 0, 0))
      fail("NCLIP accepted vertices without precise Z", 1, 0);
   GTE_data_reg[12].flags |= VALID_2;
   GTE_data_reg[13].flags |= VALID_2;
   GTE_data_reg[14].flags |= VALID_2;
   if (!PGXP_NCLIP_valid(0, 0, 0))
      fail("NCLIP rejected precise XYZ vertices", 0, 1);

   GTE_data_reg[12].x = 0.0f;
   GTE_data_reg[12].y = 0.0f;
   GTE_data_reg[13].x = 1.0f;
   GTE_data_reg[13].y = 0.0f;
   GTE_data_reg[14].x = 0.0f;
   GTE_data_reg[14].y = 1.0f;
   if (PGXP_NCLIP_sign() != 1)
      fail("NCLIP lost positive precise orientation", 0, 1);
   GTE_data_reg[13].x = 0.0f;
   GTE_data_reg[13].y = 1.0f;
   GTE_data_reg[14].x = 1.0f;
   GTE_data_reg[14].y = 0.0f;
   if (PGXP_NCLIP_sign() != -1)
      fail("NCLIP lost negative precise orientation", 0, 1);
   GTE_data_reg[14].x = 0.05f;
   if (PGXP_NCLIP_sign() != 0)
      fail("NCLIP accepted unstable precise orientation", 1, 0);
   GTE_data_reg[14].x = 0.101f;
   if (PGXP_NCLIP_sign() != -1)
      fail("NCLIP rejected stable precise orientation", 0, 1);
   GTE_data_reg[13].x = 1.0f;
   GTE_data_reg[13].y = 0.0f;
   GTE_data_reg[14].x = 0.0f;
   GTE_data_reg[14].y = 0.101f;
   if (PGXP_NCLIP_sign() != 1)
      fail("NCLIP rejected stable positive orientation", 0, 1);
}

static void test_architectural_zero_add_identity(void)
{
   const uint32_t runtime_zero_operands = INSTR_RS(4) | INSTR_RT(3) |
      INSTR_RD(5) | 0x21u;
   const uint32_t architectural_right_zero = INSTR_RS(4) |
      INSTR_RD(5) | 0x21u;
   const uint32_t architectural_left_zero = INSTR_RT(3) |
      INSTR_RD(5) | 0x21u;

   CPU_reg[4] = PGXP_value_zero;
   SetValue(&CPU_reg[4], 1);
   CPU_reg[4].x = 1.25f;
   CPU_reg[4].z = 7.5f;
   CPU_reg[0] = PGXP_value_zero;
   PGXP_CPU_ADDU(architectural_right_zero, 1, 1, 0);
   if (CPU_reg[5].x != 1.25f || CPU_reg[5].z != 7.5f)
      fail("architectural right-zero ADD lost identity", 0, 1);

   CPU_reg[3] = PGXP_value_zero;
   SetValue(&CPU_reg[3], 1);
   CPU_reg[3].x = 1.25f;
   CPU_reg[3].z = 7.5f;
   PGXP_CPU_ADDU(architectural_left_zero, 1, 0, 1);
   if (CPU_reg[5].x != 1.25f || CPU_reg[5].z != 7.5f)
      fail("architectural left-zero ADD lost identity", 0, 1);

   /* Ordinary registers whose native value happens to be zero may still
    * carry meaningful fractional coordinates on either side. */
   SetValue(&CPU_reg[4], 1);
   SetValue(&CPU_reg[3], 0);
   CPU_reg[4].x = 1.25f;
   CPU_reg[3].x = 0.5f;
   PGXP_CPU_ADDU(runtime_zero_operands, 1, 1, 0);
   if (CPU_reg[5].x != 1.75f)
      fail("runtime-zero right ADD lost precision", 0, 1);

   SetValue(&CPU_reg[4], 0);
   SetValue(&CPU_reg[3], 1);
   CPU_reg[4].x = 0.5f;
   CPU_reg[3].x = 1.25f;
   PGXP_CPU_ADDU(runtime_zero_operands, 1, 0, 1);
   if (CPU_reg[5].x != 1.75f)
      fail("runtime-zero left ADD lost precision", 0, 1);
}

static void test_memory_zero_add_identity(void)
{
   const uint32_t instr = INSTR_RS(4) | INSTR_RD(5) | 0x21u;
   const uint32_t zero_source = INSTR_RD(5) | 0x21u;
   const uint32_t wrong_direction = INSTR_RT(4) | INSTR_RD(5) | 0x21u;
   const uint32_t nonzero_rt = INSTR_RS(4) | INSTR_RT(3) |
      INSTR_RD(5) | 0x21u;

   CPU_reg[4] = PGXP_value_zero;
   CPU_reg[5] = PGXP_value_zero;
   SetValue(&CPU_reg[4], 0x00020001u);
   CPU_reg[4].x = 1.25f;
   CPU_reg[4].y = 2.5f;
   PGXP_CPU_ADDU_Identity(instr, 0x00020001u, 0x00020001u);
   if (CPU_reg[5].x != 1.25f || CPU_reg[5].y != 2.5f ||
       CPU_reg[5].value != 0x00020001u)
      fail("memory ADDU lost exact identity", CPU_reg[5].value,
            0x00020001u);

   CPU_reg[5] = PGXP_value_zero;
   CPU_reg[5].x = 9.f;
   PGXP_CPU_ADDU_Identity(wrong_direction, 0x00020001u, 0x00020001u);
   if (CPU_reg[5].x != 9.f)
      fail("left-zero ADDU was transported", 0, 1);

   CPU_reg[5].x = 8.f;
   PGXP_CPU_ADDU_Identity(nonzero_rt, 0x00020001u, 0x00020001u);
   if (CPU_reg[5].x != 8.f)
      fail("runtime-zero ADDU was transported", 0, 1);

   CPU_reg[4].flags = VALID_01;
   CPU_reg[4].value = 0x00040003u;
   PGXP_CPU_ADDU_Identity(instr, 0x00020001u, 0x00020001u);
   if (CPU_reg[5].flags & VALID_01)
      fail("stale ADDU source stayed valid", CPU_reg[5].flags, 0);

   CPU_reg[5] = PGXP_value_zero;
   SetValue(&CPU_reg[5], 0);
   CPU_reg[5].x = 9.f;
   CPU_reg[5].y = 8.f;
   CPU_reg[5].z = 7.f;
   PGXP_CPU_ADDU_Identity(zero_source, 0, 0);
   if (CPU_reg[5].value != 0 || CPU_reg[5].x != 0.f ||
       CPU_reg[5].y != 0.f || CPU_reg[5].z != 0.f ||
       (CPU_reg[5].flags & VALID_01) != VALID_01)
      fail("zero-source ADDU retained stale precision",
            CPU_reg[5].value, 0);
}

int main(void)
{
   PGXP_Init();
   PGXP_InitMem();
   PGXP_InitGTE();
   PGXP_SetModes(PGXP_MODE_MEMORY);

   printf("[T1] swc2 $22 -> display list -> GP0\n");
   test_swc2_direct();

   printf("[T2] mfc2 + or(cmd byte) + sw -> GP0\n");
   test_mfc2_ori_sw();

   printf("[T3] three-vertex gouraud packet, colours at real offsets\n");
   test_gouraud_packet();

   printf("[T4] ColorFIFO ordering across three pushes\n");
   test_fifo_order();

   printf("[T5] negative control: CPU-composed colour must be refused\n");
   test_untracked_colour();

   printf("[T6] NCLIP native magnitude preservation\n");
   test_nclip_magnitude();

   printf("[T7] architectural-zero ADD identity\n");
   test_architectural_zero_add_identity();

   printf("[T8] Memory Only right-zero ADDU identity\n");
   test_memory_zero_add_identity();

   if (failures)
   {
      printf("\nfailures=%d\nFAIL\n", failures);
      return 1;
   }
   printf("\nFAIL count 0\nPASS\n");
   return 0;
}
