#!/usr/bin/env python3
"""Builds the PS-X EXE for the end-of-frame resolve lane (resolve_lanes.sh).

Fills all of VRAM with a pattern, then runs one pass a frame (paced by the
VBlank bit of I_STAT): a raw 4 bpp 16x16 sprite at (40,8) whose texels
are the words at (960,480) - what the previous frame drew there - looked
up in a CLUT of the pattern at (0,500), then a flat 16x16 quad at (8,8)
and one at (960,480), their colour stepping with the pass. Packed texels
of rendered pixels come from the native copy of VRAM, which the previous
frame's resolve brought up to date. The frame's writes sit far apart in
VRAM, so their bounds cover almost all of it while the pixels written are
a few hundred. After pass 250 it copies all of VRAM to RAM at 0x100000
through GP0(C0h) and stops, so the dump holds the pattern, both quads in
pass 250's colour and the sprite looked up from pass 249's.
usage: gen_resolve_exe.py out.exe"""
import struct, sys
BASE = 0xA0010000
DEST = 0xA0100000
GP0 = 0x1F801810
I_STAT = 0x1F801070
PASSES = 250
def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def andi(rt, rs, imm):  return (0xC << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def and_(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x24
def xor(rd, rs, rt):    return (rs << 21) | (rt << 16) | (rd << 11) | 0x26
def or_(rd, rs, rt):    return (rs << 21) | (rt << 16) | (rd << 11) | 0x25
def sll(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6)
def srl(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6) | 2
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def lw(rt, rs, off):    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def beq(rs, rt, off):   return (4 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def bne(rs, rt, off):   return (5 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
NOP = 0
ZERO, T0, T1, T2, T3, T4, T5, T6, T7 = 0, 8, 9, 10, 11, 12, 13, 14, 15
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
def pattern(dst, i):
    """(i ^ (i >> 5) ^ (i >> 11)) & 0xffff for pixel index i."""
    emit(srl(T6, i, 5)); emit(xor(dst, i, T6))
    emit(srl(T6, i, 11)); emit(xor(dst, dst, T6))
    emit(andi(dst, dst, 0xffff))
def quad(x0, y0, x1, y1):
    """Flat quad in colour T4 (24-bit)."""
    wait_ready()
    li32(T0, 0x28000000); emit(or_(T0, T0, T4)); emit(sw(T0, S1, 0))
    gp0(xy(x0, y0)); gp0(xy(x1, y0)); gp0(xy(x0, y1)); gp0(xy(x1, y1))

li32(S1, GP0)
li32(S3, I_STAT)
gp1(0x00000000)                        # reset
gp1(0x03000000)                        # display enable
gp1(0x08000001)                        # 320x240 NTSC
gp0(0xE3000000)                        # draw area top-left
gp0(0xE4000000 | (511 << 10) | 1023)   # draw area bottom-right
gp0(0xE5000000)                        # draw offset
gp0(0xE6000000)                        # no mask bits
gp0(0xA0000000); gp0(xy(0, 0)); gp0(xy(1024, 512))       # CPU to VRAM
emit(addiu(S0, ZERO, 0))
li32(S2, 1024 * 512)
fill = here()
pattern(T1, S0)
emit(addiu(T2, S0, 1)); pattern(T4, T2)
emit(sll(T4, T4, 16)); emit(or_(T1, T1, T4))
emit(sw(T1, S1, 0))
emit(addiu(S0, S0, 2))
emit(bne(S0, S2, (fill - here() - 4) >> 2)); emit(NOP)
wait_ready()
# Texture page (960, 256), 4 bpp, drawing to the display area allowed.
gp0(0xE1000000 | 15 | (1 << 4) | (0 << 7) | (1 << 10))

emit(addiu(S0, ZERO, 0))                                 # pass
loop = here()
# One pass a frame: acknowledge VBlank, then wait for the next one.
li32(T0, 0xFFFFFFFE); emit(sw(T0, S3, 0))
vb = here()
emit(lw(T1, S3, 0)); emit(NOP)
emit(andi(T1, T1, 1))
emit(beq(T1, ZERO, (vb - here() - 4) >> 2)); emit(NOP)
emit(addiu(S0, S0, 1))
wait_ready()
gp0(0x01000000)                                          # clear texture cache
wait_ready()
gp0(0x7D000000); gp0(xy(40, 8)); gp0(((500 << 6) << 16) | (224 << 8))  # raw 16x16 sprite
# Colour 8q+7 in red and green, q the pass & 31.
emit(andi(T4, S0, 31)); emit(sll(T4, T4, 3)); emit(ori(T4, T4, 7))
emit(sll(T5, T4, 8)); emit(or_(T4, T4, T5))
quad(8, 8, 24, 24)
quad(960, 480, 976, 496)
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
