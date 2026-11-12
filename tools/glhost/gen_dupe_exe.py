#!/usr/bin/env python3
"""Builds the PS-X EXE for the frame duping lanes (dupe_lanes.sh).

Shows a still picture: the 320x240 display area at (0,0) filled white
once. Then one pass a frame (paced by the VBlank bit of I_STAT) draws,
far outside the display area, a raw 15 bpp 16x16 sprite at (600,300)
whose texels are what the previous pass drew at (960,480), then a flat
16x16 quad at (960,480), its colour stepping with the pass: nothing on
the display changes while VRAM does, and texels of rendered pixels come
from the native copy of VRAM, which the previous frame's resolve brought
up to date. At pass OFF_PASS it turns the display off. After PASSES
passes it copies all of VRAM to RAM at 0x100000 through GP0(C0h) and
stops, so the dump holds the quad in the last pass's colour and the
sprite in the pass before's.
usage: gen_dupe_exe.py out.exe"""
import struct, sys
BASE = 0xA0010000
DEST = 0xA0100000
GP0 = 0x1F801810
I_STAT = 0x1F801070
OFF_PASS = 150
PASSES = 200
def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def andi(rt, rs, imm):  return (0xC << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def and_(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x24
def or_(rd, rs, rt):    return (rs << 21) | (rt << 16) | (rd << 11) | 0x25
def sll(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6)
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def lw(rt, rs, off):    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def beq(rs, rt, off):   return (4 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def bne(rs, rt, off):   return (5 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
NOP = 0
ZERO, T0, T1, T2, T3, T4, T5 = 0, 8, 9, 10, 11, 12, 13
S0, S1, S2, S3 = 16, 17, 18, 19
code = []
def emit(w): code.append(w)
def here(): return BASE + 4 * len(code)
def li32(r, v): emit(lui(r, (v >> 16) & 0xffff)); emit(ori(r, r, v & 0xffff))
def gp0(v): li32(T0, v); emit(sw(T0, S1, 0))
def gp1(v): li32(T0, v); emit(sw(T0, S1, 4))
def xy(x, y): return (y << 16) | x
def wait_ready():
    """GPUSTAT bit 26: ready to receive a command (the previous one done)."""
    li32(T3, 1 << 26)
    w = here()
    emit(lw(T1, S1, 4)); emit(NOP)
    emit(and_(T1, T1, T3))
    emit(beq(T1, ZERO, (w - here() - 4) >> 2)); emit(NOP)

li32(S1, GP0)
li32(S3, I_STAT)
gp1(0x00000000)                        # reset
gp1(0x08000001)                        # 320x240 NTSC
gp1(0x06000000 | 0x260 | (0xC60 << 12))
gp1(0x07000000 | 16 | (256 << 10))
gp1(0x05000000)                        # display at (0,0)
gp1(0x03000000)                        # display on
gp0(0xE3000000)                        # draw area top-left
gp0(0xE4000000 | (511 << 10) | 1023)   # draw area bottom-right
gp0(0xE5000000)                        # draw offset
gp0(0xE6000000)                        # no mask bits
wait_ready()
gp0(0x02FFFFFF); gp0(xy(0, 0)); gp0(xy(320, 240))        # the still picture
wait_ready()
# Texture page (960, 256), 15 bpp.
gp0(0xE1000000 | 15 | (1 << 4) | (2 << 7))

emit(addiu(S0, ZERO, 0))                                 # pass
loop = here()
# One pass a frame: acknowledge VBlank, then wait for the next one.
li32(T0, 0xFFFFFFFE); emit(sw(T0, S3, 0))
vb = here()
emit(lw(T1, S3, 0)); emit(NOP)
emit(andi(T1, T1, 1))
emit(beq(T1, ZERO, (vb - here() - 4) >> 2)); emit(NOP)
emit(addiu(S0, S0, 1))
# Colour 8q+7 in red and green, q the pass & 31.
emit(andi(T4, S0, 31)); emit(sll(T4, T4, 3)); emit(ori(T4, T4, 7))
emit(sll(T5, T4, 8)); emit(or_(T4, T4, T5))
wait_ready()
gp0(0x01000000)                                          # clear texture cache
wait_ready()
gp0(0x7D000000); gp0(xy(600, 300)); gp0(224 << 8)       # raw 16x16 sprite
wait_ready()
li32(T0, 0x28000000); emit(or_(T0, T0, T4)); emit(sw(T0, S1, 0))
gp0(xy(960, 480)); gp0(xy(976, 480)); gp0(xy(960, 496)); gp0(xy(976, 496))
li32(T1, OFF_PASS)
off = len(code)
emit(NOP); emit(NOP)                   # bne past the display off, below
gp1(0x03000001)                        # display off, once
code[off] = bne(S0, T1, len(code) - off - 1)
li32(T1, PASSES)
emit(bne(S0, T1, (loop - here() - 4) >> 2)); emit(NOP)

wait_ready()
gp0(0xC0000000); gp0(xy(0, 0)); gp0(xy(1024, 512))      # VRAM to CPU
li32(T3, 0x08000000)                                     # GPUSTAT ready
wait = here()
emit(lw(T1, S1, 4)); emit(NOP)
emit(and_(T1, T1, T3))
emit(beq(T1, ZERO, (wait - here() - 4) >> 2)); emit(NOP)
li32(S2, DEST)
li32(T2, DEST + 1024 * 512 * 2)
copy = here()
emit(lw(T1, S1, 0)); emit(NOP)
emit(sw(T1, S2, 0))
emit(addiu(S2, S2, 4))
emit(bne(S2, T2, (copy - here() - 4) >> 2)); emit(NOP)
halt = here()
emit(j(halt)); emit(NOP)

text = b''.join(struct.pack('<I', w) for w in code)
text += b'\0' * ((-len(text)) % 0x800)
hdr = bytearray(0x800); hdr[0:8] = b'PS-X EXE'
struct.pack_into('<I', hdr, 0x10, BASE)
struct.pack_into('<I', hdr, 0x18, BASE & 0x9fffffff)
struct.pack_into('<I', hdr, 0x1c, len(text))
struct.pack_into('<I', hdr, 0x30, 0xA00ffff0)
open(sys.argv[1], 'wb').write(bytes(hdr) + text)
