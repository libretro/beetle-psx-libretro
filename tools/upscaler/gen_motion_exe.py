#!/usr/bin/env python3
"""PS-X EXE: a scene for the temporal upscaler lane (tools/upscaler/lanes.sh).

One iteration per vertical blank, double buffered (draw at y 0 / 256 with
the drawing area set to that buffer, display the other), 320x240:
  - a dark blue background fill
  - an opaque flat triangle sliding right 2 pixels a frame (wrapping)
  - an opaque Gouraud quad sliding down 1 pixel a frame (wrapping)
  - a monochrome rectangle (a sprite to the GPU) sliding up and left
  - an opaque flat triangle put through the GTE (RTPS, 512 units deep,
    H 512: a pixel a unit) sliding right a pixel a frame, sent as games
    send theirs, by DMA from a packet in RAM: with PGXP on, its vertices
    carry a depth
  - a semi-transparent quad (B/2 + F/2) standing still at the bottom right:
    the only translucent thing, for the transparency mask
Each frame it also stores its iteration count at 0x801FFFF0, which is not
needed by the checks but keeps the loop honest.
usage: gen_motion_exe.py out.exe"""
import struct, sys
BASE = 0xA0010000
GP0 = 0x1F801810
I_STAT = 0x1F801070

def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def andi(rt, rs, imm):  return (0xC << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def sll(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6)
def or_(rd, rs, rt):    return (rs << 21) | (rt << 16) | (rd << 11) | 0x25
def addu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x21
def subu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x23
def and_(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x24
def sltu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x2B
def mtc0(rt, rd):       return 0x40800000 | (rt << 16) | (rd << 11)
def mtc2(rt, rd):       return 0x48800000 | (rt << 16) | (rd << 11)
def ctc2(rt, rd):       return 0x48C00000 | (rt << 16) | (rd << 11)
def mfc2(rt, rd):       return 0x48000000 | (rt << 16) | (rd << 11)
RTPS = 0x4A180001
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def lw(rt, rs, off):    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def beq(rs, rt, off):   return (4 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def bne(rs, rt, off):   return (5 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
NOP = 0
ZERO, T0, T1, T2, T3, T4, T5, T6, T7 = 0, 8, 9, 10, 11, 12, 13, 14, 15
S1, S2, S3, S4, S5, S6, S7 = 17, 18, 19, 20, 21, 22, 23
code = []
def emit(w): code.append(w)
def here(): return BASE + 4 * len(code)
def li32(r, v): emit(lui(r, (v >> 16) & 0xffff)); emit(ori(r, r, v & 0xffff))
def branch(op, rs, rt, target): emit(op(rs, rt, (target - here() - 4) >> 2)); emit(NOP)
def wait_bit(bit):
    li32(T7, 1 << bit)
    w = here()
    emit(lw(T0, S1, 4)); emit(NOP)
    emit(and_(T0, T0, T7))
    branch(beq, T0, ZERO, w)
def gp0(v): li32(T0, v); emit(sw(T0, S1, 0))
def gp0r(r): emit(sw(r, S1, 0))
def cmd(v): wait_bit(26); gp0(v)
def gp1(v): li32(T0, v); emit(sw(T0, S1, 4))
def xy(x, y): return (y << 16) | x

def wrap(dst, src, period):
    """dst = src mod period (period a power of two)"""
    emit(andi(dst, src, period - 1))

def vertex(reg_x, reg_y, dx, dy):
    """GP0 vertex word: (reg_y + dy) << 16 | (reg_x + dx), into T1"""
    emit(addiu(T2, reg_y, dy)); emit(sll(T2, T2, 16))
    emit(addiu(T3, reg_x, dx)); emit(or_(T1, T2, T3))
    gp0r(T1)

li32(S1, GP0)
# the GTE: on (COP2 usable), rotation the identity, no translation,
# H 512, the screen offset (10, 100)
li32(T0, 0x40000000); emit(mtc0(T0, 12))
li32(T0, 0x1000); emit(ctc2(T0, 0)); emit(ctc2(T0, 2)); emit(ctc2(T0, 4))
emit(ctc2(ZERO, 1)); emit(ctc2(ZERO, 3)); emit(ctc2(ZERO, 5)); emit(ctc2(ZERO, 6)); emit(ctc2(ZERO, 7))
li32(T0, 10 << 16); emit(ctc2(T0, 24))
li32(T0, 100 << 16); emit(ctc2(T0, 25))
li32(T0, 512); emit(ctc2(T0, 26))
emit(ctc2(ZERO, 27)); emit(ctc2(ZERO, 28))
# DMA channel 2 (GPU) enabled, the GPU taking DMA into GP0
li32(T4, 0x1F8010F0); emit(lw(T0, T4, 0)); emit(NOP); emit(ori(T0, T0, 0x0800)); emit(sw(T0, T4, 0))
gp1(0x04000002)
li32(S7, 0x80100000)                    # the packet
gp1(0x00000000); gp1(0x03000000); gp1(0x08000001)
gp1(0x06000000 | (0x260 + 320 * 8) << 12 | 0x260)
gp1(0x07000000 | (0x10 + 240) << 10 | 0x10)
gp0(0xE1000000)                         # no dither (it would lock the colours to the pixels), B/2 + F/2
gp0(0xE3000000)
gp0(0xE4000000 | (239 << 10) | 319)
gp0(0xE5000000)
emit(addiu(S5, ZERO, 0))                # frame counter

frame = here()
# the buffer drawn: y = (frame & 1) * 256; the drawing area and offset follow
emit(andi(S6, S5, 1)); emit(sll(S6, S6, 8))           # S6 = 0 or 256
emit(sll(T1, S6, 10)); li32(T2, 0xE3000000); emit(or_(T3, T1, T2)); wait_bit(26); gp0r(T3)
li32(T2, 0xE4000000 | (239 << 10) | 319); emit(or_(T3, T1, T2)); wait_bit(26); gp0r(T3)
emit(sll(T1, S6, 11)); li32(T2, 0xE5000000); emit(or_(T3, T1, T2)); wait_bit(26); gp0r(T3)
# display the other buffer
li32(T2, 0x05040000)
emit(andi(T3, S5, 1)); branch(beq, T3, ZERO, here() + 16)
li32(T2, 0x05000000)
emit(sw(T2, S1, 4))
# background fill of the buffer drawn (fill ignores the offset)
emit(sll(T1, S6, 16))
cmd(0x02401810); gp0r(T1); gp0(xy(320, 240))
# the triangle: x = frame * 2 mod 256, from 20
wrap(S2, S5, 128); emit(sll(S2, S2, 1))
cmd(0x2000C0E0)                                        # flat opaque, light blue
vertex(S2, ZERO, 20, 20)
vertex(S2, ZERO, 84, 36)
vertex(S2, ZERO, 28, 92)
# the Gouraud quad: y = frame mod 128, from 100 to 148, x 40..104
wrap(S3, S5, 128)
cmd(0x3800E040)
vertex(S2, S3, 160, 100)   # moves with both counters: right 2, down 1 a frame
gp0(0x0040E0FF)
vertex(S2, S3, 224, 100)
gp0(0x00FF4040)
vertex(S2, S3, 160, 148)
gp0(0x0040FF40)
vertex(S2, S3, 224, 148)
# the rectangle: up and left one pixel a frame, from (280, 200)
emit(addiu(T4, ZERO, 127)); emit(subu(S4, T4, S3))     # 127 - (frame mod 128)
cmd(0x6000FFFF)                                        # monochrome rect, yellow
vertex(S4, S4, 100, 60)
gp0((24 << 16) | 32)
# the GTE triangle: TRX = frame mod 64, each vertex projected by RTPS
# and sent from the register MFC2 filled (what PGXP follows)
emit(andi(T0, S5, 63)); emit(ctc2(T0, 5))
li32(T0, 0x20FF00FF); emit(sw(T0, S7, 0))              # flat opaque, magenta
for i, (vx, vy) in enumerate(((0, 0), (40, 0), (0, 40))):
    li32(T0, (vy << 16) | vx); emit(mtc2(T0, 0))
    li32(T0, 512); emit(mtc2(T0, 1))
    emit(NOP); emit(RTPS); emit(NOP); emit(NOP); emit(NOP)
    emit(mfc2(T1, 14)); emit(NOP); emit(NOP)
    emit(sw(T1, S7, 4 * (i + 1)))
wait_bit(28)                                           # ready for DMA
li32(T4, 0x1F8010A0)
li32(T0, 0x00100000); emit(sw(T0, T4, 0))              # D2_MADR
li32(T0, (1 << 16) | 4); emit(sw(T0, T4, 4))           # D2_BCR: one block of 4 words
li32(T0, 0x01000201); emit(sw(T0, T4, 8))              # D2_CHCR: block mode, to the GPU, go
li32(T7, 0x01000000)
dw = here()
emit(lw(T0, T4, 8)); emit(NOP)
emit(and_(T0, T0, T7))
branch(bne, T0, ZERO, dw)                              # until it is done
# the translucent quad, standing still
cmd(0x2A00FF00)                                        # flat semi-transparent green
gp0(xy(220, 170)); gp0(xy(300, 170)); gp0(xy(220, 230)); gp0(xy(300, 230))
# count the frame, and wait for the vertical blank
li32(T1, 0x801FFFF0); emit(sw(S5, T1, 0))
li32(T4, I_STAT); emit(addiu(T5, ZERO, 0)); emit(sw(T5, T4, 0))   # acknowledge
vb = here()
emit(lw(T0, T4, 0)); emit(NOP)
emit(andi(T0, T0, 1))
branch(beq, T0, ZERO, vb)
emit(addiu(S5, S5, 1))
emit(j(frame)); emit(NOP)

text = b''.join(struct.pack('<I', w) for w in code)
text += b'\0' * ((-len(text)) % 0x800)
hdr = bytearray(0x800); hdr[0:8] = b'PS-X EXE'
struct.pack_into('<I', hdr, 0x10, BASE)
struct.pack_into('<I', hdr, 0x18, BASE & 0x9fffffff)
struct.pack_into('<I', hdr, 0x1c, len(text))
struct.pack_into('<I', hdr, 0x30, 0xA01ffff0)
open(sys.argv[1], 'wb').write(bytes(hdr) + text)
