#!/usr/bin/env python3
"""Builds the PS-X EXE for the VRAM copy lanes (copy_lanes.sh).

Fills all of VRAM with a pattern whose bit 15 (the mask bit) varies, then
draws flat quads (some with the mask bit) under several copy sources, so
those copies start from rendered pixels and run in an upscaled
renderer's scaled framebuffer, then issues GP0(80h) VRAM-to-VRAM copies
covering what the hardware's order
makes visible: rows overlapping downwards (each row reads rows already
written) and upwards, a same-row shift in both directions across 128-pixel
chunks, source and destination wrapping in x and in y, the mask test, the
forced mask bit, both together, and an overlapping masked copy. Then it
loops copying all of VRAM to RAM at 0x100000 through GP0(C0h), so the
host's RAM dump shows VRAM as the content reads it.
usage: gen_copy_exe.py out.exe [gp1loop] [smallread]
  gp1loop    the loop also sends the display start and mode again, unchanged,
             as many games do every frame: the screen stays static
  smallread  each pass of the loop draws a 16x16 quad at (100,100) and
             uploads an 8x8 block at (140,104), both stepping with the
             pass, then reads back only the 63x32 rect at (97,96) around
             them to RAM at 0x100000; after pass 250 it stops, so the dump
             holds pass 250 exactly. No quads or copies before the loop."""
import struct, sys
BASE = 0xA0010000
DEST = 0xA0100000
GP0 = 0x1F801810
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
S0, S1, S2 = 16, 17, 18
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

# Pattern for pixel index i: (i ^ (i >> 5) ^ (i >> 11)) & 0xffff.
def pattern(dst, i):
    emit(srl(T6, i, 5)); emit(xor(dst, i, T6))
    emit(srl(T6, i, 11)); emit(xor(dst, dst, T6))
    emit(andi(dst, dst, 0xffff))

li32(S1, GP0)
gp1(0x00000000)                        # reset
gp1(0x03000000)                        # display enable
gp1(0x08000001)                        # 320x240 NTSC
gp0(0xE3000000)                        # draw area top-left
gp0(0xE4000000 | (511 << 10) | 1023)   # draw area bottom-right
gp0(0xE5000000)                        # draw offset
gp0(0xE6000000)                        # no mask bits
gp0(0xA0000000); gp0(xy(0, 0)); gp0(xy(1024, 512))       # CPU to VRAM
emit(addiu(S0, ZERO, 0))                                 # i
li32(S2, 1024 * 512)
fill = here()
pattern(T1, S0)
emit(addiu(T2, S0, 1)); pattern(T4, T2)
emit(sll(T4, T4, 16)); emit(or_(T1, T1, T4))
emit(sw(T1, S1, 0))
emit(addiu(S0, S0, 2))
emit(bne(S0, S2, (fill - here() - 4) >> 2)); emit(NOP)

# (mask setting E6, 24-bit color, x0, y0, x1, y1): flat quads. Channels
# are 8q+7, which every RGB5 reduction (q = c >> 3 or floor(c * 31 / 255))
# takes to q, so the lanes compare copies rather than color policy.
QUADS = [
    (1, 0xFF870F, 600, 296, 632, 416),  # copies 8-10 sources, mask set
    (0, 0x17FF37, 632, 296, 664, 416),
    (1, 0x3767C7, 896, 256, 1024, 288), # copy 14 source, both sides of
    (0, 0xC76737, 0, 256, 184, 288),    # the x wrap
    (0, 0x878787, 0, 0, 48, 24),        # copy 0 source
]
small = 'smallread' in sys.argv[2:]
if small:
    QUADS = []      # the read rect holds the fill pattern and the passes
for e6, rgb, x0, y0, x1, y1 in QUADS:
    # Vertex x is 11-bit signed: reach x 1024 through the draw offset.
    o = 512 if x1 > 1023 else 0
    wait_ready()
    gp0(0xE5000000 | o)
    gp0(0xE6000000 | e6)
    gp0(0x28000000 | rgb)
    gp0(xy(x0 - o, y0)); gp0(xy(x1 - o, y0)); gp0(xy(x0 - o, y1)); gp0(xy(x1 - o, y1))
wait_ready()
gp0(0xE5000000)

# (mask setting E6, src x, src y, dst x, dst y, w, h)
COPIES = [
    (0, 0, 0, 3, 5, 64, 40),          # rows overlap, destination below
    (0, 100, 14, 96, 8, 64, 40),      # rows overlap, destination above
    (0, 0, 100, 50, 100, 300, 8),     # same rows, shift right over chunks
    (0, 450, 100, 400, 100, 300, 8),  # same rows, shift left over chunks
    (0, 1000, 150, 500, 150, 64, 16), # source wraps in x
    (0, 200, 170, 990, 170, 64, 16),  # destination wraps in x
    (0, 300, 500, 300, 200, 32, 24),  # source wraps in y
    (0, 400, 220, 400, 500, 32, 24),  # destination wraps in y
    (2, 600, 300, 700, 300, 64, 32),  # mask test
    (1, 600, 340, 700, 340, 64, 32),  # set mask
    (3, 600, 380, 700, 380, 64, 32),  # mask test and set mask
    (2, 800, 300, 802, 303, 64, 32),  # overlap with mask test
    (0, 600, 420, 700, 420, 64, 32),  # plain
    (0, 1010, 440, 20, 440, 40, 60),  # source wraps in x, rows below 512
    (1, 900, 260, 880, 262, 300, 20), # overlap, wraps in x, set mask
]
if small:
    COPIES = []
for e6, sx, sy, dx, dy, w, h in COPIES:
    wait_ready()
    gp0(0xE6000000 | e6)
    gp0(0x80000000); gp0(xy(sx, sy)); gp0(xy(dx, dy)); gp0(xy(w, h))
wait_ready()
gp0(0xE6000000)

if 'smallread' in sys.argv[2:]:
    emit(addiu(S0, ZERO, 0))                             # pass
loop = here()
if 'gp1loop' in sys.argv[2:]:
    gp1(0x05000000)                    # display start (0,0), unchanged
    gp1(0x08000001)                    # 320x240 NTSC, unchanged
RX, RY, RW, RH = (97, 96, 63, 32) if small else (0, 0, 1024, 512)
PASSES = 250
if small:
    # A quad whose colour (8q+7 channels, q the pass & 31) steps every
    # pass keeps the rect rendered by the GPU since the last read, and an
    # upload of the word pass | 0x4000 lands in it from the CPU.
    emit(addiu(S0, S0, 1))
    emit(andi(T4, S0, 31)); emit(sll(T4, T4, 3)); emit(ori(T4, T4, 7))
    emit(sll(T5, T4, 8)); emit(or_(T4, T4, T5))
    wait_ready()
    li32(T0, 0x28000000); emit(or_(T0, T0, T4)); emit(sw(T0, S1, 0))
    gp0(xy(100, 100)); gp0(xy(116, 100)); gp0(xy(100, 116)); gp0(xy(116, 116))
    wait_ready()
    gp0(0xA0000000); gp0(xy(140, 104)); gp0(xy(8, 8))
    emit(ori(T4, S0, 0x4000)); emit(sll(T5, T4, 16)); emit(or_(T4, T4, T5))
    for _ in range(32):
        emit(sw(T4, S1, 0))
wait_ready()
gp0(0xC0000000); gp0(xy(RX, RY)); gp0(xy(RW, RH))       # VRAM to CPU
li32(T3, 0x08000000)                                     # GPUSTAT ready
wait = here()
emit(lw(T1, S1, 4)); emit(NOP)
emit(and_(T1, T1, T3))
emit(beq(T1, ZERO, (wait - here() - 4) >> 2)); emit(NOP)
li32(S2, DEST)
li32(T2, DEST + RW * RH * 2)
copy = here()
emit(lw(T1, S1, 0)); emit(NOP)
emit(sw(T1, S2, 0))
emit(addiu(S2, S2, 4))
emit(bne(S2, T2, (copy - here() - 4) >> 2)); emit(NOP)
if small:
    li32(T1, PASSES)
    emit(bne(S0, T1, (loop - here() - 4) >> 2)); emit(NOP)
    halt = here()
    emit(j(halt)); emit(NOP)
emit(j(loop)); emit(NOP)

text = b''.join(struct.pack('<I', w) for w in code)
text += b'\0' * ((-len(text)) % 0x800)
hdr = bytearray(0x800); hdr[0:8] = b'PS-X EXE'
struct.pack_into('<I', hdr, 0x10, BASE)
struct.pack_into('<I', hdr, 0x18, BASE & 0x9fffffff)
struct.pack_into('<I', hdr, 0x1c, len(text))
struct.pack_into('<I', hdr, 0x30, 0xA00ffff0)
open(sys.argv[1], 'wb').write(bytes(hdr) + text)
