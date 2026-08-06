/* Video CD support for the SCPH-5903. See vcd.h for the hardware notes.
 *
 * Style: C89, /-* *-/ comments only, no C99 declarations-after-statement,
 * MSVC-clean. The only C99-ish dependency is the vendored pl_mpeg.h, which
 * lives under deps/ and is compiled as its own translation unit.
 */

#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <retro_inline.h>

#include "vcd.h"
#include "../../libretro_cbs.h"
#include "../cdrom/cdromif.h"

#define PLM_NO_STDIO 1
#include "../../deps/pl_mpeg/pl_mpeg.h"

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

/* Streaming window handed to pl_mpeg. Four seconds of 1.15 Mbit/s VCD video
 * plus slack, rounded to a whole number of Form 2 payloads. */
#define VCD_RING_BYTES    (VCD_FORM2_PAYLOAD * 512)

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
   plm_buffer_t *buf;
   plm_t        *plm;

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

VCD_DiscType VCD_ProbeDisc(VCD_DiscInfo *out_info)
{
   uint8_t      sec[2048];
   VCD_DiscInfo di;
   CDIF        *c = CDIF_GetCurrent();
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

static void frame_to_fb(const plm_frame_t *fr)
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
      const uint8_t *yr = fr->y.data  + (size_t)y * fr->y.width;
      const uint8_t *cb = fr->cb.data + (size_t)(y >> 1) * fr->cb.width;
      const uint8_t *cr = fr->cr.data + (size_t)(y >> 1) * fr->cr.width;
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

static void push_audio(const plm_samples_t *s)
{
   unsigned i;

   for (i = 0; i < PLM_AUDIO_SAMPLES_PER_FRAME; i++)
   {
      size_t nxt = (vcd.awr + 1) % VCD_AUDIO_FRAMES;
      float  l   = s->interleaved[i * 2 + 0];
      float  r   = s->interleaved[i * 2 + 1];
      int    li, ri;

      if (nxt == vcd.ard)
         break;                  /* overrun; drop rather than stall decode */

      li = (int)(l * 32767.0f);
      ri = (int)(r * 32767.0f);
      if (li >  32767) li =  32767;
      if (li < -32768) li = -32768;
      if (ri >  32767) ri =  32767;
      if (ri < -32768) ri = -32768;

      vcd.abuf[vcd.awr * 2 + 0] = (int16_t)li;
      vcd.abuf[vcd.awr * 2 + 1] = (int16_t)ri;
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
   if (vcd.plm)
   {
      plm_destroy(vcd.plm);      /* also destroys the attached buffer */
      vcd.plm = NULL;
      vcd.buf = NULL;
   }
   else if (vcd.buf)
   {
      plm_buffer_destroy(vcd.buf);
      vcd.buf = NULL;
   }

   vcd.headers_ok = false;
   vcd.fb_valid   = false;
   vcd.ard = vcd.awr = 0;

   vcd.buf = plm_buffer_create_with_capacity(VCD_RING_BYTES);
   if (!vcd.buf)
      return;

   /* plm_buffer_create_with_capacity() already sets discard-read-bytes, so
    * the window slides on its own: bytes the decoder has consumed are
    * reclaimed and plm_buffer_write() rejects the overflow rather than
    * growing without bound. A VCD is CBR, so the decoder never falls more
    * than a fraction of a second behind the drive; dropping the excess is
    * the correct failure mode when it does. */
   vcd.plm = plm_create_with_buffer(vcd.buf, 1);
   if (!vcd.plm)
      return;

   plm_set_video_enabled(vcd.plm, 1);
   plm_set_audio_enabled(vcd.plm, 1);
   plm_set_audio_stream(vcd.plm, 0);
   plm_set_loop(vcd.plm, 0);
}

void VCD_FeedSector(const uint8_t *raw2352, uint32_t lba)
{
   const uint8_t *sub;
   const uint8_t *payload;

   if (vcd.mode == VCD_MODE_OFF || !vcd.buf)
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

   plm_buffer_write(vcd.buf, (uint8_t *)payload, VCD_FORM2_PAYLOAD);
   vcd.pos_lba = lba;

   if (!vcd.headers_ok && vcd.plm && plm_has_headers(vcd.plm))
   {
      vcd.headers_ok = true;
      vcd.srate      = (unsigned)plm_get_samplerate(vcd.plm);
      if (!vcd.srate)
         vcd.srate = 44100;
   }
}

bool VCD_RunFrame(void)
{
   plm_frame_t   *fr;
   plm_samples_t *sm;
   bool           got = false;

   if (!vcd.plm || !vcd.headers_ok)
      return false;
   if (vcd.xport != VCD_XPORT_PLAY)
      return false;

   /* Audio first: MP2 frames are 1152 samples, so several may be pending for
    * each picture. Drain what is available, then take at most one picture so
    * that output stays locked to the frontend's frame cadence. */
   while ((sm = plm_decode_audio(vcd.plm)) != NULL)
   {
      push_audio(sm);
      if (((vcd.awr - vcd.ard + VCD_AUDIO_FRAMES) % VCD_AUDIO_FRAMES) >
          (VCD_AUDIO_FRAMES / 2))
         break;
   }

   fr = plm_decode_video(vcd.plm);
   if (fr)
   {
      frame_to_fb(fr);
      got = true;
   }

   return got;
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
   if (vcd.plm && vcd.headers_ok)
   {
      double f = plm_get_framerate(vcd.plm);
      if (f > 1.0)
         return f;
   }
   return vcd.info.pal ? 25.0 : (30000.0 / 1001.0);
}

/* --------------------------------------------------------------------- */
/* Transport                                                             */
/* --------------------------------------------------------------------- */

void VCD_Play(unsigned entry_index)
{
   if (entry_index >= vcd.info.num_entries)
      return;
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
   vcd.pos_lba = lba;
   stream_reset();
   vcd.xport = VCD_XPORT_PLAY;
}

VCD_Transport VCD_GetTransport(void) { return vcd.xport; }
uint32_t      VCD_GetPositionLBA(void) { return vcd.pos_lba; }

void VCD_SetPadState(uint16_t buttons)
{
   vcd.pad_prev = vcd.pad;
   vcd.pad      = buttons;
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
 * CAUTION: the real SC430924 firmware only relays five opaque bytes between
 * the kernel and the daughterboard; the daughterboard's own firmware has never
 * been dumped, so the meaning of State/Task and of req is NOT publicly known.
 * The encoding below is ours. It is sufficient for the kernel's player to see
 * a live, well-formed board and a sane clock, but the kernel's own state
 * machine may still not advance exactly as it would on real hardware. Treat
 * BOARD mode as best-effort and prefer HLE mode until someone captures a real
 * SIO trace. */
#define VCD_REQ_NONE     0x00
#define VCD_REQ_PLAY     0x01
#define VCD_REQ_PAUSE    0x02
#define VCD_REQ_STOP     0x03
#define VCD_REQ_SEEK     0x04
#define VCD_REQ_NEXT     0x05
#define VCD_REQ_PREV     0x06
#define VCD_REQ_PRESENT  0x40    /* board answered            */
#define VCD_REQ_DISC_OK  0x80    /* a VCD is actually mounted */

void VCD_SioExchange(const uint8_t *args, uint8_t *resp)
{
   unsigned m, s, f;
   uint8_t  req = VCD_REQ_PRESENT;
   uint16_t joy = (uint16_t)(args[0] | ((uint16_t)args[1] << 8));
   uint16_t edge;

   vcd.board_state = args[2];
   vcd.board_task  = args[3];

   /* Buttons are active-low on this link. Convert to active-high and take a
    * rising edge so one press produces one request. */
   joy  = (uint16_t)~joy;
   edge = (uint16_t)(joy & ~vcd.pad_prev);
   vcd.pad_prev = joy;

   if (vcd.info.type != VCD_DISC_NONE)
      req |= VCD_REQ_DISC_OK;

   if      (edge & (1u << 5))  { req |= VCD_REQ_PLAY;  VCD_Play(vcd.cur_entry); }
   else if (edge & (1u << 4))  { req |= VCD_REQ_PAUSE; VCD_Pause(); }
   else if (edge & (1u << 1))  { req |= VCD_REQ_STOP;  VCD_Stop();  }
   else if (edge & (1u << 11)) { req |= VCD_REQ_NEXT;  VCD_NextTrack(); }
   else if (edge & (1u << 10)) { req |= VCD_REQ_PREV;  VCD_PrevTrack(); }

   lba_to_msf(vcd.pos_lba, &m, &s, &f);

   vcd.last_req = req;

   resp[0] = req;
   resp[1] = bin2bcd(m % 100u);
   resp[2] = bin2bcd(s);
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
   vcd.pad = vcd.pad_prev = 0;
   stream_reset();
}

void VCD_Kill(void)
{
   if (vcd.plm)
      plm_destroy(vcd.plm);
   else if (vcd.buf)
      plm_buffer_destroy(vcd.buf);
   vcd.plm = NULL;
   vcd.buf = NULL;

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
   /* Placeholder: wire into the core's SFORMAT machinery alongside the other
    * PSX subsystems. Fields that must round-trip:
    *   mode, av_switch, xport, pos_lba, cur_entry, pad, pad_prev,
    *   board_state, board_task, last_req
    * On load: stream_reset() and let VCD_FeedSector re-fill from pos_lba. */
   (void)sm;
   (void)data_only;

   if (load)
      stream_reset();

   return 1;
}
