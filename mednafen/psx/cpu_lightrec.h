/******************************************************************************/
/* Mednafen Sony PS1 Emulation Module                                         */
/******************************************************************************/
/* cpu_lightrec.h:
**  Copyright (C) 2011-2016 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __MDFN_PSX_CPU_LIGHTREC_H
#define __MDFN_PSX_CPU_LIGHTREC_H

#include <lightrec-config.h>
#include <lightrec.h>

/* 8MB should rarely fill up (4 IPI average for entire 2MB ram)
   0MB will disable, 1MB will fill and clean the buffer quickly
   good for finding issues with codebuffer cleanup */
#define LIGHTREC_CODEBUFFER_SIZE 8*1024*1024

class PS_CPU_LIGHTREC : public PS_CPU
{
 public:

 PS_CPU_LIGHTREC(PS_CPU *beetle_cpu) MDFN_COLD;

 void SetOptions(bool interpreter, bool invalidate, bool spgp_opt, bool dynarec);

 //virtual method overrides
 ~PS_CPU_LIGHTREC() MDFN_COLD;
 pscpu_timestamp_t Run(pscpu_timestamp_t timestamp_in, bool BIOSPrintMode, bool ILHMode);
 void Power(void) MDFN_COLD;
 void AssertIRQ(unsigned which, bool asserted);
 int StateAction(StateMem *sm, const unsigned load, const bool data_only);
 void lightrec_plugin_clear(uint32 addr, uint32 size);
 void print_for_big_ass_debugger(int32 timestamp, uint32 PC);

 private:

 PS_CPU *Cpu;

 //Lightrec specific methods
 static struct lightrec_registers *lightrec_regs;
 static void enable_ram(struct lightrec_state *state, bool enable);
 static void cop2_op(struct lightrec_state *state, uint32 op);
 static void pgxp_cop2_notify(lightrec_state *state, uint32 op, uint32 data);
 static struct lightrec_ops ops;
 static struct lightrec_ops pgxp_ops;
 static struct lightrec_mem_map_ops pgxp_hw_regs_ops;
 static struct lightrec_mem_map_ops pgxp_nonhw_regs_ops;
 static struct lightrec_mem_map_ops hw_regs_ops;
 static struct lightrec_mem_map_ops cache_ctrl_ops;
 static struct lightrec_mem_map lightrec_map[];
 static void hw_write_byte(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static void hw_write_half(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static void hw_write_word(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static uint8 hw_read_byte(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static uint16 hw_read_half(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static uint32 hw_read_word(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static void pgxp_hw_write_byte(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static void pgxp_hw_write_half(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static void pgxp_hw_write_word(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static uint8 pgxp_hw_read_byte(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static uint16 pgxp_hw_read_half(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static uint32 pgxp_hw_read_word(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static void pgxp_nonhw_write_byte(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static void pgxp_nonhw_write_half(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static void pgxp_nonhw_write_word(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static uint8 pgxp_nonhw_read_byte(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static uint16 pgxp_nonhw_read_half(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static uint32 pgxp_nonhw_read_word(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static void cache_ctrl_write_word(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem, uint32 val);
 static uint32 cache_ctrl_read_word(struct lightrec_state *state, uint32 opcode, void *host, uint32 mem);
 static void reset_target_cycle_count(struct lightrec_state *state, pscpu_timestamp_t timestamp);
 int lightrec_plugin_init();
 void CopyToLightrec();
 void CopyFromLightrec();
 void SetUnsafeFlags();
};

#endif
