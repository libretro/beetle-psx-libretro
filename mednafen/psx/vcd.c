/* Video CD support for the SCPH-5903. See vcd.h for the hardware notes.
 *
 * Style: C89, /-* *-/ comments only, no C99 declarations-after-statement,
 * MSVC-clean.
 *
 * The decode stack is libretro-common throughout: rmpeg1_ps demultiplexes the
 * program stream, rmpeg1_video decodes MPEG-1 video, and rmp3 -- which covers
 * MPEG audio layers 1, 2 and 3, not only layer 3 -- decodes the MP2 audio a
 * Video CD carries. No vendored third-party decoder is involved.
 */

#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <retro_inline.h>

#include "vcd.h"

#include "../state.h"
#include "../state_helpers.h"
#include "../../libretro_cbs.h"
#include "../cdrom/cdromif.h"

#include <formats/rmpeg1_ps.h>
#include <formats/rmpeg1_video.h>
#include <formats/rmp3.h>

/* --------------------------------------------------------------------- */
/* Constants                                                             */
/* --------------------------------------------------------------------- */

/* Fixed sector numbers mandated by the VCD spec (MSF -> LBA, LBA = ((m*60+s)
 * *75+f)-150). Players are explicitly permitted to ignore the ISO filesystem
 * and address these directly, which is what we do. */
#define VCD_LBA_PVD        16    /* 00:02:16 */
#define VCD_LBA_INFO      150    /* 00:04:00 */
#define VCD_LBA_ENTRIES   151    /* 00:04:01 */

/* Largest picture we will accept. VCD is 352x240/352x288; SVCD goes to
 * 480x480 and a few oddballs use 704x480. */
#define VCD_MAX_W         720
#define VCD_MAX_H         576

/* Streaming window handed to the demuxer. Four seconds of 1.15 Mbit/s VCD
 * video plus slack, rounded to a whole number of Form 2 payloads. */
#define VCD_RING_BYTES    (VCD_FORM2_PAYLOAD * 512)

/* Staging for one call into the audio decoder. An MP2 frame is 1152 samples
 * per channel and a VCD sector carries three of them, so this is a few
 * sectors' worth and the loop below rarely goes round twice. */
#define VCD_AUD_CHUNK     8192

/* Decoded audio staging ring, in stereo frames. */
#define VCD_AUDIO_FRAMES  16384

/* --------------------------------------------------------------------- */
/* State                                                                 */
/* --------------------------------------------------------------------- */

typedef struct
{
   VCD_Mode      mode;
   VCD_DiscInfo  info;
   VCD_Transport xport;

   bool          av_switch;      /* Port F.3: false = GPU/SPU, true = board */
   bool          headers_ok;

   /* demux/decode */
   rmpeg1_ps_t    *ps;
   rmpeg1_video_t *vid;
   rmp3_stream_t  *aud;

   /* video out */
   uint8_t      *fb;             /* VCD_MAX_W * VCD_MAX_H * 4 */
   unsigned      fb_w, fb_h;
   size_t        fb_pitch;
   bool          fb_valid;
   bool          fb_is_xrgb;     /* false = RGB565 */

   /* audio out */
   int16_t      *abuf;           /* VCD_AUDIO_FRAMES * 2 */
   size_t        ard, awr;
   unsigned      srate;

   /* transport */
   uint32_t      pos_lba;
   unsigned      cur_entry;
   uint16_t      pad, pad_prev;

   /* board bridge */
   uint8_t       last_req;
   bool          board_detected;
   bool          board_started;
   uint8_t       board_state;
   uint8_t       board_task;
} VCD_State;

static VCD_State vcd;

/* --------------------------------------------------------------------- */
/* Helpers                                                               */
/* --------------------------------------------------------------------- */

static INLINE uint8_t bin2bcd(unsigned v)
{
   return (uint8_t)(((v / 10) % 10) << 4 | (v % 10));
}

static INLINE unsigned bcd2bin(uint8_t v)
{
   return (unsigned)((v >> 4) * 10 + (v & 0x0F));
}

static INLINE uint32_t msf_to_lba(unsigned m, unsigned s, unsigned f)
{
   int32_t lba = (int32_t)(((m * 60u) + s) * 75u + f) - 150;
   return (lba < 0) ? 0u : (uint32_t)lba;
}

static INLINE void lba_to_msf(uint32_t lba, unsigned *m, unsigned *s, unsigned *f)
{
   uint32_t t = lba + 150u;
   *m = (unsigned)(t / (60u * 75u));
   *s = (unsigned)((t / 75u) % 60u);
   *f = (unsigned)(t % 75u);
}

/* Every multi-byte field inside VCD/SVCD/EXT control files is big-endian,
 * regardless of host or of the little-endian ISO structures around them. */
static INLINE uint16_t rd_be16(const uint8_t *p)
{
   return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static INLINE uint32_t rd_be32(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
          ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

/* --------------------------------------------------------------------- */
/* Disc probe                                                            */
/* --------------------------------------------------------------------- */

static VCD_DiscType classify_info(const uint8_t *info)
{
   if (!memcmp(info, "VIDEO_CD", 8))
   {
      /* [008h] version major, [009h] system profile tag. */
      if (info[8] >= 0x02)
         return VCD_DISC_VCD20;
      return VCD_DISC_VCD11;
   }
   if (!memcmp(info, "SUPERVCD", 8))
      return VCD_DISC_SVCD;
   if (!memcmp(info, "HQ-VCD  ", 8))
      return VCD_DISC_HQVCD;
   return VCD_DISC_NONE;
}

VCD_DiscType VCD_ProbeDisc(struct CDIF *cdif, VCD_DiscInfo *out_info)
{
   uint8_t      sec[2048];
   VCD_DiscInfo di;
   CDIF        *c = (CDIF *)cdif;
   unsigned     n, i;
   uint32_t     psd_size;

   memset(&di, 0, sizeof(di));

   if (!c)
      goto done;

   /* The bridge signature in the PVD is the cheapest reliable discriminator:
    * every VCD/SVCD carries "CD-RTOS CD-BRIDGE" as System Identifier. Bail
    * early on ordinary PSX discs so we never touch LBA 150 on them. */
   if (!CDIF_ReadSector(c, sec, VCD_LBA_PVD, 1))
      goto done;
   if (memcmp(&sec[1], "CD001", 5))
      goto done;
   if (memcmp(&sec[8], "CD-RTOS CD-BRIDGE", 17))
      goto done;

   if (!CDIF_ReadSector(c, sec, VCD_LBA_INFO, 1))
      goto done;

   di.type = classify_info(sec);
   if (di.type == VCD_DISC_NONE)
      goto done;

   memcpy(di.album_id, &sec[0x0A], 16);
   di.album_id[16] = '\0';

   /* [01Eh] 13 bytes of PAL flags, one bit per track, MSB-first. Track 2 is
    * the first video track, so bit index 1 is the one that matters. */
   di.pal = (sec[0x1E] & 0x40) ? true : false;

   psd_size    = rd_be32(&sec[0x2C]);
   di.has_pbc  = (psd_size != 0);

   if (!CDIF_ReadSector(c, sec, VCD_LBA_ENTRIES, 1))
      goto done;

   /* A disc whose INFO identifies it as a Video CD is one, even if the
    * chapter list is unreadable -- the type comes from INFO, not ENTRIES.
    * Keep the type and return an empty list rather than reporting the disc
    * as something else entirely; the caller has to cope with an empty list
    * anyway, since a disc can legitimately carry very few entries.
    *
    * This was previously the behaviour by accident, via a fallthrough with
    * di.type already assigned. It is now deliberate, and the caller logs it. */
   if (memcmp(sec, "ENTRYVCD", 8) && memcmp(sec, "ENTRYSVD", 8))
      goto done;

   n = rd_be16(&sec[0x0A]);
   if (n > VCD_MAX_ENTRIES)
      n = VCD_MAX_ENTRIES;

   for (i = 0; i < n; i++)
   {
      const uint8_t *e = &sec[0x0C + i * 4];
      unsigned m, s, f;

      /* All four bytes BCD: track, then MM:SS:FF. */
      m = bcd2bin(e[1]);
      s = bcd2bin(e[2]);
      f = bcd2bin(e[3]);

      if (s > 59 || f > 74)
         continue;               /* malformed entry, skip rather than trust */

      di.entries[di.num_entries].track = (uint8_t)bcd2bin(e[0]);
      di.entries[di.num_entries].lba   = msf_to_lba(m, s, f);
      di.num_entries++;
   }

done:
   if (out_info)
      *out_info = di;
   return di.type;
}

/* --------------------------------------------------------------------- */
/* Colour conversion                                                     */
/* --------------------------------------------------------------------- */

/* BT.601 limited-range YCbCr 4:2:0 -> RGB, integer, 16.16-ish fixed point.
 *
 *   R = 1.164(Y-16)                 + 1.596(Cr-128)
 *   G = 1.164(Y-16) - 0.391(Cb-128) - 0.813(Cr-128)
 *   B = 1.164(Y-16) + 2.018(Cb-128)
 *
 * Scaled by 1<<10. Kept integer deliberately: float here would make frame
 * output host-FPU dependent, which we do not want in a core that has been
 * systematically de-floated elsewhere.
 */
#define YC_Y   1192   /* 1.164 * 1024 */
#define YC_RV  1634   /* 1.596 * 1024 */
#define YC_GU   400   /* 0.391 * 1024 */
#define YC_GV   833   /* 0.813 * 1024 */
#define YC_BU  2066   /* 2.018 * 1024 */

static INLINE int clamp255(int v)
{
   if (v < 0)   return 0;
   if (v > 255) return 255;
   return v;
}

static void frame_to_fb(const rmpeg1_video_frame_t *fr)
{
   unsigned w = fr->width;
   unsigned h = fr->height;
   unsigned x, y;

   if (w > VCD_MAX_W) w = VCD_MAX_W;
   if (h > VCD_MAX_H) h = VCD_MAX_H;

   vcd.fb_w     = w;
   vcd.fb_h     = h;
   vcd.fb_pitch = (size_t)VCD_MAX_W * (vcd.fb_is_xrgb ? 4u : 2u);

   for (y = 0; y < h; y++)
   {
      const uint8_t *yr = fr->y  + (size_t)y * fr->y_stride;
      const uint8_t *cb = fr->cb + (size_t)(y >> 1) * fr->c_stride;
      const uint8_t *cr = fr->cr + (size_t)(y >> 1) * fr->c_stride;
      uint8_t       *dr = vcd.fb + (size_t)y * vcd.fb_pitch;

      for (x = 0; x < w; x++)
      {
         int yy = ((int)yr[x] - 16) * YC_Y;
         int u  =  (int)cb[x >> 1] - 128;
         int v  =  (int)cr[x >> 1] - 128;
         int r  = clamp255((yy + YC_RV * v) >> 10);
         int g  = clamp255((yy - YC_GU * u - YC_GV * v) >> 10);
         int b  = clamp255((yy + YC_BU * u) >> 10);

         if (vcd.fb_is_xrgb)
         {
            uint32_t p = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            memcpy(dr + (size_t)x * 4, &p, 4);
         }
         else
         {
            uint16_t p = (uint16_t)(((r & 0xF8) << 8) |
                                    ((g & 0xFC) << 3) |
                                    ((b & 0xF8) >> 3));
            memcpy(dr + (size_t)x * 2, &p, 2);
         }
      }
   }

   vcd.fb_valid = true;
}

/* --------------------------------------------------------------------- */
/* Audio staging                                                         */
/* --------------------------------------------------------------------- */

/* rmp3 hands back interleaved s16 already, so this is a copy into the ring
 * rather than a float conversion. */
static void push_audio(const int16_t *pcm, size_t frames)
{
   size_t i;

   for (i = 0; i < frames; i++)
   {
      size_t nxt = (vcd.awr + 1) % VCD_AUDIO_FRAMES;

      if (nxt == vcd.ard)
         break;                  /* overrun; drop rather than stall decode */

      vcd.abuf[vcd.awr * 2 + 0] = pcm[i * 2 + 0];
      vcd.abuf[vcd.awr * 2 + 1] = pcm[i * 2 + 1];
      vcd.awr = nxt;
   }
}

size_t VCD_GetAudio(int16_t *out, size_t max_frames)
{
   size_t n = 0;

   while (n < max_frames && vcd.ard != vcd.awr)
   {
      out[n * 2 + 0] = vcd.abuf[vcd.ard * 2 + 0];
      out[n * 2 + 1] = vcd.abuf[vcd.ard * 2 + 1];
      vcd.ard = (vcd.ard + 1) % VCD_AUDIO_FRAMES;
      n++;
   }
   return n;
}

/* --------------------------------------------------------------------- */
/* Stream plumbing                                                       */
/* --------------------------------------------------------------------- */

static void stream_reset(void)
{
   if (vcd.vid)
      rmpeg1_video_free(vcd.vid);
   if (vcd.aud)
      rmp3_stream_free(vcd.aud);
   if (vcd.ps)
      rmpeg1_ps_free(vcd.ps);

   vcd.vid = NULL;
   vcd.aud = NULL;
   vcd.ps  = NULL;

   vcd.headers_ok = false;
   vcd.fb_valid   = false;
   vcd.ard = vcd.awr = 0;

   /* The demuxer window slides on its own: bytes the decoders have consumed
    * are reclaimed and a write that would overflow is truncated rather than
    * growing the buffer. A VCD is CBR, so the decoders never fall more than
    * a fraction of a second behind the drive; dropping the excess is the
    * correct failure mode when they do. */
   vcd.ps  = rmpeg1_ps_init(VCD_RING_BYTES);
   vcd.vid = rmpeg1_video_init();
   vcd.aud = rmp3_stream_new();
}

/* Hand one elementary stream packet to the decoder it belongs to. */
static void route_packet(const rmpeg1_ps_packet_t *pkt)
{
   if (pkt->type == RMPEG1_PS_VIDEO)
   {
      size_t off = 0;

      if (!vcd.vid)
         return;

      while (off < pkt->size)
      {
         size_t got = rmpeg1_video_write(vcd.vid, pkt->data + off,
                                         pkt->size - off);
         if (!got)
            break;               /* window full; drained in VCD_RunFrame */
         off += got;
      }
      return;
   }

   if (pkt->type == RMPEG1_PS_AUDIO)
   {
      size_t off = 0;

      if (!vcd.aud)
         return;

      /* rmp3's stream interface carries any unconsumed tail internally, so a
       * packet ending mid-frame costs nothing here. */
      while (off < pkt->size)
      {
         int16_t pcm[VCD_AUD_CHUNK * 2];
         size_t  rd = 0, wr = 0;
         int     r;

         rmp3_stream_set_in(vcd.aud, pkt->data + off, pkt->size - off);
         rmp3_stream_set_out_s16(vcd.aud, pcm, VCD_AUD_CHUNK);
         r = rmp3_stream_process(vcd.aud, &rd, &wr);

         off += rd;
         if (wr)
            push_audio(pcm, wr);

         if (r == RMP3_STREAM_ERROR || r == RMP3_STREAM_END)
            break;
         if (!rd && !wr)
            break;
      }
   }
}

void VCD_FeedSector(const uint8_t *raw2352, uint32_t lba)
{
   const uint8_t     *sub;
   const uint8_t     *payload;
   rmpeg1_ps_packet_t pkt;

   if (vcd.mode == VCD_MODE_OFF || !vcd.ps)
      return;
   if (vcd.mode == VCD_MODE_BOARD && !vcd.av_switch)
      return;

   /* raw2352 = 12 sync + 4 header + 8 subheader + 2324 user + 4 EDC.
    * Subheader byte 2 is the submode: bit5 selects Form 2, bit1 marks video,
    * bit2 marks audio. MPEG payload sectors are Form 2 with video or audio
    * set; anything else on the track is padding or control. */
   sub = raw2352 + 16;
   if (!(sub[2] & 0x20))
      return;                    /* Form 1: not stream data */
   if (!(sub[2] & 0x06))
      return;                    /* neither video nor audio */

   payload = raw2352 + 24;

   rmpeg1_ps_write(vcd.ps, payload, VCD_FORM2_PAYLOAD);
   vcd.pos_lba = lba;

   while (rmpeg1_ps_next(vcd.ps, &pkt))
      route_packet(&pkt);

}

bool VCD_RunFrame(void)
{
   rmpeg1_video_frame_t fr;

   if (!vcd.vid)
      return false;
   if (vcd.xport != VCD_XPORT_PLAY)
      return false;

   /* One picture per call, so output stays locked to the frontend's frame
    * cadence. Audio is decoded as packets arrive in VCD_FeedSector and is
    * already sitting in the ring.
    *
    * Note the decoder parses the sequence header inside decode(), not on
    * write, so this must not be gated on headers_ok -- doing that deadlocks:
    * the flag never gets set because decode is never called, and decode is
    * never called because the flag is not set. headers_ok is a report of
    * what has been seen, not a precondition. */
   if (!rmpeg1_video_decode(vcd.vid, &fr))
      return false;

   if (!vcd.headers_ok)
   {
      unsigned ch = 0, rate = 0;

      vcd.headers_ok = true;

      if (vcd.aud && rmp3_stream_info(vcd.aud, &ch, &rate) && rate)
         vcd.srate = rate;
      if (!vcd.srate)
         vcd.srate = 44100;
   }

   frame_to_fb(&fr);
   return true;
}

const void *VCD_GetVideo(unsigned *w, unsigned *h, size_t *pitch)
{
   if (!vcd.fb_valid)
      return NULL;
   if (w)     *w     = vcd.fb_w;
   if (h)     *h     = vcd.fb_h;
   if (pitch) *pitch = vcd.fb_pitch;
   return vcd.fb;
}

unsigned VCD_GetSampleRate(void)
{
   return vcd.srate ? vcd.srate : 44100;
}

double VCD_GetFrameRate(void)
{
   if (vcd.vid && vcd.headers_ok)
   {
      unsigned n = 0, d = 0;

      rmpeg1_video_framerate(vcd.vid, &n, &d);
      if (n && d)
         return (double)n / (double)d;
   }
   return vcd.info.pal ? 25.0 : (30000.0 / 1001.0);
}

/* --------------------------------------------------------------------- */
/* Transport                                                             */
/* --------------------------------------------------------------------- */

/* Tell the video decoder no more of the current track is coming, then drain
 * whatever it was holding back. A picture is only known to be complete when
 * the following start code arrives, and the decoder holds one reference for
 * B-picture reordering, so without this a track change loses its last couple
 * of frames. */
static void flush_video(void)
{
   rmpeg1_video_frame_t fr;
   int guard = 0;

   if (!vcd.vid)
      return;

   rmpeg1_video_flush(vcd.vid);

   while (rmpeg1_video_decode(vcd.vid, &fr) && guard++ < 8)
      frame_to_fb(&fr);
}


void VCD_Play(unsigned entry_index)
{
   if (entry_index >= vcd.info.num_entries)
      return;
   flush_video();
   vcd.cur_entry = entry_index;
   vcd.pos_lba   = vcd.info.entries[entry_index].lba;
   stream_reset();
   vcd.xport = VCD_XPORT_PLAY;
}

void VCD_Pause(void)
{
   if (vcd.xport == VCD_XPORT_PLAY)
      vcd.xport = VCD_XPORT_PAUSE;
   else if (vcd.xport == VCD_XPORT_PAUSE)
      vcd.xport = VCD_XPORT_PLAY;
}

void VCD_Stop(void)
{
   vcd.xport = VCD_XPORT_STOP;
   stream_reset();
}

void VCD_NextTrack(void)
{
   if (vcd.cur_entry + 1 < vcd.info.num_entries)
      VCD_Play(vcd.cur_entry + 1);
}

void VCD_PrevTrack(void)
{
   if (vcd.cur_entry > 0)
      VCD_Play(vcd.cur_entry - 1);
}

void VCD_SeekLBA(uint32_t lba)
{
   flush_video();
   vcd.pos_lba = lba;
   stream_reset();
   vcd.xport = VCD_XPORT_PLAY;
}

VCD_Transport VCD_GetTransport(void) { return vcd.xport; }
uint32_t      VCD_GetPositionLBA(void) { return vcd.pos_lba; }

void VCD_SetPadState(uint16_t buttons)
{
   uint16_t edge;

   vcd.pad_prev = vcd.pad;
   vcd.pad      = buttons;

   /* Act on rising edges so one press is one action. BOARD mode does not act
    * here: the daughterboard owns the transport there, and the pad is passed
    * to it over the SIO bridge instead. */
   if (vcd.mode != VCD_MODE_HLE)
      return;

   edge = (uint16_t)(buttons & ~vcd.pad_prev);

   if (edge & VCD_PAD_START)
   {
      if (vcd.xport == VCD_XPORT_STOP)
         VCD_Play(vcd.cur_entry);
      else
         VCD_Pause();
   }
   if (edge & VCD_PAD_SELECT)
      VCD_Stop();
   if (edge & VCD_PAD_RIGHT)
      VCD_NextTrack();
   if (edge & VCD_PAD_LEFT)
      VCD_PrevTrack();
}

/* --------------------------------------------------------------------- */
/* Command 1Fh bridge                                                    */
/* --------------------------------------------------------------------- */

void VCD_SetAVSwitch(bool vcd_output)
{
   if (vcd.av_switch == vcd_output)
      return;

   vcd.av_switch = vcd_output;

   /* Real hardware throws three analogue multiplexors here; the daughterboard
    * keeps running either way. We take the switch as the cue to arm or idle
    * the decoder, since there is nothing to show while the GPU owns output. */
   if (vcd_output)
   {
      stream_reset();
      if (vcd.info.num_entries)
      {
         vcd.cur_entry = 0;
         vcd.pos_lba   = vcd.info.entries[0].lba;
      }
      vcd.xport = VCD_XPORT_PLAY;
   }
   else
      vcd.xport = VCD_XPORT_STOP;
}

bool VCD_GetAVSwitch(void)
{
   return vcd.av_switch;
}

/* Request codes returned in the "req" byte.
 *
 * These are not guesses. The daughterboard's own firmware has never been
 * dumped, but its firmware is not what we need: what matters is how the
 * kernel *interprets* the bytes it gets back, and the kernel is dumped.
 *
 * VideoCdSio's response is consumed at 80010C5Ch, which dispatches on the req
 * byte: codes 00h..07h through a jump table at 800189ECh, 80h and 81h by
 * separate branches, everything else ignored. Each handler opens by logging a
 * debug string when the flag at 80026FA0h is set, and those strings are Sony's
 * own names for the requests -- the protocol is documented inside the binary.
 *
 * Derived, with the addresses to re-check the work:
 *
 *   00h  no request                          80010FD0h (returns immediately)
 *   01h  "-- play --"                        80010CE8h
 *   02h  "-- pause --"                       80010DF0h
 *   03h  "-- stop --"                        80010E3Ch
 *   04h  "-- tocread --"                     80010E88h
 *   05h  "-- vcd ack --"                     80010EF0h
 *   06h  "-- ff --"     (fast forward)       80010F20h
 *   07h  "-- fr --"     (fast reverse)       80010F6Ch
 *   80h  board present                       80010FB8h -> state 1
 *   81h  board absent                        80010FC8h -> state 2
 *
 * 80h/81h are the answer to the detection probe: both call the setter at
 * 800104C0h, and the "Check VideoCD..." routine at 800101ACh reads it back
 * through 800104CCh and prints "Found" for state 1, "Not found" for state 2.
 *
 * The three bytes after req are the position, in BCD: the play handler copies
 * them straight into a Setloc (command 02h) followed by SeekP (16h), so they
 * are the seek target the board is asking the host to move to, in the same
 * encoding Setloc takes.
 *
 * What the kernel *sends* is [subcmd, JoyL, JoyH, State, Task, 0], built at
 * 800109E8h. The pad halfword comes from 80010540h, State from the byte at
 * 800191F4h and Task from 800191F0h -- and both of those are derived from the
 * drive, not invented, which is what makes the sequencing recoverable without
 * a real machine.
 *
 * 80010540h issues Nop (01h) for the status byte and then GetID (1Ah), and
 * 800105A0h onward turns the results into State:
 *
 *   stat bit7 (playing)      -> State 1
 *   stat bit1 clear (motor off) -> State 0
 *   stat bit6 clear (not seeking) -> State 2
 *   otherwise (seeking)      -> State unchanged
 *
 * So State is simply what the drive is doing: 0 stopped, 1 playing, 2 idle,
 * held across a seek. Task is the request the host wants acknowledged:
 *
 *   stat bit3 (IdError) and GetID flags bit6 -> Task 80h  (no disc)
 *   a pending-flag global at 80019
 *   1F8h clear                -> Task 00h
 *   otherwise                                -> Task FFh  (idle)
 *   stat bit0 set, GetID bit7 clear          -> Task 01h  (disc newly valid)
 *   a set bit4 in the flag byte at 8001A718h -> Task 0Ah
 *
 * and 80010C70h resets Task to FFh at the top of the response dispatcher, so
 * every non-idle Task is a one-shot the board is expected to acknowledge.
 *
 * The consequence for a stand-in board: the kernel drives the exchange from
 * drive state it already has, and the board only has to answer. Answering
 * PRESENT once to the 01h probe and then issuing transport requests against
 * the position bytes is a sequence the kernel accepts, because nothing in its
 * state machine requires a request it has not been given a reason for. What
 * cannot be recovered this way is what the *real* board chooses to send when
 * -- that is its firmware's business -- only what the kernel will accept. */
#define VCD_REQ_NONE      0x00
#define VCD_REQ_PLAY      0x01
#define VCD_REQ_PAUSE     0x02
#define VCD_REQ_STOP      0x03
#define VCD_REQ_TOCREAD   0x04
#define VCD_REQ_ACK       0x05
#define VCD_REQ_FF        0x06
#define VCD_REQ_FR        0x07
#define VCD_REQ_PRESENT   0x80
#define VCD_REQ_ABSENT    0x81

void VCD_SioExchange(const uint8_t *args, uint8_t *resp)
{
   unsigned m, s2, f;
   uint8_t  req = VCD_REQ_NONE;
   uint16_t joy = (uint16_t)(args[0] | ((uint16_t)args[1] << 8));
   uint16_t edge;

   vcd.board_state = args[2];
   vcd.board_task  = args[3];

   /* Buttons are active-low on this link. Convert to active-high and take a
    * rising edge so one press produces one request. */
   joy  = (uint16_t)~joy;
   edge = (uint16_t)(joy & ~vcd.pad_prev);
   vcd.pad_prev = joy;

   /* The kernel probes first and will not proceed until the board says
    * whether it is there. Answer that before anything else. */
   if (!vcd.board_detected)
   {
      vcd.board_detected = true;
      req = (vcd.info.type != VCD_DISC_NONE) ? VCD_REQ_PRESENT
                                             : VCD_REQ_ABSENT;
   }
   else if (!vcd.board_started && vcd.info.num_entries)
   {
      /* Ask the host to move to the first chapter and start. The kernel turns
       * this into Setloc + SeekP against the position bytes below. */
      vcd.board_started = true;
      vcd.cur_entry     = 0;
      vcd.pos_lba       = vcd.info.entries[0].lba;
      req               = VCD_REQ_PLAY;
      VCD_Play(0);
   }
   else if (edge & (1u << 5))        /* Start  */
   {
      VCD_Pause();
      req = (vcd.xport == VCD_XPORT_PLAY) ? VCD_REQ_PLAY : VCD_REQ_PAUSE;
   }
   else if (edge & (1u << 1))        /* Triangle */
   {
      VCD_Stop();
      req = VCD_REQ_STOP;
   }
   else if (edge & (1u << 11))       /* Right */
   {
      VCD_NextTrack();
      req = VCD_REQ_PLAY;
   }
   else if (edge & (1u << 10))       /* Left */
   {
      VCD_PrevTrack();
      req = VCD_REQ_PLAY;
   }
   else if (edge & (1u << 4))        /* Cross  -> fast forward */
      req = VCD_REQ_FF;
   else if (edge & (1u << 3))        /* Circle -> fast reverse */
      req = VCD_REQ_FR;

   lba_to_msf(vcd.pos_lba, &m, &s2, &f);

   vcd.last_req = req;

   /* Position is BCD: the kernel hands these three bytes straight to Setloc,
    * which takes packed BCD and rejects anything else. */
   resp[0] = req;
   resp[1] = bin2bcd(m % 100u);
   resp[2] = bin2bcd(s2);
   resp[3] = bin2bcd(f);
   resp[4] = 0x00;
}

/* --------------------------------------------------------------------- */
/* Lifecycle                                                             */
/* --------------------------------------------------------------------- */

void VCD_SetMode(VCD_Mode mode)
{
   vcd.mode = mode;
   if (mode == VCD_MODE_OFF)
      VCD_Stop();
}

VCD_Mode VCD_GetMode(void)
{
   return vcd.mode;
}

void VCD_Init(void)
{
   memset(&vcd, 0, sizeof(vcd));

   vcd.fb   = (uint8_t *)calloc((size_t)VCD_MAX_W * VCD_MAX_H, 4);
   vcd.abuf = (int16_t *)calloc(VCD_AUDIO_FRAMES * 2, sizeof(int16_t));

   vcd.srate      = 44100;
   vcd.fb_is_xrgb = false;
   vcd.xport      = VCD_XPORT_STOP;
}

void VCD_Reset(void)
{
   vcd.xport     = VCD_XPORT_STOP;
   vcd.av_switch = false;
   vcd.cur_entry = 0;
   vcd.board_detected = false;
   vcd.board_started  = false;
   vcd.pad = vcd.pad_prev = 0;
   stream_reset();
}

void VCD_Kill(void)
{
   if (vcd.vid)
      rmpeg1_video_free(vcd.vid);
   if (vcd.aud)
      rmp3_stream_free(vcd.aud);
   if (vcd.ps)
      rmpeg1_ps_free(vcd.ps);
   vcd.vid = NULL;
   vcd.aud = NULL;
   vcd.ps  = NULL;

   free(vcd.fb);
   free(vcd.abuf);
   vcd.fb   = NULL;
   vcd.abuf = NULL;
}

/* --------------------------------------------------------------------- */
/* Save states                                                           */
/* --------------------------------------------------------------------- */

/* The decoder's internal state (reference pictures, bit reservoir, ring
 * contents) is deliberately NOT serialised: it is large, it is owned by a
 * third-party decoder with no state-export API, and a VCD is losslessly
 * re-seekable. We store the transport instead and re-prime the stream from the
 * saved LBA on load, which costs one GOP of latency and nothing else. */
int VCD_StateAction(void *sm, int load, int data_only)
{
   int      ret;
   uint8_t  xport   = (uint8_t)vcd.xport;
   uint8_t  mode    = (uint8_t)vcd.mode;
   uint32_t entry   = vcd.cur_entry;

   SFORMAT StateRegs[] =
   {
      SFVARN(mode,                "vcd_mode"),
      SFVARN(xport,               "vcd_xport"),
      SFVARN_BOOL(vcd.av_switch,  "vcd_av_switch"),
      SFVARN(vcd.pos_lba,         "vcd_pos_lba"),
      SFVARN(entry,               "vcd_cur_entry"),
      SFVARN(vcd.pad,             "vcd_pad"),
      SFVARN(vcd.pad_prev,        "vcd_pad_prev"),
      SFVARN(vcd.board_state,     "vcd_board_state"),
      SFVARN(vcd.board_task,      "vcd_board_task"),
      SFVARN(vcd.last_req,        "vcd_last_req"),
      SFVARN_BOOL(vcd.board_detected, "vcd_board_detected"),
      SFVARN_BOOL(vcd.board_started,  "vcd_board_started"),
      SFEND
   };

   ret = MDFNSS_StateAction(sm, load, data_only, StateRegs, "VCD");

   if (load)
   {
      /* The decoders' internal state -- reference pictures, the MP2 bit
       * reservoir, the demuxer window -- is deliberately not serialised. It
       * is large, it belongs to modules with no state-export API, and it is
       * entirely recoverable: a Video CD is losslessly re-readable, so the
       * cheapest correct thing is to drop it and let the drive re-prime the
       * pipeline from the restored position. The cost is one GOP of latency
       * after a load, and nothing else.
       *
       * This is also why it must not be skipped when the transport happens
       * to be stopped: a stale reference picture from before the load would
       * otherwise survive and be predicted from. */
      stream_reset();

      vcd.mode  = (VCD_Mode)mode;
      vcd.xport = (VCD_Transport)xport;

      /* A state saved against a different disc, or a corrupt one, must not
       * be able to index outside the chapter list. */
      vcd.cur_entry = (entry < vcd.info.num_entries) ? entry : 0;

      /* The pad shadow decides which buttons read as newly pressed. Restoring
       * it verbatim is right -- it is what the guest last saw -- but a load
       * mid-press must not synthesise a release-and-press on the next frame,
       * so the live pad is aligned to it and the next real edge comes from
       * the frontend. */
      vcd.pad = vcd.pad_prev;
   }

   return ret;
}
