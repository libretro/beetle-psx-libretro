#ifndef __MDFN_PSX_SPU_H
#define __MDFN_PSX_SPU_H

#include <stdint.h>

#include "../state.h"

enum
{
   ADSR_ATTACK = 0,
   ADSR_DECAY = 1,
   ADSR_SUSTAIN = 2,
   ADSR_RELEASE = 3
};

// Buffers 44.1KHz samples, should have enough for two(worst-case scenario) video frames(2* ~735 frames NTSC, 2* ~882 PAL) plus jitter plus enough for the resampler leftovers.
// We'll just go with 4096 because powers of 2 are AWESOME and such.

typedef struct
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
} SPU_ADSR;

typedef struct
{
   uint16_t Control;
   uint16_t Current;
   uint32_t Divider;
} SPU_Sweep;

typedef struct
{
   int16_t DecodeBuffer[0x20];
   int16_t DecodeM2;
   int16_t DecodeM1;

   uint32_t DecodePlayDelay;
   uint32_t DecodeWritePos;
   uint32_t DecodeReadPos;
   uint32_t DecodeAvail;

   bool IgnoreSampLA;

   uint8_t DecodeShift;
   uint8_t DecodeWeight;
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
} SPU_Voice;

/*
 * Audio output buffer. SPU samples are mixed into this buffer at
 * the SPU's internal 44.1 kHz rate; the libretro frontend drains
 * it once per video frame via audio_batch_cb. Sized for two
 * worst-case frames (2*882 samples in PAL) plus headroom for
 * resampler leftovers and jitter; 4096 because powers of two are
 * convenient.
 */
extern uint32_t IntermediateBufferPos;
extern int16_t  IntermediateBuffer[4096][2];

void     SPU_Init(void);
void     SPU_Kill(void);

void     SPU_Power(void);
void     SPU_Write(int32_t timestamp, uint32_t A, uint16_t V);
uint16_t SPU_Read(int32_t timestamp, uint32_t A);

void     SPU_WriteDMA(uint32_t V);
uint32_t SPU_ReadDMA(void);

int32_t  SPU_UpdateFromCDC(int32_t clocks);

int      SPU_StateAction(StateMem *sm, int load, int data_only);

#endif
