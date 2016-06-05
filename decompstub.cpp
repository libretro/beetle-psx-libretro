#include <jit/jit.h>
#include <stdint.h>

// This is passed as an array of uint32_t
typedef struct state_s {
	uint32_t reg[32];
	uint32_t pc;
	uint32_t hi, lo;
} state_t;

bool decompile(jit_function_t func, jit_value_t state, uint32_t pc, uint32_t inst, bool &branched);

jit_value_t _make_uint(jit_function_t func, uint32_t val) {
	return jit_value_create_nint_constant(func, jit_type_uint, val);
}
#define make_uint(val) _make_uint(func, (val))

jit_type_t sig_1, sig_2, sig_3;
void store_memory(int size, uint32_t ptr, uint32_t val) {
}

void call_store_memory(jit_function_t func, int size, jit_value_t ptr, jit_value_t val) {
	jit_value_t args[] = {make_uint(size), ptr, val};
	jit_insn_call_native(func, 0, (void *) store_memory, sig_3, args, 3, 0);
}

uint32_t load_memory(int size, uint32_t ptr) {
	return 0;
}

jit_value_t call_load_memory(jit_function_t func, int size, jit_value_t ptr) {
	jit_value_t args[] = {make_uint(size), ptr};
	return jit_insn_call_native(func, 0, (void *) load_memory, sig_2, args, 2, 0);
}

uint32_t read_copreg(int cop, int reg) {
	return 0;
}

jit_value_t call_read_copreg(jit_function_t func, int cop, int reg) {
	jit_value_t args[] = {make_uint(cop), make_uint(reg)};
	return jit_insn_call_native(func, 0, (void *) read_copreg, sig_2, args, 2, 0);
}

uint32_t read_copcreg(int cop, int reg) {
	return 0;
}

jit_value_t call_read_copcreg(jit_function_t func, int cop, int reg) {
	jit_value_t args[] = {make_uint(cop), make_uint(reg)};
	return jit_insn_call_native(func, 0, (void *) read_copcreg, sig_2, args, 2, 0);
}

void write_copreg(int cop, int reg, uint32_t val) {
}

void call_write_copreg(jit_function_t func, int cop, int reg, jit_value_t val) {
	jit_value_t args[] = {make_uint(cop), make_uint(reg), val};
	jit_insn_call_native(func, 0, (void *) write_copreg, sig_3, args, 3, 0);
}

void write_copcreg(int cop, int reg, uint32_t val) {
}

void call_write_copcreg(jit_function_t func, int cop, int reg, jit_value_t val) {
	jit_value_t args[] = {make_uint(cop), make_uint(reg), val};
	jit_insn_call_native(func, 0, (void *) write_copcreg, sig_3, args, 3, 0);
}

void copfun(int cop, int cofun) {
}

jit_value_t call_copfun(jit_function_t func, int cop, int cofun) {
	jit_value_t args[] = {make_uint(cop), make_uint(cofun)};
	return jit_insn_call_native(func, 0, (void *) copfun, sig_2, args, 2, 0);
}

int32_t signext(int size, uint32_t imm) {
	if(size == 8)
		return (int32_t) ((int8_t) ((uint8_t) imm));
	else if(size == 16)
		return (int32_t) ((int16_t) ((uint16_t) imm));
	return (int32_t) imm;
}

jit_value_t call_signext(jit_function_t func, int size, jit_value_t val) {
	jit_value_t args[] = {make_uint(size), val};
	return jit_insn_call_native(func, 0, (void *) signext, sig_2, args, 2, 0);
}

void syscall(int code) {
}

void call_syscall(jit_function_t func, uint32_t code) {
	jit_value_t args[] = {make_uint(code)};
	jit_insn_call_native(func, 0, (void *) syscall, sig_1, args, 1, 0);
}

void break_(int code) {
}

void call_break(jit_function_t func, uint32_t code) {
	jit_value_t args[] = {make_uint(code)};
	jit_insn_call_native(func, 0, (void *) break_, sig_1, args, 1, 0);
}

void branch(uint32_t target) {
}

void call_branch(jit_function_t func, jit_value_t val) {
	jit_value_t args[] = {val};
	jit_insn_call_native(func, 0, (void *) branch, sig_1, args, 1, 0);
}

void overflow(uint32_t a, uint32_t b, int dir) {
}

void call_overflow(jit_function_t func, jit_value_t a, jit_value_t b, int dir) {
	jit_value_t args[] = {a, b, make_uint(dir)};
	jit_insn_call_native(func, 0, (void *) overflow, sig_3, args, 3, 0);
}

jit_context_t context;

jit_type_t block_sig;

void init_decompiler() {
	context = jit_context_create();
	jit_context_build_start(context);

	jit_type_t s3params[3];
	s3params[0] = jit_type_uint;
	s3params[1] = jit_type_uint;
	s3params[2] = jit_type_uint;
	sig_3 = jit_type_create_signature(jit_abi_cdecl, jit_type_uint, s3params, 3, 1);
	
	jit_type_t sparams[2];
	sparams[0] = jit_type_uint;
	sparams[1] = jit_type_uint;
	sig_2 = jit_type_create_signature(jit_abi_cdecl, jit_type_uint, sparams, 2, 1);
	
	jit_type_t lparams[2];
	lparams[0] = jit_type_uint;
	sig_1 = jit_type_create_signature(jit_abi_cdecl, jit_type_uint, lparams, 1, 1);

	jit_type_t params[1];
	params[0] = jit_type_create_pointer(jit_type_uint, 0);
	block_sig = jit_type_create_signature(jit_abi_cdecl, jit_type_void, params, 1, 1);
}

jit_function_t create_function() {
	auto func = jit_function_create(context, signature);
	auto statevar = jit_value_get_param(func, 0);
	decompile(func, statevar, 0xDEADBEE0, 0x0, branched);
	return 0;
}