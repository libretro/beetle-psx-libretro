#!/usr/bin/env python3
"""PS-X EXE: a game-shaped GPU workload, for benchmarking the hardware
renderers and for tools/rhi_coherence/rollback_lanes.sh.

Setup: uploads a 256x256 15bpp texture page at VRAM (512,0). Then loops
forever, one iteration ~ one game frame:
  - draw offset alternates between two buffers (y 0 / 256), display shows
    the other one (double buffering)
  - background fill
  - streams a 64x64 CPU->VRAM upload into the texture page
  - 600 Gouraud-textured 32x32 quads (every 4th semi-transparent)
  - 400 textured 16x16 sprites
  - one 64x64 VRAM->VRAM copy
Options (any order after the output path):
  readback  a 16x16 GP0(C0h) VRAM->CPU read per iteration
  additive  semi-transparent quads in additive mode
  drawarea  the drawing area follows the buffer being drawn, as a
            double-buffered game sets it (otherwise it is all of VRAM)
  feedback  copies part of the previous frame's buffer into the texture
            page the sprites sample, so each frame depends on what the GPU
            rendered before
usage: gen_frame_exe.py out.exe [readback] [additive] [drawarea] [feedback]"""
import struct, sys
BASE = 0xA0010000
GP0 = 0x1F801810
QUADS = 600
SPRITES = 400
READBACK = 'readback' in sys.argv[2:]
ADDITIVE = 'additive' in sys.argv[2:]
DRAWAREA = 'drawarea' in sys.argv[2:]
FEEDBACK = 'feedback' in sys.argv[2:]

def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def andi(rt, rs, imm):  return (0xC << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def xori(rt, rs, imm):  return (0xE << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def sll(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6)
def or_(rd, rs, rt):    return (rs << 21) | (rt << 16) | (rd << 11) | 0x25
def addu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x21
def and_(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x24
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def lw(rt, rs, off):    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def beq(rs, rt, off):   return (4 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def bne(rs, rt, off):   return (5 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
NOP = 0
ZERO, T0, T1, T2, T3, T4, T5, T6, T7 = 0, 8, 9, 10, 11, 12, 13, 14, 15
S1, S2, S3, S4, S5, S6 = 17, 18, 19, 20, 21, 22
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
TPAGE = 8 | (2 << 7)

def upload(x, y, w, h):
    """CPU->VRAM of w*h pixels, pattern from the frame counter."""
    cmd(0xA0000000); gp0(xy(x, y)); gp0((h << 16) | w)
    emit(addiu(T2, ZERO, 0)); li32(T3, w * h // 2)
    loop = here()
    wait_bit(28)                       # ready to receive DMA block
    emit(sll(T1, T2, 3)); emit(addu(T1, T1, S5)); emit(ori(T1, T1, 0x8421))
    emit(sll(T4, T1, 16)); emit(or_(T1, T1, T4))
    gp0r(T1)
    emit(addiu(T2, T2, 1))
    branch(bne, T2, T3, loop)

li32(S1, GP0)
gp1(0x00000000); gp1(0x03000000); gp1(0x08000001)
gp1(0x06000000 | (0x260 + 320 * 8) << 12 | 0x260)
gp1(0x07000000 | (0x10 + 240) << 10 | 0x10)
gp0(0xE1000000 | TPAGE | (1 << 9) | ((1 << 5) if ADDITIVE else 0))  # dither, texpage 512 15bpp, B+F when additive
gp0(0xE3000000)
gp0(0xE4000000 | (511 << 10) | 1023)
emit(addiu(S5, ZERO, 0))               # frame counter
upload(512, 0, 256, 256)

frame = here()
# double buffer: draw at y = (frame & 1) * 256, display the other
emit(andi(T1, S5, 1)); emit(sll(T1, T1, 19))
li32(T2, 0xE5000000); emit(or_(T1, T1, T2)); wait_bit(26); gp0r(T1)
if DRAWAREA:
    # drawing area = the buffer being drawn, as a double-buffered game sets it
    emit(andi(T1, S5, 1)); emit(sll(T1, T1, 18))         # y = 256 -> << 10
    li32(T2, 0xE3000000); emit(or_(T3, T1, T2)); wait_bit(26); gp0r(T3)
    li32(T2, 0xE4000000 | (239 << 10) | 319); emit(or_(T3, T1, T2)); wait_bit(26); gp0r(T3)
li32(T2, 0x05040000)                   # display y = 256 when drawing to 0
emit(andi(T3, S5, 1)); branch(beq, T3, ZERO, here() + 16)
li32(T2, 0x05000000)
emit(sw(T2, S1, 4))
# background (fill ignores the draw offset: fill both halves' current one)
emit(andi(T1, S5, 1)); emit(sll(T1, T1, 24))  # y = 256 -> << 16
cmd(0x02201830); emit(sw(T1, S1, 0)); gp0(xy(320, 240))
upload(512, 0, 64, 64)
if FEEDBACK:
    # render-to-texture feedback: copy part of the PREVIOUS frame's buffer
    # into the texture page the sprites sample, so each frame depends on
    # what the GPU rendered before
    emit(andi(T1, S5, 1)); emit(xori(T1, T1, 1)); emit(sll(T1, T1, 24))   # y 256 -> << 16
    li32(T2, (32 << 16) | 32); emit(addu(T1, T1, T2))
    cmd(0x80000000); gp0r(T1); gp0(xy(544, 32)); gp0((32 << 16) | 32)
# quads
emit(addiu(S2, ZERO, 0)); emit(addiu(S3, ZERO, 0)); emit(addiu(S4, ZERO, 0))
li32(S6, QUADS)
q = here()
emit(andi(T5, S4, 3))
li32(T6, 0x3C000000); li32(T4, 0x3E000000)
emit(bne(T5, ZERO, 2)); emit(NOP)      # every 4th quad semi-transparent
emit(or_(T6, T4, ZERO))
wait_bit(26)
li32(T1, 0x00806040); emit(or_(T1, T1, T6)); gp0r(T1)            # cmd|c0
emit(sll(T1, S3, 16)); emit(or_(T1, T1, S2)); gp0r(T1)            # v0
li32(T0, 0x00000000); gp0r(T0)                                    # clut|uv0
gp0(0x00408060)                                                   # c1
emit(addiu(T2, T1, 32)); gp0r(T2)                                 # v1
li32(T0, (TPAGE << 16) | 0x001F); gp0r(T0)                        # tpage|uv1
gp0(0x00604080)                                                   # c2
li32(T3, 32 << 16); emit(addu(T2, T1, T3)); gp0r(T2)              # v2
gp0(0x1F00)                                                       # uv2
gp0(0x00806080)                                                   # c3
emit(addiu(T2, T2, 32)); gp0r(T2)                                 # v3
gp0(0x1F1F)                                                       # uv3
emit(addiu(S2, S2, 37)); emit(andi(S2, S2, 255))
emit(addiu(S3, S3, 53)); emit(andi(S3, S3, 191))
emit(addiu(S4, S4, 1))
branch(bne, S4, S6, q)
# sprites
emit(addiu(S4, ZERO, 0)); li32(S6, SPRITES)
s = here()
wait_bit(26)
gp0(0x64808080)
emit(sll(T1, S3, 16)); emit(or_(T1, T1, S2)); gp0r(T1)
gp0(0x00002020)
gp0((16 << 16) | 16)
emit(addiu(S2, S2, 29)); emit(andi(S2, S2, 255))
emit(addiu(S3, S3, 17)); emit(andi(S3, S3, 191))
emit(addiu(S4, S4, 1))
branch(bne, S4, S6, s)
# VRAM -> VRAM copy
cmd(0x80000000); gp0(xy(512, 0)); gp0(xy(768, 0)); gp0((64 << 16) | 64)
if READBACK:
    cmd(0xC0000000); gp0(xy(0, 0)); gp0((16 << 16) | 16)
    emit(addiu(T2, ZERO, 0)); emit(addiu(T3, ZERO, 128))
    rl = here()
    wait_bit(27)
    emit(lw(T1, S1, 0)); emit(NOP)
    emit(addiu(T2, T2, 1))
    branch(bne, T2, T3, rl)
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
