#!/usr/bin/env python3
"""Builds the PS-X EXE for the hardware color lanes (color_lanes.sh).

Draws once:
  - (0,0)-(127,127): a 32x32 grid of 4x4 cells. Cell (d, s) is an opaque
    quad of RGB5 grey d covered by a semi-transparent average quad of grey s,
    so it must read back as (s + d) >> 1 in every channel.
  - (128,0)-(255,255): a white fill crossed by an opaque Gouraud triangle
    with diagonal edges, whose upscaled edges differ from native rasterization.
Then it loops forever copying VRAM (0,0)-(255,255) to RAM at 0x100000
through GP0(C0h), so the host's RAM dump shows VRAM as the content reads it
at the end of the run."""
import struct, sys
BASE = 0xA0010000
DEST = 0xA0100000
GP0 = 0x1F801810
def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def and_(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x24
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def sll(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6)
def or_(rd, rs, rt):    return (rs << 21) | (rt << 16) | (rd << 11) | 0x25
def addu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x21
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def lw(rt, rs, off):    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def beq(rs, rt, off):   return (4 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def bne(rs, rt, off):   return (5 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
NOP = 0
ZERO, T0, T1, T2, T3, T4, T5, T6, T7 = 0, 8, 9, 10, 11, 12, 13, 14, 15
S0, S1, S2, S3, S4 = 16, 17, 18, 19, 20
code = []
def emit(w): code.append(w)
def here(): return BASE + 4 * len(code)
def li32(r, v): emit(lui(r, v >> 16)); emit(ori(r, r, v & 0xffff))
def gp0(v): li32(T0, v); emit(sw(T0, S1, 0))
def gp1(v): li32(T0, v); emit(sw(T0, S1, 4))
def xy(x, y): return (y << 16) | x

li32(S1, GP0)
gp1(0x00000000)                        # reset
gp1(0x03000000)                        # display enable
gp1(0x08000001)                        # 320x240 NTSC
gp0(0xE1000400)                        # draw to display, average, no dither
gp0(0xE3000000)                        # draw area top-left
gp0(0xE4000000 | (511 << 10) | 1023)   # draw area bottom-right
gp0(0xE5000000)                        # draw offset
def grey(dst, code):
    """dst = 24-bit grey with RGB5 code `code` (register) in every channel."""
    emit(sll(dst, code, 3))
    emit(sll(T7, dst, 8)); emit(or_(dst, dst, T7))
    emit(sll(T7, T7, 8)); emit(or_(dst, dst, T7))
def quad(cmd, color, x0, y0):
    """Four-vertex flat quad, 4x4 pixels at (x0, y0) held in registers."""
    li32(T0, cmd << 24); emit(or_(T0, T0, color)); emit(sw(T0, S1, 0))
    emit(sll(T6, y0, 16)); emit(or_(T6, T6, x0)); emit(sw(T6, S1, 0))
    emit(addiu(T5, T6, 4)); emit(sw(T5, S1, 0))
    li32(T4, 4 << 16)
    emit(addu(T5, T6, T4)); emit(sw(T5, S1, 0))
    emit(addiu(T5, T5, 4)); emit(sw(T5, S1, 0))

emit(addiu(S3, ZERO, 0))                                 # s
s_loop = here()
emit(addiu(S4, ZERO, 0))                                 # d
d_loop = here()
emit(sll(S0, S4, 2)); emit(sll(S2, S3, 2))               # x = 4d, y = 4s
grey(T1, S4); quad(0x28, T1, S0, S2)                     # destination
grey(T1, S3); quad(0x2A, T1, S0, S2)                     # average source
emit(addiu(S4, S4, 1)); emit(addiu(T2, ZERO, 32))
emit(bne(S4, T2, (d_loop - here() - 4) >> 2)); emit(NOP)
emit(addiu(S3, S3, 1))
emit(bne(S3, T2, (s_loop - here() - 4) >> 2)); emit(NOP)
gp0(0x02FFFFFF); gp0(xy(128, 0)); gp0(xy(128, 256))     # white fill
gp0(0x300000FF); gp0(xy(140, 10))                        # Gouraud triangle
gp0(0x0000FF00); gp0(xy(250, 60))
gp0(0x00FF0000); gp0(xy(160, 240))
loop = here()
gp0(0xC0000000); gp0(xy(0, 0)); gp0(xy(256, 256))       # VRAM to CPU
li32(T3, 0x08000000)                                     # GPUSTAT ready
wait = here()
emit(lw(T1, S1, 4)); emit(NOP)
emit(and_(T1, T1, T3))
emit(beq(T1, ZERO, (wait - here() - 4) >> 2)); emit(NOP)
li32(S2, DEST)
li32(T2, DEST + 256 * 256 * 2)
copy = here()
emit(lw(T1, S1, 0)); emit(NOP)
emit(sw(T1, S2, 0))
emit(addiu(S2, S2, 4))
emit(bne(S2, T2, (copy - here() - 4) >> 2)); emit(NOP)
emit(j(loop)); emit(NOP)

text = b''.join(struct.pack('<I', w) for w in code)
text += b'\0' * ((-len(text)) % 0x800)
hdr = bytearray(0x800); hdr[0:8] = b'PS-X EXE'
struct.pack_into('<I', hdr, 0x10, BASE)
struct.pack_into('<I', hdr, 0x18, BASE & 0x9fffffff)
struct.pack_into('<I', hdr, 0x1c, len(text))
struct.pack_into('<I', hdr, 0x30, 0xA01ffff0)
open(sys.argv[1], 'wb').write(bytes(hdr) + text)
