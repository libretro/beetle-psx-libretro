/* Run the real GTE and consume its PGXP vertex through a command buffer. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <libretro.h>
#include "../../mednafen/psx/gte.h"
#include "../../pgxp/pgxp_gpu.h"
#include "../../pgxp/pgxp_gte.h"
#include "../../pgxp/pgxp_main.h"
#include "../../pgxp/pgxp_mem.h"

retro_log_printf_t log_cb;
bool psx_gte_overclock;
uint8_t widescreen_hack;
uint8_t widescreen_hack_aspect_ratio_setting;

int MDFNSS_StateAction(void *st, int load, int data_only,
      SFORMAT *sf, const char *name)
{
   abort();
}

static OGLVertex project(uint32_t op, uint32_t mode, uint32_t *word)
{
   OGLVertex vertex = {0};
   unsigned i;

   GTE_Power();
   PGXP_SetModes(PGXP_MODE_MEMORY | PGXP_MODE_GTE |
         PGXP_TEXTURE_CORRECTION | mode);
   GTE_WriteCR(0, 2048);
   GTE_WriteCR(2, 2048);
   GTE_WriteCR(4, 4096);
   GTE_WriteCR(26, 320);
   for (i = 0; i < 6; i += 2)
   {
      GTE_WriteDR(i, 0xfffd0003u);
      GTE_WriteDR(i + 1, 256);
   }

   GTE_Instruction((1u << 19) | op);
   *word = GTE_ReadDR(14);
   PGXP_GTE_SWC2(14u << 16, *word, 0x10000);
   PGXP_WriteCB(ReadMem(0x10000), 0);
   PGXP_GetVertex(0, word, &vertex, 0, 0);
   return vertex;
}

static void configure_fractional(unsigned h, uint32_t translation)
{
   GTE_Power();
   GTE_WriteCR(0, 4096);
   GTE_WriteCR(2, 4096);
   GTE_WriteCR(4, 2048);
   GTE_WriteCR(7, translation);
   GTE_WriteCR(26, h);
}

static void set_input(unsigned vertex, int x, int y, int z)
{
   GTE_WriteDR(vertex * 2, ((uint32_t)(uint16_t)y << 16) | (uint16_t)x);
   GTE_WriteDR(vertex * 2 + 1, (uint16_t)z);
}

static OGLVertex recover(unsigned reg, uint32_t word)
{
   OGLVertex vertex = {0};
   PGXP_GTE_SWC2(reg << 16, word, 0x10000);
   PGXP_WriteCB(ReadMem(0x10000), 0);
   PGXP_GetVertex(0, &word, &vertex, 0, 0);
   return vertex;
}

static int fractional_projection(unsigned cpu_mode)
{
   OGLVertex a, b, c, far, saturated, invalid, near, zero;
   uint32_t a_word, b_word, near_word;
   int32_t front, back, flat;

   PGXP_SetModes(PGXP_MODE_MEMORY | PGXP_MODE_GTE |
         PGXP_TEXTURE_CORRECTION | PGXP_NCLIP_IMPL | cpu_mode);
   configure_fractional(320, 0);
   set_input(0, 107, 105, 348);
   set_input(1, 107, 105, 349);
   set_input(2, 120, 106, 350);
   GTE_Instruction((1u << 19) | 0x30);
   a_word = GTE_ReadDR(12);
   b_word = GTE_ReadDR(13);
   a = recover(12, a_word);
   b = recover(13, b_word);
   c = recover(14, GTE_ReadDR(14));
   GTE_Instruction(0x06);
   flat = (int32_t)GTE_ReadDR(24);
   invalid = recover(12, 0x000a0014u);

   configure_fractional(320, 0);
   set_input(0, 107, 105, 348);
   set_input(1, 111, 105, 349);
   set_input(2, 109, 106, 348);
   GTE_Instruction((1u << 19) | 0x30);
   GTE_Instruction(0x06);
   front = (int32_t)GTE_ReadDR(24);
   set_input(0, 111, 105, 349);
   set_input(1, 107, 105, 348);
   GTE_Instruction((1u << 19) | 0x30);
   GTE_Instruction(0x06);
   back = (int32_t)GTE_ReadDR(24);

   configure_fractional(1024, 65535);
   set_input(0, 32767, 0, 1);
   GTE_Instruction((1u << 19) | 0x01);
   saturated = recover(14, GTE_ReadDR(14));
   set_input(0, 32767, 0, 2);
   GTE_Instruction((1u << 19) | 0x01);
   far = recover(14, GTE_ReadDR(14));

   configure_fractional(320, 0);
   set_input(0, 10, 12, 319);
   GTE_Instruction((1u << 19) | 0x01);
   near_word = GTE_ReadDR(14);
   near = recover(14, near_word);
   configure_fractional(0, 0);
   set_input(0, 16, 0, 0);
   GTE_Instruction((1u << 19) | 0x01);
   zero = recover(14, GTE_ReadDR(14));

   if (a_word != b_word || !a.valid_w || !b.valid_w || !c.valid_w ||
       a.w != 174.f || b.w != 174.5f || c.w != 175.f ||
       a.x != b.x || a.y != b.y || a.y <= 193.f || a.y >= 194.f ||
       flat != 0 || front <= 0 || back != -front ||
       invalid.valid_w || invalid.x != 20.f || invalid.y != 10.f ||
       saturated.w != 65535.5f || far.w != 65536.f ||
       saturated.x != 511.9921875f || far.x != 511.984375f ||
       near.w != 160.f || near.x != 20.f || near.y != 24.f ||
       zero.w != 0.f || zero.x != 31.999755859375f)
   {
      fprintf(stderr, "Projection invariant failed, mode %u: "
            "xy %.9g %.9g %.9g %.9g, w %.9g %.9g %.9g, "
            "clip %d %d %d, invalid %.9g %.9g, sat %.9g %.9g, "
            "far %.9g %.9g, near %.9g %.9g %.9g, zero %.9g %.9g\n",
            cpu_mode, a.x, a.y, b.x, b.y, a.w, b.w, c.w,
            (int)flat, (int)front, (int)back, invalid.x, invalid.y,
            saturated.x, saturated.w, far.x, far.w,
            near.x, near.y, near.w, zero.x, zero.w);
      return 1;
   }
   return 0;
}


int main(void)
{
   static const uint32_t operations[] = {0x01, 0x30};
   unsigned i;
   int failed = 0;

   GTE_Init();
   PGXP_Init();
   for (i = 0; i < sizeof(operations) / sizeof(operations[0]); i++)
   {
      uint32_t off_word, on_word;
      const OGLVertex off = project(operations[i], 0, &off_word);
      const OGLVertex on = project(operations[i], PGXP_MODE_CPU, &on_word);

      if (off_word != on_word || !off.valid_w || !on.valid_w ||
          off.x != 1.25f || off.y != -2.5f || off.w != 256.f ||
          on.x != off.x || on.y != off.y || on.w != off.w)
      {
         fprintf(stderr, "GTE op %02x changes projection with CPU tracking mode\n",
               (unsigned)operations[i]);
         failed = 1;
      }
   }
   failed |= fractional_projection(0);
   failed |= fractional_projection(PGXP_MODE_CPU);
   PGXP_Shutdown();
   return failed;
}
