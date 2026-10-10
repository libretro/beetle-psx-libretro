#!/usr/bin/env python3
"""DMA program: each iteration clears an ordering table with DMA6, sends
a linked list of rectangles with DMA2 in linked-list mode, polls both
channels' control words until they finish (counting the polls), reads
GPUSTAT and the DMA interrupt register, and every 32 iterations resets
the MDEC and reads its status. The accumulated value pins DMA's update
cadence and everything it drives."""
import struct, sys
BASE=0xA0010000; RESULT=0xA00F0000; GP0=0x1F801810; DMAB=0x1F801080
OT=0x00100000   # ordering table, 8 entries, in RAM (physical)
LIST=0x00100100 # linked list packets
def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def addu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x21
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def lw(rt, rs, off):    return (0x23 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def bne(rs, rt, off):   return (5 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def andi(rt, rs, imm):  return (0xC << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def srl(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6) | 2
NOP=0
ZERO,V0,A0,A1,A2,T0,T1,T2,T3,T4,T5,T6,T7,S0,S1,S2,S3,S4=0,2,4,5,6,8,9,10,11,12,13,14,15,16,17,18,19,20
code=[]
def emit(w): code.append(w)
def here(): return BASE+4*len(code)
def li32(r,v): emit(lui(r,v>>16)); emit(ori(r,r,v&0xffff))
li32(S1,GP0); li32(S2,RESULT); li32(S3,DMAB); li32(S4,0x1F801820)
emit(addiu(S0,ZERO,0)); emit(addiu(A0,ZERO,0))
li32(T0,0x00000000); emit(sw(T0,S1,4))
li32(T0,0x03000000); emit(sw(T0,S1,4))
li32(T0,0x08000001); emit(sw(T0,S1,4))
li32(T0,0xE1000400); emit(sw(T0,S1,0))
li32(T0,0xE3000000); emit(sw(T0,S1,0))
li32(T0,0xE4000000|(511<<10)|1023); emit(sw(T0,S1,0))
li32(T0,0xE5000000); emit(sw(T0,S1,0))
li32(T0,0x1F1F1F1F); emit(sw(T0,S3,0x70))   # DPCR: enable all channels
# build the linked list once: 4 packets of 3-word rects, terminator
li32(T1,0xA0000000|LIST)
for k in range(4):
    nxt = (LIST + (k+1)*16) & 0xffffff if k < 3 else 0xffffff
    hdr = (3<<24) | nxt
    li32(T0,hdr); emit(sw(T0,T1,k*16+0))
    li32(T0,0x60FF8040 ^ (k*0x001010)); emit(sw(T0,T1,k*16+4))
    li32(T0,((k*20)<<16)|(k*24)); emit(sw(T0,T1,k*16+8))
    li32(T0,(16<<16)|16); emit(sw(T0,T1,k*16+12))
loop=here()
# DMA6: clear OT (8 entries) at OT+28 going down
li32(T0,OT+28); emit(sw(T0,S3,0x60))
emit(addiu(T0,ZERO,8)); emit(sw(T0,S3,0x64))
li32(T0,0x11000002); emit(sw(T0,S3,0x68))
p=here(); emit(lw(T2,S3,0x68)); emit(NOP); emit(addiu(A0,A0,1)); emit(srl(T3,T2,24)); emit(andi(T3,T3,1)); emit(bne(T3,ZERO,(p-here()-4)>>2)); emit(NOP)
# DMA2: send linked list
li32(T0,LIST); emit(sw(T0,S3,0x20))
emit(sw(ZERO,S3,0x24))
li32(T0,0x01000401); emit(sw(T0,S3,0x28))
p=here(); emit(lw(T2,S3,0x28)); emit(NOP); emit(addiu(A0,A0,1)); emit(srl(T3,T2,24)); emit(andi(T3,T3,1)); emit(bne(T3,ZERO,(p-here()-4)>>2)); emit(NOP)
emit(lw(T2,S1,4)); emit(NOP); emit(addu(A0,A0,T2))      # GPUSTAT
emit(lw(T2,S3,0x74)); emit(NOP); emit(addu(A0,A0,T2))   # DICR
# every 32 iterations: MDEC reset + status read
emit(andi(T6,S0,31))
skip=len(code); emit(0); emit(NOP)
li32(T0,0x80000000); emit(sw(T0,S4,4))
emit(lw(T2,S4,4)); emit(NOP); emit(addu(A0,A0,T2))
tgt=here(); code[skip]=bne(T6,ZERO,(tgt-(BASE+4*skip)-4)>>2)
for _ in range(400): emit(addiu(A1,A1,1))               # let the GPU go idle
emit(addiu(S0,S0,1)); emit(sw(S0,S2,4)); emit(sw(A0,S2,0))
emit(j(loop)); emit(NOP)
text=b''.join(struct.pack('<I',w) for w in code); text+=b'\0'*((-len(text))%0x800)
hdr=bytearray(0x800); hdr[0:8]=b'PS-X EXE'
struct.pack_into('<I',hdr,0x10,BASE); struct.pack_into('<I',hdr,0x18,BASE&0x9fffffff)
struct.pack_into('<I',hdr,0x1c,len(text)); struct.pack_into('<I',hdr,0x30,0xA01ffff0)
open(sys.argv[1],'wb').write(bytes(hdr)+text)
