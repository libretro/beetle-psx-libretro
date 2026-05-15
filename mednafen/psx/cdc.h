#ifndef __MDFN_PSX_CDC_H
#define __MDFN_PSX_CDC_H

#include <stdint.h>
#include <boolean.h>

#include "../state.h"

#include "../cdrom/cdromif.h"   /* Now plain C - shared header for both languages */

/* CD-DA / XA audio buffer.  Filled by the disc-read pipeline (CDDA
 * sectors decoded directly, XA sectors via XA ADPCM decode), drained
 * by the SPU through CDC_GetCDAudioSample. */
typedef struct CD_Audio_Buffer
{
   int16_t  Samples[2][0x1000];   /* [0][...] = l, [1][...] = r */
   uint32_t Size;
   uint32_t Freq;
   uint32_t ReadPos;
} CD_Audio_Buffer;

/* SimpleFIFO is a fixed-power-of-two-capacity ring buffer of bytes,
 * used by the CDC DMA path; the only place it lives is inside
 * struct PS_CDC.  Was a tiny C++ class with inline methods - now
 * a plain C struct, with the access helpers static-inlined into
 * cdc.c.  Field layout is preserved for savestate compatibility. */
typedef struct SimpleFIFO
{
   uint8_t  *data;
   uint32_t size;
   uint32_t read_pos;
   uint32_t write_pos;
   uint32_t in_count;
} SimpleFIFO;

/* Number of pipelined sectors held by the disc-read delay buffer.
 * Was a class-scoped enum constant inside PS_CDC; promoted to a
 * #define so the array dimension below is a constant expression
 * in C as well as C++. */
#define CDC_SECTOR_PIPE_COUNT 2

/* Each SectorPipe slot holds 2352 bytes of audio/data payload
 * followed by 96 bytes of P-W subchannel.  HandlePlayRead reads
 * disc sectors directly into the next slot (no intermediate copy)
 * and DecodeSubQ consumes the trailing 96 bytes in place. */
#define CDC_SECTOR_PIPE_BYTES (2352 + 96)

/* PS_CDC was a C++ class with all members private and access via
 * PSX_CDC->Method(...).  It is now a plain struct: the fields
 * below are the former class members in the same declaration
 * order (savestate-compatible), and the former member functions
 * are declared at the bottom of this header as free functions
 * PS_CDC_*(struct PS_CDC *cdc, ...). */
typedef struct PS_CDC
{
   /* Fields formerly in the public section. */
   CD_Audio_Buffer AudioBuffer;
   int             DriveStatus;

   /* Fields formerly in the private section. */
   CDIF      *Cur_CDIF;
   bool      DiscChanged;
   int32_t   DiscStartupDelay;

   uint8_t   Pending_DecodeVolume[2][2];   /* [data_source][output_port] */
   uint8_t   DecodeVolume[2][2];

   int16_t   ADPCM_ResampBuf[2][32 * 2];
   uint8_t   ADPCM_ResampCurPos;
   uint8_t   ADPCM_ResampCurPhase;

   uint8_t   RegSelector;
   uint8_t   ArgsBuf[16];
   uint8_t   ArgsWP;          /* 5-bit (0..31) */
   uint8_t   ArgsRP;          /* 5-bit (0..31) */

   uint8_t   ArgsReceiveLatch;
   uint8_t   ArgsReceiveBuf[32];
   uint8_t   ArgsReceiveIn;

   uint8_t   ResultsBuffer[16];
   uint8_t   ResultsIn;       /* 5-bit (0..31) */
   uint8_t   ResultsWP;       /* Write position, 4-bit (0..15) */
   uint8_t   ResultsRP;       /* Read position,  4-bit (0..15) */

   SimpleFIFO DMABuffer;
   uint8_t   SB[2340];
   uint32_t  SB_In;

   uint8_t   SectorPipe[CDC_SECTOR_PIPE_COUNT][CDC_SECTOR_PIPE_BYTES];
   uint8_t   SectorPipe_Pos;
   uint8_t   SectorPipe_In;

   uint8_t   SubQBuf[0xC];
   uint8_t   SubQBuf_Safe[0xC];
   bool      SubQChecksumOK;

   bool      HeaderBufValid;
   uint8_t   HeaderBuf[12];

   uint8_t   IRQBuffer;
   uint8_t   IRQOutTestMask;
   /* IRQBuffer being non-zero prevents new results and a new IRQ
    * from coming in and erasing the current results, but at least
    * one game clears the IRQ state BEFORE reading the results, so
    * we have a delay between IRQBuffer being cleared and when we
    * allow new results in. */
   int32_t   CDCReadyReceiveCounter;

   uint8_t   FilterFile;
   uint8_t   FilterChan;

   uint8_t   PendingCommand;
   int       PendingCommandPhase;
   int32_t   PendingCommandCounter;

   int32_t   SPUCounter;

   uint8_t   Mode;

   int       StatusAfterSeek;
   bool      Forward;
   bool      Backward;
   bool      Muted;

   int32_t   PlayTrackMatch;

   int32_t   PSRCounter;

   int32_t   CurSector;
   uint32_t  SectorsRead;     /* Reset on Read or Play start; used by
                                 the rough simulation of PS1 SetLoc-
                                 then-Read-then-Pause-then-Read behaviour. */

   unsigned  AsyncIRQPending;
   uint8_t   AsyncResultsPending[16];
   uint8_t   AsyncResultsPendingCount;

   int32_t   SeekTarget;
   uint32_t  SeekRetryCounter;

   int32_t   lastts;

   TOC       toc;
   bool      IsPSXDisc;
   uint8_t   DiscID[4];

   int32_t   CommandLoc;
   bool      CommandLoc_Dirty;

   int16_t   xa_previous[2][2];
   bool      xa_cur_set;
   uint8_t   xa_cur_file;
   uint8_t   xa_cur_chan;

   uint8_t   ReportLastF;
} PS_CDC;

/* Public API.  Each function takes the instance pointer as its
 * first argument; otherwise the signatures match the former
 * PS_CDC member functions one-to-one. */
void     PS_CDC_Init(PS_CDC *cdc);
void     PS_CDC_Destroy(PS_CDC *cdc);

/* Global instance pointer.  Allocated in libretro.cpp's InitCommon
 * and freed in Cleanup; declared here so both C++ callers (libretro.cpp,
 * gpu.cpp) and the in-file C-linkage shims (CDC_DMARead, CDC_GetCDAudioSample,
 * defined in cdc.cpp) agree on the symbol. */
extern PS_CDC *PSX_CDC;

void     PS_CDC_SetDisc(PS_CDC *cdc, bool tray_open,
                        CDIF *cdif, const char *disc_id);

void     PS_CDC_Power(PS_CDC *cdc);
int      PS_CDC_StateAction(PS_CDC *cdc, StateMem *sm,
                            int load, int data_only);
void     PS_CDC_ResetTS(PS_CDC *cdc);

int32_t  PS_CDC_CalcNextEvent(PS_CDC *cdc);

int32_t  PS_CDC_Update(PS_CDC *cdc, const int32_t timestamp);

void     PS_CDC_Write(PS_CDC *cdc, const int32_t timestamp,
                      uint32_t A, uint8_t V);
uint8_t  PS_CDC_Read(PS_CDC *cdc, const int32_t timestamp,
                     uint32_t A);

bool     PS_CDC_DMACanRead(PS_CDC *cdc);
uint32_t PS_CDC_DMARead(PS_CDC *cdc);
void     PS_CDC_SoftReset(PS_CDC *cdc);

void     PS_CDC_GetCDAudio(PS_CDC *cdc, int32_t samples[2],
                           const unsigned freq);

#endif /* __MDFN_PSX_CDC_H */
