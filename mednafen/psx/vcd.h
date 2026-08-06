#ifndef __MDFN_PSX_VCD_H
#define __MDFN_PSX_VCD_H

/* Video CD support for the SCPH-5903 (PU-16 "PSX with Video CD").
 *
 * Hardware background
 * -------------------
 * The SCPH-5903 is an otherwise ordinary LATE-PU-8 class PlayStation with:
 *
 *   - A 1 Mbyte kernel ROM (M538032E-02) instead of the usual 512 Kbyte part.
 *     Low  512K = "System ROM Version 2.2 12/04/95 J" kernel.
 *     High 512K = shell overlay at BFC80000h (copied to 80030000h, 4DFF0h
 *                 bytes) plus a separate Video CD player image at BFCD0000h
 *                 (copied to 80010000h, 20000h bytes).
 *
 *   - A CDROM sub-CPU (SC430924, firmware "15 Aug 1996 vC2") carrying one
 *     extra host command, 1Fh, which is a bridge to an MPEG daughterboard.
 *
 *   - The daughterboard itself, which is NOT on the CPU bus. It listens to the
 *     CD DSP's serial audio bus directly, decodes MPEG-1, and drives its own
 *     analogue RGB + audio outputs. Three 4053C/2283 triple multiplexors on
 *     the mainboard select between GPU output and daughterboard output.
 *
 * The consequence for emulation is that MPEG video never touches GPU VRAM and
 * MPEG audio never touches the SPU. "VCD mode" is a wholesale substitution of
 * the video and audio front end, which is why this lives in its own module and
 * only needs two small hooks in cdc.c and libretro.c.
 *
 * Modes
 * -----
 * VCD_MODE_BOARD  The SCPH-5903 kernel is loaded and running its own player
 *                 UI. We emulate the daughterboard: command 1Fh/01h exchanges
 *                 pad state for transport requests, 1Fh/02h throws the A/V
 *                 multiplexor. Sector payloads are tapped out of the CDC read
 *                 path, exactly as the real board taps the DSP bus.
 *
 * VCD_MODE_HLE    No SCPH-5903 kernel available. The PSX is not booted at all;
 *                 we drive the disc directly and run our own transport and
 *                 player. This is the fallback path and is also the only way
 *                 to get full-colour, full-rate playback with B-frames, since
 *                 that is what the daughterboard did and the PSX proper cannot.
 *
 * Both modes share the demux/decode pipeline in this file.
 *
 * Verified from the ROM image and from psx-spx; anything marked SPECULATIVE
 * below has not been confirmed against real SCPH-5903 hardware.
 */

#include <stdint.h>
#include <stddef.h>
#include <boolean.h>

struct CDIF;

#ifdef __cplusplus
extern "C" {
#endif

#define VCD_MAX_TRACKS   99
#define VCD_MAX_ENTRIES  500

/* Mode 2 Form 2 user-data payload size; the MPEG-1 program stream on a VCD is
 * the concatenation of these across the video tracks. */
#define VCD_FORM2_PAYLOAD 2324

typedef enum
{
   VCD_DISC_NONE = 0,
   VCD_DISC_VCD11,      /* "VIDEO_CD", sys profile tag 01h */
   VCD_DISC_VCD20,      /* "VIDEO_CD", version 02h         */
   VCD_DISC_SVCD,       /* "SUPERVCD"                      */
   VCD_DISC_HQVCD       /* "HQ-VCD  "                      */
} VCD_DiscType;

typedef enum
{
   VCD_MODE_OFF = 0,
   VCD_MODE_BOARD,
   VCD_MODE_HLE
} VCD_Mode;

typedef enum
{
   VCD_XPORT_STOP = 0,
   VCD_XPORT_PLAY,
   VCD_XPORT_PAUSE,
   VCD_XPORT_SEEK
} VCD_Transport;

/* One chapter/entry from ENTRIES.VCD. All on-disc values are BCD and the
 * 16/32-bit fields in VCD/SVCD/EXT files are BIG-endian; these are decoded. */
typedef struct
{
   uint8_t  track;      /* 2..99 */
   uint32_t lba;        /* absolute LBA */
} VCD_Entry;

typedef struct
{
   VCD_DiscType type;
   bool         pal;              /* from INFO.VCD PAL flags */
   bool         has_pbc;          /* PSD.VCD size nonzero    */
   unsigned     num_entries;
   VCD_Entry    entries[VCD_MAX_ENTRIES];
   char         album_id[17];
} VCD_DiscInfo;

/* ---- lifecycle ------------------------------------------------------- */

void VCD_Init(void);
void VCD_Kill(void);
void VCD_Reset(void);

/* Probe a mounted disc. Returns the detected type and fills *out_info when
 * non-NULL. Safe to call with cdif == NULL.
 *
 * The CDIF is passed in rather than fetched: libretro.c owns the disc array
 * and the tray state, and probing has to work for any disc in an m3u set,
 * not only whichever one happens to be current. */
VCD_DiscType VCD_ProbeDisc(struct CDIF *cdif, VCD_DiscInfo *out_info);

/* Select the operating mode. Called once after BIOS load: pass have_5903 =
 * true when the loaded kernel is the 1 Mbyte SCPH-5903 image. */
void VCD_SetMode(VCD_Mode mode);
VCD_Mode VCD_GetMode(void);

/* ---- CDC bridge (command 1Fh) ---------------------------------------- */

/* Cmd 1Fh,01h,JoyL,JoyH,State,Task,00h --> INT3(stat,req,mm,ss,ff,x)
 *
 * The PSX side is a courier: it forwards pad state and the drive-door bit to
 * the board and receives back a transport request plus the board's idea of the
 * current position. args[] is the five bytes after the subfunction; resp[] is
 * filled with the five response bytes that follow stat.
 *
 * JoyL/JoyH bit assignment (confirmed by nocash against the SC430924 dump):
 *   0 drive door (0=open)     8  dpad up      (0=pressed)
 *   1 triangle   (0=pressed)  9  dpad down    (0=pressed)
 *   2 square     (0=pressed)  10 dpad left    (0=pressed)
 *   3 circle     (0=pressed)  11 dpad right   (0=pressed)
 *   4 cross      (0=pressed)  12..15 SPECULATIVE, sent as 1
 *   5 start      (0=pressed)
 *   6 select     (0=pressed)
 *   7 always 0
 *
 * The State/Task bytes and the returned req byte are only partially
 * documented; the encoding used here is our own and is described in vcd.c.
 */
void VCD_SioExchange(const uint8_t *args, uint8_t *resp);

/* Cmd 1Fh,02h,flag,x,x,x,x --> INT3(stat,0,0,x,x,x)
 * flag 00h = normal (GPU/SPU), 01h..FFh = daughterboard drives A/V. */
void VCD_SetAVSwitch(bool vcd_output);
bool VCD_GetAVSwitch(void);

/* ---- sector tap ------------------------------------------------------ */

/* Called from the CDC sector read path with a full 2352-byte raw sector.
 * Ignored unless the A/V switch is engaged (BOARD mode) or we are driving the
 * transport ourselves (HLE mode). Form 1 sectors and non-MPEG tracks are
 * dropped. */
void VCD_FeedSector(const uint8_t *raw2352, uint32_t lba);

/* ---- transport (HLE mode) -------------------------------------------- */

void VCD_Play(unsigned entry_index);
void VCD_Pause(void);
void VCD_Stop(void);
void VCD_NextTrack(void);
void VCD_PrevTrack(void);
void VCD_SeekLBA(uint32_t lba);
VCD_Transport VCD_GetTransport(void);
uint32_t VCD_GetPositionLBA(void);

/* Feed one frame of pad state. Bit order is the frontend's, packed by the
 * caller:
 *
 *   0 select   2 up     4 left   6 A (cross)   8 X (triangle)
 *   1 start    3 down   5 right  7 B (circle)  9 Y (square)
 *
 * HLE mode acts on the edges directly -- start plays and pauses, select
 * stops, left and right change track. BOARD mode repacks them into the
 * daughterboard's own bit order and forwards them over command 1Fh. */
void VCD_SetPadState(uint16_t buttons);

/* Bit positions in the VCD_SetPadState mask. */
#define VCD_PAD_SELECT  (1u << 0)
#define VCD_PAD_START   (1u << 1)
#define VCD_PAD_UP      (1u << 2)
#define VCD_PAD_DOWN    (1u << 3)
#define VCD_PAD_LEFT    (1u << 4)
#define VCD_PAD_RIGHT   (1u << 5)
#define VCD_PAD_CROSS   (1u << 6)
#define VCD_PAD_CIRCLE  (1u << 7)
#define VCD_PAD_TRIANGLE (1u << 8)
#define VCD_PAD_SQUARE  (1u << 9)

/* ---- output ---------------------------------------------------------- */

/* Decode up to one frame's worth of the pending stream. Returns true when a
 * new picture is ready. Call once per retro_run() while VCD output is active.
 */
bool VCD_RunFrame(void);

/* Current picture, in the pixel format the core was configured with.
 * Returns NULL when nothing has been decoded yet. */
const void *VCD_GetVideo(unsigned *w, unsigned *h, size_t *pitch);

/* Drain decoded MP2 audio. Writes interleaved s16 stereo, returns frames. */
size_t VCD_GetAudio(int16_t *out, size_t max_frames);

unsigned VCD_GetSampleRate(void);
double   VCD_GetFrameRate(void);

/* ---- save states ----------------------------------------------------- */

/* Round-trips the transport only. The decoders' internal state is not saved:
 * a Video CD is losslessly re-readable, so on load the pipeline is dropped
 * and re-primed from the restored position, which costs one GOP of latency
 * and nothing else. */
int VCD_StateAction(void *sm, int load, int data_only);

#ifdef __cplusplus
}
#endif

#endif
