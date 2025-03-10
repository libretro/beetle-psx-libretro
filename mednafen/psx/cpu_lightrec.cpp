/******************************************************************************/
/* Mednafen Sony PS1 Emulation Module                                         */
/******************************************************************************/
/* cpu_lightrec.cpp:
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

#include "psx.h"
#include "cpu_lightrec.h"

#include "../state_helpers.h"
#include "../mednafen.h"

#include <unistd.h>

// iCB: PGXP STUFF
#include "../pgxp/pgxp_cpu.h"
#include "../pgxp/pgxp_gte.h"
#include "../pgxp/pgxp_main.h"
uint32_t pgxpMode;

bool useInterpreter;
bool useInvalidate;
bool useSPGPopt;
bool prev_dynarec;
extern uint8 psx_mmap;
extern uint8 *lightrec_codebuffer;
static struct lightrec_state *lightrec_state;
struct lightrec_registers * PS_CPU_LIGHTREC::lightrec_regs;
uint32_t cpu_timestamp;
char cache_buf[64 * 1024];

PS_CPU_LIGHTREC::PS_CPU_LIGHTREC(PS_CPU *beetle_cpu)
{
 Cpu = beetle_cpu;
}

PS_CPU_LIGHTREC::~PS_CPU_LIGHTREC()
{
 Cpu = NULL;
 if (lightrec_state)
  lightrec_destroy(lightrec_state);
}

const uint8_t *PSX_LoadExpansion1(void);

void PS_CPU_LIGHTREC::Power(void)
{
 lightrec_plugin_init();
}

int PS_CPU_LIGHTREC::StateAction(StateMem *sm, const unsigned load, const bool data_only)
{
 if(!load)
 {
  if(lightrec_state)
   //get data from lightrec struct before save
   CopyFromLightrec();
 }

 int ret = Cpu->StateAction(sm, load, data_only);

 if(load)
 {
  if(lightrec_state)
  {
   lightrec_invalidate_all(lightrec_state);
   //move loaded data into lightrec_regs struct
   CopyToLightrec();
  }
 }
 return ret;
}

void PS_CPU_LIGHTREC::AssertIRQ(unsigned which, bool asserted)
{
 Cpu->AssertIRQ(which,asserted);

 lightrec_regs->cp0[CP0REG_CAUSE] &= ~(1 << (10 + which));

 if(asserted)
  lightrec_regs->cp0[CP0REG_CAUSE] |= 1 << (10 + which);

 lightrec_set_exit_flags(lightrec_state, LIGHTREC_EXIT_CHECK_INTERRUPT);
}

void PS_CPU_LIGHTREC::SetOptions(bool interpreter, bool invalidate, bool spgp_opt, bool dynarec)
{
 bool init = false;

 if(lightrec_state)
 {
  if(prev_dynarec && !dynarec)
   //move regs out of lightrec when switching to beetle interpreter
   CopyFromLightrec();
  else if(!prev_dynarec && dynarec)
   //move regs into lightrec when switching from beetle interpreter
   CopyToLightrec();

  //only re-init if needed to allow calling setoptions at any time
  //lightrec init needs to be already been done, running in dynarec mode
  //and some option has changed
  if(dynarec && (prev_dynarec != dynarec ||
     useInterpreter != interpreter || pgxpMode != PGXP_GetModes()))
      init = true;
 }

 //update now that checking for option changes has been done
 pgxpMode = PGXP_GetModes();
 useInterpreter = interpreter;
 useSPGPopt = spgp_opt;
 useInvalidate = invalidate;
 prev_dynarec = dynarec;

 if(init)
    lightrec_plugin_init();
 else if(lightrec_state)
    //set flags to updated values if init has not been called
    SetUnsafeFlags();
}

#ifdef LIGHTREC_DEBUG
u32 lightrec_begin_cycles = 0;

void PS_CPU_LIGHTREC::print_for_big_ass_debugger(int32_t timestamp, uint32_t PC)
{
	uint8_t *psxM = (uint8_t *) MainRAM->data8;
	uint8_t *psxR = (uint8_t *) BIOSROM->data8;
	uint8_t *psxH = (uint8_t *) ScratchRAM->data8;

	unsigned int i;

	printf("CYCLE 0x%08x PC 0x%08x", timestamp, PC);

#ifdef LIGHTREC_VERY_DEBUG
	printf(" RAM 0x%08x SCRATCH 0x%08x",
		hash_calculate(psxM, 0x200000),
		hash_calculate(psxH, 0x400));
#endif

	printf(" CP0 0x%08x",
		hash_calculate(&(lightrec_regs->cp0),
			sizeof(lightrec_regs->cp0)));

#ifdef LIGHTREC_VERY_DEBUG
	for (i = 0; i < 32; i++)
		printf(" GPR[%i] 0x%08x", i, lightrec_regs->gpr[i]);
	printf(" LO 0x%08x", lightrec_regs->gpr[32]);
	printf(" HI 0x%08x", lightrec_regs->gpr[33]);
#else
	printf(" GPR 0x%08x", hash_calculate(&(lightrec_regs->gpr), 32*sizeof(uint32_t)));
#endif
	printf("\n");
}
#endif /* LIGHTREC_DEBUG */

pscpu_timestamp_t PS_CPU_LIGHTREC::Run(pscpu_timestamp_t timestamp_in, bool BIOSPrintMode, bool ILHMode)
{
        uint32 PC = Cpu->BACKED_PC;
	u32 flags;

	do {
#ifdef LIGHTREC_DEBUG
		u32 oldpc = PC;
#endif
		lightrec_regs->cp0[CP0REG_SR] = Cpu->CP0.SR;
		lightrec_regs->cp0[CP0REG_CAUSE] = Cpu->CP0.CAUSE;
		lightrec_regs->cp0[CP0REG_EPC] = Cpu->CP0.EPC;

		lightrec_reset_cycle_count(lightrec_state, timestamp_in + cpu_timestamp);

		if (useInterpreter)
			PC = lightrec_run_interpreter(lightrec_state, PC, next_event_ts + cpu_timestamp);
		else
			PC = lightrec_execute(lightrec_state, PC, next_event_ts + cpu_timestamp);

		timestamp_in = lightrec_current_cycle_count(lightrec_state) - cpu_timestamp;

		Cpu->CP0.SR = lightrec_regs->cp0[CP0REG_SR];
		Cpu->CP0.CAUSE = lightrec_regs->cp0[CP0REG_CAUSE];

		flags = lightrec_exit_flags(lightrec_state);

		if (flags & (LIGHTREC_EXIT_SEGFAULT|LIGHTREC_EXIT_NOMEM|LIGHTREC_EXIT_UNKNOWN_OP)) {
			if (flags & LIGHTREC_EXIT_UNKNOWN_OP)
				log_cb(RETRO_LOG_ERROR, "Unknown Operation in block at PC 0x%08x\n", PC);
			if (flags & LIGHTREC_EXIT_NOMEM)
				log_cb(RETRO_LOG_ERROR, "Out of memory at cycle 0x%08x\n", timestamp_in);
			else
				log_cb(RETRO_LOG_ERROR, "Segfault at cycle 0x%08x\n", timestamp_in);

			exit(1);
		}
		else if (flags & LIGHTREC_EXIT_SYSCALL)
			PC = Cpu->Exception(EXCEPTION_SYSCALL, PC, PC, 0);
		else if (flags & LIGHTREC_EXIT_BREAK)
			PC = Cpu->Exception(EXCEPTION_BP, PC, PC, 0);

#ifdef LIGHTREC_DEBUG
		if (timestamp_in >= lightrec_begin_cycles && PC != oldpc){
			print_for_big_ass_debugger(timestamp_in, PC);
		}
#endif
		if ((Cpu->CP0.SR & Cpu->CP0.CAUSE & 0xFF00) && (Cpu->CP0.SR & 1)) {
			/* Handle software interrupts */
			PC = Cpu->Exception(EXCEPTION_INT, PC, PC, 0);
		}
	} while(MDFN_LIKELY(PSX_EventHandler(timestamp_in)));

	cpu_timestamp += timestamp_in;

	/* wrap slightly earlier to avoid issues with target < current timestamp */
	if(cpu_timestamp>0xFE000000)
		cpu_timestamp &= 0x01FFFFFF;

        Cpu->BACKED_PC = PC;

	return timestamp_in;
}

#define ARRAY_SIZE(x) (sizeof(x) ? sizeof(x) / sizeof((x)[0]) : 0)

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#	define LE32TOH(x)	__builtin_bswap32(x)
#	define HTOLE32(x)	__builtin_bswap32(x)
#	define LE16TOH(x)	__builtin_bswap16(x)
#	define HTOLE16(x)	__builtin_bswap16(x)
#else
#	define LE32TOH(x)	(x)
#	define HTOLE32(x)	(x)
#	define LE16TOH(x)	(x)
#	define HTOLE16(x)	(x)
#endif

enum opcodes {
        OP_SPECIAL              = 0x00,
        OP_REGIMM               = 0x01,
        OP_J                    = 0x02,
        OP_JAL                  = 0x03,
        OP_BEQ                  = 0x04,
        OP_BNE                  = 0x05,
        OP_BLEZ                 = 0x06,
        OP_BGTZ                 = 0x07,
        OP_ADDI                 = 0x08,
        OP_ADDIU                = 0x09,
        OP_SLTI                 = 0x0a,
        OP_SLTIU                = 0x0b,
        OP_ANDI                 = 0x0c,
        OP_ORI                  = 0x0d,
        OP_XORI                 = 0x0e,
        OP_LUI                  = 0x0f,
        OP_CP0                  = 0x10,
        OP_CP2                  = 0x12,
        OP_LB                   = 0x20,
        OP_LH                   = 0x21,
        OP_LWL                  = 0x22,
        OP_LW                   = 0x23,
        OP_LBU                  = 0x24,
        OP_LHU                  = 0x25,
        OP_LWR                  = 0x26,
        OP_SB                   = 0x28,
        OP_SH                   = 0x29,
        OP_SWL                  = 0x2a,
        OP_SW                   = 0x2b,
        OP_SWR                  = 0x2e,
        OP_LWC2                 = 0x32,
        OP_SWC2                 = 0x3a,
};

static char *name = (char*) "beetle_psx_libretro";

void PS_CPU_LIGHTREC::pgxp_cop2_notify(struct lightrec_state *state, u32 op, u32 data)
{
	if((op >> 26) == OP_CP2) {
		switch ((op >> 21) & 0x1F) {
			case 0x00: PGXP_GTE_MFC2(op, data, data); break;
			case 0x02: PGXP_GTE_CFC2(op, data, data); break;
			case 0x04: PGXP_GTE_MTC2(op, data, data); break;
			case 0x06: PGXP_GTE_CTC2(op, data, data); break;
		}
	}
}

static bool cp2_ops[0x40] = {0,1,0,0,0,0,1,0,0,0,0,0,1,0,0,0,
                             1,1,1,1,1,0,1,0,0,0,0,1,1,0,1,0,
                             1,0,0,0,0,0,0,0,1,1,1,0,0,1,1,0,
                             1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1};

void PS_CPU_LIGHTREC::cop2_op(struct lightrec_state *state, u32 func)
{
   if (MDFN_UNLIKELY(!cp2_ops[func & 0x3f]))
   {
      MDFN_DispMessage(3, RETRO_LOG_WARN,
            RETRO_MESSAGE_TARGET_LOG, RETRO_MESSAGE_TYPE_NOTIFICATION_ALT,
            "Invalid CP2 function %u\n", func);
   }
   else
   {
      GTE_Instruction(func);
   }
}

void PS_CPU_LIGHTREC::reset_target_cycle_count(struct lightrec_state *state, pscpu_timestamp_t timestamp){
	if (timestamp >= next_event_ts)
		lightrec_set_exit_flags(state, LIGHTREC_EXIT_CHECK_INTERRUPT);
}

void PS_CPU_LIGHTREC::hw_write_byte(struct lightrec_state *state,
		u32 opcode, void *host,	u32 mem, u32 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	PSX_MemWrite8(timestamp, mem, val);

	reset_target_cycle_count(state, timestamp);
}

void PS_CPU_LIGHTREC::pgxp_nonhw_write_byte(struct lightrec_state *state,
		u32 opcode, void *host,	u32 mem, u32 val)
{
	*(u8 *)host = val;
	PGXP_CPU_SB(opcode, val, mem);

	if (useInvalidate)
		lightrec_invalidate(state, mem, 1);
}

void PS_CPU_LIGHTREC::pgxp_hw_write_byte(struct lightrec_state *state,
		u32 opcode, void *host,	u32 mem, u32 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	PSX_MemWrite8(timestamp, mem, val);

	PGXP_CPU_SB(opcode, val, mem);

	reset_target_cycle_count(state, timestamp);
}

void PS_CPU_LIGHTREC::hw_write_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	PSX_MemWrite16(timestamp, mem, val);

	reset_target_cycle_count(state, timestamp);
}

void PS_CPU_LIGHTREC::pgxp_nonhw_write_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	*(u16 *)host = HTOLE16(val);
	PGXP_CPU_SH(opcode, val, mem);

	if (useInvalidate)
		lightrec_invalidate(state, mem, 2);
}

void PS_CPU_LIGHTREC::pgxp_hw_write_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	PSX_MemWrite16(timestamp, mem, val);

	PGXP_CPU_SH(opcode, val, mem);

	reset_target_cycle_count(state, timestamp);
}

void PS_CPU_LIGHTREC::hw_write_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	PSX_MemWrite32(timestamp, mem, val);

	reset_target_cycle_count(state, timestamp);
}

void PS_CPU_LIGHTREC::pgxp_nonhw_write_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	*(u32 *)host = HTOLE32(val);

	switch (opcode >> 26){
		case 0x2A:
			PGXP_CPU_SWL(opcode, val, mem + (opcode & 0x3));
			break;
		case 0x2B:
			PGXP_CPU_SW(opcode, val, mem);
			break;
		case 0x2E:
			PGXP_CPU_SWR(opcode, val, mem + (opcode & 0x3));
			break;
		case 0x3A:
			PGXP_GTE_SWC2(opcode, val, mem);
			break;
		default:
			break;
	}

	if (useInvalidate)
		lightrec_invalidate(state, mem, 4);
}

void PS_CPU_LIGHTREC::pgxp_nonhw_write_word_unsigned(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	*(u32 *)host = HTOLE32(val);

	PGXP_CPU_SW(opcode, val, mem);

	if (useInvalidate)
		lightrec_invalidate(state, mem, 4);
}

void PS_CPU_LIGHTREC::pgxp_hw_write_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	PSX_MemWrite32(timestamp, mem, val);

	switch (opcode >> 26){
		case OP_SWL:
			PGXP_CPU_SWL(opcode, val, mem + (opcode & 0x3));
			break;
		case OP_SW:
			PGXP_CPU_SW(opcode, val, mem);
			break;
		case OP_SWR:
			PGXP_CPU_SWR(opcode, val, mem + (opcode & 0x3));
			break;
		case OP_SWC2:
			PGXP_GTE_SWC2(opcode, val, mem);
			break;
		default:
			break;
	}

	reset_target_cycle_count(state, timestamp);
}

u8 PS_CPU_LIGHTREC::hw_read_byte(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u8 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	val = PSX_MemRead8(timestamp, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp + cpu_timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

u8 PS_CPU_LIGHTREC::pgxp_nonhw_read_byte(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u8 val = *(u8 *)host;

	if((opcode >> 26) == OP_LB)
		PGXP_CPU_LB(opcode, val, mem);
	else
		PGXP_CPU_LBU(opcode, val, mem);

	return val;
}

u8 PS_CPU_LIGHTREC::pgxp_hw_read_byte(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u8 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	val = PSX_MemRead8(timestamp, mem);

	if((opcode >> 26) == OP_LB)
		PGXP_CPU_LB(opcode, val, mem);
	else
		PGXP_CPU_LBU(opcode, val, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp + cpu_timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

u16 PS_CPU_LIGHTREC::hw_read_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u16 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	val = PSX_MemRead16(timestamp, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp + cpu_timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

u16 PS_CPU_LIGHTREC::pgxp_nonhw_read_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u16 val = LE16TOH(*(u16 *)host);

	if((opcode >> 26) == OP_LH)
		PGXP_CPU_LH(opcode, val, mem);
	else
		PGXP_CPU_LHU(opcode, val, mem);

	return val;
}

u16 PS_CPU_LIGHTREC::pgxp_hw_read_half(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u16 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	val = PSX_MemRead16(timestamp, mem);

	if((opcode >> 26) == OP_LH)
		PGXP_CPU_LH(opcode, val, mem);
	else
		PGXP_CPU_LHU(opcode, val, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp + cpu_timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

u32 PS_CPU_LIGHTREC::hw_read_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u32 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	val = PSX_MemRead32(timestamp, mem);

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp + cpu_timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

u32 PS_CPU_LIGHTREC::pgxp_nonhw_read_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u32 val = LE32TOH(*(u32 *)host);

	switch (opcode >> 26){
		case OP_LWL:
			//TODO: OR with masked register
			PGXP_CPU_LWL(opcode, val << (24-(opcode & 0x3)*8), mem + (opcode & 0x3));
			break;
		case OP_LW:
			PGXP_CPU_LW(opcode, val, mem);
			break;
		case OP_LWR:
			//TODO: OR with masked register
			PGXP_CPU_LWR(opcode, val >> ((opcode & 0x3)*8), mem + (opcode & 0x3));
			break;
		case OP_LWC2:
			PGXP_GTE_LWC2(opcode, val, mem);
			break;
		default:
			break;
	}

	return val;
}

u32 PS_CPU_LIGHTREC::pgxp_nonhw_read_word_unsigned(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u32 val = LE32TOH(*(u32 *)host);

	PGXP_CPU_LW(opcode, val, mem);

	return val;
}

u32 PS_CPU_LIGHTREC::pgxp_hw_read_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	u32 val;

	pscpu_timestamp_t timestamp = lightrec_current_cycle_count(state) - cpu_timestamp;

	val = PSX_MemRead32(timestamp, mem);

	switch (opcode >> 26){
		case OP_LWL:
			//TODO: OR with masked register
			PGXP_CPU_LWL(opcode, val << (24-(opcode & 0x3)*8), mem + (opcode & 0x3));
			break;
		case OP_LW:
			PGXP_CPU_LW(opcode, val, mem);
			break;
		case OP_LWR:
			//TODO: OR with masked register
			PGXP_CPU_LWR(opcode, val >> ((opcode & 0x3)*8), mem + (opcode & 0x3));
			break;
		case OP_LWC2:
			PGXP_GTE_LWC2(opcode, val, mem);
			break;
		default:
			break;
	}

	/* Calling PSX_MemRead* might update timestamp - Make sure
	 * here that state->current_cycle stays in sync. */
	lightrec_reset_cycle_count(lightrec_state, timestamp + cpu_timestamp);

	reset_target_cycle_count(state, timestamp);

	return val;
}

struct lightrec_mem_map_ops PS_CPU_LIGHTREC::pgxp_nonhw_regs_ops = {
	.sb = pgxp_nonhw_write_byte,
	.sh = pgxp_nonhw_write_half,
	.sw = pgxp_nonhw_write_word,
	.lb = pgxp_nonhw_read_byte,
	.lh = pgxp_nonhw_read_half,
	.lw = pgxp_nonhw_read_word,
	.lwu = pgxp_nonhw_read_word_unsigned,
	.swu = pgxp_nonhw_write_word_unsigned,
};

struct lightrec_mem_map_ops PS_CPU_LIGHTREC::pgxp_hw_regs_ops = {
	.sb = pgxp_hw_write_byte,
	.sh = pgxp_hw_write_half,
	.sw = pgxp_hw_write_word,
	.lb = pgxp_hw_read_byte,
	.lh = pgxp_hw_read_half,
	.lw = pgxp_hw_read_word,
};

struct lightrec_mem_map_ops PS_CPU_LIGHTREC::hw_regs_ops = {
	.sb = hw_write_byte,
	.sh = hw_write_half,
	.sw = hw_write_word,
	.lb = hw_read_byte,
	.lh = hw_read_half,
	.lw = hw_read_word,
};

u32 PS_CPU_LIGHTREC::cache_ctrl_read_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem)
{
	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LW(opcode, BIU, mem);

	return BIU;
}

void PS_CPU_LIGHTREC::cache_ctrl_write_word(struct lightrec_state *state,
		u32 opcode, void *host, u32 mem, u32 val)
{
	BIU = val;

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SW(opcode, BIU, mem);
}

struct lightrec_mem_map_ops PS_CPU_LIGHTREC::cache_ctrl_ops = {
	.sb = NULL,
	.sh = NULL,
	.sw = cache_ctrl_write_word,
	.lb = NULL,
	.lh = NULL,
	.lw = cache_ctrl_read_word,
};

struct lightrec_mem_map PS_CPU_LIGHTREC::lightrec_map[] = {
	[PSX_MAP_KERNEL_USER_RAM] = {
		/* Kernel and user memory */
		.pc = 0x00000000,
		.length = 0x200000,
	},
	[PSX_MAP_BIOS] = {
		/* BIOS */
		.pc = 0x1fc00000,
		.length = 0x80000,
	},
	[PSX_MAP_SCRATCH_PAD] = {
		/* Scratch pad */
		.pc = 0x1f800000,
		.length = 0x400,
	},
	[PSX_MAP_PARALLEL_PORT] = {
		/* Parallel port */
		.pc = 0x1f000000,
		.length = 0x800000,
	},
	[PSX_MAP_HW_REGISTERS] = {
		/* Hardware registers */
		.pc = 0x1f801000,
		.length = 0x2000,
	},
	[PSX_MAP_CACHE_CONTROL] = {
		/* Cache control */
		.pc = 0x5ffe0130,
		.length = 4,
		.address = NULL,
		.ops = &cache_ctrl_ops,
	},

	/* Mirrors of the kernel/user memory */
	[PSX_MAP_MIRROR1] = {
		.pc = 0x00200000,
		.length = 0x200000,
		.address = NULL,
		.ops = NULL,
		.mirror_of = &lightrec_map[PSX_MAP_KERNEL_USER_RAM],
	},
	[PSX_MAP_MIRROR2] = {
		.pc = 0x00400000,
		.length = 0x200000,
		.address = NULL,
		.ops = NULL,
		.mirror_of = &lightrec_map[PSX_MAP_KERNEL_USER_RAM],
	},
	[PSX_MAP_MIRROR3] = {
		.pc = 0x00600000,
		.length = 0x200000,
		.address = NULL,
		.ops = NULL,
		.mirror_of = &lightrec_map[PSX_MAP_KERNEL_USER_RAM],
	},

        /* Code buffer */
	[PSX_MAP_CODE_BUFFER] = {
	},

	/* Mirror of the parallel port. Only used by the PS2/PS3 BIOS */
	[PSX_MAP_PPORT_MIRROR] = {
		.pc = 0x1fa00000,
		.length = 0x10000,
		.address = NULL,
		.ops = NULL,
		.mirror_of = &lightrec_map[PSX_MAP_PARALLEL_PORT],
	},
};

void PS_CPU_LIGHTREC::enable_ram(struct lightrec_state *state, _Bool enable)
{
	if (enable) {
		memcpy(MainRAM->data8, cache_buf, sizeof(cache_buf));
	} else {
		memcpy(cache_buf, MainRAM->data8, sizeof(cache_buf));
	}
}

struct lightrec_ops PS_CPU_LIGHTREC::ops = {
	.cop2_op = cop2_op,
	.enable_ram = enable_ram,
};

struct lightrec_ops PS_CPU_LIGHTREC::pgxp_ops = {
	.cop2_notify = pgxp_cop2_notify,
	.cop2_op = cop2_op,
	.enable_ram = enable_ram,
};

int PS_CPU_LIGHTREC::lightrec_plugin_init()
{
	struct lightrec_ops *cop_ops;
	uint8_t *psxM = (uint8_t *) MainRAM->data8;
	uint8_t *psxR = (uint8_t *) BIOSROM->data8;
	uint8_t *psxH = (uint8_t *) ScratchRAM->data8;
	uint8_t *psxP = (uint8_t *) PSX_LoadExpansion1();

	if(lightrec_state){
		//move data out of lightrec_regs struct, GTE stops using lightrec regs
		CopyFromLightrec();
		log_cb(RETRO_LOG_INFO, "Lightrec restarting\n");
		lightrec_destroy(lightrec_state);
	}else{
		log_cb(RETRO_LOG_INFO, "Lightrec map addresses: M=0x%lx, P=0x%lx, R=0x%lx, H=0x%lx\n",
			(uintptr_t) psxM,
			(uintptr_t) psxP,
			(uintptr_t) psxR,
			(uintptr_t) psxH);
	}

	lightrec_map[PSX_MAP_KERNEL_USER_RAM].address = psxM;

	if(psx_mmap == 4){
		lightrec_map[PSX_MAP_MIRROR1].address = psxM + 0x200000;
		lightrec_map[PSX_MAP_MIRROR2].address = psxM + 0x400000;
		lightrec_map[PSX_MAP_MIRROR3].address = psxM + 0x600000;
	}

	lightrec_map[PSX_MAP_BIOS].address = psxR;
	lightrec_map[PSX_MAP_SCRATCH_PAD].address = psxH;
	lightrec_map[PSX_MAP_PARALLEL_PORT].address = psxP;

	if(lightrec_codebuffer){
		lightrec_map[PSX_MAP_CODE_BUFFER].address = lightrec_codebuffer;
		lightrec_map[PSX_MAP_CODE_BUFFER].length = LIGHTREC_CODEBUFFER_SIZE,
		log_cb(RETRO_LOG_INFO, "Lightrec codebuffer address: 0x%lx, size: %uMB (0x%08x)\n", lightrec_codebuffer, LIGHTREC_CODEBUFFER_SIZE/(1024*1024),lightrec_map[PSX_MAP_CODE_BUFFER].length);
	}

	if (PGXP_GetModes() & (PGXP_MODE_MEMORY | PGXP_MODE_GTE)){
		lightrec_map[PSX_MAP_HW_REGISTERS].ops = &pgxp_hw_regs_ops;
		lightrec_map[PSX_MAP_KERNEL_USER_RAM].ops = &pgxp_nonhw_regs_ops;
		lightrec_map[PSX_MAP_BIOS].ops = &pgxp_nonhw_regs_ops;
		lightrec_map[PSX_MAP_SCRATCH_PAD].ops = &pgxp_nonhw_regs_ops;

		cop_ops = &pgxp_ops;
	} else {
		lightrec_map[PSX_MAP_HW_REGISTERS].ops = &hw_regs_ops;
		lightrec_map[PSX_MAP_KERNEL_USER_RAM].ops = NULL;
		lightrec_map[PSX_MAP_BIOS].ops = NULL;
		lightrec_map[PSX_MAP_SCRATCH_PAD].ops = NULL;

		cop_ops = &ops;
	}

	lightrec_state = lightrec_init(name,
			lightrec_map, ARRAY_SIZE(lightrec_map), cop_ops);

	lightrec_regs = lightrec_get_registers(lightrec_state);

	SetUnsafeFlags();

	//move data into lightrec_regs struct, makes GTE use lightrec regs
	CopyToLightrec();

	cpu_timestamp = 0;

	return 0;
}

void PS_CPU_LIGHTREC::SetUnsafeFlags()
{
	u32 flags = useSPGPopt?LIGHTREC_OPT_SP_GP_HIT_RAM:0;
	//set invalidate dma only flag when invalidate false
	if(!useInvalidate)
		flags |= LIGHTREC_OPT_INV_DMA_ONLY;
	lightrec_set_unsafe_opt_flags(lightrec_state, flags);
}

void PS_CPU_LIGHTREC::CopyToLightrec()
{
        memcpy(lightrec_regs->gpr,&(Cpu->GPR),32*sizeof(uint32_t));
        lightrec_regs->gpr[32] = Cpu->LO;
        lightrec_regs->gpr[33] = Cpu->HI;
        memcpy(lightrec_regs->cp0,&(Cpu->CP0),32*sizeof(uint32_t));
        GTE_SwitchRegisters(true,lightrec_regs->cp2d);
}

void PS_CPU_LIGHTREC::CopyFromLightrec()
{
        memcpy(&(Cpu->GPR),lightrec_regs->gpr,32*sizeof(uint32_t));
        Cpu->LO = lightrec_regs->gpr[32];
        Cpu->HI = lightrec_regs->gpr[33];
        memcpy(&(Cpu->CP0),lightrec_regs->cp0,32*sizeof(uint32_t));
        GTE_SwitchRegisters(false,lightrec_regs->cp2d);
}

void PS_CPU_LIGHTREC::lightrec_plugin_clear(u32 addr, u32 size)
{
	if (lightrec_state)	/* size * 4: uses DMA units */
		lightrec_invalidate(lightrec_state, addr, size * 4);
}
