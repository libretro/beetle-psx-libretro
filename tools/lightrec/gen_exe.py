#!/usr/bin/env python3
"""Builds the PS-X EXE the lightrec harness runs.

The program lives in KSEG1 (uncached, so the interpreter and the dynarec
see the same instruction stream after a patch) and does, per iteration:
  - calls N leaf functions that each add a small immediate to v0,
  - patches the first word of one of them (self-modifying code: dead
    blocks, cancelled and in-flight compilations),
  - runs a counted inner loop with an internal branch target and enters
    it once from the middle (a block covered by another block),
  - stores the running sum and the iteration count to RESULT.
lightrec_host.c recomputes the sum from the count and compares."""
import struct, sys

N = 256
BASE = 0xA0010000
RESULT = 0xA00F0000
FUNC_STRIDE = 16

def addiu(rt, rs, imm): return (9 << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def slti(rt, rs, imm):  return (0xA << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def xori(rt, rs, imm):  return (0xE << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def ori(rt, rs, imm):   return (0xD << 26) | (rs << 21) | (rt << 16) | (imm & 0xffff)
def lui(rt, imm):       return (0xF << 26) | (rt << 16) | (imm & 0xffff)
def addu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x21
def subu(rd, rs, rt):   return (rs << 21) | (rt << 16) | (rd << 11) | 0x23
def or_(rd, rs, rt):    return (rs << 21) | (rt << 16) | (rd << 11) | 0x25
def sll(rd, rt, sa):    return (rt << 16) | (rd << 11) | (sa << 6)
def jal(t):             return (3 << 26) | ((t >> 2) & 0x3ffffff)
def j(t):               return (2 << 26) | ((t >> 2) & 0x3ffffff)
def jr(rs):             return (rs << 21) | 8
def sw(rt, rs, off):    return (0x2B << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
def bne(rs, rt, off):   return (5 << 26) | (rs << 21) | (rt << 16) | (off & 0xffff)
NOP = 0
ZERO, V0, T0, T1, T2, T3, T4, T5, T6, T7, S0, S1, S2, T8, T9, RA = 0, 2, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 24, 25, 31

code = []
def emit(w): code.append(w)
def here(): return BASE + 4 * len(code)
def patch(i, w): code[i] = w

# Function table lives after main; reserve its address by laying main out
# with a placeholder for the function base once known.
main_words = []
emit(lui(S1, RESULT >> 16))
emit(addiu(S0, ZERO, 0))
emit(addiu(S2, ZERO, 0))
emit(addiu(T0, ZERO, 0))
emit(addiu(T2, ZERO, 0))
emit(addiu(T9, ZERO, 0))
loop = here()
emit(addiu(V0, ZERO, 0))
call_slots = []
for f in range(N):
    call_slots.append(len(code)); emit(0)   # jal, patched once funcs are placed
    emit(NOP)
emit(addu(S2, S2, V0))
# patch function T0: word = 0x24420000 | (2 - T2)
emit(sll(T4, T0, 4))
funcbase_lui = len(code); emit(0)
funcbase_ori = len(code); emit(0)
emit(addu(T4, T4, T5))
emit(addiu(T3, ZERO, 2))
emit(subu(T3, T3, T2))
emit(lui(T6, 0x2442))
emit(or_(T3, T3, T6))
emit(sw(T3, T4, 0))
emit(addiu(T0, T0, 1))
emit(slti(T7, T0, N))
skip_bne = len(code); emit(0)
emit(NOP)
emit(addiu(T0, ZERO, 0))
emit(xori(T2, T2, 1))
skip = here()
patch(skip_bne, bne(T7, ZERO, (skip - (BASE + 4 * skip_bne) - 4) >> 2))
emit(addiu(T8, ZERO, 16))
inner = here()
emit(addiu(T8, T8, -1))
inner_mid = here()
emit(addu(S2, S2, T8))
emit(bne(T8, ZERO, (inner - here() - 4) >> 2))
emit(NOP)
after_bne = len(code); emit(0)
emit(NOP)
emit(addiu(T9, ZERO, 1))
emit(addiu(T8, ZERO, 0))
emit(j(inner_mid))
emit(NOP)
after_mid = here()
patch(after_bne, bne(T9, ZERO, (after_mid - (BASE + 4 * after_bne) - 4) >> 2))
emit(addiu(T9, ZERO, 0))
emit(sw(S2, S1, 0))
emit(addiu(S0, S0, 1))
emit(sw(S0, S1, 4))
emit(j(loop))
emit(NOP)

while (here() & 0xff): emit(NOP)
funcbase = here()
patch(funcbase_lui, lui(T5, funcbase >> 16))
patch(funcbase_ori, ori(T5, T5, funcbase & 0xffff))
for f in range(N):
    patch(call_slots[f], jal(funcbase + f * FUNC_STRIDE))
    emit(addiu(V0, V0, 1)); emit(jr(RA)); emit(NOP); emit(NOP)

text = b''.join(struct.pack('<I', w) for w in code)
text += b'\0' * ((-len(text)) % 0x800)
hdr = bytearray(0x800)
hdr[0:8] = b'PS-X EXE'
struct.pack_into('<I', hdr, 0x10, BASE)          # pc0
struct.pack_into('<I', hdr, 0x18, BASE & 0x9fffffff)  # text address (RAM)
struct.pack_into('<I', hdr, 0x1c, len(text))
struct.pack_into('<I', hdr, 0x30, 0xA01ffff0)    # stack
open(sys.argv[1], 'wb').write(bytes(hdr) + text)
