#ifndef __MDFN_PSX_CPU_H
#define __MDFN_PSX_CPU_H

#include "gte.h"

#define PS_CPU_EMULATE_ICACHE 1

#define FAST_MAP_SHIFT        16
#define FAST_MAP_PSIZE        (1 << FAST_MAP_SHIFT)

#define CP0REG_BPC            3   /* PC breakpoint address */
#define CP0REG_BDA            5   /* Data load/store breakpoint address */
#define CP0REG_TAR            6   /* Target address */
#define CP0REG_DCIC           7   /* Cache control */
#define CP0REG_BDAM           9   /* Data load/store address mask */
#define CP0REG_BPCM           11  /* PC breakpoint address mask */
#define CP0REG_SR             12
#define CP0REG_CAUSE          13
#define CP0REG_EPC            14
#define CP0REG_PRID           15  /* Product ID */
#define CP0REG_ERREG          16

#define EXCEPTION_INT         0
#define EXCEPTION_MOD         1
#define EXCEPTION_TLBL        2
#define EXCEPTION_TLBS        3
#define EXCEPTION_ADEL        4 /* Address error on load */
#define EXCEPTION_ADES        5 /* Address error on store */
#define EXCEPTION_IBE         6 /* Instruction bus error */
#define EXCEPTION_DBE         7 /* Data bus error */
#define EXCEPTION_SYSCALL     8 /* System call */
#define EXCEPTION_BP          9 /* Breakpoint */
#define EXCEPTION_RI         10 /* Reserved instruction */
#define EXCEPTION_COPU       11 /* Coprocessor unusable */
#define EXCEPTION_OV         12 /* Arithmetic overflow */

#define GSREG_GPR             0
#define GSREG_PC             32
#define GSREG_PC_NEXT        33
#define GSREG_IN_BD_SLOT     34
#define GSREG_LO             35
#define GSREG_HI             36
#define GSREG_SR             37
#define GSREG_CAUSE          38
#define GSREG_EPC            39

class PS_CPU
{
   public:

      PS_CPU();
      ~PS_CPU();

      void SetFastMap(void *region_mem, uint32_t region_address, uint32_t region_size);

      INLINE void SetEventNT(const int32_t next_event_ts_arg)
      {
         next_event_ts = next_event_ts_arg;
      }

      int32_t Run(int32_t timestamp_in);

      void Power(void);

      // which ranges 0-5, inclusive
      void AssertIRQ(unsigned which, bool asserted);

      void SetHalt(bool status);

      // TODO eventually: factor BIU address decoding directly in the CPU core somehow without hurting speed.
      void SetBIU(uint32_t val);
      uint32_t GetBIU(void);

      int StateAction(StateMem *sm, int load, int data_only);

   private:

      uint32_t GPR[32 + 1];	// GPR[32] Used as dummy in load delay simulation(indexing past the end of real GPR)
      uint32_t LO;
      uint32_t HI;


      uint32_t BACKED_PC;
      uint32_t BACKED_new_PC;
      uint32_t BACKED_new_PC_mask;

      uint32_t IPCache;
      void RecalcIPCache(void);
      bool Halted;

      uint32_t BACKED_LDWhich;
      uint32_t BACKED_LDValue;
      uint32_t LDAbsorb;

      int32_t next_event_ts;
      int32_t gte_ts_done;
      int32_t muldiv_ts_done;

      uint32_t BIU;

      struct __ICache
      {
         uint32_t TV;
         uint32_t Data;
      };

      union
      {
         __ICache ICache[1024];
         uint32_t ICache_Bulk[2048];
      };


      struct
      {
         union
         {
            uint32_t Regs[32];
            struct
            {
               uint32_t Unused00;
               uint32_t Unused01;
               uint32_t Unused02;
               uint32_t BPC;		// RW
               uint32_t Unused04;
               uint32_t BDA;		// RW
               uint32_t TAR;
               uint32_t DCIC;	// RW
               uint32_t Unused08;	
               uint32_t BDAM;	// R/W
               uint32_t Unused0A;
               uint32_t BPCM;	// R/W
               uint32_t SR;		// R/W
               uint32_t CAUSE;	// R/W(partial)
               uint32_t EPC;		// R
               uint32_t PRID;	// R
               uint32_t ERREG;	// ?(may not exist, test)
            };
         };
      } CP0;

      uint8_t ReadAbsorb[0x20 + 1];
      uint8_t ReadAbsorbWhich;
      uint8_t ReadFudge;

      uint8 MULT_Tab24[24];

      MultiAccessSizeMem<1024, uint32, false> ScratchRAM;

      uint8_t *FastMap[1 << (32 - FAST_MAP_SHIFT)];
      uint8_t DummyPage[FAST_MAP_PSIZE];


      uint32_t Exception(uint32_t code, uint32_t PC, const uint32_t NPM) MDFN_WARN_UNUSED_RESULT;

      template<bool DebugMode> int32_t RunReal(int32_t timestamp_in);

      template<typename T> T PeekMemory(uint32_t address) MDFN_COLD;
      template<typename T> T ReadMemory(int32_t &timestamp, uint32_t address, bool DS24 = false, bool LWC_timing = false);
      template<typename T> void WriteMemory(int32_t &timestamp, uint32_t address, uint32_t value, bool DS24 = false);

      // Mednafen debugger stuff follows:
   public:
      void SetCPUHook(void (*cpuh)(const int32_t timestamp, uint32_t pc), void (*addbt)(uint32_t from, uint32_t to, bool exception));
      void CheckBreakpoints(void (*callback)(bool write, uint32_t address, unsigned int len), uint32_t instr);


      uint32_t GetRegister(unsigned int which, char *special, const uint32_t special_len);
      void SetRegister(unsigned int which, uint32_t value);
      bool PeekCheckICache(uint32_t PC, uint32_t *iw);
      uint8_t PeekMem8(uint32_t A);
      uint16_t PeekMem16(uint32_t A);
      uint32_t PeekMem32(uint32_t A);
   private:
      void (*CPUHook)(const int32_t timestamp, uint32_t pc);
      void (*ADDBT)(uint32_t from, uint32_t to, bool exception);
};

#endif
