/* Mednafen - Multi-system Emulator
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "psx.h"
#include "cpu.h"

#include "../state_helpers.h"

// iCB: PGXP STUFF
#include "../pgxp/pgxp_cpu.h"
#include "../pgxp/pgxp_gte.h"
#include "../pgxp/pgxp_main.h"
// int pgxpMode = PGXP_GetModes();

extern bool psx_gte_overclock;

#define BIU_ENABLE_ICACHE_S1	0x00000800	// Enable I-cache, set 1
#define BIU_ICACHE_FSIZE_MASK	0x00000300  // I-cache fill size mask; 0x000 = 2 words, 0x100 = 4 words, 0x200 = 8 words, 0x300 = 16 words
#define BIU_ENABLE_DCACHE	   0x00000080	// Enable D-cache
#define BIU_DCACHE_SCRATCHPAD	0x00000008	// Enable scratchpad RAM mode of D-cache
#define BIU_TAG_TEST_MODE	   0x00000004	// Enable TAG test mode(IsC must be set to 1 as well presumably?)
#define BIU_INVALIDATE_MODE	0x00000002	// Enable Invalidate mode(IsC must be set to 1 as well presumably?)
#define BIU_LOCK_MODE		   0x00000001	// Enable Lock mode(IsC must be set to 1 as well presumably?)

PS_CPU::PS_CPU()
{
   uint64_t a;
   unsigned i;
   Halted = false;

   memset(FastMap, 0, sizeof(FastMap));
   memset(DummyPage, 0xFF, sizeof(DummyPage));	// 0xFF to trigger an illegal instruction exception, so we'll know what's up when debugging.

   for(a = 0x00000000; a < (UINT64_C(1) << 32); a += FAST_MAP_PSIZE)
      SetFastMap(DummyPage, a, FAST_MAP_PSIZE);

   CPUHook = NULL;
   ADDBT = NULL;

   GTE_Init();

   for(i = 0; i < 24; i++)
   {
      uint8 v = 7;

      if(i < 12)
         v += 4;

      if(i < 21)
         v += 3;

      MULT_Tab24[i] = v;
   }
}

PS_CPU::~PS_CPU()
{


}

void PS_CPU::SetFastMap(void *region_mem, uint32_t region_address, uint32_t region_size)
{
   uint64_t A;
   // FAST_MAP_SHIFT
   // FAST_MAP_PSIZE

   for(A = region_address; A < (uint64)region_address + region_size; A += FAST_MAP_PSIZE)
      FastMap[A >> FAST_MAP_SHIFT] = ((uint8_t *)region_mem - region_address);
}

INLINE void PS_CPU::RecalcIPCache(void)
{
   IPCache = 0;

   if(((CP0.SR & CP0.CAUSE & 0xFF00) && (CP0.SR & 1)))
      IPCache = 0x80;

   if(Halted)
      IPCache = 0x80;
}

void PS_CPU::SetHalt(bool status)
{
   Halted = status;
   RecalcIPCache();
}

void PS_CPU::Power(void)
{
   unsigned i;

   assert(sizeof(ICache) == sizeof(ICache_Bulk));

   memset(GPR, 0, sizeof(GPR));
   memset(&CP0, 0, sizeof(CP0));
   LO = 0;
   HI = 0;

   gte_ts_done = 0;
   muldiv_ts_done = 0;

   BACKED_PC = 0xBFC00000;
   BACKED_new_PC = 4;
   BACKED_new_PC_mask = ~0U;

   BACKED_LDWhich = 0x20;
   BACKED_LDValue = 0;
   LDAbsorb = 0;
   memset(ReadAbsorb, 0, sizeof(ReadAbsorb));
   ReadAbsorbWhich = 0;
   ReadFudge = 0;

   CP0.SR |= (1 << 22);	// BEV
   CP0.SR |= (1 << 21);	// TS

   CP0.PRID = 0x2;

   RecalcIPCache();


   BIU = 0;

   memset(ScratchRAM.data8, 0, 1024);

   PGXP_Init();

   // Not quite sure about these poweron/reset values:
   for(i = 0; i < 1024; i++)
   {
      ICache[i].TV = 0x2 | ((BIU & 0x800) ? 0x0 : 0x1);
      ICache[i].Data = 0;
   }

   GTE_Power();
}

int PS_CPU::StateAction(StateMem *sm, int load, int data_only)
{
   SFORMAT StateRegs[] =
   {
      SFARRAY32(GPR, 32),
      SFVAR(LO),
      SFVAR(HI),
      SFVAR(BACKED_PC),
      SFVAR(BACKED_new_PC),
      SFVAR(BACKED_new_PC_mask),

      SFVAR(IPCache),
      SFVAR(Halted),

      SFVAR(BACKED_LDWhich),
      SFVAR(BACKED_LDValue),
      SFVAR(LDAbsorb),

      SFVAR(next_event_ts),
      SFVAR(gte_ts_done),
      SFVAR(muldiv_ts_done),

      SFVAR(BIU),
      SFARRAY32(ICache_Bulk, 2048),

      SFARRAY32(CP0.Regs, 32),

      SFARRAY(ReadAbsorb, 0x20),
      SFVARN(ReadAbsorb[0x20], "ReadAbsorbDummy"),
      SFVAR(ReadAbsorbWhich),
      SFVAR(ReadFudge),

      SFARRAY(ScratchRAM.data8, 1024),

      SFEND
   };
   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "CPU");

   ret &= GTE_StateAction(sm, load, data_only);

   if(load)
   {

   }

   return(ret);
}

void PS_CPU::AssertIRQ(unsigned which, bool asserted)
{
   assert(which <= 5);

   CP0.CAUSE &= ~(1 << (10 + which));

   if(asserted)
      CP0.CAUSE |= 1 << (10 + which);

   RecalcIPCache();
}

void PS_CPU::SetBIU(uint32_t val)
{
   const uint32_t old_BIU = BIU;

   BIU = val & ~(0x440);

   if((BIU ^ old_BIU) & 0x800)
   {
      unsigned i;

      if(BIU & 0x800)	// ICache enabled
      {
         for(i = 0; i < 1024; i++)
            ICache[i].TV &= ~0x1;
      }
      else			// ICache disabled
      {
         for(i = 0; i < 1024; i++)
            ICache[i].TV |= 0x1;
      }
   }

   PSX_DBG(PSX_DBG_SPARSE, "[CPU] Set BIU=0x%08x\n", BIU);
}

uint32_t PS_CPU::GetBIU(void)
{
   return BIU;
}

static const uint32_t addr_mask[8] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
				     0x7FFFFFFF, 0x1FFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };

template<typename T>
INLINE T PS_CPU::PeekMemory(uint32_t address)
{
   address &= addr_mask[address >> 29];

   if(address >= 0x1F800000 && address <= 0x1F8003FF)
      return ScratchRAM.Read<T>(address & 0x3FF);

   //assert(!(CP0.SR & 0x10000));

   if(sizeof(T) == 1)
      return PSX_MemPeek8(address);
   else if(sizeof(T) == 2)
      return PSX_MemPeek16(address);
   return PSX_MemPeek32(address);
}

template<typename T>
void PS_CPU::PokeMemory(uint32 address, T value)
{
   address &= addr_mask[address >> 29];

   if(address >= 0x1F800000 && address <= 0x1F8003FF)
      return ScratchRAM.Write<T>(address & 0x3FF, value);

   if(sizeof(T) == 1)
      PSX_MemPoke8(address, value);
   else if(sizeof(T) == 2)
      PSX_MemPoke16(address, value);
   else
      PSX_MemPoke32(address, value);
}

template<typename T>
INLINE T PS_CPU::ReadMemory(int32_t &timestamp, uint32_t address, bool DS24, bool LWC_timing)
{
   T ret;

   ReadAbsorb[ReadAbsorbWhich] = 0;
   ReadAbsorbWhich = 0;

   address &= addr_mask[address >> 29];

   if(address >= 0x1F800000 && address <= 0x1F8003FF)
   {
      LDAbsorb = 0;

      if(DS24)
         return ScratchRAM.ReadU24(address & 0x3FF);
      return ScratchRAM.Read<T>(address & 0x3FF);
   }

   timestamp += (ReadFudge >> 4) & 2;

   //assert(!(CP0.SR & 0x10000));

   int32_t lts = timestamp;

   if(sizeof(T) == 1)
      ret = PSX_MemRead8(lts, address);
   else if(sizeof(T) == 2)
      ret = PSX_MemRead16(lts, address);
   else
   {
      if(DS24)
         ret = PSX_MemRead24(lts, address) & 0xFFFFFF;
      else
         ret = PSX_MemRead32(lts, address);
   }

   if(LWC_timing)
      lts += 1;
   else
      lts += 2;

   LDAbsorb = (lts - timestamp);
   timestamp = lts;

   return(ret);
}

template<typename T>
INLINE void PS_CPU::WriteMemory(int32_t &timestamp, uint32_t address, uint32_t value, bool DS24)
{
   if(MDFN_LIKELY(!(CP0.SR & 0x10000)))
   {
      address &= addr_mask[address >> 29];

      if(address >= 0x1F800000 && address <= 0x1F8003FF)
      {
         if(DS24)
            ScratchRAM.WriteU24(address & 0x3FF, value);
         else
            ScratchRAM.Write<T>(address & 0x3FF, value);

         return;
      }

      if(sizeof(T) == 1)
         PSX_MemWrite8(timestamp, address, value);
      else if(sizeof(T) == 2)
         PSX_MemWrite16(timestamp, address, value);
      else
      {
         if(DS24)
            PSX_MemWrite24(timestamp, address, value);
         else
            PSX_MemWrite32(timestamp, address, value);
      }
   }
   else
   {
	   if(BIU & BIU_ENABLE_ICACHE_S1)	// Instruction cache is enabled/active
	   {
		   if(BIU & (BIU_TAG_TEST_MODE | BIU_INVALIDATE_MODE | BIU_LOCK_MODE))
		   {
			   const uint8 valid_bits = (BIU & BIU_TAG_TEST_MODE) ? ((value << ((address & 0x3) * 8)) & 0x0F) : 0x00;
			   __ICache* const ICI = &ICache[((address & 0xFF0) >> 2)];

			   //
			   // Set validity bits and tag.
			   //
			   for(unsigned i = 0; i < 4; i++)
				   ICI[i].TV = ((valid_bits & (1U << i)) ? 0x00 : 0x02) | (address & 0xFFFFFFF0) | (i << 2);
		   }
		   else
         {
            ICache[(address & 0xFFC) >> 2].Data = value << ((address & 0x3) * 8);
         }
      }

      if((BIU & 0x081) == 0x080)	// Writes to the scratchpad(TODO test)
      {
         if(DS24)
            ScratchRAM.WriteU24(address & 0x3FF, value);
         else
            ScratchRAM.Write<T>(address & 0x3FF, value);
      }
      //printf("IsC WRITE%d 0x%08x 0x%08x -- CP0.SR=0x%08x\n", (int)sizeof(T), address, value, CP0.SR);
   }
}

INLINE uint32 PS_CPU::ReadInstruction(pscpu_timestamp_t &timestamp, uint32 address)
{
	uint32 instr = ICache[(address & 0xFFC) >> 2].Data;

	if(ICache[(address & 0xFFC) >> 2].TV != address)
	{
		ReadAbsorb[ReadAbsorbWhich] = 0;
		ReadAbsorbWhich = 0;

		// FIXME: Handle executing out of scratchpad.
		if(address >= 0xA0000000 || !(BIU & 0x800))
		{
			instr = LoadU32_LE((uint32_t *)&FastMap[address >> FAST_MAP_SHIFT][address]);

			if (!psx_gte_overclock)
			{
				// Approximate best-case cache-disabled time, per PS1 tests
				// (executing out of 0xA0000000+); it can be 5 in 
				// *some* sequences of code(like a lot of sequential "nop"s, 
				// probably other simple instructions too).
				timestamp += 4;
			}
		}
		else
		{
			__ICache *ICI = &ICache[((address & 0xFF0) >> 2)];
			const uint32_t *FMP = (uint32_t *)&FastMap[(address &~ 0xF) >> FAST_MAP_SHIFT][address &~ 0xF];

			// | 0x2 to simulate (in)validity bits.
			ICI[0x00].TV = (address &~ 0xF) | 0x00 | 0x2;
			ICI[0x01].TV = (address &~ 0xF) | 0x04 | 0x2;
			ICI[0x02].TV = (address &~ 0xF) | 0x08 | 0x2;
			ICI[0x03].TV = (address &~ 0xF) | 0x0C | 0x2;

			// When overclock is enabled, remove code cache fetch latency
			if (!psx_gte_overclock)
				timestamp += 3;

			switch(address & 0xC)
			{
			case 0x0:
				if (!psx_gte_overclock)
					timestamp++;
				ICI[0x00].TV &= ~0x2;
				ICI[0x00].Data = LoadU32_LE(&FMP[0]);
			case 0x4:
				if (!psx_gte_overclock)
					timestamp++;
				ICI[0x01].TV &= ~0x2;
				ICI[0x01].Data = LoadU32_LE(&FMP[1]);
			case 0x8:
				if (!psx_gte_overclock)
					timestamp++;
				ICI[0x02].TV &= ~0x2;
				ICI[0x02].Data = LoadU32_LE(&FMP[2]);
			case 0xC:
				if (!psx_gte_overclock)
					timestamp++;
				ICI[0x03].TV &= ~0x2;
				ICI[0x03].Data = LoadU32_LE(&FMP[3]);
				break;
			}
			instr = ICache[(address & 0xFFC) >> 2].Data;
		}
	}

	return instr;
}

uint32_t PS_CPU::Exception(uint32_t code, uint32_t PC, const uint32 NP, const uint32_t NPM, const uint32_t instr)
{
   const bool AfterBranchInstr = !(NPM & 0x1);
   const bool BranchTaken = !(NPM & 0x3);
   uint32_t handler = 0x80000080;

   assert(code < 16);

   if(CP0.SR & (1 << 22))	// BEV
      handler = 0xBFC00180;

   CP0.EPC = PC;
   if(AfterBranchInstr)
   {
      CP0.EPC -= 4;
      CP0.TAR = (PC & (NPM | 3)) + NP;
   }

#ifdef HAVE_DEBUG
   if(ADDBT)
      ADDBT(PC, handler, true);
#endif

   // "Push" IEc and KUc(so that the new IEc and KUc are 0)
   CP0.SR = (CP0.SR & ~0x3F) | ((CP0.SR << 2) & 0x3F);

   // Setup cause register
   CP0.CAUSE &= 0x0000FF00;
   CP0.CAUSE |= code << 2;

   // If EPC was adjusted -= 4 because we are after a branch instruction, set bit 31.
   CP0.CAUSE |= AfterBranchInstr << 31;
   CP0.CAUSE |= BranchTaken << 30;
   CP0.CAUSE |= (instr << 2) & (0x3 << 28); // CE

   RecalcIPCache();

   return(handler);
}

#define BACKING_TO_ACTIVE			\
	PC = BACKED_PC;				\
	new_PC = BACKED_new_PC;			\
	new_PC_mask = BACKED_new_PC_mask;	\
	LDWhich = BACKED_LDWhich;		\
	LDValue = BACKED_LDValue;

#define ACTIVE_TO_BACKING			\
	BACKED_PC = PC;				\
	BACKED_new_PC = new_PC;			\
	BACKED_new_PC_mask = new_PC_mask;	\
	BACKED_LDWhich = LDWhich;		\
	BACKED_LDValue = LDValue;

#define GPR_DEPRES_BEGIN { uint8_t back = ReadAbsorb[0];
#define GPR_DEP(n) { unsigned tn = (n); ReadAbsorb[tn] = 0; }
#define GPR_RES(n) { unsigned tn = (n); ReadAbsorb[tn] = 0; }
#define GPR_DEPRES_END ReadAbsorb[0] = back; }

template<bool DebugMode>
int32_t PS_CPU::RunReal(int32_t timestamp_in)
{
   uint32_t PC;
   uint32_t new_PC;
   uint32_t new_PC_mask;
   uint32_t LDWhich;
   uint32_t LDValue;
   int32_t timestamp = timestamp_in;

   //printf("%d %d\n", gte_ts_done, muldiv_ts_done);

   gte_ts_done += timestamp;
   muldiv_ts_done += timestamp;

   BACKING_TO_ACTIVE;

   do
   {
      //printf("Running: %d %d\n", timestamp, next_event_ts);
      while(MDFN_LIKELY(timestamp < next_event_ts))
      {
         uint32_t instr;
         uint32_t opf;

         // Zero must be zero...until the Master Plan is enacted.
         GPR[0] = 0;

#ifdef HAVE_DEBUG
         if(DebugMode && CPUHook)
         {
            ACTIVE_TO_BACKING;

            // For save states in step mode.
            gte_ts_done -= timestamp;
            muldiv_ts_done -= timestamp;

            CPUHook(timestamp, PC);

            // For save states in step mode.
            gte_ts_done += timestamp;
            muldiv_ts_done += timestamp;

            BACKING_TO_ACTIVE;
         }
#endif

		 //
		 // Instruction fetch
		 //
         if(MDFN_UNLIKELY(PC & 0x3))
         {
            // This will block interrupt processing, but since we're going more for keeping broken homebrew/hacks from working
            // than super-duper-accurate pipeline emulation, it shouldn't be a problem.
            new_PC = Exception(EXCEPTION_ADEL, PC, new_PC, new_PC_mask, 0);
            new_PC_mask = 0;
            goto OpDone;
         }

		 instr = ReadInstruction(timestamp, PC);


         //printf("PC=%08x, SP=%08x - op=0x%02x - funct=0x%02x - instr=0x%08x\n", PC, GPR[29], instr >> 26, instr & 0x3F, instr);
         //for(int i = 0; i < 32; i++)
         // printf("%02x : %08x\n", i, GPR[i]);
         //printf("\n");

		 //
		 // Instruction decode
		 //
         opf = instr & 0x3F;

         if(instr & (0x3F << 26))
            opf = 0x40 | (instr >> 26);

         opf |= IPCache;

         if(ReadAbsorb[ReadAbsorbWhich])
            ReadAbsorb[ReadAbsorbWhich]--;

         else
            timestamp++;

   #define DO_LDS() { GPR[LDWhich] = LDValue; ReadAbsorb[LDWhich] = LDAbsorb; ReadFudge = LDWhich; ReadAbsorbWhich |= LDWhich & 0x1F; LDWhich = 0x20; }
   #define BEGIN_OPF(name) { op_##name:
   #define END_OPF goto OpDone; }

   #define DO_BRANCH(offset, mask)			\
	{						\
	  PC = (PC & new_PC_mask) + new_PC;		\
	 new_PC = (offset);				\
	 new_PC_mask = (mask) & ~3;			\
	 /* Lower bits of new_PC_mask being clear signifies being in a branch delay slot. (overloaded behavior for performance) */	\
	 goto SkipNPCStuff;				\
	}


   #define ITYPE uint32 rs MDFN_NOWARN_UNUSED = (instr >> 21) & 0x1F; uint32 rt MDFN_NOWARN_UNUSED = (instr >> 16) & 0x1F; uint32 immediate = (int32)(int16)(instr & 0xFFFF); /*printf(" rs=%02x(%08x), rt=%02x(%08x), immediate=(%08x) ", rs, GPR[rs], rt, GPR[rt], immediate);*/
   #define ITYPE_ZE uint32 rs MDFN_NOWARN_UNUSED = (instr >> 21) & 0x1F; uint32 rt MDFN_NOWARN_UNUSED = (instr >> 16) & 0x1F; uint32 immediate = instr & 0xFFFF; /*printf(" rs=%02x(%08x), rt=%02x(%08x), immediate=(%08x) ", rs, GPR[rs], rt, GPR[rt], immediate);*/
   #define JTYPE uint32 target = instr & ((1 << 26) - 1); /*printf(" target=(%08x) ", target);*/
   #define RTYPE uint32 rs MDFN_NOWARN_UNUSED = (instr >> 21) & 0x1F; uint32 rt MDFN_NOWARN_UNUSED = (instr >> 16) & 0x1F; uint32 rd MDFN_NOWARN_UNUSED = (instr >> 11) & 0x1F; uint32 shamt MDFN_NOWARN_UNUSED = (instr >> 6) & 0x1F; /*printf(" rs=%02x(%08x), rt=%02x(%08x), rd=%02x(%08x) ", rs, GPR[rs], rt, GPR[rt], rd, GPR[rd]);*/

#if !defined(__GNUC__) || defined(NO_COMPUTED_GOTO)
   /* (uint8) cast for cheaper alternative to generated branch+compare bounds check instructions, but still more
      expensive than computed goto which needs no masking nor bounds checking.
   */
   #define CGBEGIN { enum { CGESB = 1 + __COUNTER__ }; switch((uint8)opf) {
   #define CGE(l) case __COUNTER__ - CGESB: goto l;
   #define CGEND } }
#else
   #define CGBEGIN static const void *const op_goto_table[256] = {
   #define CGE(l) &&l,
   #define CGEND }; goto *op_goto_table[opf];
#endif

   CGBEGIN
    CGE(op_SLL)  CGE(op_ILL)   CGE(op_SRL)  CGE(op_SRA)   CGE(op_SLLV)    CGE(op_ILL)   CGE(op_SRLV) CGE(op_SRAV)
    CGE(op_JR)   CGE(op_JALR)  CGE(op_ILL)  CGE(op_ILL)   CGE(op_SYSCALL) CGE(op_BREAK) CGE(op_ILL)  CGE(op_ILL)
    CGE(op_MFHI) CGE(op_MTHI)  CGE(op_MFLO) CGE(op_MTLO)  CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)
    CGE(op_MULT) CGE(op_MULTU) CGE(op_DIV)  CGE(op_DIVU)  CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)
    CGE(op_ADD)  CGE(op_ADDU)  CGE(op_SUB)  CGE(op_SUBU)  CGE(op_AND)     CGE(op_OR)    CGE(op_XOR)  CGE(op_NOR)
    CGE(op_ILL)  CGE(op_ILL)   CGE(op_SLT)  CGE(op_SLTU)  CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)
    CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)
    CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)     CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)

    CGE(op_ILL)  CGE(op_BCOND) CGE(op_J)    CGE(op_JAL)   CGE(op_BEQ)  CGE(op_BNE) CGE(op_BLEZ) CGE(op_BGTZ)
    CGE(op_ADDI) CGE(op_ADDIU) CGE(op_SLTI) CGE(op_SLTIU) CGE(op_ANDI) CGE(op_ORI) CGE(op_XORI) CGE(op_LUI)
    CGE(op_COP0) CGE(op_COP1)  CGE(op_COP2) CGE(op_COP3)  CGE(op_ILL)  CGE(op_ILL) CGE(op_ILL)  CGE(op_ILL)
    CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL)   CGE(op_ILL)  CGE(op_ILL) CGE(op_ILL)  CGE(op_ILL)
    CGE(op_LB)   CGE(op_LH)    CGE(op_LWL)  CGE(op_LW)    CGE(op_LBU)  CGE(op_LHU) CGE(op_LWR)  CGE(op_ILL)
    CGE(op_SB)   CGE(op_SH)    CGE(op_SWL)  CGE(op_SW)    CGE(op_ILL)  CGE(op_ILL) CGE(op_SWR)  CGE(op_ILL)
    CGE(op_LWC0) CGE(op_LWC1)  CGE(op_LWC2) CGE(op_LWC3)  CGE(op_ILL)  CGE(op_ILL) CGE(op_ILL)  CGE(op_ILL)
    CGE(op_SWC0) CGE(op_SWC1)  CGE(op_SWC2) CGE(op_SWC3)  CGE(op_ILL)  CGE(op_ILL) CGE(op_ILL)  CGE(op_ILL)

    // Interrupt portion of this table is constructed so that an interrupt won't be taken when the PC is pointing to a GTE instruction,
    // to avoid problems caused by pipeline vs coprocessor nuances that aren't emulated.
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)

    CGE(op_ILL)       CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_COP2)      CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
    CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT) CGE(op_INTERRUPT)
   CGEND

   {
    BEGIN_OPF(ILL);
	     PSX_WARNING("[CPU] Unknown instruction @%08x = %08x, op=%02x, funct=%02x", PC, instr, instr >> 26, (instr & 0x3F));
	     DO_LDS();
	     new_PC = Exception(EXCEPTION_RI, PC, new_PC, new_PC_mask, instr);
	     new_PC_mask = 0;
    END_OPF;

    //
    // ADD - Add Word
    //
    BEGIN_OPF(ADD);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] + GPR[rt];
	bool ep = ((~(GPR[rs] ^ GPR[rt])) & (GPR[rs] ^ result)) & 0x80000000;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ADD(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	if(MDFN_UNLIKELY(ep))
	{
	 new_PC = Exception(EXCEPTION_OV, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
	else
	 GPR[rd] = result;

    END_OPF;

    //
    // ADDI - Add Immediate Word
    //
    BEGIN_OPF(ADDI);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

        uint32 result = GPR[rs] + immediate;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ADDI(instr, result, GPR[rs]);

	bool ep = ((~(GPR[rs] ^ immediate)) & (GPR[rs] ^ result)) & 0x80000000;

	

	DO_LDS();

        if(MDFN_UNLIKELY(ep))
	{
	 new_PC = Exception(EXCEPTION_OV, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
        else
         GPR[rt] = result;

    END_OPF;

    //
    // ADDIU - Add Immediate Unsigned Word
    //
    BEGIN_OPF(ADDIU);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = GPR[rs] + immediate;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ADDIU(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;

    //
    // ADDU - Add Unsigned Word
    //
    BEGIN_OPF(ADDU);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] + GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ADDU(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // AND - And
    //
    BEGIN_OPF(AND);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] & GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_AND(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // ANDI - And Immediate
    //
    BEGIN_OPF(ANDI);
	ITYPE_ZE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = GPR[rs] & immediate;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ANDI(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;

    //
    // BEQ - Branch on Equal
    //
    BEGIN_OPF(BEQ);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	bool result = (GPR[rs] == GPR[rt]);

	DO_LDS();

	if(result)
	{
	 DO_BRANCH((immediate << 2), ~0U);
	}
    END_OPF;

    // Bah, why does MIPS encoding have to be funky like this. :(
    // Handles BGEZ, BGEZAL, BLTZ, BLTZAL
    BEGIN_OPF(BCOND);
	const uint32 tv = GPR[(instr >> 21) & 0x1F];
	uint32 riv = (instr >> 16) & 0x1F;
	uint32 immediate = (int32)(int16)(instr & 0xFFFF);
	bool result = (int32)(tv ^ (riv << 31)) < 0;
	const uint32 link = ((riv & 0x1E) == 0x10) ? 31 : 0;

	GPR_DEPRES_BEGIN
	GPR_DEP((instr >> 21) & 0x1F);
 	GPR_RES(link);
	GPR_DEPRES_END

	DO_LDS();

	if(link)	// Unconditional link reg setting.
	 GPR[link] = PC + 8;

        if(result)
	{
	 DO_BRANCH((immediate << 2), ~0U);
	}

    END_OPF;


    //
    // BGTZ - Branch on Greater than Zero
    //
    BEGIN_OPF(BGTZ);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	bool result = (int32)GPR[rs] > 0;

	DO_LDS();

	if(result)
	{
	 DO_BRANCH((immediate << 2), ~0U);
	}
    END_OPF;

    //
    // BLEZ - Branch on Less Than or Equal to Zero
    //
    BEGIN_OPF(BLEZ);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	bool result = (int32)GPR[rs] <= 0;

	DO_LDS();

	if(result)
	{
	 DO_BRANCH((immediate << 2), ~0U);
	}

    END_OPF;

    //
    // BNE - Branch on Not Equal
    //
    BEGIN_OPF(BNE);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	bool result = GPR[rs] != GPR[rt];

	DO_LDS();

	if(result)
	{
	 DO_BRANCH((immediate << 2), ~0U);
	}

    END_OPF;

    //
    // BREAK - Breakpoint
    //
    BEGIN_OPF(BREAK);
	PSX_WARNING("[CPU] BREAK BREAK BREAK BREAK DAAANCE -- PC=0x%08x", PC);

	DO_LDS();
	new_PC = Exception(EXCEPTION_BP, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;

    // Cop "instructions":	CFCz(no CP0), COPz, CTCz(no CP0), LWCz(no CP0), MFCz, MTCz, SWCz(no CP0)
    //
    // COP0 instructions
    BEGIN_OPF(COP0);
	uint32 sub_op = (instr >> 21) & 0x1F;

	if(sub_op & 0x10)
	 sub_op = 0x10 + (instr & 0x3F);

	//printf("COP0 thing: %02x\n", sub_op);
	switch(sub_op)
   {
      default:
         DO_LDS();
         break;

      case 0x00:		// MFC0	- Move from Coprocessor
         {
            uint32 rt = (instr >> 16) & 0x1F;
            uint32 rd = (instr >> 11) & 0x1F;

            //printf("MFC0: rt=%d <- rd=%d(%08x)\n", rt, rd, CP0.Regs[rd]);
            DO_LDS();

            LDAbsorb = 0;
            LDWhich = rt;
            LDValue = CP0.Regs[rd];
         }
         break;

      case 0x04:		// MTC0	- Move to Coprocessor
         {
            uint32 rt = (instr >> 16) & 0x1F;
            uint32 rd = (instr >> 11) & 0x1F;
            uint32 val = GPR[rt];

            if(rd != CP0REG_PRID && rd != CP0REG_CAUSE && rd != CP0REG_SR && val)
            {
               PSX_WARNING("[CPU] Unimplemented MTC0: rt=%d(%08x) -> rd=%d", rt, GPR[rt], rd);
            }

            switch(rd)
            {
               case CP0REG_BPC:
                  CP0.BPC = val;
                  break;

               case CP0REG_BDA:
                  CP0.BDA = val;
                  break;

               case CP0REG_TAR:
                  CP0.TAR = val;
                  break;

               case CP0REG_DCIC:
                  CP0.DCIC = val & 0xFF80003F;
                  break;

               case CP0REG_BDAM:
                  CP0.BDAM = val;
                  break;

               case CP0REG_BPCM:
                  CP0.BPCM = val;
                  break;

               case CP0REG_CAUSE:
                  CP0.CAUSE &= ~(0x3 << 8);
                  CP0.CAUSE |= val & (0x3 << 8);
                  RecalcIPCache();
                  break;

               case CP0REG_SR:
                  if((CP0.SR ^ val) & 0x10000)
                     PSX_DBG(PSX_DBG_SPARSE, "[CPU] IsC %u->%u\n", (bool)(CP0.SR & (1U << 16)), (bool)(val & (1U << 16)));

                  CP0.SR = val & ~( (0x3 << 26) | (0x3 << 23) | (0x3 << 6));
                  RecalcIPCache();
                  break;
            }
         }
         DO_LDS();
         break;

      case (0x10 + 0x10):	// RFE
         // "Pop"
         DO_LDS();
         CP0.SR = (CP0.SR & ~0x0F) | ((CP0.SR >> 2) & 0x0F);
         RecalcIPCache();
         break;
   }
    END_OPF;

    //
    // COP1
    //
    BEGIN_OPF(COP1);
	DO_LDS();
        new_PC = Exception(EXCEPTION_COPU, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;

    //
    // COP2
    //
    BEGIN_OPF(COP2);
	uint32 sub_op = (instr >> 21) & 0x1F;

   if (sub_op >= 0x10 && sub_op <= 0x1F)
   {
      //printf("%08x\n", PC);
      if(timestamp < gte_ts_done)
         timestamp = gte_ts_done;
      gte_ts_done = timestamp + GTE_Instruction(instr);
      DO_LDS();
   }
   else switch(sub_op)
   {
      default:
         DO_LDS();
         break;

      case 0x00:		// MFC2	- Move from Coprocessor
         {
            uint32 rt = (instr >> 16) & 0x1F;
            uint32 rd = (instr >> 11) & 0x1F;

            DO_LDS();

            if(timestamp < gte_ts_done)
            {
               LDAbsorb = gte_ts_done - timestamp;
               timestamp = gte_ts_done;
            }
            else
               LDAbsorb = 0;

            LDWhich = rt;
            LDValue = GTE_ReadDR(rd);

			if (PGXP_GetModes() & PGXP_MODE_GTE)
				PGXP_GTE_MFC2(instr, LDValue, LDValue);
         }
         break;

      case 0x04:		// MTC2	- Move to Coprocessor
         {
            uint32 rt = (instr >> 16) & 0x1F;
            uint32 rd = (instr >> 11) & 0x1F;
            uint32 val = GPR[rt];

            if(timestamp < gte_ts_done)
               timestamp = gte_ts_done;

            //printf("GTE WriteDR: %d %d\n", rd, val);
            GTE_WriteDR(rd, val);

			if (PGXP_GetModes() & PGXP_MODE_GTE)
				PGXP_GTE_MTC2(instr, val, val);

            DO_LDS();
         }
         break;

      case 0x02:		// CFC2
         {
            uint32 rt = (instr >> 16) & 0x1F;
            uint32 rd = (instr >> 11) & 0x1F;

            DO_LDS();

            if(timestamp < gte_ts_done)
            {
               LDAbsorb = gte_ts_done - timestamp;
               timestamp = gte_ts_done;
            }
            else
               LDAbsorb = 0;

            LDWhich = rt;
            LDValue = GTE_ReadCR(rd);

			if (PGXP_GetModes() & PGXP_MODE_GTE)
				PGXP_GTE_CFC2(instr, LDValue, LDValue);
            //printf("GTE ReadCR: %d %d\n", rd, GPR[rt]);
         }		
         break;

      case 0x06:		// CTC2
         {
            uint32 rt = (instr >> 16) & 0x1F;
            uint32 rd = (instr >> 11) & 0x1F;
            uint32 val = GPR[rt];

            //printf("GTE WriteCR: %d %d\n", rd, val);

            if(timestamp < gte_ts_done)
               timestamp = gte_ts_done;

            GTE_WriteCR(rd, val);		

			if (PGXP_GetModes() & PGXP_MODE_GTE)
				PGXP_GTE_CTC2(instr, val, val);

            DO_LDS();
         }
         break;
   }
    END_OPF;

    //
    // COP3
    //
    BEGIN_OPF(COP3);
	DO_LDS();
        new_PC = Exception(EXCEPTION_COPU, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;

    //
    // LWC0
    //
    BEGIN_OPF(LWC0);
	DO_LDS();
        new_PC = Exception(EXCEPTION_COPU, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;

    //
    // LWC1
    //
    BEGIN_OPF(LWC1);
	DO_LDS();
        new_PC = Exception(EXCEPTION_COPU, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;

    //
    // LWC2
    //
    BEGIN_OPF(LWC2);
        ITYPE;
        uint32 address = GPR[rs] + immediate;

	DO_LDS();

        if(MDFN_UNLIKELY(address & 3))
	{
         new_PC = Exception(EXCEPTION_ADEL, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
        else
	{
         if(timestamp < gte_ts_done)
          timestamp = gte_ts_done;

		 uint32_t value = ReadMemory<uint32>(timestamp, address, false, true);
         GTE_WriteDR(rt, value);

		 if (PGXP_GetModes() & PGXP_MODE_GTE)
			 PGXP_GTE_LWC2(instr, value, address);
	}

	// GTE stuff here
    END_OPF;

    //
    // LWC3
    //
    BEGIN_OPF(LWC3);
	DO_LDS();
        new_PC = Exception(EXCEPTION_COPU, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;


    //
    // SWC0
    //
    BEGIN_OPF(SWC0);
	DO_LDS();
	new_PC = Exception(EXCEPTION_COPU, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;

    //
    // SWC1
    //
    BEGIN_OPF(SWC1);
	DO_LDS();
        new_PC = Exception(EXCEPTION_COPU, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;

    //
    // SWC2
    //
    BEGIN_OPF(SWC2);
        ITYPE;
        uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(address & 0x3))
	{
	 new_PC = Exception(EXCEPTION_ADES, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
	else
	{
         if(timestamp < gte_ts_done)
          timestamp = gte_ts_done;

	 WriteMemory<uint32>(timestamp, address, GTE_ReadDR(rt));

	 if (PGXP_GetModes() & PGXP_MODE_GTE)
		 PGXP_GTE_SWC2(instr, GTE_ReadDR(rt), address);
	}

	DO_LDS();
    END_OPF;

    //
    // SWC3
    ///
    BEGIN_OPF(SWC3);
	DO_LDS();
        new_PC = Exception(EXCEPTION_RI, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;


    //
    // DIV - Divide Word
    //
    BEGIN_OPF(DIV);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        if(!GPR[rt])
        {
           if(GPR[rs] & 0x80000000)
              LO = 1;
           else
              LO = 0xFFFFFFFF;

           HI = GPR[rs];
        }
        else if(GPR[rs] == 0x80000000 && GPR[rt] == 0xFFFFFFFF)
        {
           LO = 0x80000000;
           HI = 0;
        }
        else
        {
           LO = (int32)GPR[rs] / (int32)GPR[rt];
           HI = (int32)GPR[rs] % (int32)GPR[rt];
        }
	muldiv_ts_done = timestamp + 37;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_DIV(instr, HI, LO, GPR[rs], GPR[rt]);

	DO_LDS();

    END_OPF;

    // DIVU - Divide Unsigned Word
    BEGIN_OPF(DIVU);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	if(!GPR[rt])
	{
	 LO = 0xFFFFFFFF;
	 HI = GPR[rs];
	}
	else
	{
	 LO = GPR[rs] / GPR[rt];
	 HI = GPR[rs] % GPR[rt];
	}
 	muldiv_ts_done = timestamp + 37;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_DIVU(instr, HI, LO, GPR[rs], GPR[rt]);

	DO_LDS();
    END_OPF;

    //
    // J - Jump
    //
    BEGIN_OPF(J);
	JTYPE;

	DO_LDS();

	DO_BRANCH(target << 2, 0xF0000000);
    END_OPF;

    //
    // JAL - Jump and Link
    //
    BEGIN_OPF(JAL);
	JTYPE;

	//GPR_DEPRES_BEGIN
	GPR_RES(31);
	//GPR_DEPRES_END

	DO_LDS();

	GPR[31] = PC + 8;

	DO_BRANCH(target << 2, 0xF0000000);
    END_OPF;

    //
    // JALR - Jump and Link Register
    //
    BEGIN_OPF(JALR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 tmp = GPR[rs];

	DO_LDS();

	GPR[rd] = PC + 8;

	DO_BRANCH(tmp, 0);

    END_OPF;

    //
    // JR - Jump Register
    //
    BEGIN_OPF(JR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 bt = GPR[rs];

	DO_LDS();

	DO_BRANCH(bt, 0);

    END_OPF;

    //
    // LUI - Load Upper Immediate
    //
    BEGIN_OPF(LUI);
	ITYPE_ZE;		// Actually, probably would be sign-extending...if we were emulating a 64-bit MIPS chip :b

	GPR_DEPRES_BEGIN
	GPR_RES(rt);
	GPR_DEPRES_END

	DO_LDS();

	GPR[rt] = immediate << 16;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_LUI(instr, GPR[rt]);

    END_OPF;

    //
    // MFHI - Move from HI
    //
    BEGIN_OPF(MFHI);
	RTYPE;

	GPR_DEPRES_BEGIN
 	GPR_RES(rd);
	GPR_DEPRES_END

	DO_LDS();

	if(timestamp < muldiv_ts_done)
	{
	 if(timestamp == muldiv_ts_done - 1)
	  muldiv_ts_done--;
	 else
	 {
	  do
	  {
	   if(ReadAbsorb[ReadAbsorbWhich])
	    ReadAbsorb[ReadAbsorbWhich]--;
	   timestamp++;
	  } while(timestamp < muldiv_ts_done);
	 }
	}

	GPR[rd] = HI;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MFHI(instr, GPR[rd], HI);

    END_OPF;


    //
    // MFLO - Move from LO
    //
    BEGIN_OPF(MFLO);
	RTYPE;

	GPR_DEPRES_BEGIN
 	GPR_RES(rd);
	GPR_DEPRES_END

	DO_LDS();

	if(timestamp < muldiv_ts_done)
	{
	 if(timestamp == muldiv_ts_done - 1)
	  muldiv_ts_done--;
	 else
	 {
	  do
	  {
	   if(ReadAbsorb[ReadAbsorbWhich])
	    ReadAbsorb[ReadAbsorbWhich]--;
	   timestamp++;
	  } while(timestamp < muldiv_ts_done);
	 }
	}

	GPR[rd] = LO;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MFLO(instr, GPR[rd], LO);

    END_OPF;


    //
    // MTHI - Move to HI
    //
    BEGIN_OPF(MTHI);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	HI = GPR[rs];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MTHI(instr, HI, GPR[rs]);

	DO_LDS();

    END_OPF;

    //
    // MTLO - Move to LO
    //
    BEGIN_OPF(MTLO);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	LO = GPR[rs];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MTLO(instr, LO, GPR[rs]);

	DO_LDS();

    END_OPF;


    //
    // MULT - Multiply Word
    //
    BEGIN_OPF(MULT);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	uint64 result;

	result = (int64)(int32)GPR[rs] * (int32)GPR[rt];
	muldiv_ts_done = timestamp + MULT_Tab24[MDFN_lzcount32((GPR[rs] ^ ((int32)GPR[rs] >> 31)) | 0x400)];
	DO_LDS();

	LO = result;
	HI = result >> 32;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MULT(instr, HI, LO, GPR[rs], GPR[rt]);

    END_OPF;

    //
    // MULTU - Multiply Unsigned Word
    //
    BEGIN_OPF(MULTU);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	uint64 result;

	result = (uint64)GPR[rs] * GPR[rt];
	muldiv_ts_done = timestamp + MULT_Tab24[MDFN_lzcount32(GPR[rs] | 0x400)];
	DO_LDS();

	LO = result;
	HI = result >> 32;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_MULTU(instr, HI, LO, GPR[rs], GPR[rt]);

    END_OPF;


    //
    // NOR - NOR
    //
    BEGIN_OPF(NOR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = ~(GPR[rs] | GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_NOR(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // OR - OR
    //
    BEGIN_OPF(OR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] | GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_OR(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // ORI - OR Immediate
    //
    BEGIN_OPF(ORI);
	ITYPE_ZE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = GPR[rs] | immediate;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_ORI(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;


    //
    // SLL - Shift Word Left Logical
    //
    BEGIN_OPF(SLL);	// SLL
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rt] << shamt;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLL(instr, result, GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SLLV - Shift Word Left Logical Variable
    //
    BEGIN_OPF(SLLV);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rt] << (GPR[rs] & 0x1F);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLLV(instr, result, GPR[rt], GPR[rs]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // SLT - Set on Less Than
    //
    BEGIN_OPF(SLT);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = (bool)((int32)GPR[rs] < (int32)GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLT(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SLTI - Set on Less Than Immediate
    //
    BEGIN_OPF(SLTI);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = (bool)((int32)GPR[rs] < (int32)immediate);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLTI(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;


    //
    // SLTIU - Set on Less Than Immediate, Unsigned
    //
    BEGIN_OPF(SLTIU);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = (bool)(GPR[rs] < (uint32)immediate);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLTIU(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;

    END_OPF;


    //
    // SLTU - Set on Less Than, Unsigned
    //
    BEGIN_OPF(SLTU);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = (bool)(GPR[rs] < GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SLTU(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SRA - Shift Word Right Arithmetic
    //
    BEGIN_OPF(SRA);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = ((int32)GPR[rt]) >> shamt;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SRA(instr, result, GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SRAV - Shift Word Right Arithmetic Variable
    //
    BEGIN_OPF(SRAV);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = ((int32)GPR[rt]) >> (GPR[rs] & 0x1F);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SRAV(instr, result, GPR[rt], GPR[rs]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SRL - Shift Word Right Logical
    //
    BEGIN_OPF(SRL);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rt] >> shamt;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SRL(instr, result, GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // SRLV - Shift Word Right Logical Variable
    //
    BEGIN_OPF(SRLV);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rt] >> (GPR[rs] & 0x1F);

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SRLV(instr, result, GPR[rt], GPR[rs]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SUB - Subtract Word
    //
    BEGIN_OPF(SUB);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] - GPR[rt];
	bool ep = (((GPR[rs] ^ GPR[rt])) & (GPR[rs] ^ result)) & 0x80000000;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SUB(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	if(MDFN_UNLIKELY(ep))
	{
	 new_PC = Exception(EXCEPTION_OV, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
	else
	 GPR[rd] = result;

    END_OPF;


    //
    // SUBU - Subtract Unsigned Word
    //
    BEGIN_OPF(SUBU); // SUBU
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] - GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_SUBU(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;


    //
    // SYSCALL
    //
    BEGIN_OPF(SYSCALL);
	DO_LDS();

	new_PC = Exception(EXCEPTION_SYSCALL, PC, new_PC, new_PC_mask, instr);
        new_PC_mask = 0;
    END_OPF;


    //
    // XOR
    //
    BEGIN_OPF(XOR);
	RTYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
 	GPR_RES(rd);
	GPR_DEPRES_END

	uint32 result = GPR[rs] ^ GPR[rt];

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_XOR(instr, result, GPR[rs], GPR[rt]);

	DO_LDS();

	GPR[rd] = result;

    END_OPF;

    //
    // XORI - Exclusive OR Immediate
    //
    BEGIN_OPF(XORI);
	ITYPE_ZE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_RES(rt);
	GPR_DEPRES_END

	uint32 result = GPR[rs] ^ immediate;

	if (PGXP_GetModes() & PGXP_MODE_CPU)
		PGXP_CPU_XORI(instr, result, GPR[rs]);

	DO_LDS();

	GPR[rt] = result;
    END_OPF;

    //
    // Memory access instructions(besides the coprocessor ones) follow:
    //

    //
    // LB - Load Byte
    //
    BEGIN_OPF(LB);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

	uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(LDWhich == rt))
	 LDWhich = 0;

	DO_LDS();

	LDWhich = rt;
	LDValue = (int32)ReadMemory<int8>(timestamp, address);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LB(instr, LDValue, address);
    END_OPF;

    //
    // LBU - Load Byte Unsigned
    //
    BEGIN_OPF(LBU);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(LDWhich == rt))
	 LDWhich = 0;

	DO_LDS();

        LDWhich = rt;
	LDValue = ReadMemory<uint8>(timestamp, address);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LBU(instr, LDValue, address);
    END_OPF;

    //
    // LH - Load Halfword
    //
    BEGIN_OPF(LH);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(address & 1))
	{
	 DO_LDS();

	 new_PC = Exception(EXCEPTION_ADEL, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
	else
	{
	 if(MDFN_UNLIKELY(LDWhich == rt))
	  LDWhich = 0;

	 DO_LDS();

	 LDWhich = rt;
         LDValue = (int32)ReadMemory<int16>(timestamp, address);
	}
	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LH(instr, LDValue, address);
    END_OPF;

    //
    // LHU - Load Halfword Unsigned
    //
    BEGIN_OPF(LHU);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

        if(MDFN_UNLIKELY(address & 1))
	{
	 DO_LDS();

         new_PC = Exception(EXCEPTION_ADEL, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
	else
	{
	 if(MDFN_UNLIKELY(LDWhich == rt))
	  LDWhich = 0;

	 DO_LDS();

	 LDWhich = rt;
         LDValue = ReadMemory<uint16>(timestamp, address);
	}

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LHU(instr, LDValue, address);
    END_OPF;


    //
    // LW - Load Word
    //
    BEGIN_OPF(LW);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

        if(MDFN_UNLIKELY(address & 3))
	{
	 DO_LDS();

         new_PC = Exception(EXCEPTION_ADEL, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
        else
	{
	 if(MDFN_UNLIKELY(LDWhich == rt))
	  LDWhich = 0;

	 DO_LDS();

	 LDWhich = rt;
         LDValue = ReadMemory<uint32>(timestamp, address);
	}

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_LW(instr, LDValue, address);
    END_OPF;

    //
    // SB - Store Byte
    //
    BEGIN_OPF(SB);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

	uint32 address = GPR[rs] + immediate;

	WriteMemory<uint8>(timestamp, address, GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SB(instr, GPR[rt], address);

	DO_LDS();
    END_OPF;

    // 
    // SH - Store Halfword
    //
    BEGIN_OPF(SH);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(address & 0x1))
	{
	 new_PC = Exception(EXCEPTION_ADES, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
	else
	 WriteMemory<uint16>(timestamp, address, GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SH(instr, GPR[rt], address);

	DO_LDS();
    END_OPF;

    // 
    // SW - Store Word
    //
    BEGIN_OPF(SW);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	if(MDFN_UNLIKELY(address & 0x3))
	{
	 new_PC = Exception(EXCEPTION_ADES, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
	else
	 WriteMemory<uint32>(timestamp, address, GPR[rt]);

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SW(instr, GPR[rt], address);

	DO_LDS();
    END_OPF;

    // LWL and LWR load delay slot tomfoolery appears to apply even to MFC0! (and probably MFCn and CFCn as well, though they weren't explicitly tested)

    //
    // LWL - Load Word Left
    //
    BEGIN_OPF(LWL);
	ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	//GPR_DEP(rt);
	GPR_DEPRES_END

	uint32 address = GPR[rs] + immediate;
	uint32 v = GPR[rt];

	if(LDWhich == rt)
	{
	 v = LDValue;
	 ReadFudge = 0;
	}
	else
	{
	 DO_LDS();
	}

	LDWhich = rt;
   switch(address & 0x3)
   {
      case 0:
         LDValue = (v & ~(0xFF << 24)) | (ReadMemory<uint8>(timestamp, address & ~3) << 24);
         break;
      case 1:
         LDValue = (v & ~(0xFFFF << 16)) | (ReadMemory<uint16>(timestamp, address & ~3) << 16);
         break;
      case 2:
         LDValue = (v & ~(0xFFFFFF << 8)) | (ReadMemory<uint32>(timestamp, address & ~3, true) << 8);
         break;
      case 3:
         LDValue = (v & ~(0xFFFFFFFF << 0)) | (ReadMemory<uint32>(timestamp, address & ~3) << 0);
         break;
   }
   if (PGXP_GetModes() & PGXP_MODE_MEMORY)
	   PGXP_CPU_LWL(instr, LDValue, address);

    END_OPF;

    //
    // SWL - Store Word Left
    //
    BEGIN_OPF(SWL);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

   switch(address & 0x3)
   {
      case 0:
         WriteMemory<uint8>(timestamp, address & ~3, GPR[rt] >> 24);
         break;
      case 1:
         WriteMemory<uint16>(timestamp, address & ~3, GPR[rt] >> 16);
         break;
      case 2:
         WriteMemory<uint32>(timestamp, address & ~3, GPR[rt] >> 8, true);
         break;
      case 3:
         WriteMemory<uint32>(timestamp, address & ~3, GPR[rt] >> 0);
         break;
   }
   if (PGXP_GetModes() & PGXP_MODE_MEMORY)
	   PGXP_CPU_SWL(instr, GPR[rt], address);

   DO_LDS();

    END_OPF;

    //
    // LWR - Load Word Right
    //
    BEGIN_OPF(LWR);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	//GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;
	uint32 v = GPR[rt];

	if(LDWhich == rt)
	{
	 v = LDValue;
	 ReadFudge = 0;
	}
	else
	{
	 DO_LDS();
	}

	LDWhich = rt;
   switch(address & 0x3)
   {
      case 0:
         LDValue = (v & ~(0xFFFFFFFF)) | ReadMemory<uint32>(timestamp, address);
         break;
      case 1:
         LDValue = (v & ~(0xFFFFFF)) | ReadMemory<uint32>(timestamp, address, true);
         break;
      case 2:
         LDValue = (v & ~(0xFFFF)) | ReadMemory<uint16>(timestamp, address);
         break;
      case 3:
         LDValue = (v & ~(0xFF)) | ReadMemory<uint8>(timestamp, address);
         break;
   }

   if (PGXP_GetModes() & PGXP_MODE_MEMORY)
	   PGXP_CPU_LWR(instr, LDValue, address);

    END_OPF;

    //
    // SWR - Store Word Right
    //
    BEGIN_OPF(SWR);
        ITYPE;

	GPR_DEPRES_BEGIN
	GPR_DEP(rs);
	GPR_DEP(rt);
	GPR_DEPRES_END

        uint32 address = GPR[rs] + immediate;

	switch(address & 0x3)
   {
      case 0:
         WriteMemory<uint32>(timestamp, address, GPR[rt]);
         break;
      case 1:
         WriteMemory<uint32>(timestamp, address, GPR[rt], true);
         break;
      case 2:
         WriteMemory<uint16>(timestamp, address, GPR[rt]);
         break;
      case 3:
         WriteMemory<uint8>(timestamp, address, GPR[rt]);
         break;
   }

	if (PGXP_GetModes() & PGXP_MODE_MEMORY)
		PGXP_CPU_SWR(instr, GPR[rt], address);

	DO_LDS();

    END_OPF;

    //
    // Mednafen special instruction
    //
    BEGIN_OPF(INTERRUPT);
	if(Halted)
	{
	 goto SkipNPCStuff;
	}
	else
	{
 	 DO_LDS();

	 new_PC = Exception(EXCEPTION_INT, PC, new_PC, new_PC_mask, instr);
         new_PC_mask = 0;
	}
    END_OPF;
   }

OpDone: ;

        PC = (PC & new_PC_mask) + new_PC;
        new_PC_mask = ~0U;
        new_PC = 4;

SkipNPCStuff:	;

               //printf("\n");
      }
   } while(MDFN_LIKELY(PSX_EventHandler(timestamp)));

   if(gte_ts_done > 0)
      gte_ts_done -= timestamp;

   if(muldiv_ts_done > 0)
      muldiv_ts_done -= timestamp;

   ACTIVE_TO_BACKING;

   return(timestamp);
}

int32_t PS_CPU::Run(int32_t timestamp_in)
{
#ifdef HAVE_DEBUG
   if(CPUHook || ADDBT)
      return(RunReal<true>(timestamp_in));
#endif
   return(RunReal<false>(timestamp_in));
}

void PS_CPU::SetCPUHook(void (*cpuh)(const int32_t timestamp, uint32_t pc), void (*addbt)(uint32_t from, uint32_t to, bool exception))
{
   ADDBT = addbt;
   CPUHook = cpuh;
}

uint32_t PS_CPU::GetRegister(unsigned int which, char *special, const uint32_t special_len)
{
   if(which >= GSREG_GPR && which < (GSREG_GPR + 32))
      return GPR[which];
   switch(which)
   {
      case GSREG_PC:
         return BACKED_PC;
      case GSREG_PC_NEXT:
         return BACKED_new_PC;
      case GSREG_IN_BD_SLOT:
         return !(BACKED_new_PC_mask & 3);
      case GSREG_LO:
         return LO;
      case GSREG_HI:
         return HI;
      case GSREG_SR:
         return CP0.SR;
      case GSREG_CAUSE:
         return CP0.CAUSE;
      case GSREG_EPC:
         return CP0.EPC;
   }

   return 0;
}

void PS_CPU::SetRegister(unsigned int which, uint32_t value)
{
   if(which >= GSREG_GPR && which < (GSREG_GPR + 32))
   {
      if(which != (GSREG_GPR + 0))
         GPR[which] = value;
   }
   else switch(which)
   {
      case GSREG_PC:
         BACKED_PC = value & ~0x3;	// Remove masking if we ever add proper misaligned PC exception
         break;

      case GSREG_LO:
         LO = value;
         break;

      case GSREG_HI:
         HI = value;
         break;

      case GSREG_SR:
         CP0.SR = value;		// TODO: mask
         break;

      case GSREG_CAUSE:
         CP0.CAUSE = value;
         break;

      case GSREG_EPC:
         CP0.EPC = value & ~0x3U;
         break;
   }
}

bool PS_CPU::PeekCheckICache(uint32_t PC, uint32_t *iw)
{
   if(ICache[(PC & 0xFFC) >> 2].TV == PC)
   {
      *iw = ICache[(PC & 0xFFC) >> 2].Data;
      return(true);
   }

   return(false);
}


uint8_t PS_CPU::PeekMem8(uint32_t A)
{
 return PeekMemory<uint8>(A);
}

uint16_t PS_CPU::PeekMem16(uint32_t A)
{
 return PeekMemory<uint16>(A);
}

uint32_t PS_CPU::PeekMem32(uint32_t A)
{
 return PeekMemory<uint32>(A);
}

void PS_CPU::PokeMem8(uint32 A, uint8 V)
{
 PokeMemory<uint8>(A, V);
}

void PS_CPU::PokeMem16(uint32 A, uint16 V)
{
 PokeMemory<uint16>(A, V);
}

void PS_CPU::PokeMem32(uint32 A, uint32 V)
{
 PokeMemory<uint32>(A, V);
}

#undef BEGIN_OPF
#undef END_OPF
#undef MK_OPF

#define MK_OPF(op, funct)	((op) ? (0x40 | (op)) : (funct))
#define BEGIN_OPF(op, funct) case MK_OPF(op, funct): {
#define END_OPF } break;

// FIXME: should we breakpoint on an illegal address?  And with LWC2/SWC2 if CP2 isn't enabled?
void PS_CPU::CheckBreakpoints(void (*callback)(bool write, uint32_t address, unsigned int len), uint32_t instr)
{
   uint32 opf;

   opf = instr & 0x3F;

   if(instr & (0x3F << 26))
      opf = 0x40 | (instr >> 26);


   switch(opf)
   {
      default:
         break;

         //
         // LB - Load Byte
         //
         BEGIN_OPF(0x20, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(false, address, 1);
         END_OPF;

         //
         // LBU - Load Byte Unsigned
         //
         BEGIN_OPF(0x24, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(false, address, 1);
         END_OPF;

         //
         // LH - Load Halfword
         //
         BEGIN_OPF(0x21, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(false, address, 2);
         END_OPF;

         //
         // LHU - Load Halfword Unsigned
         //
         BEGIN_OPF(0x25, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(false, address, 2);
         END_OPF;


         //
         // LW - Load Word
         //
         BEGIN_OPF(0x23, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(false, address, 4);
         END_OPF;

         //
         // SB - Store Byte
         //
         BEGIN_OPF(0x28, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(true, address, 1);
         END_OPF;

         // 
         // SH - Store Halfword
         //
         BEGIN_OPF(0x29, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(true, address, 2);
         END_OPF;

         // 
         // SW - Store Word
         //
         BEGIN_OPF(0x2B, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(true, address, 4);
         END_OPF;

         //
         // LWL - Load Word Left
         //
         BEGIN_OPF(0x22, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         do
         {
            callback(false, address, 1);
         } while((address--) & 0x3);

         END_OPF;

         //
         // SWL - Store Word Left
         //
         BEGIN_OPF(0x2A, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         do
         {
            callback(true, address, 1);
         } while((address--) & 0x3);

         END_OPF;

         //
         // LWR - Load Word Right
         //
         BEGIN_OPF(0x26, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         do
         {
            callback(false, address, 1);
         } while((++address) & 0x3);

         END_OPF;

         //
         // SWR - Store Word Right
         //
         BEGIN_OPF(0x2E, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         do
         {
            callback(true, address, 1);
         } while((++address) & 0x3);

         END_OPF;

         //
         // LWC2
         //
         BEGIN_OPF(0x32, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(false, address, 4);
         END_OPF;

         //
         // SWC2
         //
         BEGIN_OPF(0x3A, 0);
         ITYPE;
         uint32 address = GPR[rs] + immediate;

         callback(true, address, 4);
         END_OPF;

   }
}
