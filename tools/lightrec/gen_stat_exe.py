#!/usr/bin/env python3
"""Builds the GPUSTAT-reading PS-X EXE for the lightrec lane: every
iteration pushes 200 filled rectangles into GP0 and reads GPUSTAT after
each one, accumulating the raw status words. The busy and ready bits
depend on exactly when the GPU's draw-time budget is refilled, so the
accumulated value pins the GPU's update schedule, not just its output."""
import struct, sys
BASE = 0xA0010000
RESULT = 0xA00F0000
GP0 = 0x1F801810; GP1 = 0x1F801814
def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def addu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x21
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def lw(rt, rs, off):    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def bne(rs, rt, off):   return (5 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def andi(rt, rs, imm):  return (0xC << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def sll(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6)
NOP=0
ZERO,V0,A0,A1,T0,T1,T2,T3,T4,T5,S0,S1,S2=0,2,4,5,8,9,10,11,12,13,16,17,18
code=[]
def emit(w): code.append(w)
def here(): return BASE+4*len(code)
def li32(r, v): emit(lui(r, v>>16)); emit(ori(r, r, v & 0xffff))
def gp0(r):  emit(sw(r, S1, 0))
def gp1(r):  emit(sw(r, S1, 4))
li32(S1, GP0)
li32(S2, RESULT)
emit(addiu(S0, ZERO, 0)); emit(addiu(A0, ZERO, 0)); emit(addiu(A1, ZERO, 0))
li32(T0, 0x00000000); gp1(T0)          # reset
li32(T0, 0x03000000); gp1(T0)          # display enable
li32(T0, 0x08000001); gp1(T0)          # display mode 320x240 NTSC
li32(T0, 0xE1000400); gp0(T0)          # draw mode: allow draw to display
li32(T0, 0xE3000000); gp0(T0)          # draw area TL
li32(T0, 0xE4000000 | (511<<10) | 1023); gp0(T0)   # draw area BR
li32(T0, 0xE5000000); gp0(T0)          # draw offset
loop = here()
emit(addiu(T1, ZERO, 0))               # i
li32(T2, 0x60FF8040)                   # rect, colour
inner = here()
# x = (i*5) & 255, y = (i*3) & 255 ; w,h = 16
emit(sll(T3, T1, 2)); emit(addu(T3, T3, T1)); emit(andi(T3, T3, 255))
emit(sll(T4, T1, 1)); emit(addu(T4, T4, T1)); emit(andi(T4, T4, 255))
emit(sll(T4, T4, 16)); emit(addu(T3, T3, T4))
gp0(T2)
gp0(T3)
li32(T5, (16<<16)|16); gp0(T5)
emit(lw(T4, S1, 4)); emit(NOP)
emit(addu(A0, A0, T4))
emit(andi(T4, T4, 0x7f)); emit(addu(A1, A1, T4))
for _k in range(37): emit(addiu(A1, A1, 1))
emit(addiu(T1, T1, 1))
emit(addiu(T0, ZERO, 200))
emit(bne(T1, T0, (inner - here() - 4) >> 2)); emit(NOP)
emit(addiu(S0, S0, 1))
emit(sw(S0, S2, 4)); emit(sw(A0, S2, 0))
emit(j(loop)); emit(NOP)
text=b''.join(struct.pack('<I',w) for w in code); text += b'\0'*((-len(text))%0x800)
hdr=bytearray(0x800); hdr[0:8]=b'PS-X EXE'
struct.pack_into('<I',hdr,0x10,BASE); struct.pack_into('<I',hdr,0x18,BASE&0x9fffffff)
struct.pack_into('<I',hdr,0x1c,len(text)); struct.pack_into('<I',hdr,0x30,0xA01ffff0)
open(sys.argv[1],'wb').write(bytes(hdr)+text)
