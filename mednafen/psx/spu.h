#ifndef __MDFN_PSX_SPU_H
#define __MDFN_PSX_SPU_H

#include "spu_c.h"

enum
{
   ADSR_ATTACK = 0,
   ADSR_DECAY = 1,
   ADSR_SUSTAIN = 2,
   ADSR_RELEASE = 3
};

// Buffers 44.1KHz samples, should have enough for two(worst-case scenario) video frames(2* ~735 frames NTSC, 2* ~882 PAL) plus jitter plus enough for the resampler leftovers.
// We'll just go with 4096 because powers of 2 are AWESOME and such.

struct SPU_ADSR
{
   uint16_t EnvLevel;	// We typecast it to (int16) in several places, but keep it here as (uint16) to prevent signed overflow/underflow, which compilers
   // may not treat consistently.
   uint32_t Divider;
   uint32_t Phase;

   bool AttackExp;
   bool SustainExp;
   bool SustainDec;
   bool ReleaseExp;

   int32_t AttackRate;	// Ar
   int32_t DecayRate;	// Dr * 4
   int32_t SustainRate;	// Sr
   int32_t ReleaseRate;	// Rr * 4

   int32_t SustainLevel;	// (Sl + 1) << 11
};

class PS_SPU;
class SPU_Sweep
{
   friend class PS_SPU;		// For save states - FIXME(remove in future?)

   public:
   SPU_Sweep() { }
   ~SPU_Sweep() { }

   void Power(void);

   void WriteControl(uint16_t value);
   int16 ReadVolume(void);

   void WriteVolume(int16 value);

   void Clock();

   uint16_t Control;
   uint16_t Current;
   uint32_t Divider;
};

struct SPU_Voice
{
   int16 DecodeBuffer[0x20];
   int16 DecodeM2;
   int16 DecodeM1;

   uint32 DecodePlayDelay;
   uint32 DecodeWritePos;
   uint32 DecodeReadPos;
   uint32 DecodeAvail;

   bool IgnoreSampLA;

   uint8 DecodeShift;
   uint8 DecodeWeight;
   uint8_t DecodeFlags;

   SPU_Sweep Sweep[2];

   uint16_t Pitch;
   uint32_t CurPhase;

   uint32_t StartAddr;

   uint32_t CurAddr;

   uint32_t ADSRControl;

   uint32_t LoopAddr;

   int32_t PreLRSample;	// After enveloping, but before L/R volume.  Range of -32768 to 32767

   SPU_ADSR ADSR;
};

class PS_SPU
{
   public:

      PS_SPU();
      ~PS_SPU();

      int StateAction(StateMem *sm, int load, int data_only);

      void Power(void);
      void Write(int32_t timestamp, uint32_t A, uint16_t V);
      uint16_t Read(int32_t timestamp, uint32_t A);

      void WriteDMA(uint32_t V);
      uint32_t ReadDMA(void);

      int32_t UpdateFromCDC(int32_t clocks);

   private:

      void CheckIRQAddr(uint32_t addr);
      void WriteSPURAM(uint32_t addr, uint16_t value);
      uint16_t ReadSPURAM(uint32_t addr);

      void RunDecoder(SPU_Voice *voice);

      void CacheEnvelope(SPU_Voice *voice);
      void ResetEnvelope(SPU_Voice *voice);
      void ReleaseEnvelope(SPU_Voice *voice);
      void RunEnvelope(SPU_Voice *voice);


      void RunReverb(const int32* in, int32* out);
      void RunNoise(void);
      bool GetCDAudio(int32_t &l, int32_t &r);

      SPU_Voice Voices[24];

      uint32_t NoiseDivider;
      uint32_t NoiseCounter;
      uint16_t LFSR;

      uint32_t FM_Mode;
      uint32_t Noise_Mode;
      uint32_t Reverb_Mode;

      uint32_t ReverbWA;

      SPU_Sweep GlobalSweep[2];	// Doesn't affect reverb volume!

      int32_t ReverbVol[2];

      int32_t CDVol[2];
      int32_t ExternVol[2];

      uint32_t IRQAddr;

      uint32_t RWAddr;

      uint16_t SPUControl;

      uint32_t VoiceOn;
      uint32_t VoiceOff;

      uint32_t BlockEnd;

      uint32_t CWA;

      union
      {
         uint16_t Regs[0x100];
         struct
         {
            uint16_t VoiceRegs[0xC0];
            union
            {
               uint16_t GlobalRegs[0x20];
               struct
               {
                  uint16_t _Global0[0x17];
                  uint16_t SPUStatus;
                  uint16_t _Global1[0x08];
               };
            };
            union
            {
               uint16 ReverbRegs[0x20];

               struct
               {
                  uint16 FB_SRC_A;
                  uint16 FB_SRC_B;
                  int16 IIR_ALPHA;
                  int16 ACC_COEF_A;
                  int16 ACC_COEF_B;
                  int16 ACC_COEF_C;
                  int16 ACC_COEF_D;
                  int16 IIR_COEF;
                  int16 FB_ALPHA;
                  int16 FB_X;
                  uint16 IIR_DEST_A[2];
                  uint16 ACC_SRC_A[2];
                  uint16 ACC_SRC_B[2];
                  uint16 IIR_SRC_A[2];
                  uint16 IIR_DEST_B[2];
                  uint16 ACC_SRC_C[2];
                  uint16 ACC_SRC_D[2];
                  uint16 IIR_SRC_B[2];
                  uint16 MIX_DEST_A[2];
                  uint16 MIX_DEST_B[2];
                  int16 IN_COEF[2];
               };
            };
         };
      };

      uint16_t AuxRegs[0x10];

      int16 RDSB[2][128];	// [40]
      int16 RUSB[2][64];
      int32_t RvbResPos;

      uint32_t ReverbCur;

      uint32_t Get_Reverb_Offset(uint32_t offset);
      int16 RD_RVB(uint16 raw_offs, int32 extra_offs = 0);
      void WR_RVB(uint16 raw_offs, int16 sample);

      bool IRQAsserted;

      int32_t clock_divider;

      uint16_t SPURAM[524288 / sizeof(uint16)];
};

#endif
