class DagOp;

def add : DagOp;
def sub : DagOp;
def and : DagOp;
def or : DagOp;
def nor : DagOp;
def xor : DagOp;
def mul : DagOp; // word * word -> dword
def div : DagOp; // word / word -> word
def mod : DagOp; // word % word -> word
def shl : DagOp;
def shra : DagOp;
def shrl : DagOp;
def signext : DagOp;
def zeroext : DagOp;

def eq : DagOp;
def ge : DagOp;
def gt : DagOp;
def le : DagOp;
def lt : DagOp;
def neq : DagOp;

def branch : DagOp;
def copreg : DagOp;
def copcreg : DagOp;
def gpr : DagOp;
def hi : DagOp;
def lo : DagOp;
def check_overflow : DagOp;
def pcd : DagOp;
def raise : DagOp;
def load : DagOp;
def store : DagOp;

def block : DagOp;
def if : DagOp;
def let : DagOp;
def rlet: DagOp;
def set : DagOp;
def when : DagOp;
def unsigned : DagOp;
def signed : DagOp;

// Special traps
def break : DagOp;
def syscall : DagOp;
def copfun : DagOp;

class Exception;
def ArithmeticOverflow : Exception;

class BaseInst<bits<6> op, string disasm, dag eval> {
	bits<6> Opcode = op;

	string Disasm = disasm;
	dag Eval = eval;
}

class IType<bits<6> op, string disasm, dag eval> : BaseInst<op, disasm, eval> {
	// 31-26: op
	// 25-21: rs
	// 20-16: rt
	// 15-0: imm
}

class RIType<bits<6> op, bits<5> funct, string disasm, dag eval> : BaseInst<op, disasm, eval> {
	// 31-26: op
	// 25-21: rs
	// 20-16: funct
	// 15-0: imm

	bits<5> Function = funct;
}

class JType<bits<6> op, string disasm, dag eval> : BaseInst<op, disasm, eval> {
	// 31-26: op
	// 25-0: imm
}

class RType<bits<6> funct, string disasm, dag eval> : BaseInst<0b000000, disasm, eval> {
	// 31-26: op
	// 25-21: rs
	// 20-16: rt
	// 15-11: rd
	// 10-6: shamt
	// 5-0: funct

	bits<6> Function = funct;
}

class SType<bits<6> funct, string disasm, dag eval> : BaseInst<0b000000, disasm, eval> {
	// 31-26: op
	// 25-6: code
	// 5-0: funct

	bits<6> Function = funct;
}

class CFType<bits<6> op, bits<5> funct, string disasm, dag eval> : BaseInst<op, disasm, eval> {
	// 31-26: op
	// 27-26: cop
	// 25-21: funct
	// 20-16: rt
	// 15-11: rd
	// 24-0: cofun

	bits<5> Function = funct;
}

multiclass CFDef<bits<6> op, bits<5> funct, string disasm, dag eval> {
	def : CFType<!add(op, 0b000000), funct, disasm, eval>;
	def : CFType<!add(op, 0b000001), funct, disasm, eval>;
	def : CFType<!add(op, 0b000010), funct, disasm, eval>;
	def : CFType<!add(op, 0b000011), funct, disasm, eval>;
}

def ADD : RType<0b100000, "add %$rd, %$rs, %$rt", 
	(block
		(check_overflow (add (gpr $rs), (gpr $rt))), 
		(set (gpr $rd), (add (gpr $rs), (gpr $rt))))
>;

def ADDI : IType<0b001000, "addi %$rt, %$rs, $eimm", 
	(let $eimm, (signext 16, $imm), 
		(block
			(check_overflow (add (gpr $rs), $eimm)), 
			(set (gpr $rt), (add (gpr $rs), $eimm))))
>;

def ADDIU : IType<0b001001, "addiu %$rt, %$rs, $eimm", 
	(let $eimm, (signext 16, $imm), 
		(set (gpr $rt), (add (gpr $rs), $eimm)))
>;

def ADDU : RType<0b100001, "addu %$rd, %$rs, %$rt", 
	(set (gpr $rd), (add (gpr $rs), (gpr $rt)))
>;

def AND : RType<0b100100, "and %$rd, %$rs, %$rt", 
	(set (gpr $rd), (and (gpr $rs), (gpr $rt)))
>;

def ANDI : IType<0b001100, "andi %$rt, %$rs, $eimm", 
	(let $eimm, (zeroext 16, $imm), 
		(set (gpr $rt), (and (gpr $rs), $eimm)))
>;

def BEQ : IType<0b000100, "beq %$rs, %$rt, $target", 
	(let $target, (add (pcd), (signext 18, (shl $imm, 2))), 
		(when (eq (unsigned (gpr $rs)), (unsigned (gpr $rt))), 
			(branch $target)))
>;

def BGEZ : RIType<0b000001, 0b00001, "bgez %$rs, $target", 
	(let $target, (add (pcd), (signext 18, (shl $imm, 2))), 
		(when (ge (signed (gpr $rs)), 0), 
			(branch $target)))
>;

def BGEZAL : RIType<0b000001, 0b10001, "bgezal %$rs, $target", 
	(block
		(set (gpr 31), (pcd)), 
		(let $target, (add (pcd), (signext 18, (shl $imm, 2))), 
			(when (ge (signed (gpr $rs)), 0), 
				(branch $target))))
>;

// This isn't really RIType, but that lets us constrain rt
def BGTZ : RIType<0b000111, 0b00000, "bgtz %$rs, $target", 
	(let $target, (add (pcd), (signext 18, (shl $imm, 2))), 
		(when (gt (signed (gpr $rs)), 0), 
			(branch $target)))
>;

// This isn't really RIType, but that lets us constrain rt
def BLEZ : RIType<0b000110, 0b00000, "blez %$rs, $target", 
	(let $target, (add (pcd), (signext 18, (shl $imm, 2))), 
		(when (le (signed (gpr $rs)), 0), 
			(branch $target)))
>;

def BLTZ : RIType<0b000001, 0b00000, "bltz %$rs, $target", 
	(let $target, (add (pcd), (signext 18, (shl $imm, 2))), 
		(when (lt (signed (gpr $rs)), 0), 
			(branch $target)))
>;

def BLTZAL : RIType<0b000001, 0b10000, "bltzal %$rs, $target", 
	(block
		(set (gpr 31), (pcd)), 
		(let $target, (add (pcd), (signext 18, (shl $imm, 2))), 
			(when (lt (signed (gpr $rs)), 0), 
				(branch $target))))
>;

def BNE : IType<0b000101, "bne %$rs, %$rt, $target", 
	(let $target, (add (pcd), (signext 18, (shl $imm, 2))),  
		(when (neq (gpr $rs), (gpr $rt)), 
			(branch $target)))
>;

def BREAK : SType<0b001101, "break $code", 
	(break $code)
>;

defm CFCz : CFDef<0b010000, 0b00010, "cfc$cop %$rt, $rd", 
	(set (gpr $rt), (copcreg $cop, $rd))
>;

// HACK! This should be special cased.
multiclass COPzType<string disasm, dag eval> {
	defm : CFDef<0b010000, 0b10000, disasm, eval>;
	defm : CFDef<0b010000, 0b10001, disasm, eval>;
	defm : CFDef<0b010000, 0b10010, disasm, eval>;
	defm : CFDef<0b010000, 0b10011, disasm, eval>;
	defm : CFDef<0b010000, 0b10100, disasm, eval>;
	defm : CFDef<0b010000, 0b10101, disasm, eval>;
	defm : CFDef<0b010000, 0b10110, disasm, eval>;
	defm : CFDef<0b010000, 0b10111, disasm, eval>;

	defm : CFDef<0b010000, 0b11000, disasm, eval>;
	defm : CFDef<0b010000, 0b11001, disasm, eval>;
	defm : CFDef<0b010000, 0b11010, disasm, eval>;
	defm : CFDef<0b010000, 0b11011, disasm, eval>;
	defm : CFDef<0b010000, 0b11100, disasm, eval>;
	defm : CFDef<0b010000, 0b11101, disasm, eval>;
	defm : CFDef<0b010000, 0b11110, disasm, eval>;
	defm : CFDef<0b010000, 0b11111, disasm, eval>;
}
defm COPz : COPzType<"cop$cop $cofun", 
	(copfun $cop, $cofun)
>;

defm CTCz : CFDef<0b010000, 0b00110, "ctc$cop %$rt, $rd", 
	(set (copcreg $cop, $rd), (gpr $rt))
>;

def DIV : RType<0b011010, "div %$rs, %$rt", 
	(block
		(set (lo), (div (gpr $rs), (gpr $rt))), 
		(set (hi), (mod (gpr $rs), (gpr $rt))))
>;

def DIVU : RType<0b011011, "divu %$rs, %$rt", 
	(block
		(set (lo), (div (unsigned (gpr $rs)), (unsigned (gpr $rt)))), 
		(set (hi), (mod (unsigned (gpr $rs)), (unsigned (gpr $rt)))))
>;


def J : JType<0b000010, "j $target", 
	(let $target, (add (and (pcd), 0xF0000000), (zeroext 28, (shl $imm, 2))),  
		(branch $target))
>;

def JAL : JType<0b000011, "jal $target", 
	(block
		(set (gpr 31), (add (pcd), 4)), 
		(let $target, (add (and (pcd), 0xF0000000), (zeroext 28, (shl $imm, 2))),  
			(branch $target)))
>;

def JALR : RType<0b001001, "jalr %$rd, %$rs", 
	(block
		(set (gpr $rd), (add (pcd), 4)), 
		(branch (unsigned (gpr $rs))))
>;

def JR : RType<0b001000, "jr %$rs", 
	(branch (unsigned (gpr $rs)))
>;

def LB : IType<0b100000, "lb %$rt, $offset(%$rs)", 
	(let $offset, (signext 16, $imm), 
		(set (gpr $rt), (signext 8, (load 8, (unsigned (add (gpr $rs), $offset))))))
>;

def LBU : IType<0b100100, "lbu %$rt, $offset(%$rs)", 
	(let $offset, (signext 16, $imm), 
		(set (gpr $rt), (zeroext 8, (load 8, (unsigned (add (gpr $rs), $offset))))))
>;

def LH : IType<0b100001, "lh %$rt, $offset(%$rs)", 
	(let $offset, (signext 16, $imm), 
		(set (gpr $rt), (signext 16, (load 16, (unsigned (add (gpr $rs), $offset))))))
>;

def LHU : IType<0b100101, "lhu %$rt, $offset(%$rs)", 
	(let $offset, (signext 16, $imm), 
		(set (gpr $rt), (zeroext 16, (load 16, (add (gpr $rs), $offset)))))
>;

def LUI : IType<0b001111, "lui %$rt, $imm", 
	(set (gpr $rt), (shl $imm, 16))
>;

def LW : IType<0b100011, "lw %$rt, $offset(%$rs)", 
	(let $offset, (signext 16, $imm), 
		(set (gpr $rt), (load 32, (unsigned (add (gpr $rs), $offset)))))
>;

// XXX: LWCz instruction
// XXX: LWL instruction
// XXX: LWR instruction
defm MFCz : CFDef<0b010000, 0b00000, "mfc$cop %$rt, $rd", 
	(set (gpr $rt), (copreg $cop, $rd))
>;

def MFHI : RType<0b010000, "mfhi %$rd", 
	(set (gpr $rd), (hi))
>;

def MFLO : RType<0b010010, "mflo %$rd", 
	(set (gpr $rd), (lo))
>;

defm MTCz : CFDef<0b010000, 0b00100, "mtc$cop %$rt, $rd", 
	(set (copreg $cop, $rd), (gpr $rt))
>;

def MTHI : RType<0b010001, "mthi %$rd", 
	(set (hi), (gpr $rd))
>;

def MTLO : RType<0b010011, "mtlo %$rd", 
	(set (lo), (gpr $rd))
>;

def MULT : RType<0b011000, "mult %$rs, %$rt", 
	(rlet $_t, (mul (gpr $rs), (gpr $rt)), 
		(set (lo), (and $_t, 0xFFFFFFFF)), 
		(set (hi), (shrl $_t, 32)))
>;

def MULTU : RType<0b011001, "multu %$rs, %$rt", 
	(rlet $_t, (mul (unsigned (gpr $rs)), (unsigned (gpr $rt))), 
		(set (lo), (and $_t, 0xFFFFFFFF)), 
		(set (hi), (shrl $_t, 32)))
>;

def NOR : RType<0b100111, "nor %$rd, %$rs, %$rt", 
	(set (gpr $rd), (nor (gpr $rs), (gpr $rt)))
>;

def OR : RType<0b100101, "or %$rd, %$rs, %$rt", 
	(set (gpr $rd), (or (gpr $rs), (gpr $rt)))
>;

def ORI : IType<0b001101, "ori %$rt, %$rs, $eimm", 
	(let $eimm, (zeroext 16, $imm), 
		(set (gpr $rt), (or (gpr $rs), $eimm)))
>;

def SB : IType<0b101000, "sb %$rt, $offset(%$rs)", 
	(let $offset, (signext 16, $imm), 
		(store 8, (unsigned (add (gpr $rs), $offset)), (gpr $rt)))
>;

def SH : IType<0b101001, "sh %$rt, $offset(%$rs)", 
	(let $offset, (signext 16, $imm), 
		(store 16, (unsigned (add (gpr $rs), $offset)), (gpr $rt)))
>;

def SLL : RType<0b000000, "sll %$rd, %$rt, $shamt", 
	(set (gpr $rd), (shl (gpr $rt), $shamt))
>;

def SLLV : RType<0b000100, "sllv %$rd, %$rt, %$rs", 
	(set (gpr $rd), (shl (gpr $rt), (gpr $rs)))
>;

def SLT : RType<0b101010, "slt %$rd, %$rs, %$rt", 
	(if (lt (signed (gpr $rs)), (signed (gpr $rt))),
		(set (gpr $rd), 1), 
		(set (gpr $rd), 0))
>;

def SLTI : IType<0b001010, "slti %$rt, %$rs, $eimm", 
	(let $eimm, (signext 16, $imm), 
		(if (lt (signed (gpr $rs)), $eimm),
			(set (gpr $rt), 1), 
			(set (gpr $rt), 0)))
>;

def SLTIU : IType<0b001011, "sltiu %$rt, %$rs, $eimm", 
	(let $eimm, (unsigned (signext 16, $imm)), 
		(if (lt (unsigned (gpr $rs)), $eimm),
			(set (gpr $rt), 1), 
			(set (gpr $rt), 0)))
>;

def SLTU : RType<0b101011, "sltu %$rd, %$rs, %$rt", 
	(if (lt (unsigned (gpr $rs)), (unsigned (gpr $rt))),
		(set (gpr $rd), 1), 
		(set (gpr $rd), 0))
>;

def SRA : RType<0b000011, "sra %$rd, %$rt, $shamt", 
	(set (gpr $rd), (shra (gpr $rt), $shamt))
>;

def SRAV : RType<0b000111, "srav %$rd, %$rt, $shamt", 
	(set (gpr $rd), (shra (gpr $rt), (gpr $rs)))
>;

def SRL : RType<0b000010, "srl %$rd, %$rt, $shamt", 
	(set (gpr $rd), (shrl (gpr $rt), $shamt))
>;

def SRLV : RType<0b000110, "srlv %$rd, %$rt, $shamt", 
	(set (gpr $rd), (shrl (gpr $rt), (gpr $rs)))
>;

def SUB : RType<0b100010, "sub %$rd, %$rs, %$rt", 
	(block
		(check_overflow (sub (gpr $rs), (gpr $rt))), 
		(set (gpr $rd), (sub (gpr $rs), (gpr $rt))))
>;

def SUBU : RType<0b100011, "subu %$rd, %$rs, %$rt", 
	(set (gpr $rd), (sub (gpr $rs), (gpr $rt)))
>;

def SW : IType<0b101011, "sw %$rt, $offset(%$rs)", 
	(let $offset, (signext 16, $imm), 
		(store 32, (unsigned (add (gpr $rs), $offset)), (gpr $rt)))
>;

// XXX: SWCz instruction
// XXX: SWL instruction
// XXX: SWR instruction

def SYSCALL : SType<0b001100, "syscall $code", 
	(syscall $code)
>;

def XOR : RType<0b100110, "xor %$rd, %$rs, %$rt", 
	(set (gpr $rd), (xor (gpr $rs), (gpr $rt)))
>;

def XORI : IType<0b001110, "xori %$rt, %$rs, $eimm", 
	(let $eimm, (zeroext 16, $imm), 
		(set (gpr $rt), (xor (gpr $rs), $eimm)))
>;
