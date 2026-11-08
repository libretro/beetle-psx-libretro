#!/usr/bin/env python3
"""PS-X EXE: paletted texturing for the fused HD page lanes (page_sync.sh).

Loops forever: uploads a 16-entry CLUT at (0,480) and a 4 bpp 256x256
texture page at VRAM (512,0) as two halves (the same bytes each time), draws
textured quads sampling the page through
the CLUT, and streams a 16x16 upload into a corner of the page, so each
frame the page an HD pack fuses is dirtied after draws have sampled it and
is rebuilt in place on the next frame's first draw.
usage: gen_clut_exe.py out.exe [static]
  static  no streamed upload: the page is built once"""
import struct, sys
BASE = 0xA0010000
GP0 = 0x1F801810
def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def and_(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x24
def xor(rd, rs, rt):    return (rs << 21) | (rt << 16) | (rd << 11) | 0x26
def sll(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6)
def srl(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6) | 2
def or_(rd, rs, rt):    return (rs << 21) | (rt << 16) | (rd << 11) | 0x25
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
    li32(T3, 1 << 26)
    w = here()
    emit(lw(T1, S1, 4)); emit(NOP)
    emit(and_(T1, T1, T3))
    emit(beq(T1, ZERO, (w - here() - 4) >> 2)); emit(NOP)
def upload(x, y, w, h, word_of):
    """CPU-to-VRAM of w x h halfwords; word_of(dst, i) emits code putting
    word i (two halfwords) of the stream in dst, i in register S0."""
    wait_ready()
    gp0(0xA0000000); gp0(xy(x, y)); gp0(xy(w, h))
    emit(addiu(S0, ZERO, 0))
    li32(S2, w * h // 2)
    loop = here()
    word_of(T1)
    emit(sw(T1, S1, 0))
    emit(addiu(S0, S0, 1))
    emit(bne(S0, S2, (loop - here() - 4) >> 2)); emit(NOP)

TPAGE = 8                    # x 512, y 0, 4 bpp
CLUT = (0 // 16) | (480 << 6)

li32(S1, GP0)
gp1(0x00000000)
gp1(0x03000000)
gp1(0x08000001)                        # 320x240 NTSC
gp1(0x05000000)                        # display at (0,0)
gp0(0xE1000000 | TPAGE)
gp0(0xE3000000)
gp0(0xE4000000 | (239 << 10) | 319)
gp0(0xE5000000)
# CLUT: 16 distinct opaque colors.
def clut_word(dst):
    emit(sll(T4, S0, 1)); emit(addiu(T5, T4, 1))
    emit(sll(T6, T4, 10)); emit(or_(T4, T4, T6)); emit(sll(T6, T4, 2)); emit(or_(T4, T4, T6))
    emit(sll(T6, T5, 10)); emit(or_(T5, T5, T6)); emit(sll(T6, T5, 2)); emit(or_(T5, T5, T6))
    li32(T7, 0x7fff); emit(and_(T4, T4, T7)); emit(and_(T5, T5, T7))
    emit(sll(T5, T5, 16)); emit(or_(dst, T4, T5))
# Texture: 4-bit indices from a pattern of the word index (and the half).
def tex_word(half):
    def word(dst):
        emit(srl(T4, S0, 3)); emit(xor(dst, S0, T4))
        emit(sll(T4, dst, 13)); emit(xor(dst, dst, T4))
        if half:
            li32(T4, 0x5A5A5A5A); emit(xor(dst, dst, T4))
    return word

loop = here()
# Re-sent every iteration with the same bytes, so the tracker sees them
# once the renderer is up, as it would a game's. The page is two uploads,
# so a draw across it binds two replacements and is served a fused page.
upload(0, 480, 16, 1, clut_word)
upload(512, 0, 32, 256, tex_word(0))
upload(544, 0, 32, 256, tex_word(1))
# Textured quads covering the screen, sampling the whole page.
for qx in range(0, 320, 64):
    for qy in range(0, 240, 60):
        wait_ready()
        gp0(0x2C808080)
        gp0(xy(qx, qy));           gp0((CLUT << 16) | 0x0000)
        gp0(xy(qx + 64, qy));      gp0((TPAGE << 16) | 0x00FF)
        gp0(xy(qx, qy + 60));      gp0(0xFF00)
        gp0(xy(qx + 64, qy + 60)); gp0(0xFFFF)
# Stream a changing 16x16 into the page corner: S3 counts iterations.
def stream_word(dst):
    emit(xor(dst, S0, S3)); emit(sll(T4, dst, 16)); emit(or_(dst, dst, T4))
if 'static' not in sys.argv[2:]:
    upload(560, 240, 16, 16, stream_word)
emit(addiu(S3, S3, 1))
emit(j(loop)); emit(NOP)

text = b''.join(struct.pack('<I', w) for w in code)
text += b'\0' * ((-len(text)) % 0x800)
hdr = bytearray(0x800); hdr[0:8] = b'PS-X EXE'
struct.pack_into('<I', hdr, 0x10, BASE)
struct.pack_into('<I', hdr, 0x18, BASE & 0x9fffffff)
struct.pack_into('<I', hdr, 0x1c, len(text))
struct.pack_into('<I', hdr, 0x30, 0xA01ffff0)
open(sys.argv[1], 'wb').write(bytes(hdr) + text)
