#!/usr/bin/env python3
"""Builds the PS-X EXE for the HDR post-effect lanes (post_lanes.sh).

Draws once, then idles, so every presented frame shows the same picture
(redrawing in a loop would let a frame catch the black fill alone): a black
320x240 picture with two 16x16 reference-white squares,
one at the centre (152,112) and one near the top-left corner (16,16). The
centre square shows the bloom halo and must show no chromatic aberration
(the lens model is zero at the centre); the corner one shows the
aberration's red/blue split. A mid-grey square at (152,192) is the one
value where gamma-encoded and linear differ, so it shows whether the light
handed to the post effects is really linear."""
import struct, sys
BASE = 0xA0010000
GP0 = 0x1F801810
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def and_(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x24
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def lw(rt, rs, off):    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def beq(rs, rt, off):   return (4 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
NOP = 0
ZERO, T0, T7, S1 = 0, 8, 15, 17
code = []
def emit(w): code.append(w)
def here(): return BASE + 4 * len(code)
def li32(r, v): emit(lui(r, (v >> 16) & 0xffff)); emit(ori(r, r, v & 0xffff))
def wait_ready():
    li32(T7, 1 << 26)
    w = here()
    emit(lw(T0, S1, 4)); emit(NOP)
    emit(and_(T0, T0, T7))
    emit(beq(T0, ZERO, ((w - here() - 4) >> 2))); emit(NOP)
def gp0(v): li32(T0, v); emit(sw(T0, S1, 0))
def gp1(v): li32(T0, v); emit(sw(T0, S1, 4))
def xy(x, y): return (y << 16) | x

li32(S1, GP0)
gp1(0x00000000)
gp1(0x03000000)
gp1(0x08000001)                                   # 320x240 NTSC, 15bpp
gp1(0x06000000 | (0x260 + 320 * 8) << 12 | 0x260)
gp1(0x07000000 | (0x10 + 240) << 10 | 0x10)
gp1(0x05000000)
gp0(0xE1000000)
gp0(0xE3000000)
gp0(0xE4000000 | (239 << 10) | 319)
gp0(0xE5000000)
wait_ready(); gp0(0x02000000); gp0(xy(0, 0)); gp0(xy(320, 240))
for x, y, c in ((152, 112, 0xFFFFFF), (16, 16, 0xFFFFFF), (152, 192, 0x808080)):
    wait_ready(); gp0(0x60000000 | c); gp0(xy(x, y)); gp0(xy(16, 16))
idle = here()
emit(j(idle)); emit(NOP)

text = b''.join(struct.pack('<I', w) for w in code)
text += b'\0' * ((-len(text)) % 0x800)
hdr = bytearray(0x800); hdr[0:8] = b'PS-X EXE'
struct.pack_into('<I', hdr, 0x10, BASE)
struct.pack_into('<I', hdr, 0x18, BASE & 0x9fffffff)
struct.pack_into('<I', hdr, 0x1c, len(text))
struct.pack_into('<I', hdr, 0x30, 0xA01ffff0)
open(sys.argv[1], 'wb').write(bytes(hdr) + text)
