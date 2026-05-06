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

/*
  Games to test after changing code affecting CD reading and buffering:
		Bedlam
		Rise 2

*/

/* TODO: async command counter and async command phase? */
/*

 TODO:
	Implement missing commands.

	SPU CD-DA and CD-XA streaming semantics.
*/

/*
 After eject(doesn't appear to occur when drive is in STOP state):
	* Does not appear to occur in STOP state.
	* Does not appear to occur in PAUSE state.
	* DOES appear to occur in STANDBY state. (TODO: retest)

% Result 0: 16
% Result 1: 08
% IRQ Result: e5
% 19 e0

 Command abortion tests(NOP tested):
	Does not appear to occur when in STOP or PAUSE states(STOP or PAUSE command just executed).

	DOES occur after a ReadTOC completes, if ReadTOC is not followed by a STOP or PAUSE.  Odd.
*/

#include "psx_events.h"
#include "irq.h"
#include "cdc.h"
#include "spu_c.h"

#include "../mednafen-types.h"
#include "../mednafen-endian.h"
#include "../../osd_message.h"
#include "../state_helpers.h"
#include "../cdrom/cdromif_c.h"

#include <retro_miscellaneous.h>

#include <stdlib.h>
#include <string.h>

/* SimpleFIFO is declared in cdc.h; the access helpers used to be
 * inline methods on the C++ class.  They are now static-inline
 * functions in this translation unit. */
static INLINE bool SimpleFIFO_Init(SimpleFIFO *f, uint32_t the_size)
{
   f->data = (uint8_t *)malloc(the_size * sizeof(uint8_t));
   if (!f->data)
   {
      f->size      = 0;
      f->read_pos  = 0;
      f->write_pos = 0;
      f->in_count  = 0;
      return false;
   }
   f->size      = the_size;
   f->read_pos  = 0;
   f->write_pos = 0;
   f->in_count  = 0;
   return true;
}

static INLINE void SimpleFIFO_Destroy(SimpleFIFO *f)
{
   if (f->data)
   {
      free(f->data);
      f->data = NULL;
   }
}

static INLINE uint32_t SimpleFIFO_CanWrite(const SimpleFIFO *f)
{
   return f->size - f->in_count;
}

static INLINE uint8_t SimpleFIFO_ReadByte(SimpleFIFO *f)
{
   uint8_t ret  = f->data[f->read_pos];
   f->read_pos  = (f->read_pos + 1) & (f->size - 1);
   f->in_count--;
   return ret;
}

static INLINE void SimpleFIFO_Write(SimpleFIFO *f,
      const uint8_t *src, uint32_t cnt)
{
   while (cnt)
   {
      f->data[f->write_pos] = *src;
      f->write_pos          = (f->write_pos + 1) & (f->size - 1);
      f->in_count++;
      src++;
      cnt--;
   }
}

static INLINE void SimpleFIFO_WriteByte(SimpleFIFO *f, uint8_t v)
{
   f->data[f->write_pos] = v;
   f->write_pos          = (f->write_pos + 1) & (f->size - 1);
   f->in_count++;
}

static INLINE void SimpleFIFO_Flush(SimpleFIFO *f)
{
   f->read_pos  = 0;
   f->write_pos = 0;
   f->in_count  = 0;
}

static INLINE void SimpleFIFO_SaveStatePostLoad(SimpleFIFO *f)
{
   f->read_pos  %= f->size;
   f->write_pos %= f->size;
   f->in_count  %= (f->size + 1);
}

/* Enum values formerly nested inside class PS_CDC.  Promoted to
 * file scope as anonymous enums.  Names unchanged so call sites
 * inside method bodies still resolve. */
enum
{
   CDCIRQ_NONE        = 0,
   CDCIRQ_DATA_READY  = 1,
   CDCIRQ_COMPLETE    = 2,
   CDCIRQ_ACKNOWLEDGE = 3,
   CDCIRQ_DATA_END    = 4,
   CDCIRQ_DISC_ERROR  = 5
};

/* Names guessed based on what conditions cause them. */
enum
{
   ERRCODE_BAD_ARGVAL  = 0x10,
   ERRCODE_BAD_NUMARGS = 0x20,
   ERRCODE_BAD_COMMAND = 0x40,
   ERRCODE_NOT_READY   = 0x80
};

enum
{
   MODE_SPEED     = 0x80,
   MODE_STRSND    = 0x40,
   MODE_SIZE      = 0x20,
   MODE_SIZE2     = 0x10,
   MODE_SF        = 0x08,
   MODE_REPORT    = 0x04,
   MODE_AUTOPAUSE = 0x02,
   MODE_CDDA      = 0x01
};

enum
{
   DS_STANDBY         = -2,
   DS_PAUSED          = -1,
   DS_STOPPED         = 0,
   DS_SEEKING,
   DS_SEEKING_LOGICAL,
   DS_PLAY_SEEKING,
   DS_PLAYING,
   DS_READING,
   DS_RESETTING
};

/* Command-table entry.  Was a nested type inside class PS_CDC; now
 * a free struct visible only inside this translation unit. */
typedef struct CDC_CTEntry
{
   uint8_t  args_min;
   uint8_t  args_max;
   const char *name;
   int32_t  (*func)(PS_CDC *cdc, const int arg_count, const uint8_t *args);
   int32_t  (*func2)(PS_CDC *cdc);
} CDC_CTEntry;

/* Forward decls for all PS_CDC_* free functions and SetAIP variants
 * so order-of-definition doesn't matter inside this file.  Keeps the
 * conversion mechanical - we don't have to topologically sort the
 * functions. */
static int32_t  PS_CDC_CalcSeekTime(PS_CDC *cdc, int32_t initial,
                                    int32_t target, bool motor_on,
                                    bool paused);
static void     PS_CDC_RecalcIRQ(PS_CDC *cdc);
static void     PS_CDC_BeginResults(PS_CDC *cdc);
static void     PS_CDC_WriteIRQ(PS_CDC *cdc, uint8_t V);
static void     PS_CDC_WriteResult(PS_CDC *cdc, uint8_t V);
static uint8_t  PS_CDC_ReadResult(PS_CDC *cdc);
static uint8_t  PS_CDC_MakeStatus(PS_CDC *cdc, bool cmd_error);
static bool     PS_CDC_DecodeSubQ(PS_CDC *cdc, uint8_t *subpw);
static bool     PS_CDC_CommandCheckDiscPresent(PS_CDC *cdc);
static void     PS_CDC_DMForceStop(PS_CDC *cdc);
static void     PS_CDC_ClearAIP(PS_CDC *cdc);
static void     PS_CDC_CheckAIP(PS_CDC *cdc);
static void     PS_CDC_SetAIP_Buf(PS_CDC *cdc, unsigned irq,
                                  unsigned result_count, uint8_t *r);
static void     PS_CDC_SetAIP1(PS_CDC *cdc, unsigned irq, uint8_t r0);
static void     PS_CDC_SetAIP2(PS_CDC *cdc, unsigned irq,
                               uint8_t r0, uint8_t r1);
static void     PS_CDC_EnbufferizeCDDASector(PS_CDC *cdc, const uint8_t *buf);
static bool     PS_CDC_XA_Test(PS_CDC *cdc, const uint8_t *sdata);
static void     PS_CDC_XA_ProcessSector(PS_CDC *cdc, const uint8_t *sdata,
                                        CD_Audio_Buffer *ab);
static void     PS_CDC_ClearAudioBuffers(PS_CDC *cdc);
static void     PS_CDC_ApplyVolume(PS_CDC *cdc, int32_t samples[2]);
static void     PS_CDC_ReadAudioBuffer(PS_CDC *cdc, int32_t samples[2]);
static void     PS_CDC_HandlePlayRead(PS_CDC *cdc);
static void     PS_CDC_PreSeekHack(PS_CDC *cdc, uint32_t target);
static void     PS_CDC_ReadBase(PS_CDC *cdc);
/* Command_* dispatch functions, called via the Commands[] table. */
static int32_t PS_CDC_Command_Nop(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Setloc(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Play(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Forward(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Backward(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_ReadN(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Standby(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Standby_Part2(PS_CDC *cdc);
static int32_t PS_CDC_Command_Stop(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Stop_Part2(PS_CDC *cdc);
static int32_t PS_CDC_Command_Pause(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Pause_Part2(PS_CDC *cdc);
static int32_t PS_CDC_Command_Reset(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Mute(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Demute(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Setfilter(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Setmode(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Getparam(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_GetlocL(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_GetlocP(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_ReadT(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_ReadT_Part2(PS_CDC *cdc);
static int32_t PS_CDC_Command_GetTN(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_GetTD(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_SeekL(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_SeekP(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Seek_PartN(PS_CDC *cdc);
static int32_t PS_CDC_Command_Test(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_ID(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_ID_Part2(PS_CDC *cdc);
static int32_t PS_CDC_Command_ReadS(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_Init(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_ReadTOC(PS_CDC *cdc, const int arg_count, const uint8_t *args);
static int32_t PS_CDC_Command_ReadTOC_Part2(PS_CDC *cdc);
static int32_t PS_CDC_Command_0x1d(PS_CDC *cdc, const int arg_count, const uint8_t *args);

/* Command dispatch table.  Defined here at the top (after the
 * forward decls for each Command_* function above) so it precedes
 * its only consumer, PS_CDC_Update().  Marked static const since
 * the table is read-only at runtime. */
static const CDC_CTEntry Commands[0x20] =
{
   { /* 0x00, */ 0, 0, NULL, NULL, NULL },
   { /* 0x01, */ 0, 0, "Nop", PS_CDC_Command_Nop, NULL },
   { /* 0x02, */ 3, 3, "Setloc", PS_CDC_Command_Setloc, NULL },
   { /* 0x03, */ 0, 1, "Play", PS_CDC_Command_Play, NULL },
   { /* 0x04, */ 0, 0, "Forward", PS_CDC_Command_Forward, NULL },
   { /* 0x05, */ 0, 0, "Backward", PS_CDC_Command_Backward, NULL },
   { /* 0x06, */ 0, 0, "ReadN", PS_CDC_Command_ReadN, NULL },
   { /* 0x07, */ 0, 0, "Standby", PS_CDC_Command_Standby, PS_CDC_Command_Standby_Part2 },
   { /* 0x08, */ 0, 0, "Stop", PS_CDC_Command_Stop, PS_CDC_Command_Stop_Part2 },
   { /* 0x09, */ 0, 0, "Pause", PS_CDC_Command_Pause, PS_CDC_Command_Pause_Part2 },
   { /* 0x0A, */ 0, 0, "Reset", PS_CDC_Command_Reset, NULL },
   { /* 0x0B, */ 0, 0, "Mute", PS_CDC_Command_Mute, NULL },
   { /* 0x0C, */ 0, 0, "Demute", PS_CDC_Command_Demute, NULL },
   { /* 0x0D, */ 2, 2, "Setfilter", PS_CDC_Command_Setfilter, NULL },
   { /* 0x0E, */ 1, 1, "Setmode", PS_CDC_Command_Setmode, NULL },
   { /* 0x0F, */ 0, 0, "Getparam", PS_CDC_Command_Getparam, NULL },
   { /* 0x10, */ 0, 0, "GetlocL", PS_CDC_Command_GetlocL, NULL },
   { /* 0x11, */ 0, 0, "GetlocP", PS_CDC_Command_GetlocP, NULL },
   { /* 0x12, */ 1, 1, "ReadT", PS_CDC_Command_ReadT, PS_CDC_Command_ReadT_Part2 },
   { /* 0x13, */ 0, 0, "GetTN", PS_CDC_Command_GetTN, NULL },
   { /* 0x14, */ 1, 1, "GetTD", PS_CDC_Command_GetTD, NULL },
   { /* 0x15, */ 0, 0, "SeekL", PS_CDC_Command_SeekL, PS_CDC_Command_Seek_PartN },
   { /* 0x16, */ 0, 0, "SeekP", PS_CDC_Command_SeekP, PS_CDC_Command_Seek_PartN },

   { /* 0x17, */ 0, 0, NULL, NULL, NULL },
   { /* 0x18, */ 0, 0, NULL, NULL, NULL },

   { /* 0x19, */ 1, 1/* ??? */, "Test", PS_CDC_Command_Test, NULL },
   { /* 0x1A, */ 0, 0, "ID", PS_CDC_Command_ID, PS_CDC_Command_ID_Part2 },
   { /* 0x1B, */ 0, 0, "ReadS", PS_CDC_Command_ReadS, NULL },
   { /* 0x1C, */ 0, 0, "Init", PS_CDC_Command_Init, NULL },
   { /* 0x1D, */ 2, 2, "Unknown 0x1D", PS_CDC_Command_0x1d, NULL },
   { /* 0x1E, */ 0, 0, "ReadTOC", PS_CDC_Command_ReadTOC, PS_CDC_Command_ReadTOC_Part2 },
   { /* 0x1F, */ 0, 0, NULL, NULL, NULL },
};

void PS_CDC_Init(PS_CDC *cdc)
{
   /* SimpleFIFO_Init malloc-failure leaves the FIFO unusable but
    * the rest of the struct is still in a defined-zero state.  The
    * old C++ ctor proceeded silently on alloc failure too, so this
    * matches the historical behaviour for the moment.  Future work:
    * surface the failure to the caller. */
   SimpleFIFO_Init(&cdc->DMABuffer, 4096);

   cdc->IsPSXDisc           = false;
   cdc->Cur_CDIF            = NULL;
   cdc->DriveStatus         = DS_STOPPED;
   cdc->PendingCommandPhase = 0;

   TOC_Clear(&cdc->toc);
}

extern unsigned cd_2x_speedup;
extern bool cd_async;
extern bool cd_warned_slow;
extern int64 cd_slow_timeout;

void PS_CDC_Destroy(PS_CDC *cdc)
{
   SimpleFIFO_Destroy(&cdc->DMABuffer);
}

void PS_CDC_DMForceStop(PS_CDC *cdc)
{
   cdc->PSRCounter = 0;

   if((cdc->DriveStatus != DS_PAUSED && cdc->DriveStatus != DS_STOPPED) || cdc->PendingCommandPhase >= 2)
   {
      cdc->PendingCommand = 0x00;
      cdc->PendingCommandCounter = 0;
      cdc->PendingCommandPhase = 0;
   }

   cdc->HeaderBufValid = false;
   cdc->DriveStatus = DS_STOPPED;
   PS_CDC_ClearAIP(cdc);
   cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
   cdc->SectorsRead = 0;
}

void PS_CDC_SetDisc(PS_CDC *cdc, bool tray_open, CDIF *cdif, const char *disc_id)
{
   if(tray_open)
      cdif = NULL;

   cdc->Cur_CDIF = cdif;
   cdc->IsPSXDisc = false;
   memset(cdc->DiscID, 0, sizeof(cdc->DiscID));

   if(!cdc->Cur_CDIF)
   {
      PS_CDC_DMForceStop(cdc);
   }
   else
   {
      cdc->HeaderBufValid = false;
      cdc->DiscStartupDelay = (int64)1000 * 33868800 / 1000;
      cdc->DiscChanged = true;

      CDIF_ReadTOC(cdc->Cur_CDIF, &cdc->toc);

      if(disc_id)
      {
         memcpy((char *)cdc->DiscID, disc_id, 4);
         cdc->IsPSXDisc = true;
      }
   }
}

int32 PS_CDC_CalcNextEvent(PS_CDC *cdc)
{
   int32 next_event = cdc->SPUCounter;

   if(cdc->PSRCounter > 0 && next_event > cdc->PSRCounter)
      next_event = cdc->PSRCounter;

   if(cdc->PendingCommandCounter > 0 && next_event > cdc->PendingCommandCounter)
      next_event = cdc->PendingCommandCounter;

   if(!(cdc->IRQBuffer & 0xF))
   {
      if(cdc->CDCReadyReceiveCounter > 0 && next_event > cdc->CDCReadyReceiveCounter)
         next_event = cdc->CDCReadyReceiveCounter;
   }

   if(cdc->DiscStartupDelay > 0 && next_event > cdc->DiscStartupDelay)
      next_event = cdc->DiscStartupDelay;

/*fprintf(stderr, "%d %d %d %d --- %d\n", PSRCounter, PendingCommandCounter, CDCReadyReceiveCounter, DiscStartupDelay, next_event); */

   overclock_device_to_cpu(&next_event);

   return(next_event);
}

void PS_CDC_SoftReset(PS_CDC *cdc)
{
   PS_CDC_ClearAudioBuffers(cdc);

/* Not sure about initial volume state */
   cdc->Pending_DecodeVolume[0][0] = 0x80;
   cdc->Pending_DecodeVolume[0][1] = 0x00;
   cdc->Pending_DecodeVolume[1][0] = 0x00;
   cdc->Pending_DecodeVolume[1][1] = 0x80;
   memcpy(cdc->DecodeVolume, cdc->Pending_DecodeVolume, sizeof(cdc->DecodeVolume));

   cdc->RegSelector = 0;
   memset(cdc->ArgsBuf, 0, sizeof(cdc->ArgsBuf));
   cdc->ArgsWP = cdc->ArgsRP = 0;

   memset(cdc->ResultsBuffer, 0, sizeof(cdc->ResultsBuffer));
   cdc->ResultsWP = 0;
   cdc->ResultsRP = 0;
   cdc->ResultsIn = 0;

   cdc->CDCReadyReceiveCounter = 0;

   cdc->IRQBuffer = 0;
   cdc->IRQOutTestMask = 0;
   PS_CDC_RecalcIRQ(cdc);

   SimpleFIFO_Flush(&cdc->DMABuffer);
   cdc->SB_In = 0;
   cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
   cdc->SectorsRead = 0;

   memset(cdc->SubQBuf, 0, sizeof(cdc->SubQBuf));
   memset(cdc->SubQBuf_Safe, 0, sizeof(cdc->SubQBuf_Safe));
   cdc->SubQChecksumOK = false;

   memset(cdc->HeaderBuf, 0, sizeof(cdc->HeaderBuf));


   cdc->FilterFile = 0;
   cdc->FilterChan = 0;

   cdc->PendingCommand = 0;
   cdc->PendingCommandPhase = 0;
   cdc->PendingCommandCounter = 0;

   cdc->Mode = 0x20;

   cdc->HeaderBufValid = false;
   cdc->DriveStatus = DS_STOPPED;
   PS_CDC_ClearAIP(cdc);
   cdc->StatusAfterSeek = DS_STOPPED;
   cdc->SeekRetryCounter = 0;

   cdc->Forward = false;
   cdc->Backward = false;
   cdc->Muted = false;

   cdc->PlayTrackMatch = 0;

   cdc->PSRCounter = 0;

   cdc->CurSector = 0;

   /* Sentinel: 0xFF is out-of-range for the 4-bit (SubQBuf_Safe[0x9] >> 4)
    * value compared against this in HandlePlayRead, so the first MODE_REPORT
    * frame after reset always emits a report. Without this the field could
    * retain a stale value across resets and silently swallow the first
    * report frame. */
   cdc->ReportLastF = 0xFF;

   cdc->SeekTarget = 0;

   cdc->CommandLoc = 0;
   cdc->CommandLoc_Dirty = true;

   cdc->DiscChanged = true;
}

void PS_CDC_Power(PS_CDC *cdc)
{
   SPU_Power();

   PS_CDC_SoftReset(cdc);

   cdc->DiscStartupDelay = 0;

   cdc->SPUCounter = SPU_UpdateFromCDC(0);
   cdc->lastts = 0;
}

int PS_CDC_StateAction(PS_CDC *cdc, StateMem *sm, int load, int data_only)
{
   SFORMAT StateRegs[] =
   {
      SFVARN_BOOL(cdc->DiscChanged, "DiscChanged"),
      SFVARN(cdc->DiscStartupDelay, "DiscStartupDelay"),

      SFARRAY16(&cdc->AudioBuffer.Samples[0][0], sizeof(cdc->AudioBuffer.Samples) / sizeof(cdc->AudioBuffer.Samples[0][0])),
      SFVAR(cdc->AudioBuffer.Size),
      SFVAR(cdc->AudioBuffer.Freq),
      SFVAR(cdc->AudioBuffer.ReadPos),

      SFARRAY(&cdc->Pending_DecodeVolume[0][0], 2 * 2),
      SFARRAY(&cdc->DecodeVolume[0][0], 2 * 2),

      SFARRAY16(&cdc->ADPCM_ResampBuf[0][0], sizeof(cdc->ADPCM_ResampBuf) / sizeof(cdc->ADPCM_ResampBuf[0][0])),
      SFVARN(cdc->ADPCM_ResampCurPhase, "ADPCM_ResampCurPhase"),
      SFVARN(cdc->ADPCM_ResampCurPos, "ADPCM_ResampCurPos"),



      SFVARN(cdc->RegSelector, "RegSelector"),
      SFARRAY(cdc->ArgsBuf, 16),
      SFVAR(cdc->ArgsWP),
      SFVAR(cdc->ArgsRP),

      SFVAR(cdc->ArgsReceiveLatch),
      SFARRAY(cdc->ArgsReceiveBuf, 32),
      SFVAR(cdc->ArgsReceiveIn),

      SFARRAY(cdc->ResultsBuffer, 16),
      SFVAR(cdc->ResultsIn),
      SFVAR(cdc->ResultsWP),
      SFVAR(cdc->ResultsRP),




      SFARRAY(&cdc->DMABuffer.data[0], cdc->DMABuffer.size),
      SFVAR(cdc->DMABuffer.read_pos),
      SFVAR(cdc->DMABuffer.write_pos),
      SFVAR(cdc->DMABuffer.in_count),




      SFARRAY(cdc->SB, sizeof(cdc->SB) / sizeof(cdc->SB[0])),
      SFVAR(cdc->SB_In),

      SFARRAY(&cdc->SectorPipe[0][0], sizeof(cdc->SectorPipe) / sizeof(cdc->SectorPipe[0][0])),
      SFVAR(cdc->SectorPipe_Pos),
      SFVAR(cdc->SectorPipe_In),

      SFARRAY(cdc->SubQBuf, sizeof(cdc->SubQBuf) / sizeof(cdc->SubQBuf[0])),
      SFARRAY(cdc->SubQBuf_Safe, sizeof(cdc->SubQBuf_Safe) / sizeof(cdc->SubQBuf_Safe[0])),

      SFVAR(cdc->SubQChecksumOK),

      SFVAR(cdc->HeaderBufValid),
      SFARRAY(cdc->HeaderBuf, sizeof(cdc->HeaderBuf) / sizeof(cdc->HeaderBuf[0])),

      SFVAR(cdc->IRQBuffer),
      SFVAR(cdc->IRQOutTestMask),
      SFVAR(cdc->CDCReadyReceiveCounter),

      SFVAR(cdc->FilterFile),
      SFVAR(cdc->FilterChan),

      SFVAR(cdc->PendingCommand),
      SFVAR(cdc->PendingCommandPhase),
      SFVAR(cdc->PendingCommandCounter),

      SFVAR(cdc->SPUCounter),
      SFVAR(cdc->lastts),

      SFVAR(cdc->Mode),
      SFVAR(cdc->DriveStatus),
      SFVAR(cdc->StatusAfterSeek),
      SFVAR(cdc->Forward),
      SFVAR(cdc->Backward),
      SFVAR(cdc->Muted),

      SFVAR(cdc->PlayTrackMatch),

      SFVAR(cdc->PSRCounter),

      SFVAR(cdc->CurSector),
      SFVAR(cdc->SectorsRead),


      SFVAR(cdc->AsyncIRQPending),
      SFARRAY(cdc->AsyncResultsPending, sizeof(cdc->AsyncResultsPending) / sizeof(cdc->AsyncResultsPending[0])),
      SFVAR(cdc->AsyncResultsPendingCount),

      SFVAR(cdc->SeekTarget),
      SFVAR(cdc->SeekRetryCounter),

/* FIXME: Save TOC stuff? */
#if 0
      CDUtility::TOC cdc->toc;
      bool cdc->IsPSXDisc;
      uint8 cdc->DiscID[4];
#endif
      SFVAR(cdc->CommandLoc),
         SFVAR(cdc->CommandLoc_Dirty),
         SFARRAY16(&cdc->xa_previous[0][0], sizeof(cdc->xa_previous) / sizeof(cdc->xa_previous[0][0])),

         SFVAR(cdc->xa_cur_set),
         SFVAR(cdc->xa_cur_file),
         SFVAR(cdc->xa_cur_chan),

         SFVAR(cdc->ReportLastF),

         SFEND
   };

   int ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "CDC");

   if(load)
   {
      SimpleFIFO_SaveStatePostLoad(&cdc->DMABuffer);
      cdc->SectorPipe_Pos %= CDC_SECTOR_PIPE_COUNT;

      if(cdc->AudioBuffer.Size > sizeof(cdc->AudioBuffer.Samples[0]) / sizeof(cdc->AudioBuffer.Samples[0][0]))
         cdc->AudioBuffer.Size = sizeof(cdc->AudioBuffer.Samples[0]) / sizeof(cdc->AudioBuffer.Samples[0][0]);

      if(cdc->AudioBuffer.ReadPos > cdc->AudioBuffer.Size)
         cdc->AudioBuffer.ReadPos = cdc->AudioBuffer.Size;

      cdc->ResultsRP &= 0xF;
      cdc->ResultsWP &= 0xF;
      cdc->ResultsIn &= 0x1F;

      cdc->ADPCM_ResampCurPos &= 0x1F;
      cdc->ADPCM_ResampCurPhase %= 7;


/* Handle pre-0.9.37 state loading, and maliciously-constructed/corrupted save states. */
      if(!cdc->Cur_CDIF)
         PS_CDC_DMForceStop(cdc);
   }
   return(ret);
}

void PS_CDC_ResetTS(PS_CDC *cdc)
{
   cdc->lastts = 0;
}

void PS_CDC_RecalcIRQ(PS_CDC *cdc)
{
   IRQ_Assert(IRQ_CD, (bool)(cdc->IRQBuffer & (cdc->IRQOutTestMask & 0x1F)));
}
/*static int32 doom_ts; */
void PS_CDC_WriteIRQ(PS_CDC *cdc, uint8 V)
{
   assert(cdc->CDCReadyReceiveCounter <= 0);
   assert(!(cdc->IRQBuffer & 0xF));


   cdc->CDCReadyReceiveCounter = 2000;  /*1024; */

   cdc->IRQBuffer = (cdc->IRQBuffer & 0x10) | V;
   PS_CDC_RecalcIRQ(cdc);
}

void PS_CDC_BeginResults(PS_CDC *cdc)
{
/*if(ResultsIn) */
/* { */
/* printf("Cleared %d results. IRQBuffer=0x%02x\n", ResultsIn, IRQBuffer); */
/*} */

   cdc->ResultsIn = 0;
   cdc->ResultsWP = 0;
   cdc->ResultsRP = 0;

   memset(cdc->ResultsBuffer, 0x00, sizeof(cdc->ResultsBuffer));
}

void PS_CDC_WriteResult(PS_CDC *cdc, uint8 V)
{
   cdc->ResultsBuffer[cdc->ResultsWP] = V;
   cdc->ResultsWP = (cdc->ResultsWP + 1) & 0xF;
   cdc->ResultsIn = (cdc->ResultsIn + 1) & 0x1F;
}

uint8 PS_CDC_ReadResult(PS_CDC *cdc)
{
   uint8 ret = cdc->ResultsBuffer[cdc->ResultsRP];

   cdc->ResultsRP = (cdc->ResultsRP + 1) & 0xF;
   cdc->ResultsIn = (cdc->ResultsIn - 1) & 0x1F;

   return ret;
}

uint8 PS_CDC_MakeStatus(PS_CDC *cdc, bool cmd_error)
{
   uint8 ret = 0;

   /* Are these bit positions right? */

   switch (cdc->DriveStatus)
   {
      case DS_PLAYING:
         ret |= 0x80;
         break;
      case DS_READING:
         /* Probably will want to be careful with this HeaderBufValid 
          * versus seek/read bit business in the future as it is a bit fragile;
          * "Gran Turismo 1"'s music is a good test case. */
         if(cdc->HeaderBufValid)
         {
            ret |= 0x20;
            break;
         }
         /* fall-through */
      case DS_SEEKING:
      case DS_SEEKING_LOGICAL:
         ret |= 0x40;
         break;
   }

   if(!cdc->Cur_CDIF || cdc->DiscChanged)
      ret |= 0x10;

   if(cdc->DriveStatus != DS_STOPPED)
      ret |= 0x02;

   if(cmd_error)
      ret |= 0x01;

   cdc->DiscChanged = false;  /* FIXME: Only do it on NOP command execution? */

   return(ret);
}

bool PS_CDC_DecodeSubQ(PS_CDC *cdc, uint8 *subpw)
{
   uint8 tmp_q[0xC];

   memset(tmp_q, 0, 0xC);

   {
      int i;
      for (i = 0; i < 96; i++)
      tmp_q[i >> 3] |= ((subpw[i] & 0x40) >> 6) << (7 - (i & 7));
   }

   if((tmp_q[0] & 0xF) == 1)
   {
      memcpy(cdc->SubQBuf, tmp_q, 0xC);
      cdc->SubQChecksumOK = subq_check_checksum(tmp_q);

      if(cdc->SubQChecksumOK)
      {
         memcpy(cdc->SubQBuf_Safe, tmp_q, 0xC);
         return(true);
      }
   }

   return(false);
}

static const int16 CDADPCMImpulse[7][25] =
{
   {     0,    -5,    17,   -35,    70,   -23,   -68,   347,  -839,  2062, -4681, 15367, 21472, -5882,  2810, -1352,   635,  -235,    26,    43,   -35,    16,    -8,     2,     0,  }, /* 0 */
   {     0,    -2,    10,   -34,    65,   -84,    52,     9,  -266,  1024, -2680,  9036, 26516, -6016,  3021, -1571,   848,  -365,   107,    10,   -16,    17,    -8,     3,    -1,  }, /* 1 */
   {    -2,     0,     3,   -19,    60,   -75,   162,  -227,   306,   -67,  -615,  3229, 29883, -4532,  2488, -1471,   882,  -424,   166,   -27,     5,     6,    -8,     3,    -1,  }, /* 2 */
   {    -1,     3,    -2,    -5,    31,   -74,   179,  -402,   689,  -926,  1272, -1446, 31033, -1446,  1272,  -926,   689,  -402,   179,   -74,    31,    -5,    -2,     3,    -1,  }, /* 3 */
   {    -1,     3,    -8,     6,     5,   -27,   166,  -424,   882, -1471,  2488, -4532, 29883,  3229,  -615,   -67,   306,  -227,   162,   -75,    60,   -19,     3,     0,    -2,  }, /* 4 */
   {    -1,     3,    -8,    17,   -16,    10,   107,  -365,   848, -1571,  3021, -6016, 26516,  9036, -2680,  1024,  -266,     9,    52,   -84,    65,   -34,    10,    -2,     0,  }, /* 5 */
   {     0,     2,    -8,    16,   -35,    43,    26,  -235,   635, -1352,  2810, -5882, 21472, 15367, -4681,  2062,  -839,   347,   -68,   -23,    70,   -35,    17,    -5,     0,  }, /* 6 */
};

void PS_CDC_ReadAudioBuffer(PS_CDC *cdc, int32 samples[2])
{
   samples[0] = cdc->AudioBuffer.Samples[0][cdc->AudioBuffer.ReadPos];
   samples[1] = cdc->AudioBuffer.Samples[1][cdc->AudioBuffer.ReadPos];

   cdc->AudioBuffer.ReadPos++;
}

INLINE void PS_CDC_ApplyVolume(PS_CDC *cdc, int32 samples[2])
{
   int32 left_out, right_out;

   /* Muted is the resting state any time CD audio isn't actively
    * playing - skip the entire mix-and-saturate pipeline. The
    * historical version computed left_out/right_out
    * unconditionally (including the clamps) and only zeroed them
    * at the end; that was 4 multiplies + 4 shifts + 2 adds + 4
    * saturating compares of pure waste per sample on the muted
    * path. */
   if (cdc->Muted)
   {
      samples[0] = 0;
      samples[1] = 0;
      return;
   }

   /* DecodeVolume is a 2x2 mixing matrix:
    *   [ src_L_to_dst_L  src_L_to_dst_R ]
    *   [ src_R_to_dst_L  src_R_to_dst_R ]
    * applied as a >> 7 fixed-point multiply (volume in 0.7 fmt).
    * Take care not to alter samples[] before we're done
    * calculating both output channels - the L/R outputs depend
    * on both L/R inputs. */
   left_out  = ((samples[0] * cdc->DecodeVolume[0][0]) >> 7) + ((samples[1] * cdc->DecodeVolume[1][0]) >> 7);
   right_out = ((samples[0] * cdc->DecodeVolume[0][1]) >> 7) + ((samples[1] * cdc->DecodeVolume[1][1]) >> 7);

   /* Saturate each output channel to signed 16-bit. The CDC
    * publishes samples in the int16 range to the SPU's CD-DA
    * input mixer; clipping here matches PS1 silicon behaviour. */
   if (left_out  < -32768) left_out  = -32768;
   if (left_out  >  32767) left_out  =  32767;
   if (right_out < -32768) right_out = -32768;
   if (right_out >  32767) right_out =  32767;

   samples[0] = left_out;
   samples[1] = right_out;
}

/* This function must always set samples[0] and samples[1], even if just to 0; */
/* range of samples[n] shall be restricted to -32768 through 32767. */
void PS_CDC_GetCDAudio(PS_CDC *cdc, int32 samples[2], const unsigned freq)
{
   if(freq == 7 || freq == 14)
   {
      PS_CDC_ReadAudioBuffer(cdc, samples);
      if(freq == 14)
         PS_CDC_ReadAudioBuffer(cdc, samples);
   }
   else
   {
      /* Fractional-rate path: 25-tap windowed-sinc resampler per
       * channel. Was using an int32 out_tmp[2] stack scratch
       * accumulator; folded directly into samples[i] now since
       * each channel's accumulation is independent and the final
       * write was already to samples[i]. */
      {
         unsigned i;
         for (i = 0; i < 2; i++)
      {
         const int16 *imp = CDADPCMImpulse[cdc->ADPCM_ResampCurPhase];
         int16       *wf  = &cdc->ADPCM_ResampBuf[i][(cdc->ADPCM_ResampCurPos + 32 - 25) & 0x1F];
         int32        acc = 0;

         {
            unsigned s;
            for (s = 0; s < 25; s++)
            acc += imp[s] * wf[s];
         }

         acc >>= 15;
         /* Saturate resampled output to signed 16-bit; required
          * by this function's contract per its leading comment. */
         if (acc < -32768) acc = -32768;
         if (acc >  32767) acc =  32767;
         samples[i] = acc;
      }
      }

      cdc->ADPCM_ResampCurPhase += freq;

      if(cdc->ADPCM_ResampCurPhase >= 7)
      {
         int32 raw[2];

         raw[0] = raw[1] = 0;

         cdc->ADPCM_ResampCurPhase -= 7;
         PS_CDC_ReadAudioBuffer(cdc, raw);

         {
            unsigned i;
            for (i = 0; i < 2; i++)
         {
            cdc->ADPCM_ResampBuf[i][cdc->ADPCM_ResampCurPos +  0] = 
               cdc->ADPCM_ResampBuf[i][cdc->ADPCM_ResampCurPos + 32] = raw[i];
         }
         }
         cdc->ADPCM_ResampCurPos = (cdc->ADPCM_ResampCurPos + 1) & 0x1F;
      }
   }

/* Algorithmically, volume is applied after resampling for CD-XA ADPCM playback, */
/* per PS1 tests(though when "mute" is applied wasn't tested). */
   PS_CDC_ApplyVolume(cdc, samples);
}


typedef struct XA_Subheader
{
   uint8 file;
   uint8 channel;
   uint8 submode;
   uint8 coding;

   uint8 file_dup;
   uint8 channel_dup;
   uint8 submode_dup;
   uint8 coding_dup;
} XA_Subheader;

typedef struct XA_SoundGroup
{
   uint8 params[16];
   uint8 samples[112];
} XA_SoundGroup;

#define XA_SUBMODE_EOF		0x80
#define XA_SUBMODE_REALTIME	0x40
#define XA_SUBMODE_FORM		0x20
#define XA_SUBMODE_TRIGGER	0x10
#define XA_SUBMODE_DATA		0x08
#define XA_SUBMODE_AUDIO	0x04
#define XA_SUBMODE_VIDEO	0x02
#define XA_SUBMODE_EOR		0x01

#define XA_CODING_EMPHASIS	0x40

/*#define XA_CODING_BPS_MASK	0x30 */
/*#define XA_CODING_BPS_4BIT	0x00 */
/*#define XA_CODING_BPS_8BIT	0x10 */
/*#define XA_CODING_SR_MASK	0x0C */
/*#define XA_CODING_SR_378	0x00 */
/*#define XA_CODING_SR_ */

#define XA_CODING_8BIT		0x10
#define XA_CODING_189		0x04
#define XA_CODING_STEREO	0x01

/* Special regression prevention test cases: */
/*	Um Jammer Lammy (start doing poorly) */
/*	Yarudora Series Vol.1 - Double Cast (non-FMV speech) */

bool PS_CDC_XA_Test(PS_CDC *cdc, const uint8 *sdata)
{
   const XA_Subheader *sh = (const XA_Subheader *)&sdata[12 + 4];

   if(!(cdc->Mode & MODE_STRSND))
      return false;

   if(!(sh->submode & XA_SUBMODE_AUDIO))
      return false;

   if((cdc->Mode & MODE_SF) && (sh->file != cdc->FilterFile || sh->channel != cdc->FilterChan))
      return false;

   if(!cdc->xa_cur_set || (cdc->Mode & MODE_SF))
   {
      cdc->xa_cur_set = true;
      cdc->xa_cur_file = sh->file;
      cdc->xa_cur_chan = sh->channel;
   }
   else if(sh->file != cdc->xa_cur_file || sh->channel != cdc->xa_cur_chan)
      return false;

   if(sh->submode & XA_SUBMODE_EOF)
   {
/*puts("YAY"); */
      cdc->xa_cur_set = false;
      cdc->xa_cur_file = 0;
      cdc->xa_cur_chan = 0;
   }

   return true;
}

void PS_CDC_ClearAudioBuffers(PS_CDC *cdc)
{
   memset(&cdc->AudioBuffer, 0, sizeof(cdc->AudioBuffer));
   memset(cdc->xa_previous, 0, sizeof(cdc->xa_previous));

   cdc->xa_cur_set = false;
   cdc->xa_cur_file = 0;
   cdc->xa_cur_chan = 0;

   memset(cdc->ADPCM_ResampBuf, 0, sizeof(cdc->ADPCM_ResampBuf));
   cdc->ADPCM_ResampCurPhase = 0;
   cdc->ADPCM_ResampCurPos = 0;
}


/* output should be readable at -2 and -1 */
static void DecodeXAADPCM(const uint8 *input, int16 *output, const unsigned shift, const unsigned weight)
{
/* Weights copied over from SPU channel ADPCM playback code, */
/* may not be entirely the same for CD-XA ADPCM, we need to run tests. */
   static const int32 Weights[16][2] =
   {
/* s-1    s-2 */
      {   0,    0 },
      {  60,    0 },
      { 115,  -52 },
      {  98,  -55 },
      { 122,  -60 },
   };

   {
      int i;
      for (i = 0; i < 28; i++)
   {
      int32 sample = (int16)(input[i] << 8);
      sample >>= shift;

      sample += ((output[i - 1] * Weights[weight][0]) >> 6) + ((output[i - 2] * Weights[weight][1]) >> 6);

      /* Saturate to signed 16-bit. The clamped value is fed back
       * via output[i-1]/output[i-2] into subsequent iterations'
       * IIR-style filter (weights from SPU's ADPCM playback - may
       * not be exact for CD-XA ADPCM per the comment above). */
      if (sample < -32768) sample = -32768;
      if (sample >  32767) sample =  32767;
      output[i] = sample;
   }
   }
}

void PS_CDC_XA_ProcessSector(PS_CDC *cdc, const uint8 *sdata, CD_Audio_Buffer *ab)
{
   const XA_Subheader *sh = (const XA_Subheader *)&sdata[12 + 4];
   const unsigned unit_index_shift = (sh->coding & XA_CODING_8BIT) ? 0 : 1;

   ab->ReadPos = 0;
   ab->Size = 18 * (4 << unit_index_shift) * 28;

   if(sh->coding & XA_CODING_STEREO)
      ab->Size >>= 1;

   ab->Freq = (sh->coding & XA_CODING_189) ? 3 : 6;

/*fprintf(stderr, "Coding: %02x %02x\n", sh->coding, sh->coding_dup); */

   {
      unsigned group;
      for (group = 0; group < 18; group++)
   {
      const XA_SoundGroup *sg = (const XA_SoundGroup *)&sdata[12 + 4 + 8 + group * 128];

      {
         unsigned unit;
         for (unit = 0; unit < (4U << unit_index_shift); unit++)
      {
         const uint8 param = sg->params[(unit & 3) | ((unit & 4) << 1)];
         const uint8 param_copy = sg->params[4 | (unit & 3) | ((unit & 4) << 1)];
         uint8 ibuffer[28];
         int16 obuffer[2 + 28];
         bool ocn;

         {
            unsigned i;
            for (i = 0; i < 28; i++)
         {
            uint8 tmp = sg->samples[i * 4 + (unit >> unit_index_shift)];

            if(unit_index_shift)
            {
               tmp <<= (unit & 1) ? 0 : 4;
               tmp &= 0xf0;
            }

            ibuffer[i] = tmp;
         }
         }

         ocn = (bool)(unit & 1) && (sh->coding & XA_CODING_STEREO);

         obuffer[0] = cdc->xa_previous[ocn][0];
         obuffer[1] = cdc->xa_previous[ocn][1];

         DecodeXAADPCM(ibuffer, &obuffer[2], param & 0x0F, param >> 4);

         cdc->xa_previous[ocn][0] = obuffer[28];
         cdc->xa_previous[ocn][1] = obuffer[29];

         if(param != param_copy)
            memset(obuffer, 0, sizeof(obuffer));

         if(sh->coding & XA_CODING_STEREO)
         {
            {
               unsigned s;
               for (s = 0; s < 28; s++)
            {
               ab->Samples[ocn][group * (2 << unit_index_shift) * 28 + (unit >> 1) * 28 + s] = obuffer[2 + s];
            }
            }
         }
         else
         {
            {
               unsigned s;
               for (s = 0; s < 28; s++)
            {
               ab->Samples[0][group * (4 << unit_index_shift) * 28 + unit * 28 + s] = obuffer[2 + s];
               ab->Samples[1][group * (4 << unit_index_shift) * 28 + unit * 28 + s] = obuffer[2 + s];
            }
            }
         }
      }
      }
   }
   }
}

void PS_CDC_ClearAIP(PS_CDC *cdc)
{
   cdc->AsyncResultsPendingCount = 0;
   cdc->AsyncIRQPending = 0;
}

void PS_CDC_CheckAIP(PS_CDC *cdc)
{
   if(cdc->AsyncIRQPending && cdc->CDCReadyReceiveCounter <= 0)
   {
      PS_CDC_BeginResults(cdc);

      {
         unsigned i;
         for (i = 0; i < cdc->AsyncResultsPendingCount; i++)
         PS_CDC_WriteResult(cdc, cdc->AsyncResultsPending[i]);
      }

      PS_CDC_WriteIRQ(cdc, cdc->AsyncIRQPending);

      PS_CDC_ClearAIP(cdc);
   }
}

static void PS_CDC_SetAIP_Buf(PS_CDC *cdc, unsigned irq, unsigned result_count, uint8_t *r)
{
   PS_CDC_ClearAIP(cdc);

   cdc->AsyncResultsPendingCount = result_count;

   {
      unsigned i;
      for (i = 0; i < result_count; i++)
      cdc->AsyncResultsPending[i] = r[i];
   }

   cdc->AsyncIRQPending = irq;

   PS_CDC_CheckAIP(cdc);
}

static void PS_CDC_SetAIP1(PS_CDC *cdc, unsigned irq, uint8_t result0)
{
   uint8 tr[1];
   tr[0] = result0;

   PS_CDC_SetAIP_Buf(cdc, irq, 1, tr);
}

static void PS_CDC_SetAIP2(PS_CDC *cdc, unsigned irq, uint8_t result0, uint8_t result1)
{
   uint8 tr[2];
   tr[0] = result0;
   tr[1] = result1;
   PS_CDC_SetAIP_Buf(cdc, irq, 2, tr);
}


void PS_CDC_EnbufferizeCDDASector(PS_CDC *cdc, const uint8 *buf)
{
   CD_Audio_Buffer *ab = &cdc->AudioBuffer;

   ab->Freq = 7 * ((cdc->Mode & MODE_SPEED) ? 2 : 1);
   ab->Size = 588;

   if(cdc->SubQBuf_Safe[0] & 0x40)
   {
      {
         int i;
         for (i = 0; i < 588; i++)
      {
         ab->Samples[0][i] = 0;
         ab->Samples[1][i] = 0;
      }
      }
   }
   else
   {
      {
         int i;
         for (i = 0; i < 588; i++)
      {
         ab->Samples[0][i] = (int16)MDFN_de16lsb(&buf[i * sizeof(int16) * 2 + 0]);
         ab->Samples[1][i] = (int16)MDFN_de16lsb(&buf[i * sizeof(int16) * 2 + 2]);
      }
      }
   }

   ab->ReadPos = 0;
}

void PS_CDC_HandlePlayRead(PS_CDC *cdc)
{
   uint8 read_buf[2352 + 96];
   unsigned speed_mul;


   if(cdc->CurSector >= ((int32)cdc->toc.tracks[100].lba + 300) && cdc->CurSector >= (75 * 60 * 75 - 150))
   {
      cdc->DriveStatus = DS_STOPPED;
      cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
      cdc->SectorsRead = 0;
      return;
   }

   if (cd_async && cdc->SeekRetryCounter)
   {
      if (!CDIF_ReadRawSector(cdc->Cur_CDIF, read_buf, cdc->CurSector, 0))
      {
         cdc->SeekRetryCounter--;
         cdc->PSRCounter = 33868800 / 75;
         return;
      }
   }
   else if (cd_warned_slow)
   {
      /* Return value intentionally discarded - on failure CDIF_ST
       * zeroes read_buf via cdromif.cpp:641 and CDAccess_Image always
       * populates it to a defined state via MakeSubPQ + memset, so
       * DecodeSubQ below is safe to run on the buffer regardless. */
      (void)CDIF_ReadRawSector(cdc->Cur_CDIF, read_buf, cdc->CurSector, -1);
   }
   else if (!CDIF_ReadRawSector(cdc->Cur_CDIF, read_buf, cdc->CurSector, cd_slow_timeout))
   {
      if (cd_async)
         osd_message(3, RETRO_LOG_WARN,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "*Really* slow CD image read detected: consider using precache CD Access Method");
      else
         osd_message(3, RETRO_LOG_WARN,
               RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION,
               "Slow CD image read detected: consider using async or precache CD Access Method");

      cd_warned_slow = true;
      /* Same intentional-discard contract as above. */
      (void)CDIF_ReadRawSector(cdc->Cur_CDIF, read_buf, cdc->CurSector, -1);
   }

   PS_CDC_DecodeSubQ(cdc, read_buf + 2352);

   if(cdc->SubQBuf_Safe[1] == 0xAA && (cdc->DriveStatus == DS_PLAYING || (!(cdc->SubQBuf_Safe[0] & 0x40) && (cdc->Mode & MODE_CDDA))))
   {
      cdc->HeaderBufValid = false;


/* Status in this end-of-disc context here should be generated after we're in the pause state. */
      cdc->DriveStatus = DS_PAUSED;
      cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
      cdc->SectorsRead = 0;
      PS_CDC_SetAIP1(cdc, CDCIRQ_DATA_END, PS_CDC_MakeStatus(cdc, false));

      return;
   }

   if(cdc->DriveStatus == DS_PLAYING)
   {
/* Note: Some game(s) start playing in the pregap of a track(so don't replace this with a simple subq index == 0 check for autopause). */
      if(cdc->PlayTrackMatch == -1 && cdc->SubQChecksumOK)
         cdc->PlayTrackMatch = cdc->SubQBuf_Safe[0x1];

      if((cdc->Mode & MODE_AUTOPAUSE) && cdc->PlayTrackMatch != -1 && cdc->SubQBuf_Safe[0x1] != cdc->PlayTrackMatch)
      {
/* Status needs to be taken before we're paused(IE it should still report playing). */
         PS_CDC_SetAIP1(cdc, CDCIRQ_DATA_END, PS_CDC_MakeStatus(cdc, false));

         cdc->DriveStatus = DS_PAUSED;
         cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
         cdc->SectorsRead = 0;
         cdc->PSRCounter = 0;
         return;
      }

      if((cdc->Mode & MODE_REPORT) && (((cdc->SubQBuf_Safe[0x9] >> 4) != cdc->ReportLastF) || cdc->Forward || cdc->Backward) && cdc->SubQChecksumOK)
      {
         uint8 tr[8];
#if 1
         uint16 abs_lev_max = 0;
         bool abs_lev_chselect = cdc->SubQBuf_Safe[0x8] & 0x01;

         {
            int i;
            for (i = 0; i < 588; i++)
            {
               int v = abs((int16)MDFN_de16lsb(&read_buf[i * 4 + (abs_lev_chselect * 2)]));
               if (v > 32767) v = 32767;
               if ((uint16)v > abs_lev_max) abs_lev_max = (uint16)v;
            }
         }
         abs_lev_max |= abs_lev_chselect << 15;
#endif

         cdc->ReportLastF = cdc->SubQBuf_Safe[0x9] >> 4;

         tr[0] = PS_CDC_MakeStatus(cdc, false);
         tr[1] = cdc->SubQBuf_Safe[0x1];  /* Track */
         tr[2] = cdc->SubQBuf_Safe[0x2];  /* Index */

         if(cdc->SubQBuf_Safe[0x9] & 0x10)
         {
            tr[3] = cdc->SubQBuf_Safe[0x3];  /* R M */
            tr[4] = cdc->SubQBuf_Safe[0x4] | 0x80;  /* R S */
            tr[5] = cdc->SubQBuf_Safe[0x5];  /* R F */
         }
         else	
         {
            tr[3] = cdc->SubQBuf_Safe[0x7];  /* A M */
            tr[4] = cdc->SubQBuf_Safe[0x8];  /* A S */
            tr[5] = cdc->SubQBuf_Safe[0x9];  /* A F */
         }

         tr[6] = abs_lev_max >> 0;
         tr[7] = abs_lev_max >> 8;

         PS_CDC_SetAIP_Buf(cdc, CDCIRQ_DATA_READY, 8, tr);
      }
   }

   if(cdc->SectorPipe_In >= CDC_SECTOR_PIPE_COUNT)
   {
      uint8* buf = cdc->SectorPipe[cdc->SectorPipe_Pos];
      cdc->SectorPipe_In--;

      if(cdc->DriveStatus == DS_READING)
      {
         if(cdc->SubQBuf_Safe[0] & 0x40)  /*) || !(Mode & MODE_CDDA)) */
         {
            memcpy(cdc->HeaderBuf, buf + 12, 12);
            cdc->HeaderBufValid = true;

            if((cdc->Mode & MODE_STRSND) && (buf[12 + 3] == 0x2) && ((buf[12 + 6] & 0x64) == 0x64))
            {
               if(PS_CDC_XA_Test(cdc, buf))
               {
                  if(!(cdc->AudioBuffer.ReadPos < cdc->AudioBuffer.Size))
                     PS_CDC_XA_ProcessSector(cdc, buf, &cdc->AudioBuffer);
               }
            }
            else
            {
/* maybe if(!(Mode & 0x30)) too? */
               if(!(buf[12 + 6] & 0x20))
               {
                  if(!edc_lec_check_and_correct(buf, true))
                  {
                     osd_message(3, RETRO_LOG_ERROR,
                           RETRO_MESSAGE_TARGET_ALL, RETRO_MESSAGE_TYPE_NOTIFICATION_ALT,
                           "Bad sector? - %d", cdc->CurSector);
                  }
               }

               {
                  int32 offs = (cdc->Mode & 0x20) ? 0 : 12;
                  int32 size = (cdc->Mode & 0x20) ? 2340 : 2048;

                  if(cdc->Mode & 0x10)
                  {
                     offs = 12;
                     size = 2328;
                  }

                  memcpy(cdc->SB, buf + 12 + offs, size);
                  cdc->SB_In = size;
                  PS_CDC_SetAIP1(cdc, CDCIRQ_DATA_READY, PS_CDC_MakeStatus(cdc, false));
               }
            }
         }
      }

      if(!(cdc->SubQBuf_Safe[0] & 0x40) && ((cdc->Mode & MODE_CDDA) || cdc->DriveStatus == DS_PLAYING))
      {
         if(!(cdc->AudioBuffer.ReadPos < cdc->AudioBuffer.Size))
            PS_CDC_EnbufferizeCDDASector(cdc, buf);
      }
   }

   memcpy(cdc->SectorPipe[cdc->SectorPipe_Pos], read_buf, 2352);
   cdc->SectorPipe_Pos = (cdc->SectorPipe_Pos + 1) % CDC_SECTOR_PIPE_COUNT;
   cdc->SectorPipe_In++;

   if (cdc->Mode & MODE_SPEED) {
/* We're in 2x mode */
      if (cdc->Mode & (MODE_CDDA | MODE_STRSND)) {
/* We're probably streaming audio to the CD drive, keep the */
/* native speed */
         speed_mul = 2;
      } else {
/* *Probably* not streaming audio, we can try increasing the */
/* *CD speed beyond native */
         speed_mul = 2 * cd_2x_speedup;
      }
   } else {
/* 1x mode */
      speed_mul = 1;
   }

   cdc->PSRCounter += 33868800 / (75 * speed_mul);

   if(cdc->DriveStatus == DS_PLAYING)
   {
/* FIXME: What's the real fast-forward and backward speed? */
      if(cdc->Forward)
         cdc->CurSector += 12;
      else if(cdc->Backward)
      {
         cdc->CurSector -= 12;

         if(cdc->CurSector < 0)  /* FIXME: How does a real PS handle this condition? */
            cdc->CurSector = 0;
      }
      else
         cdc->CurSector++;
   }
   else
      cdc->CurSector++;

   cdc->SectorsRead++;
}

int32_t PS_CDC_Update(PS_CDC *cdc, const int32_t timestamp)
{
   int32 clocks = timestamp - cdc->lastts;

   overclock_cpu_to_device(&clocks);

/*doom_ts = timestamp; */

   while(clocks > 0)
   {
      int32 chunk_clocks = clocks;

      if(cdc->PSRCounter > 0 && chunk_clocks > cdc->PSRCounter)
         chunk_clocks = cdc->PSRCounter;

      if(cdc->PendingCommandCounter > 0 && chunk_clocks > cdc->PendingCommandCounter)
         chunk_clocks = cdc->PendingCommandCounter;

      /* SPUCounter is set in PS_CDC_Power(cdc) from SPU_UpdateFromCDC(0) and
       * reassigned every loop iteration from UpdateFromCDC's return,
       * which (per spu.c's clock_divider arithmetic) is positive
       * provided spu_samples > 0. The current build always has
       * spu_samples > 0, but matching the gating used by the other
       * counters above is cheap insurance: if SPUCounter ever became
       * <=0 here, chunk_clocks could be clamped to 0 and the outer
       * `while(clocks > 0)` loop would spin without progress. */
      if(cdc->SPUCounter > 0 && chunk_clocks > cdc->SPUCounter)
         chunk_clocks = cdc->SPUCounter;

      if(cdc->DiscStartupDelay > 0)
      {
         if(chunk_clocks > cdc->DiscStartupDelay)
            chunk_clocks = cdc->DiscStartupDelay;

         cdc->DiscStartupDelay -= chunk_clocks;

         if(cdc->DiscStartupDelay <= 0)
            cdc->DriveStatus = DS_PAUSED;  /* or is it supposed to be DS_STANDBY? */
      }

      if(!(cdc->IRQBuffer & 0xF))
      {
         if(cdc->CDCReadyReceiveCounter > 0 && chunk_clocks > cdc->CDCReadyReceiveCounter)
            chunk_clocks = cdc->CDCReadyReceiveCounter;

         if(cdc->CDCReadyReceiveCounter > 0)
            cdc->CDCReadyReceiveCounter -= chunk_clocks;
      }

      PS_CDC_CheckAIP(cdc);

      if(cdc->PSRCounter > 0)
      {

         cdc->PSRCounter -= chunk_clocks;

         if(cdc->PSRCounter <= 0) 
         {
            switch (cdc->DriveStatus)
            {
               case DS_RESETTING:
                  PS_CDC_SetAIP1(cdc, CDCIRQ_COMPLETE, PS_CDC_MakeStatus(cdc, false));

                  cdc->Muted = false;  /* Does it get reset here? */
                  PS_CDC_ClearAudioBuffers(cdc);

                  cdc->SB_In          = 0;
                  cdc->SectorPipe_Pos = 0;
                  cdc->SectorPipe_In  = 0;
                  cdc->SectorsRead    = 0;
                  cdc->Mode           = 0x20; /* Confirmed (and see "This Is Football 2"). */
                  cdc->CurSector      = 0;
                  cdc->CommandLoc     = 0;

                  cdc->DriveStatus    = DS_PAUSED;  /* or DS_STANDBY? */
                  PS_CDC_ClearAIP(cdc);
                  break;
               case DS_SEEKING:
                  {
                     int x;
                     cdc->CurSector = cdc->SeekTarget;

/* CurSector + x for "Tomb Raider"'s sake, as it relies on behavior that we can't emulate very well without a more accurate CD drive */
/* emulation model. */
                     for(x = -1; x >= -16; x--)
                     {
                        uint8 pwbuf[96];
                        CDIF_ReadRawSectorPWOnly(cdc->Cur_CDIF, pwbuf, cdc->CurSector + x, false);
                        if(PS_CDC_DecodeSubQ(cdc, pwbuf))
                           break;
                     }

                     cdc->DriveStatus = cdc->StatusAfterSeek;

                     if(cdc->DriveStatus != DS_PAUSED && cdc->DriveStatus != DS_STANDBY)
                        cdc->PSRCounter = 33868800 / (75 * ((cdc->Mode & MODE_SPEED) ? (2 * cd_2x_speedup) : 1));
                  }
                  break;
               case DS_SEEKING_LOGICAL:
                  {
                     uint8 pwbuf[96];
                     cdc->CurSector = cdc->SeekTarget;
                     CDIF_ReadRawSectorPWOnly(cdc->Cur_CDIF, pwbuf, cdc->CurSector, false);
                     PS_CDC_DecodeSubQ(cdc, pwbuf);

                     if(!(cdc->Mode & MODE_CDDA) && !(cdc->SubQBuf_Safe[0] & 0x40))
                     {
                        if(!cdc->SeekRetryCounter)
                        {
                           cdc->DriveStatus = DS_STANDBY;
                           PS_CDC_SetAIP2(cdc, CDCIRQ_DISC_ERROR, PS_CDC_MakeStatus(cdc, false) | 0x04, 0x04);
                        }
                        else
                        {
                           cdc->SeekRetryCounter--;
                           cdc->PSRCounter = 33868800 / 75;
                        }
                     }
                     else
                     {
                        cdc->DriveStatus = cdc->StatusAfterSeek;

                        if(cdc->DriveStatus != DS_PAUSED && cdc->DriveStatus != DS_STANDBY)
                           cdc->PSRCounter = 33868800 / (75 * ((cdc->Mode & MODE_SPEED) ? (2 * cd_2x_speedup) : 1));
                     }
                  }
                  break;
               case DS_READING:
               case DS_PLAYING:
                  PS_CDC_HandlePlayRead(cdc);
                  break;
            }
         }
      }

      if(cdc->PendingCommandCounter > 0)
      {
         cdc->PendingCommandCounter -= chunk_clocks;

         if(cdc->PendingCommandCounter <= 0 && cdc->CDCReadyReceiveCounter > 0)
         {
            cdc->PendingCommandCounter = cdc->CDCReadyReceiveCounter;  /*256; */
         }
/*else if(PendingCommandCounter <= 0 && PSRCounter > 0 && PSRCounter < 2000) */
/*{ */
/* PendingCommandCounter = PSRCounter + 1; */
/*} */
         else if(cdc->PendingCommandCounter <= 0)
         {
            int32 next_time = 0;

            if(cdc->PendingCommandPhase >= 2)	/* Command phase 2+ */
            {
               const CDC_CTEntry *command;
               PS_CDC_BeginResults(cdc);

               command = &Commands[cdc->PendingCommand];

               next_time = command->func2(cdc);
            }
            else switch (cdc->PendingCommandPhase)
            {
               case -1:
                  if(cdc->ArgsRP != cdc->ArgsWP)
                  {
                     cdc->ArgsReceiveLatch = cdc->ArgsBuf[cdc->ArgsRP & 0x0F];
                     cdc->ArgsRP = (cdc->ArgsRP + 1) & 0x1F;
                     cdc->PendingCommandPhase += 1;
                     next_time = 1815;
                  }
                  else
                  {
                     cdc->PendingCommandPhase += 2;
                     next_time = 8500;
                  }
                  break;
               case 0: /* Command phase 0 */
                  if(cdc->ArgsReceiveIn < 32)
                     cdc->ArgsReceiveBuf[cdc->ArgsReceiveIn++] = cdc->ArgsReceiveLatch;

                  if(cdc->ArgsRP != cdc->ArgsWP)
                  {
                     cdc->ArgsReceiveLatch = cdc->ArgsBuf[cdc->ArgsRP & 0x0F];
                     cdc->ArgsRP = (cdc->ArgsRP + 1) & 0x1F;
                     next_time = 1815;
                  }
                  else
                  {
                     cdc->PendingCommandPhase++;
                     next_time = 8500;
                  }
                  break;
               default: /* Command phase 1 */
                  {
                     PS_CDC_BeginResults(cdc);

                     if(cdc->PendingCommand >= 0x20 || !Commands[cdc->PendingCommand].func)
                     {

                        PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, true));
                        PS_CDC_WriteResult(cdc, ERRCODE_BAD_COMMAND);
                        PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);
                     }
                     else if(cdc->ArgsReceiveIn < Commands[cdc->PendingCommand].args_min || 
                           cdc->ArgsReceiveIn > Commands[cdc->PendingCommand].args_max)
                     {
                        PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, true));
                        PS_CDC_WriteResult(cdc, ERRCODE_BAD_NUMARGS);
                        PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);
                     }
                     else
                     {
                        const CDC_CTEntry *command = &Commands[cdc->PendingCommand];
                        next_time = command->func(cdc, cdc->ArgsReceiveIn, cdc->ArgsReceiveBuf);
                        cdc->PendingCommandPhase = 2;
                     }
                     cdc->ArgsReceiveIn = 0;
                  }
                  break;
            }

            if(!next_time)
               cdc->PendingCommandCounter = 0;
            else
               cdc->PendingCommandCounter += next_time;
         }
      }

      cdc->SPUCounter = SPU_UpdateFromCDC(chunk_clocks);

      clocks -= chunk_clocks;
   }  /* end while(clocks > 0) */

   cdc->lastts = timestamp;

   return(timestamp + PS_CDC_CalcNextEvent(cdc));
}

void PS_CDC_Write(PS_CDC *cdc, const int32_t timestamp, uint32 A, uint8 V)
{
   A &= 0x3;

/*printf("Write: %08x %02x\n", A, V); */

   if(A == 0x00)
   {
      cdc->RegSelector = V & 0x3;
   }
   else
   {
      const unsigned reg_index = ((cdc->RegSelector & 0x3) * 3) + (A - 1);

      PS_CDC_Update(cdc, timestamp);

      switch(reg_index)
      {
         default:
            break;

         case 0x00:
            cdc->PendingCommandCounter = 10500 + PSX_GetRandU32(0, 3000) + 1815;
            cdc->PendingCommand = V;
            cdc->PendingCommandPhase = -1;
            cdc->ArgsReceiveIn = 0;
            break;

         case 0x01:
            cdc->ArgsBuf[cdc->ArgsWP & 0xF] = V;
            cdc->ArgsWP = (cdc->ArgsWP + 1) & 0x1F;
            break;

         case 0x02:
            if(V & 0x80)
            {
               if(!cdc->DMABuffer.in_count)
               {
                  if(!cdc->SB_In)
                  {

                     SimpleFIFO_Write(&cdc->DMABuffer, cdc->SB, 2340);

                     while(SimpleFIFO_CanWrite(&cdc->DMABuffer))
                        SimpleFIFO_WriteByte(&cdc->DMABuffer, 0x00);
                  }
                  else
                  {
                     SimpleFIFO_Write(&cdc->DMABuffer, cdc->SB, cdc->SB_In);
                     cdc->SB_In = 0;
                  }
               }
            }
            else if(V & 0x40)  /* Something CD-DA related(along with & 0x20 ???)? */
            {
               {
                  unsigned i;
                  for (i = 0; i < 4 && cdc->DMABuffer.in_count; i++)
                  SimpleFIFO_ReadByte(&cdc->DMABuffer);
               }
            }
            else
            {
               SimpleFIFO_Flush(&cdc->DMABuffer);
            }

            if(V & 0x20)
            {
               cdc->IRQBuffer |= 0x10;
               PS_CDC_RecalcIRQ(cdc);
            }
            break;

         case 0x04:
            cdc->IRQOutTestMask = V;
            PS_CDC_RecalcIRQ(cdc);
            break;

         case 0x05:
            if((cdc->IRQBuffer &~ V) != cdc->IRQBuffer && cdc->ResultsIn)
            {
/* To debug icky race-condition related problems in "Psychic Detective", and to see if any games suffer from the same potential issue */
/* (to know what to test when we emulate CPU more accurately in regards to pipeline stalls and timing, which could throw off our kludge */
/*  for this issue) */
            }

            cdc->IRQBuffer &= ~V;
            PS_CDC_RecalcIRQ(cdc);

            if(V & 0x80)  /* Forced CD hardware reset of some kind(interface, controller, and drive?)  Seems to take a while(relatively speaking) to complete. */
            {
               PS_CDC_SoftReset(cdc);
            }

            if(V & 0x40)  /* Does it clear more than arguments buffer?  Doesn't appear to clear results buffer. */
               cdc->ArgsWP = cdc->ArgsRP = 0;
            break;

         case 0x07:
            cdc->Pending_DecodeVolume[0][0] = V;
            break;

         case 0x08:
            cdc->Pending_DecodeVolume[0][1] = V;
            break;

         case 0x09:
            cdc->Pending_DecodeVolume[1][1] = V;
            break;

         case 0x0A:
            cdc->Pending_DecodeVolume[1][0] = V;
            break;

         case 0x0B:
            if(V & 0x20)
               memcpy(cdc->DecodeVolume, cdc->Pending_DecodeVolume, sizeof(cdc->DecodeVolume));
            break;
      }
      PSX_SetEventNT(PSX_EVENT_CDC, timestamp + PS_CDC_CalcNextEvent(cdc));
   }
}

uint8 PS_CDC_Read(PS_CDC *cdc, const int32_t timestamp, uint32 A)
{
   A &= 0x03;

/*printf("Read %08x\n", A); */

   if(A == 0x00)
   {
      uint8 ret = cdc->RegSelector & 0x3;

      if(cdc->ArgsWP == cdc->ArgsRP)
         ret |= 0x08;  /* Args FIFO empty. */

      if(!((cdc->ArgsWP - cdc->ArgsRP) & 0x10))
         ret |= 0x10;  /* Args FIFO has room. */

      if(cdc->ResultsIn)
         ret |= 0x20;

      if(cdc->DMABuffer.in_count)
         ret |= 0x40;

      if(cdc->PendingCommandCounter > 0 && cdc->PendingCommandPhase <= 1)
         ret |= 0x80;

      return ret;
   }

   switch(A & 0x3)
   {
      case 0x01:
         return PS_CDC_ReadResult(cdc);
      case 0x02:
         if(cdc->DMABuffer.in_count)
            return SimpleFIFO_ReadByte(&cdc->DMABuffer);
         break;
      case 0x03:
         if(cdc->RegSelector & 0x1)
            return 0xE0 | cdc->IRQBuffer;
         return 0xFF;
   }

   return 0;
}


bool PS_CDC_DMACanRead(PS_CDC *cdc)
{
   return(cdc->DMABuffer.in_count);
}

uint32 PS_CDC_DMARead(PS_CDC *cdc)
{
   unsigned i;
   uint32_t data = 0;

   for(i = 0; i < 4; i++)
   {
      if(cdc->DMABuffer.in_count)
         data |= SimpleFIFO_ReadByte(&cdc->DMABuffer) << (i * 8);
   }

   return data;
}

/*
 * C-linkage shim declared in cdc_c.h. Forwards to PS_CDC::DMARead
 * through the file-scope PSX_CDC pointer. Unlike the cpu_c.h shims,
 * this one touches instance state (DMABuffer), but the indirection
 * is the same one the original PSX_CDC->PS_CDC_DMARead(cdc) call site already
 * carried.
 */
#ifdef __cplusplus
extern "C" {
#endif

uint32_t CDC_DMARead(void)
{
   return PS_CDC_DMARead(PSX_CDC);
}

/*
 * C-linkage shim declared in cdc_c.h. Wraps the historical
 *   freq = AudioBuffer-position-and-rate gate
 *   samples[0] = samples[1] = 0;
 *   if (freq) PS_CDC::GetCDAudio(samples, freq);
 * idiom that was previously inlined at spu.c's CD-DA mix call
 * site. Centralising it here means spu.c doesn't need access to
 * CD_Audio_Buffer's internal layout (which is private to PS_CDC),
 * just to a single C-callable accessor. `samples` is always
 * written; both channels are zeroed when no CD audio is currently
 * playing or the buffer is exhausted.
 */
void CDC_GetCDAudioSample(int32_t samples[2])
{
   const unsigned freq = (PSX_CDC->AudioBuffer.ReadPos < PSX_CDC->AudioBuffer.Size)
      ? PSX_CDC->AudioBuffer.Freq : 0;

   samples[0] = 0;
   samples[1] = 0;

   if (freq)
      PS_CDC_GetCDAudio(PSX_CDC, samples, freq);
}

#ifdef __cplusplus
}
#endif

bool PS_CDC_CommandCheckDiscPresent(PS_CDC *cdc)
{
   if(!cdc->Cur_CDIF || cdc->DiscStartupDelay > 0)
   {
      PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, true));
      PS_CDC_WriteResult(cdc, ERRCODE_NOT_READY);

      PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);

      return(false);
   }

   return(true);
}

int32 PS_CDC_Command_Nop(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_Command_Setloc(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   uint8 m, s, f;

   if((args[0] & 0x0F) > 0x09 || args[0] > 0x99 ||
         (args[1] & 0x0F) > 0x09 || args[1] > 0x59 ||
         (args[2] & 0x0F) > 0x09 || args[2] > 0x74)
   {
      PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, true));
      PS_CDC_WriteResult(cdc, ERRCODE_BAD_ARGVAL);
      PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);
      return(0);
   }

   m = BCD_to_U8(args[0]);
   s = BCD_to_U8(args[1]);
   f = BCD_to_U8(args[2]);

   cdc->CommandLoc = f + 75 * s + 75 * 60 * m - 150;
   cdc->CommandLoc_Dirty = true;

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_CalcSeekTime(PS_CDC *cdc, int32 initial, int32 target, bool motor_on, bool paused)
{
   int32 ret = 0;

   if(!motor_on)
   {
      initial = 0;
      ret += 33868800;
   }

   {
      int64 calc = (int64)abs(initial - target) * 33868800 * 1000 / (72 * 60 * 75) / 1000;
      if (calc < 20000) calc = 20000;
      ret += calc;
   }

   if(abs(initial - target) >= 2250)
      ret += (int64)33868800 * 300 / 1000;
   else if(paused)
   {
/* The delay to restart from a Pause state is...very....WEIRD.  The time it takes is related to the amount of time that has passed since the pause, and */
/* where on the disc the laser head is, with generally more time passed = longer to resume, except that there's a window of time where it takes a */
/* ridiculous amount of time when not much time has passed. */

/* What we have here will be EXTREMELY simplified. */




/*if(time_passed >= 67737) */
/*{ */
/*} */
/*else */
      {
/* Take twice as long for 1x mode. */
         if (cdc->Mode & MODE_SPEED) {
            ret += 1237952 / cd_2x_speedup;
         } else {
            ret += 1237952 * 2;
         }
      }
   }
/*else if(target < initial) */
/* ret += 1000000; */

   ret += PSX_GetRandU32(0, 25000);
   return(ret);
}

/* Remove this function when we have better seek emulation; it's here because the Rockman complete works games(at least 2 and 4) apparently have finicky fubared CD */
/* access code. */
void PS_CDC_PreSeekHack(PS_CDC *cdc, uint32 target)
{
   uint8 pwbuf[96];
   int max_try = 32;

   cdc->CurSector = target;  /* If removing/changing this, take into account how it will affect ReadN/ReadS/Play/etc command calls that interrupt a seek. */
   cdc->SeekRetryCounter = 128;

/* If removing this SubQ reading bit, think about how it will interact with a Read command of data(or audio :b) sectors when Mode bit0 is 1. */
   do
   {
      CDIF_ReadRawSectorPWOnly(cdc->Cur_CDIF, pwbuf, target++, true);
   } while (!PS_CDC_DecodeSubQ(cdc, pwbuf) && --max_try > 0);
}

/*
   Play command with a track argument that's not a valid BCD quantity causes interesting half-buggy behavior on an actual PS1(unlike some of the other commands,
   an error doesn't seem to be generated for a bad BCD argument).
   */
int32 PS_CDC_Command_Play(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   PS_CDC_ClearAIP(cdc);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   cdc->Forward = cdc->Backward = false;

   if(arg_count && args[0])
   {
      int track = BCD_to_U8(args[0]);

      if(track < cdc->toc.first_track)
      {
         track = cdc->toc.first_track;
      }
      else if(track > cdc->toc.last_track)
      {
         track = cdc->toc.last_track;
      }

      PS_CDC_ClearAudioBuffers(cdc);
      cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
      cdc->SectorsRead = 0;

      cdc->PlayTrackMatch = track;


      cdc->SeekTarget = cdc->toc.tracks[track].lba;
      cdc->PSRCounter = PS_CDC_CalcSeekTime(cdc, cdc->CurSector, cdc->SeekTarget, cdc->DriveStatus != DS_STOPPED, cdc->DriveStatus == DS_PAUSED);
      cdc->HeaderBufValid = false;
      PS_CDC_PreSeekHack(cdc, cdc->SeekTarget);

      cdc->ReportLastF = 0xFF;

      cdc->DriveStatus = DS_SEEKING;
      cdc->StatusAfterSeek = DS_PLAYING;
   }
   else if(cdc->CommandLoc_Dirty || cdc->DriveStatus != DS_PLAYING)
   {
      PS_CDC_ClearAudioBuffers(cdc);
      cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
      cdc->SectorsRead = 0;

      if(cdc->CommandLoc_Dirty)
         cdc->SeekTarget = cdc->CommandLoc;
      else
         cdc->SeekTarget = cdc->CurSector;

      cdc->PlayTrackMatch = -1;

      cdc->PSRCounter = PS_CDC_CalcSeekTime(cdc, cdc->CurSector, cdc->SeekTarget, cdc->DriveStatus != DS_STOPPED, cdc->DriveStatus == DS_PAUSED);
      cdc->HeaderBufValid = false;
      PS_CDC_PreSeekHack(cdc, cdc->SeekTarget);

      cdc->ReportLastF = 0xFF;

      cdc->DriveStatus = DS_SEEKING;
      cdc->StatusAfterSeek = DS_PLAYING;
   }

   cdc->CommandLoc_Dirty = false;
   return(0);
}

int32 PS_CDC_Command_Forward(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   cdc->Backward = false;
   cdc->Forward = true;

   return(0);
}

int32 PS_CDC_Command_Backward(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   cdc->Backward = true;
   cdc->Forward = false;

   return(0);
}


void PS_CDC_ReadBase(PS_CDC *cdc)
{
   if(!cdc->IsPSXDisc)
   {
      PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, true));
      PS_CDC_WriteResult(cdc, ERRCODE_BAD_COMMAND);

      PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);
      return;
   }

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   if(cdc->DriveStatus == DS_SEEKING_LOGICAL && cdc->SeekTarget == cdc->CommandLoc && cdc->StatusAfterSeek == DS_READING)
   {
      cdc->CommandLoc_Dirty = false;
      return;
   }

   if(cdc->CommandLoc_Dirty || cdc->DriveStatus != DS_READING)
   {
/* Don't flush the DMABuffer here; see CTR course selection screen. */
      PS_CDC_ClearAIP(cdc);
      PS_CDC_ClearAudioBuffers(cdc);
      cdc->SB_In = 0;
      cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
      cdc->SectorsRead = 0;

/* TODO: separate motor start from seek phase? */

      if(cdc->CommandLoc_Dirty)
         cdc->SeekTarget = cdc->CommandLoc;
      else
         cdc->SeekTarget = cdc->CurSector;

      cdc->PSRCounter = /*903168 * 1.5 +*/ PS_CDC_CalcSeekTime(cdc, cdc->CurSector, cdc->SeekTarget, cdc->DriveStatus != DS_STOPPED, cdc->DriveStatus == DS_PAUSED);
      cdc->HeaderBufValid = false;
      PS_CDC_PreSeekHack(cdc, cdc->SeekTarget);

      cdc->DriveStatus = DS_SEEKING_LOGICAL;
      cdc->StatusAfterSeek = DS_READING;
   }

   cdc->CommandLoc_Dirty = false;
}

int32 PS_CDC_Command_ReadN(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(PS_CDC_CommandCheckDiscPresent(cdc))
      PS_CDC_ReadBase(cdc);
   return 0;
}

int32 PS_CDC_Command_ReadS(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(PS_CDC_CommandCheckDiscPresent(cdc))
      PS_CDC_ReadBase(cdc);
   return 0;
}

int32 PS_CDC_Command_Stop(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   if(cdc->DriveStatus == DS_STOPPED)
      return(5000);

   PS_CDC_ClearAudioBuffers(cdc);
   PS_CDC_ClearAIP(cdc);
   cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
   cdc->SectorsRead = 0;

   cdc->DriveStatus = DS_STOPPED;
   cdc->HeaderBufValid = false;

   return(33868);  /* FIXME, should be much higher. */
}

int32 PS_CDC_Command_Stop_Part2(PS_CDC *cdc)
{
   cdc->PSRCounter = 0;

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_COMPLETE);

   return(0);
}

int32 PS_CDC_Command_Standby(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   if(cdc->DriveStatus != DS_STOPPED)
   {
      PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, true));
      PS_CDC_WriteResult(cdc, 0x20);
      PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);
      return(0);
   }

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   PS_CDC_ClearAudioBuffers(cdc);
   PS_CDC_ClearAIP(cdc);
   cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
   cdc->SectorsRead = 0;

   cdc->DriveStatus = DS_STANDBY;

   return((int64)33868800 * 100 / 1000);  /* No idea, FIXME. */
}

int32 PS_CDC_Command_Standby_Part2(PS_CDC *cdc)
{
   cdc->PSRCounter = 0;

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_COMPLETE);

   return(0);
}

int32 PS_CDC_Command_Pause(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   if(cdc->DriveStatus == DS_PAUSED || cdc->DriveStatus == DS_STOPPED)
      return(5000);

   {
      uint32 sub = (cdc->SectorsRead < 4) ? cdc->SectorsRead : 4;
      cdc->CurSector -= sub;	/* See: Bedlam, Rise 2 */
   }
   cdc->SectorsRead = 0;

/* "Viewpoint" flips out and crashes if reading isn't stopped (almost?) immediately. */
/*PS_CDC_ClearAudioBuffers(cdc); */
   cdc->SectorPipe_Pos = cdc->SectorPipe_In = 0;
   PS_CDC_ClearAIP(cdc);
   cdc->DriveStatus = DS_PAUSED;

/* An approximation. */
   return((1124584 + ((int64)cdc->CurSector * 42596 / (75 * 60))) * ((cdc->Mode & MODE_SPEED) ? 1 : 2));
}

int32 PS_CDC_Command_Pause_Part2(PS_CDC *cdc)
{
   cdc->PSRCounter = 0;

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_COMPLETE);

   return(0);
}

int32 PS_CDC_Command_Reset(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   if(cdc->DriveStatus != DS_RESETTING)
   {
      cdc->HeaderBufValid = false;
      cdc->DriveStatus = DS_RESETTING;
      cdc->PSRCounter = 1136000;
   }

   return(0);
}

int32 PS_CDC_Command_Mute(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   cdc->Muted = true;

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_Command_Demute(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   cdc->Muted = false;

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_Command_Setfilter(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   cdc->FilterFile = args[0];
   cdc->FilterChan = args[1];


   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_Command_Setmode(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   cdc->Mode = args[0];

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_Command_Getparam(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteResult(cdc, cdc->Mode);
   PS_CDC_WriteResult(cdc, 0x00);
   PS_CDC_WriteResult(cdc, cdc->FilterFile);
   PS_CDC_WriteResult(cdc, cdc->FilterChan);

   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);


   return(0);
}

int32 PS_CDC_Command_GetlocL(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   if(!cdc->HeaderBufValid)
   {
      PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, true));
      PS_CDC_WriteResult(cdc, 0x80);
      PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);
      return(0);
   }

   {
      unsigned i;
      for (i = 0; i < 8; i++)
   {
/*printf("%d %d: %02x\n", DriveStatus, i, HeaderBuf[i]); */
      PS_CDC_WriteResult(cdc, cdc->HeaderBuf[i]);
   }
   }

   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_Command_GetlocP(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

/*printf("%2x:%2x %2x:%2x:%2x %2x:%2x:%2x\n", SubQBuf_Safe[0x1], SubQBuf_Safe[0x2], SubQBuf_Safe[0x3], SubQBuf_Safe[0x4], SubQBuf_Safe[0x5], SubQBuf_Safe[0x7], SubQBuf_Safe[0x8], SubQBuf_Safe[0x9]); */

   PS_CDC_WriteResult(cdc, cdc->SubQBuf_Safe[0x1]);  /* Track */
   PS_CDC_WriteResult(cdc, cdc->SubQBuf_Safe[0x2]);  /* Index */
   PS_CDC_WriteResult(cdc, cdc->SubQBuf_Safe[0x3]);  /* R M */
   PS_CDC_WriteResult(cdc, cdc->SubQBuf_Safe[0x4]);  /* R S */
   PS_CDC_WriteResult(cdc, cdc->SubQBuf_Safe[0x5]);  /* R F */
   PS_CDC_WriteResult(cdc, cdc->SubQBuf_Safe[0x7]);  /* A M */
   PS_CDC_WriteResult(cdc, cdc->SubQBuf_Safe[0x8]);  /* A S */
   PS_CDC_WriteResult(cdc, cdc->SubQBuf_Safe[0x9]);  /* A F */

   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_Command_ReadT(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(44100 * 768 / 1000);
}

int32 PS_CDC_Command_ReadT_Part2(PS_CDC *cdc)
{
   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_COMPLETE);

   return(0);
}

int32 PS_CDC_Command_GetTN(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteResult(cdc, U8_to_BCD(cdc->toc.first_track));
   PS_CDC_WriteResult(cdc, U8_to_BCD(cdc->toc.last_track));

   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_Command_GetTD(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   int track;
   uint8 m, s, f;

   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   if(!args[0])
      track = 100;
   else
   {
      track= BCD_to_U8(args[0]);

      if(!BCD_is_valid(args[0]) || track < cdc->toc.first_track || track > cdc->toc.last_track)	/* Error */
      {
         PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, true));
         PS_CDC_WriteResult(cdc, ERRCODE_BAD_ARGVAL);
         PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);
         return(0);
      }
   }

   LBA_to_AMSF(cdc->toc.tracks[track].lba, &m, &s, &f);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteResult(cdc, U8_to_BCD(m));
   PS_CDC_WriteResult(cdc, U8_to_BCD(s));
/*PS_CDC_WriteResult(cdc, U8_to_BCD(f)); */

   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(0);
}

int32 PS_CDC_Command_SeekL(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   cdc->SeekTarget = cdc->CommandLoc;

   cdc->PSRCounter = (33868800 / (75 * ((cdc->Mode & MODE_SPEED) ? 2 : 1))) + PS_CDC_CalcSeekTime(cdc, cdc->CurSector, cdc->SeekTarget, cdc->DriveStatus != DS_STOPPED, cdc->DriveStatus == DS_PAUSED);
   cdc->HeaderBufValid = false;
   PS_CDC_PreSeekHack(cdc, cdc->SeekTarget);
   cdc->DriveStatus = DS_SEEKING_LOGICAL;
   cdc->StatusAfterSeek = DS_STANDBY;
   PS_CDC_ClearAIP(cdc);

   return(cdc->PSRCounter);
}

int32 PS_CDC_Command_SeekP(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   cdc->SeekTarget = cdc->CommandLoc;

   cdc->PSRCounter = PS_CDC_CalcSeekTime(cdc, cdc->CurSector, cdc->SeekTarget, cdc->DriveStatus != DS_STOPPED, cdc->DriveStatus == DS_PAUSED);
   cdc->HeaderBufValid = false;
   PS_CDC_PreSeekHack(cdc, cdc->SeekTarget);
   cdc->DriveStatus = DS_SEEKING;
   cdc->StatusAfterSeek = DS_STANDBY;
   PS_CDC_ClearAIP(cdc);

   return(cdc->PSRCounter);
}

int32 PS_CDC_Command_Seek_PartN(PS_CDC *cdc)
{
   if(cdc->DriveStatus == DS_STANDBY)
   {
      PS_CDC_BeginResults(cdc);
      PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
      PS_CDC_WriteIRQ(cdc, CDCIRQ_COMPLETE);

      return(0);
   }

   return((cdc->PSRCounter > 256) ? cdc->PSRCounter : 256);
}

int32 PS_CDC_Command_Test(PS_CDC *cdc, const int arg_count, const uint8 *args)
{

   if ((args[0] >= 0x00 && args[0] <= 0x03) || (args[0] >= 0x10 && args[0] <= 0x1A))
   {
      PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
      PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);
   }
   else switch(args[0])
   {
      default:
         PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, true));
         PS_CDC_WriteResult(cdc, 0x10);
         PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);
         break;
#if 0
      case 0x50:  /* *Need to retest this test command, it takes additional arguments??? Or in any case, it generates a different error code(0x20) than most other Test */
/* sub-commands that generate an error code(0x10). */
         break;

/* Same with 0x60, 0x71-0x76 */

#endif

      case 0x51:  /* *Need to retest this test command */
         PS_CDC_WriteResult(cdc, 0x01);
         PS_CDC_WriteResult(cdc, 0x00);
         PS_CDC_WriteResult(cdc, 0x00);
         break;

      case 0x75:  /* *Need to retest this test command */
         PS_CDC_WriteResult(cdc, 0x00);
         PS_CDC_WriteResult(cdc, 0xC0);
         PS_CDC_WriteResult(cdc, 0x00);
         PS_CDC_WriteResult(cdc, 0x00);
         break;


/* SCEx counters not reset by command 0x0A. */


      case 0x04:  /* Reset SCEx counters */
         PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
         PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);
         break;

      case 0x05:  /* Read SCEx counters */
         PS_CDC_WriteResult(cdc, 0x00);  /* Number of TOC/leadin reads? (apparently increases by 1 or 2 per ReadTOC, even on non-PSX music CD) */
         PS_CDC_WriteResult(cdc, 0x00);  /* Number of SCEx strings received? (Stays at zero on music CD) */
         PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);
         break;

      case 0x20:
         PS_CDC_WriteResult(cdc, 0x97);
         PS_CDC_WriteResult(cdc, 0x01);
         PS_CDC_WriteResult(cdc, 0x10);
         PS_CDC_WriteResult(cdc, 0xC2);

         PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);
         break;

      case 0x21:  /* *Need to retest this test command. */
         PS_CDC_WriteResult(cdc, 0x01);
         PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);
         break;

      case 0x22:
         {
            static const uint8 td[7] = { 0x66, 0x6f, 0x72, 0x20, 0x55, 0x2f, 0x43 };

            {
               unsigned i;
               for (i = 0; i < 7; i++)
               PS_CDC_WriteResult(cdc, td[i]);
            }

            PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);
         }
         break;

      case 0x23:
      case 0x24:
         {
            static const uint8 td[8] = { 0x43, 0x58, 0x44, 0x32, 0x35, 0x34, 0x35, 0x51 };

            {
               unsigned i;
               for (i = 0; i < 8; i++)
               PS_CDC_WriteResult(cdc, td[i]);
            }

            PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);
         }
         break;

      case 0x25:
         {
            static const uint8 td[8] = { 0x43, 0x58, 0x44, 0x31, 0x38, 0x31, 0x35, 0x51 };

            {
               unsigned i;
               for (i = 0; i < 8; i++)
               PS_CDC_WriteResult(cdc, td[i]);
            }

            PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);
         }
         break;
   }
   return(0);
}

int32 PS_CDC_Command_ID(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(0);

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

   return(33868);
}

int32 PS_CDC_Command_ID_Part2(PS_CDC *cdc)
{
   if(cdc->IsPSXDisc)
   {
      PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
      PS_CDC_WriteResult(cdc, 0x00);
      PS_CDC_WriteResult(cdc, 0x20);
      PS_CDC_WriteResult(cdc, 0x00);
   }
   else
   {
      PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false) | 0x08);
      PS_CDC_WriteResult(cdc, 0x90);
      PS_CDC_WriteResult(cdc, cdc->toc.disc_type);
      PS_CDC_WriteResult(cdc, 0x00);
   }

   if(cdc->IsPSXDisc)
   {
      PS_CDC_WriteResult(cdc, cdc->DiscID[0]);
      PS_CDC_WriteResult(cdc, cdc->DiscID[1]);
      PS_CDC_WriteResult(cdc, cdc->DiscID[2]);
      PS_CDC_WriteResult(cdc, cdc->DiscID[3]);
   }
   else
   {
      PS_CDC_WriteResult(cdc, 0xff);
      PS_CDC_WriteResult(cdc, 0);
      PS_CDC_WriteResult(cdc, 0);
      PS_CDC_WriteResult(cdc, 0);
   }

   if(cdc->IsPSXDisc)
      PS_CDC_WriteIRQ(cdc, CDCIRQ_COMPLETE);
   else
      PS_CDC_WriteIRQ(cdc, CDCIRQ_DISC_ERROR);

   return(0);
}

int32 PS_CDC_Command_Init(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   return(0);
}

int32 PS_CDC_Command_ReadTOC(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   int32 ret_time;

   cdc->HeaderBufValid = false;
   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);

/* ReadTOC doesn't error out if the tray is open, and it completes rather quickly in that case. */

   if(!PS_CDC_CommandCheckDiscPresent(cdc))
      return(26000);



/* A gross approximation. */
/* The penalty for the drive being stopped seems to be rather high(higher than what PS_CDC_CalcSeekTime(cdc) currently introduces), although */
/* that should be investigated further. */

/* ...and not to mention the time taken varies from disc to disc even! */
   ret_time = 30000000 + PS_CDC_CalcSeekTime(cdc, cdc->CurSector, 0, cdc->DriveStatus != DS_STOPPED, cdc->DriveStatus == DS_PAUSED);

   cdc->DriveStatus = DS_PAUSED;  /* Ends up in a pause state when the command is finished.  Maybe we should add DS_READTOC or something... */
   PS_CDC_ClearAIP(cdc);

   return ret_time;
}

int32 PS_CDC_Command_ReadTOC_Part2(PS_CDC *cdc)
{
/*if(!PS_CDC_CommandCheckDiscPresent(cdc)) */
/* DriveStatus = DS_PAUSED; */

   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_COMPLETE);

   return(0);
}

int32 PS_CDC_Command_0x1d(PS_CDC *cdc, const int arg_count, const uint8 *args)
{
   PS_CDC_WriteResult(cdc, PS_CDC_MakeStatus(cdc, false));
   PS_CDC_WriteIRQ(cdc, CDCIRQ_ACKNOWLEDGE);
   return(0);
}
