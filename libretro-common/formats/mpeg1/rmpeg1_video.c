/* Copyright (C) 2010-2026 The RetroArch team
 *
 * ---------------------------------------------------------------------------
 * The following license statement only applies to this file (rmpeg1_video.c).
 * ---------------------------------------------------------------------------
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* MPEG-1 video decoder, intra pictures.
 *
 * Bitstream hierarchy (11172-2 clause 2.4.2):
 *
 *   sequence_header  B3h   geometry, frame rate, quantiser matrices
 *   group_of_pictures B8h  timecode; carries no decoding state
 *   picture          00h   temporal_reference, coding type
 *   slice         01h..AFh one row (or part of one) of macroblocks
 *   macroblock            address increment, type, then six blocks
 *   block                 DC differential then run/level AC pairs
 *
 * Every layer above macroblock is byte-aligned behind a start code, so the
 * parser resynchronises trivially; within a slice it is a pure bit stream.
 *
 * Style: C89, no declarations after statements, no // comments.
 */

#include <stdlib.h>
#include <string.h>

#include <retro_inline.h>

#include <formats/rmpeg1_video.h>

#include "rmpeg1_tables.h"

#define RMPEG1_START_PICTURE   0x00
#define RMPEG1_START_SLICE_LO  0x01
#define RMPEG1_START_SLICE_HI  0xAF
#define RMPEG1_START_USER_DATA 0xB2
#define RMPEG1_START_SEQUENCE  0xB3
#define RMPEG1_START_EXTENSION 0xB5
#define RMPEG1_START_SEQ_END   0xB7
#define RMPEG1_START_GOP       0xB8

#define RMPEG1_PIC_I           1
#define RMPEG1_PIC_P           2
#define RMPEG1_PIC_B           3
#define RMPEG1_PIC_D           4

#define RMPEG1_WINDOW          (1024 * 1024)
#define RMPEG1_MAX_W           4095
#define RMPEG1_MAX_H           2800

/* --------------------------------------------------------------------- */
/* Constants from the specification                                      */
/* --------------------------------------------------------------------- */

/* scan[0][v][u], H.262 Figure 7-2: zigzag position -> raster index. */
static const uint8_t rmpeg1_zigzag[64] =
{
    0,  1,  8, 16,  9,  2,  3, 10,
   17, 24, 32, 25, 18, 11,  4,  5,
   12, 19, 26, 33, 40, 48, 41, 34,
   27, 20, 13,  6,  7, 14, 21, 28,
   35, 42, 49, 56, 57, 50, 43, 36,
   29, 22, 15, 23, 30, 37, 44, 51,
   58, 59, 52, 45, 38, 31, 39, 46,
   53, 60, 61, 54, 47, 55, 62, 63
};

/* Default intra quantiser matrix, in raster order. */
static const uint8_t rmpeg1_default_intra[64] =
{
    8, 16, 19, 22, 26, 27, 29, 34,
   16, 16, 22, 24, 27, 29, 34, 37,
   19, 22, 26, 27, 29, 34, 34, 38,
   22, 22, 26, 27, 29, 34, 37, 40,
   22, 26, 27, 29, 32, 35, 40, 48,
   26, 27, 29, 32, 35, 40, 48, 58,
   26, 27, 29, 34, 38, 46, 56, 69,
   27, 29, 35, 38, 46, 56, 69, 83
};

/* frame_rate_code -> exact rational, H.262 Table 6-4. */
static const unsigned rmpeg1_fps_num[16] =
{ 0, 24000, 24, 25, 30000, 30, 50, 60000, 60, 0, 0, 0, 0, 0, 0, 0 };
static const unsigned rmpeg1_fps_den[16] =
{ 1,  1001,  1,  1,  1001,  1,  1,  1001,  1, 1, 1, 1, 1, 1, 1, 1 };

/* --------------------------------------------------------------------- */
/* State                                                                 */
/* --------------------------------------------------------------------- */

struct rmpeg1_video
{
   uint8_t  *buf;
   size_t    cap;
   size_t    wr;
   size_t    rd;         /* byte cursor  */
   unsigned  bit;        /* 0..7 within buf[rd] */

   bool      have_seq;
   unsigned  width, height;
   unsigned  mb_w, mb_h;
   unsigned  y_stride, c_stride;
   unsigned  fps_code, aspect_code;

   uint8_t   intra_q[64];
   uint8_t   non_intra_q[64];

   uint8_t  *plane_y;
   uint8_t  *plane_cb;
   uint8_t  *plane_cr;
   size_t    plane_bytes;

   /* per-picture */
   unsigned  temporal_ref;
   unsigned  coding_type;
   int       quant_scale;
   int       dc_pred[3];      /* Y, Cb, Cr */
   unsigned  mb_addr;

   uint32_t  skipped;
   uint32_t  errors;
};

/* --------------------------------------------------------------------- */
/* Bit reader                                                            */
/* --------------------------------------------------------------------- */

static INLINE size_t bits_left(const rmpeg1_video_t *v)
{
   return ((v->wr - v->rd) * 8) - v->bit;
}

static INLINE unsigned get_bit(rmpeg1_video_t *v)
{
   unsigned b;

   if (v->rd >= v->wr)
      return 0;

   b = (v->buf[v->rd] >> (7 - v->bit)) & 1u;

   if (++v->bit == 8)
   {
      v->bit = 0;
      v->rd++;
   }
   return b;
}

static uint32_t get_bits(rmpeg1_video_t *v, unsigned n)
{
   uint32_t r = 0;

   while (n--)
      r = (r << 1) | get_bit(v);
   return r;
}

/* Peek without consuming; returns bits left-aligned in the low n bits, zero
 * padded past the end of the buffer. */
static uint32_t peek_bits(const rmpeg1_video_t *v, unsigned n)
{
   size_t   rd  = v->rd;
   unsigned bit = v->bit;
   uint32_t r   = 0;

   while (n--)
   {
      unsigned b = 0;

      if (rd < v->wr)
         b = (v->buf[rd] >> (7 - bit)) & 1u;

      r = (r << 1) | b;

      if (++bit == 8)
      {
         bit = 0;
         rd++;
      }
   }
   return r;
}

static void byte_align(rmpeg1_video_t *v)
{
   if (v->bit)
   {
      v->bit = 0;
      v->rd++;
   }
}

/* --------------------------------------------------------------------- */
/* VLC decode                                                            */
/* --------------------------------------------------------------------- */

/* Linear scan over a table ordered by increasing code length. The tables are
 * small and this keeps the generated table verifiable by eye against the
 * specification; a lookup tree can replace it later without changing the
 * table itself. Returns the matching entry, or NULL. */
static const rmpeg1_vlc_t *vlc_decode(rmpeg1_video_t *v,
      const rmpeg1_vlc_t *tab)
{
   const rmpeg1_vlc_t *e;

   for (e = tab; e->len; e++)
   {
      if ((unsigned)e->len > bits_left(v))
         continue;
      if (peek_bits(v, (unsigned)e->len) == e->code)
      {
         (void)get_bits(v, (unsigned)e->len);
         return e;
      }
   }
   return NULL;
}

/* --------------------------------------------------------------------- */
/* Start codes                                                           */
/* --------------------------------------------------------------------- */

/* Find the next start code at or after the byte cursor. Returns its value
 * and leaves the cursor just past the 4-byte code, or -1 if none is buffered
 * (leaving the last three bytes unconsumed, since a code may straddle). */
static int next_start_code(rmpeg1_video_t *v)
{
   byte_align(v);

   while (v->rd + 4 <= v->wr)
   {
      if (     v->buf[v->rd    ] == 0x00
            && v->buf[v->rd + 1] == 0x00
            && v->buf[v->rd + 2] == 0x01)
      {
         int code = v->buf[v->rd + 3];
         v->rd   += 4;
         return code;
      }
      v->rd++;
   }
   return -1;
}

/* Offset of the next start code at or after `from`, or (size_t)-1. */
static size_t scan_start_code(const rmpeg1_video_t *v, size_t from)
{
   while (from + 4 <= v->wr)
   {
      if (     v->buf[from] == 0x00 && v->buf[from + 1] == 0x00
            && v->buf[from + 2] == 0x01)
         return from;
      from++;
   }
   return (size_t)-1;
}

static bool at_start_code(const rmpeg1_video_t *v)
{
   /* 23 zero bits then a one: inside a slice this is how the macroblock loop
    * knows it has run out of macroblocks. Checked on the bit cursor, since
    * the last macroblock may not have ended byte-aligned. */
   return peek_bits(v, 23) == 0;
}

/* --------------------------------------------------------------------- */
/* Plane management                                                      */
/* --------------------------------------------------------------------- */

static bool alloc_planes(rmpeg1_video_t *v)
{
   size_t ysz, csz;

   v->y_stride = v->mb_w * 16;
   v->c_stride = v->mb_w * 8;

   ysz = (size_t)v->y_stride * (v->mb_h * 16);
   csz = (size_t)v->c_stride * (v->mb_h * 8);

   free(v->plane_y);
   free(v->plane_cb);
   free(v->plane_cr);

   v->plane_y  = (uint8_t *)calloc(1, ysz);
   v->plane_cb = (uint8_t *)calloc(1, csz);
   v->plane_cr = (uint8_t *)calloc(1, csz);

   if (!v->plane_y || !v->plane_cb || !v->plane_cr)
   {
      free(v->plane_y);
      free(v->plane_cb);
      free(v->plane_cr);
      v->plane_y = v->plane_cb = v->plane_cr = NULL;
      v->plane_bytes = 0;
      return false;
   }

   v->plane_bytes = ysz;
   return true;
}

/* --------------------------------------------------------------------- */
/* IDCT                                                                  */
/* --------------------------------------------------------------------- */

/* Separable integer IDCT, AAN-style constants scaled to 11 fractional bits.
 * 11172-2 does not mandate a particular IDCT, only that it conform to
 * IEEE 1180-1990 accuracy; rows then columns with rounding at each stage is
 * within that. Deliberately integer: a float IDCT would make output depend
 * on host FPU rounding, which is not acceptable in a core whose determinism
 * is otherwise enforced. */

#define W1 2841   /* cos(1*pi/16) * sqrt(2) * 2048 */
#define W2 2676
#define W3 2408
#define W5 1609
#define W6 1108
#define W7 565

static void idct_row(const int16_t *in, int *b)
{
   int x0, x1, x2, x3, x4, x5, x6, x7, x8;

   x1 = (int)in[4] << 11;
   x2 = in[6];
   x3 = in[2];
   x4 = in[1];
   x5 = in[7];
   x6 = in[5];
   x7 = in[3];

   /* All-zero AC row: the DC term alone, replicated. Common enough in real
    * content that skipping the butterflies is worth the branch. */
   if (!(x1 | x2 | x3 | x4 | x5 | x6 | x7))
   {
      int dc = (int)in[0] << 3;
      b[0] = b[1] = b[2] = b[3] = b[4] = b[5] = b[6] = b[7] = dc;
      return;
   }

   x0 = ((int)in[0] << 11) + 128;

   x8 = W7 * (x4 + x5);
   x4 = x8 + (W1 - W7) * x4;
   x5 = x8 - (W1 + W7) * x5;
   x8 = W3 * (x6 + x7);
   x6 = x8 - (W3 - W5) * x6;
   x7 = x8 - (W3 + W5) * x7;

   x8 = x0 + x1;
   x0 -= x1;
   x1 = W6 * (x3 + x2);
   x2 = x1 - (W2 + W6) * x2;
   x3 = x1 + (W2 - W6) * x3;
   x1 = x4 + x6;
   x4 -= x6;
   x6 = x5 + x7;
   x5 -= x7;

   x7 = x8 + x3;
   x8 -= x3;
   x3 = x0 + x2;
   x0 -= x2;
   x2 = (int)(((int64_t)181 * (x4 + x5) + 128) >> 8);
   x4 = (int)(((int64_t)181 * (x4 - x5) + 128) >> 8);

   b[0] = (x7 + x1) >> 8;
   b[1] = (x3 + x2) >> 8;
   b[2] = (x0 + x4) >> 8;
   b[3] = (x8 + x6) >> 8;
   b[4] = (x8 - x6) >> 8;
   b[5] = (x0 - x4) >> 8;
   b[6] = (x3 - x2) >> 8;
   b[7] = (x7 - x1) >> 8;
}

static void idct_col_store(const int *b, uint8_t *dst, unsigned stride)
{
   int x0, x1, x2, x3, x4, x5, x6, x7, x8;
   int i;
   int out[8];

   x1 = b[8 * 4] << 8;
   x2 = b[8 * 6];
   x3 = b[8 * 2];
   x4 = b[8 * 1];
   x5 = b[8 * 7];
   x6 = b[8 * 5];
   x7 = b[8 * 3];

   if (!(x1 | x2 | x3 | x4 | x5 | x6 | x7))
   {
      int dc = (b[0] + 32) >> 6;
      for (i = 0; i < 8; i++)
         out[i] = dc;
   }
   else
   {
      x0 = (b[0] << 8) + 8192;

      x8 = W7 * (x4 + x5) + 4;
      x4 = (x8 + (W1 - W7) * x4) >> 3;
      x5 = (x8 - (W1 + W7) * x5) >> 3;
      x8 = W3 * (x6 + x7) + 4;
      x6 = (x8 - (W3 - W5) * x6) >> 3;
      x7 = (x8 - (W3 + W5) * x7) >> 3;

      x8 = x0 + x1;
      x0 -= x1;
      x1 = W6 * (x3 + x2) + 4;
      x2 = (x1 - (W2 + W6) * x2) >> 3;
      x3 = (x1 + (W2 - W6) * x3) >> 3;
      x1 = x4 + x6;
      x4 -= x6;
      x6 = x5 + x7;
      x5 -= x7;

      x7 = x8 + x3;
      x8 -= x3;
      x3 = x0 + x2;
      x0 -= x2;
      /* The rotation by 181/256 (i.e. sqrt(2)/2) is the one place where a
       * 32-bit product is not wide enough. By this point in the column pass
       * x4 and x5 have accumulated two stages of gain, so their difference
       * can reach ~1.3e7; multiplied by 181 that is ~2.3e9, past INT32_MAX.
       *
       * Legal MPEG-1 content never gets there -- the dequantiser saturates
       * and real coefficients are nothing like uniformly maximal -- which is
       * why the usual 32-bit formulation survives in practice. An IEEE 1180
       * sweep at L=300 reaches it in roughly one block in ten thousand, and
       * the wrap turns a saturated-white sample into black. Widen the two
       * products rather than hope. */
      x2 = (int)(((int64_t)181 * (x4 + x5) + 128) >> 8);
      x4 = (int)(((int64_t)181 * (x4 - x5) + 128) >> 8);

      out[0] = (x7 + x1) >> 14;
      out[1] = (x3 + x2) >> 14;
      out[2] = (x0 + x4) >> 14;
      out[3] = (x8 + x6) >> 14;
      out[4] = (x8 - x6) >> 14;
      out[5] = (x0 - x4) >> 14;
      out[6] = (x3 - x2) >> 14;
      out[7] = (x7 - x1) >> 14;
   }

   for (i = 0; i < 8; i++)
   {
      /* An intra block reconstructs the sample value itself, not a residual:
       * the DC coefficient already carries the block mean (predictor 1024,
       * which the IDCT scales back to 128). Adding a 128 bias here -- as a
       * non-intra block would need before summing with its prediction --
       * shifts the whole picture and clips the highlights. */
      int p = out[i];

      if (p < 0)
         p = 0;
      else if (p > 255)
         p = 255;

      dst[(size_t)i * stride] = (uint8_t)p;
   }
}

static void idct_block(const int16_t *blk, uint8_t *dst, unsigned stride)
{
   /* Row results go to a 32-bit scratch rather than back into the 16-bit
    * coefficient block. Storing them as int16 is the usual shortcut and is
    * safe for real content, but it overflows on pathological input -- an
    * IEEE 1180 sweep at L=300 turns bright blocks into dark ones -- and 256
    * bytes of scratch is cheap next to getting that wrong. */
   int tmp[64];
   int i;

   for (i = 0; i < 8; i++)
      idct_row(blk + i * 8, tmp + i * 8);
   for (i = 0; i < 8; i++)
      idct_col_store(tmp + i, dst + i, stride);
}

/* --------------------------------------------------------------------- */
/* Block layer                                                           */
/* --------------------------------------------------------------------- */

/* Decode one intra block into blk[64] in raster order.
 *
 * 11172-2 intra dequantisation:
 *   DC:  rec = 8 * QF[0]
 *   AC:  rec = (2 * QF * quant_scale * W) / 16, then made odd by moving one
 *        step toward zero when it comes out even and non-zero, then clamped
 *        to [-2048, 2047].
 *
 * The oddification is the mismatch control: it is what keeps encoder and
 * decoder IDCTs from drifting apart over a run of predicted pictures. It
 * applies to AC only; the DC term is exact.
 */
static bool decode_intra_block(rmpeg1_video_t *v, int16_t *blk, int cc)
{
   const rmpeg1_vlc_t *e;
   int      dc_size;
   int      diff = 0;
   unsigned idx  = 0;   /* zigzag position */

   memset(blk, 0, sizeof(int16_t) * 64);

   e = vlc_decode(v, cc == 0 ? rmpeg1_vlc_dc_lum : rmpeg1_vlc_dc_chr);
   if (!e)
      return false;
   dc_size = e->a;

   if (dc_size)
   {
      int val = (int)get_bits(v, (unsigned)dc_size);

      /* The differential is stored without a sign bit: the top bit being
       * clear means negative, and the value is biased accordingly. */
      if (val < (1 << (dc_size - 1)))
         val -= (1 << dc_size) - 1;
      diff = val;
   }

   v->dc_pred[cc] += diff;
   blk[0] = (int16_t)(v->dc_pred[cc] * 8);

   for (;;)
   {
      int run, level, sign, pos, rec;

      if (peek_bits(v, RMPEG1_DCT_EOB_LEN) == RMPEG1_DCT_EOB_CODE)
      {
         (void)get_bits(v, RMPEG1_DCT_EOB_LEN);
         break;
      }

      if (peek_bits(v, RMPEG1_DCT_ESCAPE_LEN) == RMPEG1_DCT_ESCAPE_CODE)
      {
         (void)get_bits(v, RMPEG1_DCT_ESCAPE_LEN);
         run   = (int)get_bits(v, 6);
         level = (int)get_bits(v, 8);

         /* 11172-2 escape levels: an 8-bit field, with 00h and 80h meaning
          * that a second 8-bit field follows to extend the range. */
         if (level == 0)
            level = (int)get_bits(v, 8);
         else if (level == 128)
            level = (int)get_bits(v, 8) - 256;
         else if (level > 128)
            level -= 256;
      }
      else
      {
         /* Only the second and later coefficients of an intra block use this
          * table; the DC term was handled above, so the "first coefficient"
          * spelling of (0,1) never occurs here. */
         e = vlc_decode(v, rmpeg1_vlc_dct_next);
         if (!e)
            return false;
         run   = e->a;
         level = e->b;
         sign  = (int)get_bit(v);
         if (sign)
            level = -level;
      }

      idx += (unsigned)run + 1;
      if (idx > 63)
         return false;

      pos = rmpeg1_zigzag[idx];

      rec = (2 * level * v->quant_scale * (int)v->intra_q[pos]) / 16;

      if (rec > 0 && !(rec & 1))
         rec--;
      else if (rec < 0 && !(rec & 1))
         rec++;

      if (rec >  2047) rec =  2047;
      if (rec < -2048) rec = -2048;

      blk[pos] = (int16_t)rec;
   }

   return true;
}

/* --------------------------------------------------------------------- */
/* Macroblock layer                                                      */
/* --------------------------------------------------------------------- */

static void mb_plane_ptrs(rmpeg1_video_t *v, unsigned addr,
      uint8_t **y, uint8_t **cb, uint8_t **cr)
{
   unsigned mx = addr % v->mb_w;
   unsigned my = addr / v->mb_w;

   *y  = v->plane_y  + (size_t)my * 16 * v->y_stride + (size_t)mx * 16;
   *cb = v->plane_cb + (size_t)my *  8 * v->c_stride + (size_t)mx *  8;
   *cr = v->plane_cr + (size_t)my *  8 * v->c_stride + (size_t)mx *  8;
}

static bool decode_intra_macroblock(rmpeg1_video_t *v, unsigned addr)
{
   int16_t  blk[64];
   uint8_t *py, *pcb, *pcr;
   int      i;

   if (addr >= v->mb_w * v->mb_h)
      return false;

   mb_plane_ptrs(v, addr, &py, &pcb, &pcr);

   for (i = 0; i < 4; i++)
   {
      uint8_t *dst = py + (size_t)(i >> 1) * 8 * v->y_stride + (size_t)(i & 1) * 8;

      if (!decode_intra_block(v, blk, 0))
         return false;
      idct_block(blk, dst, v->y_stride);
   }

   if (!decode_intra_block(v, blk, 1))
      return false;
   idct_block(blk, pcb, v->c_stride);

   if (!decode_intra_block(v, blk, 2))
      return false;
   idct_block(blk, pcr, v->c_stride);

   return true;
}

/* --------------------------------------------------------------------- */
/* Slice layer                                                           */
/* --------------------------------------------------------------------- */

static void reset_dc_predictors(rmpeg1_video_t *v)
{
   /* 11172-2: predictors reset to 1024 (i.e. 128 << 3) at the start of every
    * slice, so a lost slice cannot propagate a DC error across the picture. */
   v->dc_pred[0] = v->dc_pred[1] = v->dc_pred[2] = 1024 / 8;
}

static bool decode_slice(rmpeg1_video_t *v, unsigned vertical_pos)
{
   unsigned mb_row;

   if (vertical_pos < 1 || vertical_pos > v->mb_h)
      return false;

   mb_row       = vertical_pos - 1;
   v->quant_scale = (int)get_bits(v, 5);
   if (v->quant_scale == 0)
      return false;

   /* extra_bit_slice: a run of optional 8-bit extension bytes, each preceded
    * by a set bit, terminated by a clear bit. */
   while (get_bit(v))
      (void)get_bits(v, 8);

   reset_dc_predictors(v);
   v->mb_addr = mb_row * v->mb_w - 1;

   for (;;)
   {
      const rmpeg1_vlc_t *e;
      unsigned increment = 0;

      if (bits_left(v) < 24)
         return false;
      if (at_start_code(v))
         break;

      /* macroblock_escape adds 33 and may repeat. macroblock_stuffing is
       * MPEG-1 only -- MPEG-2 dropped it, so H.262 Table B.1 does not list
       * it -- and is simply discarded. Both are 11 bits and share the
       * 00000001 prefix, so they must be tested before the B.1 lookup. */
      for (;;)
      {
         if (peek_bits(v, RMPEG1_MBA_STUFFING_LEN) == RMPEG1_MBA_STUFFING_CODE)
         {
            (void)get_bits(v, RMPEG1_MBA_STUFFING_LEN);
            continue;
         }
         if (peek_bits(v, RMPEG1_MBA_ESCAPE_LEN) == RMPEG1_MBA_ESCAPE_CODE)
         {
            (void)get_bits(v, RMPEG1_MBA_ESCAPE_LEN);
            increment += 33;
            continue;
         }
         break;
      }

      e = vlc_decode(v, rmpeg1_vlc_mba);
      if (!e)
         return false;
      increment += (unsigned)e->a;

      /* In an I-picture every macroblock is coded, so an increment above one
       * would imply skipped macroblocks, which 11172-2 forbids here. Treat
       * it as corruption rather than leaving a hole in the plane. */
      v->mb_addr += increment;

      /* macroblock_type, I-picture: '1' = Intra, '01' = Intra with a new
       * quantiser_scale (H.262 Table B.2). */
      if (get_bit(v))
      {
         /* Intra */
      }
      else
      {
         if (!get_bit(v))
            return false;
         v->quant_scale = (int)get_bits(v, 5);
         if (v->quant_scale == 0)
            return false;
      }

      if (!decode_intra_macroblock(v, v->mb_addr))
         return false;
   }

   return true;
}

/* --------------------------------------------------------------------- */
/* Headers                                                               */
/* --------------------------------------------------------------------- */

static bool parse_sequence_header(rmpeg1_video_t *v)
{
   unsigned w, h, i;

   if (bits_left(v) < 64)
      return false;

   w = get_bits(v, 12);
   h = get_bits(v, 12);

   if (w == 0 || h == 0 || w > RMPEG1_MAX_W || h > RMPEG1_MAX_H)
      return false;

   v->aspect_code = get_bits(v, 4);
   v->fps_code    = get_bits(v, 4);

   (void)get_bits(v, 18);        /* bit_rate            */
   (void)get_bit(v);             /* marker              */
   (void)get_bits(v, 10);        /* vbv_buffer_size     */
   (void)get_bit(v);             /* constrained_params  */

   if (get_bit(v))
   {
      for (i = 0; i < 64; i++)
         v->intra_q[rmpeg1_zigzag[i]] = (uint8_t)get_bits(v, 8);
   }
   if (get_bit(v))
   {
      for (i = 0; i < 64; i++)
         v->non_intra_q[rmpeg1_zigzag[i]] = (uint8_t)get_bits(v, 8);
   }

   if (!v->have_seq || w != v->width || h != v->height)
   {
      v->width  = w;
      v->height = h;
      v->mb_w   = (w + 15) / 16;
      v->mb_h   = (h + 15) / 16;

      if (!alloc_planes(v))
         return false;
   }

   v->have_seq = true;
   return true;
}

static bool parse_picture_header(rmpeg1_video_t *v)
{
   if (bits_left(v) < 29)
      return false;

   v->temporal_ref = get_bits(v, 10);
   v->coding_type  = get_bits(v, 3);
   (void)get_bits(v, 16);        /* vbv_delay */

   if (v->coding_type == RMPEG1_PIC_P || v->coding_type == RMPEG1_PIC_B)
   {
      (void)get_bit(v);          /* full_pel_forward_vector */
      (void)get_bits(v, 3);      /* forward_f_code          */
   }
   if (v->coding_type == RMPEG1_PIC_B)
   {
      (void)get_bit(v);          /* full_pel_backward_vector */
      (void)get_bits(v, 3);      /* backward_f_code          */
   }

   while (get_bit(v))
      (void)get_bits(v, 8);      /* extra_information_picture */

   return true;
}

/* --------------------------------------------------------------------- */
/* Public entry points                                                   */
/* --------------------------------------------------------------------- */

rmpeg1_video_t *rmpeg1_video_init(void)
{
   rmpeg1_video_t *v = (rmpeg1_video_t *)calloc(1, sizeof(*v));

   if (!v)
      return NULL;

   v->buf = (uint8_t *)malloc(RMPEG1_WINDOW);
   if (!v->buf)
   {
      free(v);
      return NULL;
   }
   v->cap = RMPEG1_WINDOW;

   memcpy(v->intra_q, rmpeg1_default_intra, 64);
   memset(v->non_intra_q, 16, 64);

   return v;
}

void rmpeg1_video_free(rmpeg1_video_t *v)
{
   if (!v)
      return;
   free(v->buf);
   free(v->plane_y);
   free(v->plane_cb);
   free(v->plane_cr);
   free(v);
}

void rmpeg1_video_reset(rmpeg1_video_t *v)
{
   if (!v)
      return;
   v->rd = v->wr = 0;
   v->bit = 0;
}

static void compact(rmpeg1_video_t *v)
{
   if (v->rd == 0)
      return;
   if (v->wr > v->rd)
      memmove(v->buf, v->buf + v->rd, v->wr - v->rd);
   v->wr -= v->rd;
   v->rd  = 0;
}

size_t rmpeg1_video_write(rmpeg1_video_t *v, const uint8_t *data, size_t len)
{
   size_t room;

   if (!v || !data || len == 0)
      return 0;

   if (v->cap - v->wr < len)
   {
      /* Only safe when the bit cursor is byte aligned and nothing is
       * mid-parse; decode() always leaves it that way between pictures. */
      if (v->bit == 0)
         compact(v);
   }

   room = v->cap - v->wr;
   if (len > room)
      len = room;

   if (len)
   {
      memcpy(v->buf + v->wr, data, len);
      v->wr += len;
   }
   return len;
}

int rmpeg1_video_decode(rmpeg1_video_t *v, rmpeg1_video_frame_t *out)
{
   if (!v || !out)
      return 0;

   for (;;)
   {
      size_t save_rd;
      int    code;

      save_rd = v->rd;
      code    = next_start_code(v);

      if (code < 0)
      {
         v->rd = save_rd;
         return 0;
      }

      switch (code)
      {
         case RMPEG1_START_SEQUENCE:
            if (!parse_sequence_header(v))
            {
               v->rd = save_rd + 4;
               v->bit = 0;
               v->errors++;
            }
            break;

         case RMPEG1_START_GOP:
            if (bits_left(v) < 27)
            {
               v->rd = save_rd;
               return 0;
            }
            (void)get_bits(v, 27);
            break;

         case RMPEG1_START_PICTURE:
            if (!parse_picture_header(v))
            {
               v->rd = save_rd;
               return 0;
            }
            if (v->coding_type != RMPEG1_PIC_I)
               v->skipped++;
            break;

         case RMPEG1_START_USER_DATA:
         case RMPEG1_START_EXTENSION:
            /* Consumed by the next start-code hunt. */
            break;

         case RMPEG1_START_SEQ_END:
            break;

         default:
            if (     code >= RMPEG1_START_SLICE_LO
                  && code <= RMPEG1_START_SLICE_HI)
            {
               size_t end;

               if (!v->have_seq)
                  break;

               /* A slice must be decoded atomically. The macroblock layer is
                * a pure bit stream with no length prefix anywhere, so there
                * is no way to suspend in the middle of one and resume later
                * -- and running off the end of the buffer would look exactly
                * like corrupt data. Wait until the whole slice is present,
                * which is known once the following start code is buffered.
                *
                * Getting this wrong is not a crash, it is worse: the decoder
                * reads zero-padding past the write cursor as though it were
                * bitstream, fails a VLC lookup deep inside a block, and
                * reports a table error for what is really a starved buffer. */
               end = scan_start_code(v, v->rd);
               if (end == (size_t)-1)
               {
                  v->rd  = save_rd;
                  v->bit = 0;
                  return 0;
               }

               /* Only intra pictures are reconstructed for now; other slices
                * are stepped over by the start-code scan. */
               if (v->coding_type != RMPEG1_PIC_I)
               {
                  v->rd  = end;
                  v->bit = 0;
                  break;
               }

               if (!decode_slice(v, (unsigned)code))
                  v->errors++;

               /* Resynchronise on the slice boundary we already found rather
                * than wherever the bit cursor stopped: a slice that ended
                * early must not drag the next one out of alignment. */
               v->rd  = end;
               v->bit = 0;

               {
                  size_t p = end;

                  if (p + 4 > v->wr)
                     return 0;      /* need more data to know */

                  if (     v->buf[p + 3] >= RMPEG1_START_SLICE_LO
                        && v->buf[p + 3] <= RMPEG1_START_SLICE_HI)
                     break;         /* same picture continues */

                  out->y            = v->plane_y;
                  out->cb           = v->plane_cb;
                  out->cr           = v->plane_cr;
                  out->width        = v->width;
                  out->height       = v->height;
                  out->y_stride     = v->y_stride;
                  out->c_stride     = v->c_stride;
                  out->temporal_ref = v->temporal_ref;
                  out->coding_type  = (uint8_t)v->coding_type;
                  return 1;
               }
            }
            break;
      }
   }
}

bool rmpeg1_video_has_sequence(const rmpeg1_video_t *v)
{
   return v ? v->have_seq : false;
}

unsigned rmpeg1_video_width(const rmpeg1_video_t *v)
{
   return v ? v->width : 0;
}

unsigned rmpeg1_video_height(const rmpeg1_video_t *v)
{
   return v ? v->height : 0;
}

void rmpeg1_video_framerate(const rmpeg1_video_t *v,
      unsigned *num, unsigned *den)
{
   unsigned c = (v && v->fps_code < 16) ? v->fps_code : 0;

   if (num)
      *num = rmpeg1_fps_num[c];
   if (den)
      *den = rmpeg1_fps_den[c];
}

unsigned rmpeg1_video_aspect_code(const rmpeg1_video_t *v)
{
   return v ? v->aspect_code : 0;
}

uint32_t rmpeg1_video_skipped(const rmpeg1_video_t *v)
{
   return v ? v->skipped : 0;
}

uint32_t rmpeg1_video_errors(const rmpeg1_video_t *v)
{
   return v ? v->errors : 0;
}
