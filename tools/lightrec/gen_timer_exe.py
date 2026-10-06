#!/usr/bin/env python3
"""Timer/GPU-status program: timer 0 on the dot clock, timer 1 on
hblank; each iteration reads both counters and GPUSTAT, draws a rect,
and every 64 iterations flips timer 0 between dot clock and sysclk so
the GPU's idle schedule is woken by timer mode writes too."""
import struct, sys
BASE=0xA0010000; RESULT=0xA00F0000; GP0=0x1F801810; TMR=0x1F801100
def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def addu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x21
def xor_(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x26
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def lw(rt, rs, off):    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def bne(rs, rt, off):   return (5 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def beq(rs, rt, off):   return (4 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def andi(rt, rs, imm):  return (0xC << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def sll(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6)
NOP=0
ZERO,V0,A0,A1,A2,T0,T1,T2,T3,T4,T5,T6,T7,S0,S1,S2,S3=0,2,4,5,6,8,9,10,11,12,13,14,15,16,17,18,19
code=[]
def emit(w): code.append(w)
def here(): return BASE+4*len(code)
def li32(r,v): emit(lui(r,v>>16)); emit(ori(r,r,v&0xffff))
li32(S1,GP0); li32(S2,RESULT); li32(S3,TMR)
emit(addiu(S0,ZERO,0)); emit(addiu(A0,ZERO,0)); emit(addiu(A2,ZERO,0))
li32(T0,0x00000000); emit(sw(T0,S1,4))
li32(T0,0x03000000); emit(sw(T0,S1,4))
li32(T0,0x08000001); emit(sw(T0,S1,4))
li32(T0,0xE1000400); emit(sw(T0,S1,0))
li32(T0,0xE3000000); emit(sw(T0,S1,0))
li32(T0,0xE4000000|(511<<10)|1023); emit(sw(T0,S1,0))
li32(T0,0xE5000000); emit(sw(T0,S1,0))
emit(addiu(T0,ZERO,0x100)); emit(sw(T0,S3,0x04))   # timer0 mode: dotclock
emit(addiu(T0,ZERO,0x100)); emit(sw(T0,S3,0x14))   # timer1 mode: hblank
loop=here()
emit(addiu(T1,ZERO,0))
li32(T2,0x6040C0FF)
inner=here()
emit(sll(T3,T1,2)); emit(addu(T3,T3,T1)); emit(andi(T3,T3,255))
emit(sll(T4,T1,1)); emit(addu(T4,T4,T1)); emit(andi(T4,T4,255)); emit(sll(T4,T4,16)); emit(addu(T3,T3,T4))
emit(sw(T2,S1,0)); emit(sw(T3,S1,0)); li32(T5,(16<<16)|16); emit(sw(T5,S1,0))
emit(lw(T4,S3,0x00)); emit(NOP); emit(addu(A0,A0,T4))
emit(lw(T4,S3,0x10)); emit(NOP); emit(addu(A0,A0,T4))
emit(lw(T4,S1,4)); emit(NOP); emit(addu(A0,A0,T4))
for _ in range(23): emit(addiu(A1,A1,1))
emit(addiu(T1,T1,1)); emit(addiu(T0,ZERO,100))
emit(bne(T1,T0,(inner-here()-4)>>2)); emit(NOP)
# every 64 iterations flip timer0 mode between 0x100 and 0x000
emit(andi(T6,S0,63)); emit(addiu(T7,ZERO,0))
skip=len(code); emit(0); emit(NOP)
emit(addiu(T6,ZERO,0x100)); emit(xor_(A2,A2,T6)); emit(sw(A2,S3,0x04))
tgt=here(); code[skip]=bne(T6,T7,(tgt-(BASE+4*skip)-4)>>2)
emit(addiu(S0,S0,1)); emit(sw(S0,S2,4)); emit(sw(A0,S2,0))
emit(j(loop)); emit(NOP)
text=b''.join(struct.pack('<I',w) for w in code); text+=b'\0'*((-len(text))%0x800)
hdr=bytearray(0x800); hdr[0:8]=b'PS-X EXE'
struct.pack_into('<I',hdr,0x10,BASE); struct.pack_into('<I',hdr,0x18,BASE&0x9fffffff)
struct.pack_into('<I',hdr,0x1c,len(text)); struct.pack_into('<I',hdr,0x30,0xA01ffff0)
open(sys.argv[1],'wb').write(bytes(hdr)+text)
