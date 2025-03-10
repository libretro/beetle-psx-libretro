/*
 * Copyright (C) 2012-2023  Free Software Foundation, Inc.
 *
 * This file is part of GNU lightning.
 *
 * GNU lightning is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * GNU lightning is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * Authors:
 *	Paulo Cesar Pereira de Andrade
 */
#define CHECK_POPCNTB	0

#if CHECK_POPCNTB
#include <signal.h>
#include <setjmp.h>
#endif

#define jit_arg_reg_p(i)		((i) >= 0 && (i) < 8)
#if !_CALL_SYSV
#  define jit_arg_f_reg_p(i)		((i) >= 0 && (i) < 13)
#else
#  define jit_arg_f_reg_p(i)		((i) >= 0 && (i) < 8)
#  if __WORDSIZE == 32
#    define va_gp_shift			2
#  else
#    define va_gp_shift			3
#  endif
#  define va_gp_increment		sizeof(jit_word_t)
#  define first_gp_argument		r3
#  define first_gp_offset		offsetof(jit_va_list_t,		\
						 first_gp_argument)
#  define va_fp_increment		sizeof(jit_float64_t)
#  define first_fp_argument		f1
#  define first_fp_offset		offsetof(jit_va_list_t,		\
						 first_fp_argument)
#endif
#if __BYTE_ORDER == __LITTLE_ENDIAN
#  define C_DISP			0
#  define S_DISP			0
#  define I_DISP			0
#  define F_DISP			0
#else
#  define C_DISP			(__WORDSIZE >> 3) - sizeof(jit_int8_t)
#  define S_DISP			(__WORDSIZE >> 3) - sizeof(jit_int16_t)
#  define I_DISP			(__WORDSIZE >> 3) - sizeof(jit_int32_t)
#  define F_DISP			(__WORDSIZE >> 3) - sizeof(jit_float32_t)
#endif
#define CVT_OFFSET			_jitc->function->cvt_offset
#define CHECK_CVT_OFFSET()						\
    do {								\
	if (!_jitc->function->cvt_offset) {				\
	    _jitc->again = 1;						\
	    _jitc->function->cvt_offset =				\
		 jit_allocai(sizeof(jit_float64_t));			\
	}								\
    } while (0)

/*
 * Types
 */
#if _CALL_SYSV
typedef struct jit_va_list {
    jit_uint8_t		ngpr;
    jit_uint8_t		nfpr;
    jit_uint16_t	_pad;
#  if __WORDSIZE == 64
    jit_uint32_t	_pad2;
#  endif
    jit_pointer_t	over;
    jit_pointer_t	save;
#  if __WORDSIZE == 32
    jit_word_t		_pad2;
#  endif
    jit_word_t		r3;
    jit_word_t		r4;
    jit_word_t		r5;
    jit_word_t		r6;
    jit_word_t		r7;
    jit_word_t		r8;
    jit_word_t		r9;
    jit_word_t		r10;
    jit_float64_t	f1;
    jit_float64_t	f2;
    jit_float64_t	f3;
    jit_float64_t	f4;
    jit_float64_t	f5;
    jit_float64_t	f6;
    jit_float64_t	f7;
    jit_float64_t	f8;
} jit_va_list_t;
#else
typedef jit_pointer_t jit_va_list_t;
#endif

/*
 * Prototypes
 */
#define patch(instr, node)		_patch(_jit, instr, node)
static void _patch(jit_state_t*,jit_word_t,jit_node_t*);

/* libgcc */
extern void __clear_cache(void *, void *);

#define PROTO				1
#  include "jit_ppc-cpu.c"
#  include "jit_ppc-fpu.c"
#  include "jit_fallback.c"
#undef PROTO

/*
 * Initialization
 */
jit_cpu_t		jit_cpu;
jit_register_t		_rvs[] = {
    { rc(sav) | 0,			"r0" },
    { rc(sav) | 11,			"r11" },	/* env */
    { rc(sav) | 12,			"r12" },	/* exception */
    { rc(sav) | 13,			"r13" },	/* thread */
    { rc(sav) | 2,			"r2" },		/* toc */
    { rc(sav) | rc(gpr) | 14,		"r14" },
    { rc(sav) | rc(gpr) | 15,		"r15" },
    { rc(sav) | rc(gpr) | 16,		"r16" },
    { rc(sav) | rc(gpr) | 17,		"r17" },
    { rc(sav) | rc(gpr) | 18,		"r18" },
    { rc(sav) | rc(gpr) | 19,		"r19" },
    { rc(sav) | rc(gpr) | 20,		"r20" },
    { rc(sav) | rc(gpr) | 21,		"r21" },
    { rc(sav) | rc(gpr) | 22,		"r22" },
    { rc(sav) | rc(gpr) | 23,		"r23" },
    { rc(sav) | rc(gpr) | 24,		"r24" },
    { rc(sav) | rc(gpr) | 25,		"r25" },
    { rc(sav) | rc(gpr) | 26,		"r26" },
    { rc(sav) | rc(gpr) | 27,		"r27" },
    { rc(sav) | rc(gpr) | 28,		"r28" },
    { rc(sav) | rc(gpr) | 29,		"r29" },
    { rc(sav) | rc(gpr) | 30,		"r30" },
    { rc(sav) | 1,			"r1" },
    { rc(sav) | 31,			"r31" },
    { rc(arg) | rc(gpr) | 10,		"r10" },
    { rc(arg) | rc(gpr) | 9,		"r9" },
    { rc(arg) | rc(gpr) | 8,		"r8" },
    { rc(arg) | rc(gpr) | 7,		"r7" },
    { rc(arg) | rc(gpr) | 6,		"r6" },
    { rc(arg) | rc(gpr) | 5,		"r5" },
    { rc(arg) | rc(gpr) | 4,		"r4" },
    { rc(arg) | rc(gpr) | 3,		"r3" },
    { rc(fpr) | 0,			"f0" },
    { rc(sav) | rc(fpr) | 14,		"f14" },
    { rc(sav) | rc(fpr) | 15,		"f15" },
    { rc(sav) | rc(fpr) | 16,		"f16" },
    { rc(sav) | rc(fpr) | 17,		"f17" },
    { rc(sav) | rc(fpr) | 18,		"f18" },
    { rc(sav) | rc(fpr) | 19,		"f19" },
    { rc(sav) | rc(fpr) | 20,		"f20" },
    { rc(sav) | rc(fpr) | 21,		"f21" },
    { rc(sav) | rc(fpr) | 22,		"f22" },
    { rc(sav) | rc(fpr) | 23,		"f23" },
    { rc(sav) | rc(fpr) | 24,		"f24" },
    { rc(sav) | rc(fpr) | 25,		"f25" },
    { rc(sav) | rc(fpr) | 26,		"f26" },
    { rc(sav) | rc(fpr) | 27,		"f27" },
    { rc(sav) | rc(fpr) | 28,		"f28" },
    { rc(sav) | rc(fpr) | 29,		"f29" },
    { rc(sav) | rc(fpr) | 30,		"f30" },
    { rc(sav) | rc(fpr) | 31,		"f31" },
#if !_CALL_SYSV
    { rc(arg) | rc(fpr) | 13,		"f13" },
    { rc(arg) | rc(fpr) | 12,		"f12" },
    { rc(arg) | rc(fpr) | 11,		"f11" },
    { rc(arg) | rc(fpr) | 10,		"f10" },
    { rc(arg) | rc(fpr) | 9,		"f9" },
#else
    { rc(fpr) | 13,			"f13" },
    { rc(fpr) | 12,			"f12" },
    { rc(fpr) | 11,			"f11" },
    { rc(fpr) | 10,			"f10" },
    { rc(fpr) | 9,			"f9" },
#endif
    { rc(arg) | rc(fpr) | 8,		"f8" },
    { rc(arg) | rc(fpr) | 7,		"f7" },
    { rc(arg) | rc(fpr) | 6,		"f6" },
    { rc(arg) | rc(fpr) | 5,		"f5" },
    { rc(arg) | rc(fpr) | 4,		"f4" },
    { rc(arg) | rc(fpr) | 3,		"f3" },
    { rc(arg) | rc(fpr) | 2,		"f2" },
    { rc(arg) | rc(fpr) | 1,		"f1" },
    { _NOREG,				"<none>" },
};
#if CHECK_POPCNTB
static sigjmp_buf	jit_env;
#endif

static jit_int32_t iregs[] = {
    _R14, _R15, _R16, _R17, _R18, _R19, _R20, _R21, _R22,
    _R23, _R24, _R25, _R26, _R27, _R28, _R29, _R30
};

static jit_int32_t fregs[] = {
    _F14, _F15, _F16, _F17, _F18, _F19, _F20, _F21,
};

/*
 * Implementation
 */
#if CHECK_POPCNTB
static void
sigill_handler(int signum)
{
    jit_cpu.popcntb = 0;
    siglongjmp(jit_env, 1);
}
#endif

void
jit_get_cpu(void)
{
#if CHECK_POPCNTB
    long		r12;
    struct		sigaction new_action, old_action;
    new_action.sa_handler = sigill_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGILL, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN) {
	sigaction(SIGILL, &new_action, NULL);
	if (!sigsetjmp(jit_env, 1)) {
	    jit_cpu.popcntb = 1;
	    /* popcntb %r12, %r12 */
	    __asm__ volatile("mr %%r12, %0;"
			     "popcntb %%r12, %%r12;"
			     "mr %0, %%r12;"
			     : "=r" (r12), "=r" (r12));
	    sigaction(SIGILL, &old_action, NULL);
	}
    }
#elif defined(__linux__)
    FILE	*fp;
    char	*ptr;
    long	 vers;
    char	 buf[128];

    if ((fp = fopen("/proc/cpuinfo", "r")) != NULL) {
	while (fgets(buf, sizeof(buf), fp)) {
	    if (strncmp(buf, "cpu\t\t: POWER", 12) == 0) {
		vers = strtol(buf + 12, &ptr, 10);
		jit_cpu.popcntb = vers > 5;
		break;
	    }
	}
	fclose(fp);
    }
#else
    /* By default, assume it is not available */
    jit_cpu.popcntb = 0;
#endif
}

void
_jit_init(jit_state_t *_jit)
{
    _jitc->reglen = jit_size(_rvs) - 1;
}

void
_jit_prolog(jit_state_t *_jit)
{
    jit_int32_t		 offset;

    if (_jitc->function)
	jit_epilog();
    assert(jit_regset_cmp_ui(&_jitc->regarg, 0) == 0);
    jit_regset_set_ui(&_jitc->regsav, 0);
    offset = _jitc->functions.offset;
    if (offset >= _jitc->functions.length) {
	jit_realloc((jit_pointer_t *)&_jitc->functions.ptr,
		    _jitc->functions.length * sizeof(jit_function_t),
		    (_jitc->functions.length + 16) * sizeof(jit_function_t));
	_jitc->functions.length += 16;
    }
    _jitc->function = _jitc->functions.ptr + _jitc->functions.offset++;
    _jitc->function->self.size = params_offset;
    _jitc->function->self.argi = _jitc->function->self.argf =
	_jitc->function->self.alen = 0;
    /* float conversion */
    _jitc->function->cvt_offset = 0;
    _jitc->function->self.aoff = alloca_offset - 8;
    _jitc->function->self.call = jit_call_default;
    jit_alloc((jit_pointer_t *)&_jitc->function->regoff,
	      _jitc->reglen * sizeof(jit_int32_t));

    /* _no_link here does not mean the jit_link() call can be removed
     * by rewriting as:
     * _jitc->function->prolog = jit_new_node(jit_code_prolog);
     */
    _jitc->function->prolog = jit_new_node_no_link(jit_code_prolog);
    jit_link(_jitc->function->prolog);
    _jitc->function->prolog->w.w = offset;
    _jitc->function->epilog = jit_new_node_no_link(jit_code_epilog);
    /*	u:	label value
     *	v:	offset in blocks vector
     *	w:	offset in functions vector
     */
    _jitc->function->epilog->w.w = offset;

    jit_regset_new(&_jitc->function->regset);
}

jit_int32_t
_jit_allocai(jit_state_t *_jit, jit_int32_t length)
{
    assert(_jitc->function);
    jit_check_frame();
    switch (length) {
	case 0:	case 1:						break;
	case 2:		_jitc->function->self.aoff &= -2;	break;
	case 3:	case 4:	_jitc->function->self.aoff &= -4;	break;
	default:	_jitc->function->self.aoff &= -8;	break;
    }
    _jitc->function->self.aoff -= length;
    if (!_jitc->realize) {
	jit_inc_synth_ww(allocai, _jitc->function->self.aoff, length);
	jit_dec_synth();
    }
    return (_jitc->function->self.aoff);
}

void
_jit_allocar(jit_state_t *_jit, jit_int32_t u, jit_int32_t v)
{
    jit_int32_t		 r0, r1;
    assert(_jitc->function);
    jit_inc_synth_ww(allocar, u, v);
    if (!_jitc->function->allocar) {
	_jitc->function->aoffoff = jit_allocai(sizeof(jit_int32_t));
	_jitc->function->allocar = 1;
    }
    r0 = jit_get_reg(jit_class_gpr);
    r1 = jit_get_reg(jit_class_gpr);
    jit_ldr(r0, JIT_SP);
    jit_negr(r1, v);
    jit_andi(r1, r1, -16);
    jit_ldxi_i(u, JIT_FP, _jitc->function->aoffoff);
    jit_addr(u, u, r1);
    jit_addr(JIT_SP, JIT_SP, r1);
    jit_stxi_i(_jitc->function->aoffoff, JIT_FP, u);
    jit_str(JIT_SP, r0);
    jit_unget_reg(r1);
    jit_unget_reg(r0);
    jit_dec_synth();
}

void
_jit_ret(jit_state_t *_jit)
{
    jit_node_t		*instr;
    assert(_jitc->function);
    jit_inc_synth(ret);
    /* jump to epilog */
    instr = jit_jmpi();
    jit_patch_at(instr, _jitc->function->epilog);
    jit_dec_synth();
}

void
_jit_retr(jit_state_t *_jit, jit_int32_t u, jit_code_t code)
{
    jit_code_inc_synth_w(code, u);
    jit_movr(JIT_RET, u);
    jit_ret();
    jit_dec_synth();
}

void
_jit_reti(jit_state_t *_jit, jit_word_t u, jit_code_t code)
{
    jit_code_inc_synth_w(code, u);
    jit_movi(JIT_RET, u);
    jit_ret();
    jit_dec_synth();
}

void
_jit_retr_f(jit_state_t *_jit, jit_int32_t u)
{
    jit_inc_synth_w(retr_f, u);
    if (JIT_RET != u)
	jit_movr_f(JIT_FRET, u);
    else
	jit_live(JIT_FRET);
    jit_ret();
    jit_dec_synth();
}

void
_jit_reti_f(jit_state_t *_jit, jit_float32_t u)
{
    jit_inc_synth_f(reti_f, u);
    jit_movi_f(JIT_FRET, u);
    jit_ret();
    jit_dec_synth();
}

void
_jit_retr_d(jit_state_t *_jit, jit_int32_t u)
{
    jit_inc_synth_w(retr_d, u);
    if (JIT_FRET != u)
	jit_movr_d(JIT_FRET, u);
    else
	jit_live(JIT_FRET);
    jit_ret();
    jit_dec_synth();
}

void
_jit_reti_d(jit_state_t *_jit, jit_float64_t u)
{
    jit_inc_synth_d(reti_d, u);
    jit_movi_d(JIT_FRET, u);
    jit_ret();
    jit_dec_synth();
}

void
_jit_epilog(jit_state_t *_jit)
{
    assert(_jitc->function);
    assert(_jitc->function->epilog->next == NULL);
    jit_link(_jitc->function->epilog);
    _jitc->function = NULL;
}

jit_bool_t
_jit_arg_register_p(jit_state_t *_jit, jit_node_t *u)
{
    if (u->code >= jit_code_arg_c && u->code <= jit_code_arg)
	return (jit_arg_reg_p(u->u.w));
    assert(u->code == jit_code_arg_f || u->code == jit_code_arg_d);
    return (jit_arg_f_reg_p(u->u.w));
}

void
_jit_ellipsis(jit_state_t *_jit)
{
    jit_inc_synth(ellipsis);
    if (_jitc->prepare) {
	jit_link_prepare();
	assert(!(_jitc->function->call.call & jit_call_varargs));
	_jitc->function->call.call |= jit_call_varargs;
    }
    else {
	jit_link_prolog();
	assert(!(_jitc->function->self.call & jit_call_varargs));
	_jitc->function->self.call |= jit_call_varargs;
#if _CALL_SYSV
	/* Allocate va_list like object in the stack.
	 * If applicable, with enough space to save all argument
	 * registers, and use fixed offsets for them. */
	_jitc->function->vaoff = jit_allocai(sizeof(jit_va_list_t));
#endif
	_jitc->function->vagp = _jitc->function->self.argi;
	_jitc->function->vafp = _jitc->function->self.argf;
    }
    jit_dec_synth();
}

void
_jit_va_push(jit_state_t *_jit, jit_int32_t u)
{
    jit_inc_synth_w(va_push, u);
    jit_pushargr(u);
    jit_dec_synth();
}

jit_node_t *
_jit_arg(jit_state_t *_jit, jit_code_t code)
{
    jit_node_t		*node;
    jit_int32_t		 offset;
    jit_bool_t		 incr = 1;
    assert(_jitc->function);
    assert(!(_jitc->function->self.call & jit_call_varargs));
#if STRONG_TYPE_CHECKING
    assert(code >= jit_code_arg_c && code <= jit_code_arg);
#endif
    if (jit_arg_reg_p(_jitc->function->self.argi)) {
	offset = _jitc->function->self.argi++;
#if _CALL_SYSV
	incr = 0;
#endif
    }
    else
	offset = _jitc->function->self.size;
    if (incr)
	_jitc->function->self.size += sizeof(jit_word_t);
    node = jit_new_node_ww(code, offset,
			   ++_jitc->function->self.argn);
    jit_link_prolog();
    return (node);
}

jit_node_t *
_jit_arg_f(jit_state_t *_jit)
{
    jit_node_t		*node;
    jit_int32_t		 offset;
    jit_bool_t		 incr = 1;
    assert(_jitc->function);
    if (jit_arg_f_reg_p(_jitc->function->self.argf)) {
	offset = _jitc->function->self.argf++;
#if _CALL_SYSV
	incr = 0;
#endif
    }
    else
	offset = _jitc->function->self.size + F_DISP;
#if !_CALL_SYSV
    if (jit_arg_reg_p(_jitc->function->self.argi)) {
#  if __WORDSIZE == 32
	_jitc->function->self.argi += 2;
#  else
	_jitc->function->self.argi++;
#  endif
    }
#endif
    if (incr)
	_jitc->function->self.size += sizeof(jit_word_t);
    node = jit_new_node_ww(jit_code_arg_f, offset,
			   ++_jitc->function->self.argn);
    jit_link_prolog();
    return (node);
}

jit_node_t *
_jit_arg_d(jit_state_t *_jit)
{
    jit_node_t		*node;
    jit_int32_t		 offset;
    jit_bool_t		 incr = 1;
    assert(_jitc->function);
    if (jit_arg_f_reg_p(_jitc->function->self.argf)) {
	offset = _jitc->function->self.argf++;
#if _CALL_SYSV
	incr = 0;
#endif
    }
    else {
#if _CALL_SYSV
	if (_jitc->function->self.size & 7)
	    _jitc->function->self.size += 4;
#endif
	offset = _jitc->function->self.size;
    }
#if !_CALL_SYSV
    if (jit_arg_reg_p(_jitc->function->self.argi)) {
#  if __WORDSIZE == 32
	_jitc->function->self.argi += 2;
#  else
	_jitc->function->self.argi++;
#  endif
    }
#endif
    if (incr)
	_jitc->function->self.size += sizeof(jit_float64_t);
    node = jit_new_node_ww(jit_code_arg_d, offset,
			   ++_jitc->function->self.argn);
    jit_link_prolog();
    return (node);
}

void
_jit_getarg_c(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert_arg_type(v->code, jit_code_arg_c);
    jit_inc_synth_wp(getarg_c, u, v);
    if (jit_arg_reg_p(v->u.w))
	jit_extr_c(u, JIT_RA0 - v->u.w);
    else {
	jit_check_frame();
	jit_ldxi_c(u, JIT_FP, v->u.w + C_DISP);
    }
    jit_dec_synth();
}

void
_jit_getarg_uc(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert_arg_type(v->code, jit_code_arg_c);
    jit_inc_synth_wp(getarg_uc, u, v);
    if (jit_arg_reg_p(v->u.w))
	jit_extr_uc(u, JIT_RA0 - v->u.w);
    else {
	jit_check_frame();
	jit_ldxi_uc(u, JIT_FP, v->u.w + C_DISP);
    }
    jit_dec_synth();
}

void
_jit_getarg_s(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert_arg_type(v->code, jit_code_arg_s);
    jit_inc_synth_wp(getarg_s, u, v);
    if (jit_arg_reg_p(v->u.w))
	jit_extr_s(u, JIT_RA0 - v->u.w);
    else {
	jit_check_frame();
	jit_ldxi_s(u, JIT_FP, v->u.w + S_DISP);
    }
    jit_dec_synth();
}

void
_jit_getarg_us(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert_arg_type(v->code, jit_code_arg_s);
    jit_inc_synth_wp(getarg_us, u, v);
    if (jit_arg_reg_p(v->u.w))
	jit_extr_us(u, JIT_RA0 - v->u.w);
    else {
	jit_check_frame();
	jit_ldxi_us(u, JIT_FP, v->u.w + S_DISP);
    }
    jit_dec_synth();
}

void
_jit_getarg_i(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert_arg_type(v->code, jit_code_arg_i);
    jit_inc_synth_wp(getarg_i, u, v);
    if (jit_arg_reg_p(v->u.w)) {
#if __WORDSIZE == 32
	jit_movr(u, JIT_RA0 - v->u.w);
#else
	jit_extr_i(u, JIT_RA0 - v->u.w);
#endif
    }
    else {
	jit_check_frame();
	jit_ldxi_i(u, JIT_FP, v->u.w + I_DISP);
    }
    jit_dec_synth();
}

#if __WORDSIZE == 64
void
_jit_getarg_ui(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert_arg_type(v->code, jit_code_arg_i);
    jit_inc_synth_wp(getarg_ui, u, v);
    if (jit_arg_reg_p(v->u.w))
	jit_extr_ui(u, JIT_RA0 - v->u.w);
    else {
	jit_check_frame();
	jit_ldxi_ui(u, JIT_FP, v->u.w + I_DISP);
    }
    jit_dec_synth();
}

void
_jit_getarg_l(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert_arg_type(v->code, jit_code_arg_l);
    jit_inc_synth_wp(getarg_l, u, v);
    if (jit_arg_reg_p(v->u.w))
	jit_movr(u, JIT_RA0 - v->u.w);
    else {
	jit_check_frame();
	jit_ldxi_l(u, JIT_FP, v->u.w);
    }
    jit_dec_synth();
}
#endif

void
_jit_putargr(jit_state_t *_jit, jit_int32_t u, jit_node_t *v, jit_code_t code)
{
    assert_putarg_type(code, v->code);
    jit_code_inc_synth_wp(code, u, v);
    if (jit_arg_reg_p(v->u.w))
	jit_movr(JIT_RA0 - v->u.w, u);
    else {
	jit_check_frame();
	jit_stxi(v->u.w, JIT_FP, u);
    }
    jit_dec_synth();
}

void
_jit_putargi(jit_state_t *_jit, jit_word_t u, jit_node_t *v, jit_code_t code)
{
    jit_int32_t		regno;
    assert_putarg_type(code, v->code);
    jit_code_inc_synth_wp(code, u, v);
    if (jit_arg_reg_p(v->u.w))
	jit_movi(JIT_RA0 - v->u.w, u);
    else {
	jit_check_frame();
	regno = jit_get_reg(jit_class_gpr);
	jit_movi(regno, u);
	jit_stxi(v->u.w, JIT_FP, regno);
	jit_unget_reg(regno);
    }
    jit_dec_synth();
}

void
_jit_getarg_f(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert(v->code == jit_code_arg_f);
    jit_inc_synth_wp(getarg_f, u, v);
    if (jit_arg_f_reg_p(v->u.w))
	jit_movr_d(u, JIT_FA0 - v->u.w);
    else {
	jit_check_frame();
	jit_ldxi_f(u, JIT_FP, v->u.w);
    }
    jit_dec_synth();
}

void
_jit_putargr_f(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert(v->code == jit_code_arg_f);
    jit_inc_synth_wp(putargr_f, u, v);
    if (jit_arg_f_reg_p(v->u.w))
	jit_movr_d(JIT_FA0 - v->u.w, u);
    else {
	jit_check_frame();
	jit_stxi_f(v->u.w, JIT_FP, u);
    }
    jit_dec_synth();
}

void
_jit_putargi_f(jit_state_t *_jit, jit_float32_t u, jit_node_t *v)
{
    jit_int32_t		regno;
    assert(v->code == jit_code_arg_f);
    jit_inc_synth_fp(putargi_f, u, v);
    if (jit_arg_f_reg_p(v->u.w))
	jit_movi_d(JIT_FA0 - v->u.w, u);
    else {
	jit_check_frame();
	regno = jit_get_reg(jit_class_fpr);
	jit_movi_d(regno, u);
	jit_stxi_f(v->u.w, JIT_FP, regno);
	jit_unget_reg(regno);
    }
    jit_dec_synth();
}

void
_jit_getarg_d(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert(v->code == jit_code_arg_d);
    jit_inc_synth_wp(getarg_d, u, v);
    if (jit_arg_f_reg_p(v->u.w))
	jit_movr_d(u, JIT_FA0 - v->u.w);
    else {
	jit_check_frame();
	jit_ldxi_d(u, JIT_FP, v->u.w);
    }
    jit_dec_synth();
}

void
_jit_putargr_d(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    assert(v->code == jit_code_arg_d);
    jit_inc_synth_wp(putargr_d, u, v);
    if (jit_arg_f_reg_p(v->u.w))
	jit_movr_d(JIT_FA0 - v->u.w, u);
    else {
	jit_check_frame();
	jit_stxi_d(v->u.w, JIT_FP, u);
    }
    jit_dec_synth();
}

void
_jit_putargi_d(jit_state_t *_jit, jit_float64_t u, jit_node_t *v)
{
    jit_int32_t		regno;
    assert(v->code == jit_code_arg_d);
    jit_inc_synth_dp(putargi_d, u, v);
    if (jit_arg_f_reg_p(v->u.w))
	jit_movi_d(JIT_FA0 - v->u.w, u);
    else {
	jit_check_frame();
	regno = jit_get_reg(jit_class_fpr);
	jit_movi_d(regno, u);
	jit_stxi_d(v->u.w, JIT_FP, regno);
	jit_unget_reg(regno);
    }
    jit_dec_synth();
}

void
_jit_pushargr(jit_state_t *_jit, jit_int32_t u, jit_code_t code)
{
    jit_bool_t		incr = 1;
    assert(_jitc->function);
    jit_code_inc_synth_w(code, u);
    jit_link_prepare();
    if (jit_arg_reg_p(_jitc->function->call.argi)) {
	jit_movr(JIT_RA0 - _jitc->function->call.argi, u);
	++_jitc->function->call.argi;
#if _CALL_SYSV
	incr = 0;
#endif
    }
    else
	jit_stxi(_jitc->function->call.size + params_offset, JIT_SP, u);
    if (incr)
	_jitc->function->call.size += sizeof(jit_word_t);
    jit_dec_synth();
}

void
_jit_pushargi(jit_state_t *_jit, jit_word_t u, jit_code_t code)
{
    jit_int32_t		 regno;
    jit_bool_t		 incr = 1;
    assert(_jitc->function);
    jit_code_inc_synth_w(code, u);
    jit_link_prepare();
    if (jit_arg_reg_p(_jitc->function->call.argi)) {
	jit_movi(JIT_RA0 - _jitc->function->call.argi, u);
	++_jitc->function->call.argi;
#if _CALL_SYSV
	incr = 0;
#endif
    }
    else {
	regno = jit_get_reg(jit_class_gpr);
	jit_movi(regno, u);
	jit_stxi(_jitc->function->call.size + params_offset, JIT_SP, regno);
	jit_unget_reg(regno);
    }
    if (incr)
	_jitc->function->call.size += sizeof(jit_word_t);
    jit_dec_synth();
}

void
_jit_pushargr_f(jit_state_t *_jit, jit_int32_t u)
{
    jit_bool_t		incr = 1;
    assert(_jitc->function);
    jit_inc_synth_w(pushargr_f, u);
    jit_link_prepare();
    if (jit_arg_f_reg_p(_jitc->function->call.argf)
#if !_CALL_SYSV
	&& !(_jitc->function->call.call & jit_call_varargs)
#endif
	) {
	jit_movr_d(JIT_FA0 - _jitc->function->call.argf, u);
	++_jitc->function->call.argf;
#if !_CALL_SYSV
	/* in case of excess arguments */
	if (jit_arg_reg_p(_jitc->function->call.argi)) {
#  if __WORDSIZE == 32
	    _jitc->function->call.argi += 2;
	    if (!jit_arg_reg_p(_jitc->function->call.argi - 1))
		--_jitc->function->call.argi;
#  else
	    _jitc->function->call.argi++;
#  endif
	}
#elif _CALL_SYSV
	incr = 0;
#endif
    }
#if !_CALL_SYSV
    else if (jit_arg_reg_p(_jitc->function->call.argi
#  if __WORDSIZE == 32
			  + 1
#  endif
			   )) {
	CHECK_CVT_OFFSET();
	jit_check_frame();
	jit_stxi_d(CVT_OFFSET, JIT_FP, u);
	jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_FP, CVT_OFFSET);
	_jitc->function->call.argi++;
#  if __WORDSIZE == 32
	jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_FP,
		 CVT_OFFSET + 4);
	_jitc->function->call.argi++;
#  endif
    }
#endif
    else
	jit_stxi_f(_jitc->function->call.size + params_offset + F_DISP,
		   JIT_SP, u);
    if (incr)
	_jitc->function->call.size += sizeof(jit_word_t);
    jit_dec_synth();
}

void
_jit_pushargi_f(jit_state_t *_jit, jit_float32_t u)
{
    jit_bool_t		 incr = 1;
    jit_int32_t		 regno;
    assert(_jitc->function);
    jit_inc_synth_f(pushargi_f, u);
    jit_link_prepare();
    if (jit_arg_f_reg_p(_jitc->function->call.argf)
#if !_CALL_SYSV
	&& !(_jitc->function->call.call & jit_call_varargs)
#endif
	) {
	jit_movi_d(JIT_FA0 - _jitc->function->call.argf, u);
	++_jitc->function->call.argf;
#if !_CALL_SYSV
	    /* in case of excess arguments */
#  if __WORDSIZE == 32
	_jitc->function->call.argi += 2;
	if (!jit_arg_reg_p(_jitc->function->call.argi - 1))
	    --_jitc->function->call.argi;
#  else
	_jitc->function->call.argi++;
#  endif
#elif _CALL_SYSV
	incr = 0;
#endif
    }
    else {
	regno = jit_get_reg(jit_class_fpr);
	jit_movi_f(regno, u);
#if !_CALL_SYSV
	if (jit_arg_reg_p(_jitc->function->call.argi
#  if __WORDSIZE == 32
			  + 1
#  endif
			  )) {
	    CHECK_CVT_OFFSET();
	    jit_check_frame();
	    jit_stxi_d(CVT_OFFSET, JIT_FP, regno);
	    jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_FP,
		     CVT_OFFSET);
	    _jitc->function->call.argi++;
#  if __WORDSIZE == 32
	    jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_FP,
		     CVT_OFFSET + 4);
	    _jitc->function->call.argi++;
#  endif
	}
	else
#endif
	    jit_stxi_f(_jitc->function->call.size + params_offset + F_DISP,
		       JIT_SP, regno);
	jit_unget_reg(regno);
    }
    if (incr)
	_jitc->function->call.size += sizeof(jit_word_t);
    jit_dec_synth();
}

void
_jit_pushargr_d(jit_state_t *_jit, jit_int32_t u)
{
    jit_bool_t		incr = 1;
    assert(_jitc->function);
    jit_inc_synth_w(pushargr_d, u);
    jit_link_prepare();
    if (jit_arg_f_reg_p(_jitc->function->call.argf)
#if !_CALL_SYSV
	&& !(_jitc->function->call.call & jit_call_varargs)
#endif
	) {
	jit_movr_d(JIT_FA0 - _jitc->function->call.argf, u);
	++_jitc->function->call.argf;
#if !_CALL_SYSV
	    /* in case of excess arguments */
#  if __WORDSIZE == 32
	_jitc->function->call.argi += 2;
	if (!jit_arg_reg_p(_jitc->function->call.argi - 1))
	    --_jitc->function->call.argi;
#  else
	_jitc->function->call.argi++;
#  endif
#else /* _CALL_SYSV */
	incr = 0;
#endif
    }
#if !_CALL_SYSV
    else if (jit_arg_reg_p(_jitc->function->call.argi
#  if __WORDSIZE == 32
			  + 1
#  endif
			   )) {
	CHECK_CVT_OFFSET();
	jit_check_frame();
	jit_stxi_d(CVT_OFFSET, JIT_FP, u);
	jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_FP, CVT_OFFSET);
	_jitc->function->call.argi++;
#  if __WORDSIZE == 32
	jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_FP, CVT_OFFSET + 4);
	_jitc->function->call.argi++;
#  endif
    }
    else
#endif /* !_CALL_SYSV */
    {
#if _CALL_SYSV
	if (_jitc->function->call.size & 7)
	    _jitc->function->call.size += 4;
#endif
	jit_stxi_d(_jitc->function->call.size + params_offset, JIT_SP, u);
#if !_CALL_SYSV && __WORDSIZE == 32
	if (jit_arg_reg_p(_jitc->function->call.argi)) {
	    jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_SP,
		     _jitc->function->call.size + params_offset);
	    _jitc->function->call.argi++;
	}
#endif
    }
    if (incr)
	_jitc->function->call.size += sizeof(jit_float64_t);
    jit_dec_synth();
}

void
_jit_pushargi_d(jit_state_t *_jit, jit_float64_t u)
{
    jit_int32_t		 regno;
    jit_bool_t		 incr = 1;
    assert(_jitc->function);
    jit_inc_synth_d(pushargi_d, u);
    jit_link_prepare();
    if (jit_arg_f_reg_p(_jitc->function->call.argf)
#if !_CALL_SYSV
	&& !(_jitc->function->call.call & jit_call_varargs)
#endif
	) {
	jit_movi_d(JIT_FA0 - _jitc->function->call.argf, u);
	++_jitc->function->call.argf;
#if !_CALL_SYSV
	/* in case of excess arguments */
	if (jit_arg_reg_p(_jitc->function->call.argi)) {
#  if __WORDSIZE == 32
	    _jitc->function->call.argi += 2;
	    if (!jit_arg_reg_p(_jitc->function->call.argi - 1))
		--_jitc->function->call.argi;
#  else
	    _jitc->function->call.argi++;
#  endif
	}
#else /* _CALL_SYSV */
	    incr = 0;
#endif
    }
    else {
	regno = jit_get_reg(jit_class_fpr);
	jit_movi_d(regno, u);
#if !_CALL_SYSV
	if (jit_arg_reg_p(_jitc->function->call.argi
#  if __WORDSIZE == 32
			  + 1
#  endif
			  )) {
	    CHECK_CVT_OFFSET();
	    jit_check_frame();
	    jit_stxi_d(CVT_OFFSET, JIT_FP, regno);
	    jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_FP,
		     CVT_OFFSET);
	    _jitc->function->call.argi++;
#  if __WORDSIZE == 32
	    jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_FP,
		     CVT_OFFSET + 4);
	    _jitc->function->call.argi++;
#  endif
	}
	else
#endif /* !_CALL_SYSV */
	{
#if _CALL_SYSV
	    if (_jitc->function->call.size & 7)
		_jitc->function->call.size += 4;
#endif
	    jit_stxi_d(_jitc->function->call.size + params_offset,
		       JIT_SP, regno);
#if !_CALL_SYSV && __WORDSIZE == 32
	    if (jit_arg_reg_p(_jitc->function->call.argi)) {
		jit_ldxi(JIT_RA0 - _jitc->function->call.argi, JIT_SP,
			 _jitc->function->call.size + params_offset);
		_jitc->function->call.argi++;
	    }
#endif
	}
	jit_unget_reg(regno);
    }
    if (incr)
	_jitc->function->call.size += sizeof(jit_float64_t);
    jit_dec_synth();
}

jit_bool_t
_jit_regarg_p(jit_state_t *_jit, jit_node_t *node, jit_int32_t regno)
{
    jit_int32_t		spec;
    spec = jit_class(_rvs[regno].spec);
    if (spec & jit_class_arg) {
	if (spec & jit_class_gpr) {
	    regno = JIT_RA0 - regno;
	    if (regno >= 0 && regno < node->v.w)
		return (1);
	}
	else if (spec & jit_class_fpr) {
	    regno = JIT_FA0 - regno;
	    if (regno >= 0 && regno < node->w.w)
		return (1);
	}
    }
    return (0);
}

void
_jit_finishr(jit_state_t *_jit, jit_int32_t r0)
{
    jit_node_t		*call;
    assert(_jitc->function);
    jit_inc_synth_w(finishr, r0);
    if (_jitc->function->self.alen < _jitc->function->call.size)
	_jitc->function->self.alen = _jitc->function->call.size;
    call = jit_callr(r0);
    call->v.w = _jitc->function->call.argi;
    call->w.w = _jitc->function->call.argf;
#if _CALL_SYSV
    /* If passing float arguments in registers */
    if ((_jitc->function->call.call & jit_call_varargs) && call->w.w)
	call->flag |= jit_flag_varargs;
#endif
    _jitc->function->call.argi = _jitc->function->call.argf = 0;
    _jitc->prepare = 0;
    jit_dec_synth();
}

jit_node_t *
_jit_finishi(jit_state_t *_jit, jit_pointer_t i0)
{
    jit_node_t		*node;
    assert(_jitc->function);
    jit_inc_synth_w(finishi, (jit_word_t)i0);
    if (_jitc->function->self.alen < _jitc->function->call.size)
	_jitc->function->self.alen = _jitc->function->call.size;
    node = jit_calli(i0);
    node->v.w = _jitc->function->call.argi;
    node->w.w = _jitc->function->call.argf;
#if _CALL_SYSV
    if ((_jitc->function->call.call & jit_call_varargs) && node->w.w)
	node->flag |= jit_flag_varargs;
#endif
    _jitc->function->call.argi = _jitc->function->call.argf = 0;
    _jitc->prepare = 0;
    jit_dec_synth();
    return (node);
}

void
_jit_retval_c(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth(retval_c);
    jit_extr_c(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_uc(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth(retval_uc);
    jit_extr_uc(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_s(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth(retval_s);
    jit_extr_s(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_us(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth(retval_us);
    jit_extr_us(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_i(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth(retval_i);
#if __WORDSIZE == 32
    if (r0 != JIT_RET)
	jit_movr(r0, JIT_RET);
#else
    jit_extr_i(r0, JIT_RET);
#endif
    jit_dec_synth();
}

#if __WORDSIZE == 64
void
_jit_retval_ui(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth(retval_ui);
    jit_extr_ui(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_l(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth(retval_l);
    if (r0 != JIT_RET)
	jit_movr(r0, JIT_RET);
    jit_dec_synth();
}
#endif

void
_jit_retval_f(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth(retval_f);
    jit_retval_d(r0);
    jit_dec_synth();
}

void
_jit_retval_d(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth(retval_d);
    if (r0 != JIT_FRET)
	jit_movr_d(r0, JIT_FRET);
    jit_dec_synth();
}

jit_pointer_t
_emit_code(jit_state_t *_jit)
{
    jit_node_t		*node;
    jit_node_t		*temp;
    jit_word_t		 word;
    jit_int32_t		 value;
    jit_int32_t		 offset;
    jit_bool_t       no_flag = 0;	/* Set if previous instruction is
					 * *not* a jump target. */
    struct {
	jit_node_t	*node;
	jit_word_t	 word;
	jit_function_t	 func;
#if DEVEL_DISASSEMBLER
	jit_word_t	 prevw;
#endif
	jit_word_t	 patch_offset;
#if _CALL_AIXDESC
	jit_word_t	 prolog_offset;
#endif
    } undo;
#if DEVEL_DISASSEMBLER
    jit_word_t		 prevw;
#endif

    _jitc->function = NULL;

    jit_reglive_setup();

    undo.word = 0;
    undo.node = NULL;
    undo.patch_offset = 0;

#if DEVEL_DISASSEMBLER
    prevw = _jit->pc.w;
#endif
#if _CALL_AIXDESC
    undo.prolog_offset = 0;
    for (node = _jitc->head; node; node = node->next)
	if (node->code != jit_code_label &&
	    node->code != jit_code_note &&
	    node->code != jit_code_name)
	    break;
    if (node && (node->code != jit_code_prolog ||
		 !(_jitc->functions.ptr + node->w.w)->assume_frame)) {
	/* code may start with a jump so add an initial function descriptor */
	word = _jit->pc.w + sizeof(void*) * 3;
	iw(word);			/* addr */
	iw(0);				/* toc */
	iw(0);				/* env */
    }
#endif

#define case_rr(name, type)						\
	    case jit_code_##name##r##type:				\
		name##r##type(rn(node->u.w), rn(node->v.w));		\
		break
#define case_rw(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(rn(node->u.w), node->v.w);		\
		break
#define case_wr(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(node->u.w, rn(node->v.w));		\
		break
#define case_rrr(name, type)						\
	    case jit_code_##name##r##type:				\
		name##r##type(rn(node->u.w),				\
			      rn(node->v.w), rn(node->w.w));		\
		break
#define case_rrx(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(rn(node->u.w), rn(node->v.w), node->w.w);	\
	       break
#define case_rrX(name, type)						\
	    case jit_code_##name##r##type:				\
		name##r##type(rn(node->u.w),				\
			      rn(node->v.w), rn(node->w.w));		\
		break
#define case_xrr(name, type)						\
		case jit_code_##name##i##type:				\
		name##i##type(node->u.w, rn(node->v.w), rn(node->w.w));	\
		break
#define case_Xrr(name, type)						\
	    case jit_code_##name##r##type:				\
		name##r##type(rn(node->u.w), rn(node->v.w),		\
			      rn(node->w.w));				\
		break
#define case_rrrr(name, type)						\
	    case jit_code_##name##r##type:				\
		name##r##type(rn(node->u.q.l), rn(node->u.q.h),		\
			      rn(node->v.w), rn(node->w.w));		\
		break
#define case_rqr(name, type)						\
	    case jit_code_##name##r##type:				\
		name##r##type(rn(node->u.w), rn(node->v.q.l),		\
			      rn(node->v.q.h), rn(node->w.w));		\
	    case jit_code_##name##i##type:				\
		break;
#define case_rrw(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(rn(node->u.w), rn(node->v.w), node->w.w);	\
		break
#define case_rrrw(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(rn(node->u.q.l), rn(node->u.q.h),		\
			      rn(node->v.w), node->w.w);		\
		break
#define case_rrf(name, type, size)					\
	    case jit_code_##name##i##type:				\
		assert(node->flag & jit_flag_data);			\
		name##i##type(rn(node->u.w), rn(node->v.w),		\
			      (jit_float##size##_t *)node->w.n->u.w);	\
		break
#define case_wrr(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(node->u.w, rn(node->v.w), rn(node->w.w));	\
		break
#define case_brr(name, type)						\
	    case jit_code_##name##r##type:				\
		temp = node->u.n;					\
		assert(temp->code == jit_code_label ||			\
		       temp->code == jit_code_epilog);			\
		if (temp->flag & jit_flag_patch)			\
		    name##r##type(temp->u.w, rn(node->v.w),		\
				  rn(node->w.w));			\
		else {							\
		    word = name##r##type(_jit->pc.w,			\
					 rn(node->v.w), rn(node->w.w));	\
		    patch(word, node);					\
		}							\
		break
#define case_brw(name, type)						\
	    case jit_code_##name##i##type:				\
		temp = node->u.n;					\
		assert(temp->code == jit_code_label ||			\
		       temp->code == jit_code_epilog);			\
		if (temp->flag & jit_flag_patch)			\
		    name##i##type(temp->u.w,				\
				  rn(node->v.w), node->w.w);		\
		else {							\
		    word = name##i##type(_jit->pc.w,			\
					 rn(node->v.w), node->w.w);	\
		    patch(word, node);					\
		}							\
		break
#define case_brf(name, type, size)					\
	    case jit_code_##name##i##type:				\
		temp = node->u.n;					\
		assert(temp->code == jit_code_label ||			\
		       temp->code == jit_code_epilog);			\
		if (temp->flag & jit_flag_patch)			\
		    name##i##type(temp->u.w, rn(node->v.w),		\
				(jit_float##size##_t *)node->w.n->u.w);	\
		else {							\
		    word = name##i##type(_jit->pc.w, rn(node->v.w),	\
				(jit_float##size##_t *)node->w.n->u.w);	\
		    patch(word, node);					\
		}							\
		break
    for (node = _jitc->head; node; node = node->next) {
	if (_jit->pc.uc >= _jitc->code.end)
	    return (NULL);

#if DEVEL_DISASSEMBLER
	node->offset = (jit_uword_t)_jit->pc.w - (jit_uword_t)prevw;
	prevw = _jit->pc.w;
#endif
	value = jit_classify(node->code);
	jit_regarg_set(node, value);
	switch (node->code) {
	    case jit_code_align:
		/* Must align to a power of two */
		assert(!(node->u.w & (node->u.w - 1)));
		if ((word = _jit->pc.w & (node->u.w - 1)))
		    nop(node->u.w - word);
		break;
	    case jit_code_skip:
	        nop((node->u.w + 3) & ~3);
		break;
	    case jit_code_note:		case jit_code_name:
		node->u.w = _jit->pc.w;
		break;
	    case jit_code_label:
		/* remember label is defined */
		node->flag |= jit_flag_patch;
		node->u.w = _jit->pc.w;
		break;
		case_rrr(add,);
		case_rrw(add,);
		case_rrr(addc,);
		case_rrw(addc,);
		case_rrr(addx,);
		case_rrw(addx,);
		case_rrr(sub,);
		case_rrw(sub,);
		case_rrr(subc,);
		case_rrw(subc,);
		case_rrr(subx,);
		case_rrw(subx,);
		case_rrw(rsb,);
		case_rrr(mul,);
		case_rrw(mul,);
		case_rrr(hmul,);
		case_rrw(hmul,);
		case_rrr(hmul, _u);
		case_rrw(hmul, _u);
		case_rrrr(qmul,);
		case_rrrw(qmul,);
		case_rrrr(qmul, _u);
		case_rrrw(qmul, _u);
		case_rrr(div,);
		case_rrw(div,);
		case_rrr(div, _u);
		case_rrw(div, _u);
		case_rrrr(qdiv,);
		case_rrrw(qdiv,);
		case_rrrr(qdiv, _u);
		case_rrrw(qdiv, _u);
		case_rrr(rem,);
		case_rrw(rem,);
		case_rrr(rem, _u);
		case_rrw(rem, _u);
		case_rrr(and,);
		case_rrw(and,);
		case_rrr(or,);
		case_rrw(or,);
		case_rrr(xor,);
		case_rrw(xor,);
		case_rrr(lsh,);
		case_rrw(lsh,);
		case_rrrr(qlsh,);
		case_rrrw(qlsh,);
		case_rrrr(qlsh, _u);
		case_rrrw(qlsh, _u);
		case_rrr(rsh,);
		case_rrw(rsh,);
		case_rrr(rsh, _u);
		case_rrw(rsh, _u);
		case_rrrr(qrsh,);
		case_rrrw(qrsh,);
		case_rrrr(qrsh, _u);
		case_rrrw(qrsh, _u);
		case_rrr(lrot,);
		case_rrw(lrot,);
		case_rrr(rrot,);
		case_rrw(rrot,);
	    case jit_code_extr:
		extr(rn(node->u.w), rn(node->v.w), node->w.q.l, node->w.q.h);
		break;
	    case jit_code_extr_u:
		extr_u(rn(node->u.w), rn(node->v.w), node->w.q.l, node->w.q.h);
		break;
	    case jit_code_depr:
		depr(rn(node->u.w), rn(node->v.w), node->w.q.l, node->w.q.h);
		break;
	    case jit_code_depi:
		depi(rn(node->u.w), node->v.w, node->w.q.l, node->w.q.h);
		break;
		case_rr(ext, _c);
		case_rr(ext, _uc);
		case_rr(ext, _s);
		case_rr(ext, _us);
#  if __WORDSIZE == 64
		case_rr(ext, _i);
		case_rr(ext, _ui);
#  endif
		case_rr(hton, _us);
		case_rr(hton, _ui);
#  if __WORDSIZE == 64
		case_rr(hton, _ul);
#  endif
	    case jit_code_bswapr_us:
		bswapr_us_lh(rn(node->u.w), rn(node->v.w), no_flag);
		break;
	    case jit_code_bswapr_ui:
		bswapr_ui_lw(rn(node->u.w), rn(node->v.w), no_flag);
		break;
#  if __WORDSIZE == 64
		case_rr(bswap, _ul);
#  endif
		case_rr(neg,);
		case_rr(com,);
		case_rr(clo,);
		case_rr(clz,);
		case_rr(cto,);
		case_rr(ctz,);
#define rbitr(r0, r1)	fallback_rbit(r0, r1)
		case_rr(rbit,);
		case_rr(popcnt,);
	    case jit_code_casr:
		casr(rn(node->u.w), rn(node->v.w),
		     rn(node->w.q.l), rn(node->w.q.h));
		break;
	    case jit_code_casi:
		casi(rn(node->u.w), node->v.w,
		     rn(node->w.q.l), rn(node->w.q.h));
		break;
		case_rrr(movn,);
		case_rrr(movz,);
		case_rr(mov,);
	    case jit_code_movi:
		if (node->flag & jit_flag_node) {
		    temp = node->v.n;
		    if (temp->code == jit_code_data ||
			(temp->code == jit_code_label &&
			 (temp->flag & jit_flag_patch)))
			movi(rn(node->u.w), temp->u.w);
		    else {
			assert(temp->code == jit_code_label ||
			       temp->code == jit_code_epilog);
			word = movi_p(rn(node->u.w), node->v.w);
			patch(word, node);
		    }
		}
		else
		    movi(rn(node->u.w), node->v.w);
		break;
		case_rr(trunc, _f_i);
		case_rr(trunc, _d_i);
#  if __WORDSIZE == 64
		case_rr(trunc, _f_l);
		case_rr(trunc, _d_l);
#  endif
		case_rrr(lt,);
		case_rrw(lt,);
		case_rrr(lt, _u);
		case_rrw(lt, _u);
		case_rrr(le,);
		case_rrw(le,);
		case_rrr(le, _u);
		case_rrw(le, _u);
		case_rrr(eq,);
		case_rrw(eq,);
		case_rrr(ge,);
		case_rrw(ge,);
		case_rrr(ge, _u);
		case_rrw(ge, _u);
		case_rrr(gt,);
		case_rrw(gt,);
		case_rrr(gt, _u);
		case_rrw(gt, _u);
		case_rrr(ne,);
		case_rrw(ne,);
		case_rr(ld, _c);
		case_rw(ld, _c);
		case_brr(blt,);
		case_brw(blt,);
		case_brr(blt, _u);
		case_brw(blt, _u);
		case_brr(ble,);
		case_brw(ble,);
		case_brr(ble, _u);
		case_brw(ble, _u);
		case_brr(beq,);
		case_brw(beq,);
		case_brr(bge,);
		case_brw(bge,);
		case_brr(bge, _u);
		case_brw(bge, _u);
		case_brr(bgt,);
		case_brw(bgt,);
		case_brr(bgt, _u);
		case_brw(bgt, _u);
		case_brr(bne,);
		case_brw(bne,);
		case_brr(bms,);
		case_brw(bms,);
		case_brr(bmc,);
		case_brw(bmc,);
		case_brr(boadd,);
		case_brw(boadd,);
		case_brr(boadd, _u);
		case_brw(boadd, _u);
		case_brr(bxadd,);
		case_brw(bxadd,);
		case_brr(bxadd, _u);
		case_brw(bxadd, _u);
		case_brr(bosub,);
		case_brw(bosub,);
		case_brr(bosub, _u);
		case_brw(bosub, _u);
		case_brr(bxsub,);
		case_brw(bxsub,);
		case_brr(bxsub, _u);
		case_brw(bxsub, _u);
		case_rrr(ldx, _c);
		case_rrw(ldx, _c);
		case_rr(ld, _uc);
		case_rw(ld, _uc);
		case_rrr(ldx, _uc);
		case_rrw(ldx, _uc);
		case_rr(ld, _s);
		case_rw(ld, _s);
		case_rrr(ldx, _s);
		case_rrw(ldx, _s);
		case_rr(ld, _us);
		case_rw(ld, _us);
		case_rrr(ldx, _us);
		case_rrw(ldx, _us);
		case_rr(ld, _i);
		case_rw(ld, _i);
		case_rrr(ldx, _i);
		case_rrw(ldx, _i);
#if __WORDSIZE == 64
		case_rr(ld, _ui);
		case_rw(ld, _ui);
		case_rrr(ldx, _ui);
		case_rrw(ldx, _ui);
		case_rr(ld, _l);
		case_rw(ld, _l);
		case_rrr(ldx, _l);
		case_rrw(ldx, _l);
#endif
#define unldr(r0, r1, i0)	fallback_unldr(r0, r1, i0)
	    case jit_code_unldr:
		unldr(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
#define unldi(r0, i0, i1)	fallback_unldi(r0, i0, i1)
	    case jit_code_unldi:
		unldi(rn(node->u.w), node->v.w, node->w.w);
		break;
#define unldr_u(r0, r1, i0)	fallback_unldr_u(r0, r1, i0)
	    case jit_code_unldr_u:
		unldr_u(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
#define unldi_u(r0, i0, i1)	fallback_unldi_u(r0, i0, i1)
	    case jit_code_unldi_u:
		unldi_u(rn(node->u.w), node->v.w, node->w.w);
		break;
		case_rrx(ldxb, _c);	case_rrX(ldxb, _c);
		case_rrx(ldxa, _c);	case_rrX(ldxa, _c);
		case_rrx(ldxb, _uc);	case_rrX(ldxb, _uc);
		case_rrx(ldxa, _uc);	case_rrX(ldxa, _uc);
		case_rrx(ldxb, _s);	case_rrX(ldxb, _s);
		case_rrx(ldxa, _s);	case_rrX(ldxa, _s);
		case_rrx(ldxb, _us);	case_rrX(ldxb, _us);
		case_rrx(ldxa, _us);	case_rrX(ldxa, _us);
		case_rrx(ldxb, _i);	case_rrX(ldxb, _i);
		case_rrx(ldxa, _i);	case_rrX(ldxa, _i);
#if __WORDSIZE == 64
		case_rrx(ldxb, _ui);	case_rrX(ldxb, _ui);
		case_rrx(ldxa, _ui);	case_rrX(ldxa, _ui);
		case_rrx(ldxb, _l);	case_rrX(ldxb, _l);
		case_rrx(ldxa, _l);	case_rrX(ldxa, _l);
#endif
		case_rrx(ldxb, _f);	case_rrX(ldxb, _f);
		case_rrx(ldxa, _f);	case_rrX(ldxa, _f);
		case_rrx(ldxb, _d);	case_rrX(ldxb, _d);
		case_rrx(ldxa, _d);	case_rrX(ldxa, _d);
		case_rr(st, _c);
		case_wr(st, _c);
		case_rrr(stx, _c);
		case_wrr(stx, _c);
		case_rr(st, _s);
		case_wr(st, _s);
		case_rrr(stx, _s);
		case_wrr(stx, _s);
		case_rr(st, _i);
		case_wr(st, _i);
		case_rrr(stx, _i);
		case_wrr(stx, _i);
#if __WORDSIZE == 64
		case_rr(st, _l);
		case_wr(st, _l);
		case_rrr(stx, _l);
		case_wrr(stx, _l);
#endif
#define unstr(r0, r1, i0)	fallback_unstr(r0, r1, i0)
	    case jit_code_unstr:
		unstr(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
#define unsti(i0, r0, i1)	fallback_unsti(i0, r0, i1)
	    case jit_code_unsti:
		unsti(node->u.w, rn(node->v.w), node->w.w);
		break;
		case_xrr(stxb, _c);	case_Xrr(stxb, _c);
		case_xrr(stxa, _c);	case_Xrr(stxa, _c);
		case_xrr(stxb, _s);	case_Xrr(stxb, _s);
		case_xrr(stxa, _s);	case_Xrr(stxa, _s);
		case_xrr(stxb, _i);	case_Xrr(stxb, _i);
		case_xrr(stxa, _i);	case_Xrr(stxa, _i);
#if __WORDSIZE == 64
		case_xrr(stxb, _l);	case_rrX(stxb, _l);
		case_xrr(stxa, _l);	case_rrX(stxa, _l);
#endif
		case_xrr(stxb, _f);	case_rrX(stxb, _f);
		case_xrr(stxa, _f);	case_rrX(stxa, _f);
		case_xrr(stxb, _d);	case_rrX(stxb, _d);
		case_xrr(stxa, _d);	case_rrX(stxa, _d);
		case_rr(mov, _f);
	    case jit_code_movi_f:
		assert(node->flag & jit_flag_data);
		movi_f(rn(node->u.w), (jit_float32_t *)node->v.n->u.w);
		break;
		case_rr(ext, _f);
		case_rr(ext, _d_f);
		case_rr(abs, _f);
		case_rr(neg, _f);
		case_rr(sqrt, _f);
		case_rqr(fma, _f);
		case_rqr(fms, _f);
		case_rqr(fnma, _f);
		case_rqr(fnms, _f);
		case_rrr(add, _f);
		case_rrf(add, _f, 32);
		case_rrr(sub, _f);
		case_rrf(sub, _f, 32);
		case_rrf(rsb, _f, 32);
		case_rrr(mul, _f);
		case_rrf(mul, _f, 32);
		case_rrr(div, _f);
		case_rrf(div, _f, 32);
		case_rrr(lt, _f);
		case_rrf(lt, _f, 32);
		case_rrr(le, _f);
		case_rrf(le, _f, 32);
		case_rrr(eq, _f);
		case_rrf(eq, _f, 32);
		case_rrr(ge, _f);
		case_rrf(ge, _f, 32);
		case_rrr(gt, _f);
		case_rrf(gt, _f, 32);
		case_rrr(ne, _f);
		case_rrf(ne, _f, 32);
		case_rrr(unlt, _f);
		case_rrf(unlt, _f, 32);
		case_rrr(unle, _f);
		case_rrf(unle, _f, 32);
		case_rrr(uneq, _f);
		case_rrf(uneq, _f, 32);
		case_rrr(unge, _f);
		case_rrf(unge, _f, 32);
		case_rrr(ungt, _f);
		case_rrf(ungt, _f, 32);
		case_rrr(ltgt, _f);
		case_rrf(ltgt, _f, 32);
		case_rrr(ord, _f);
		case_rrf(ord, _f, 32);
		case_rrr(unord, _f);
		case_rrf(unord, _f, 32);
		case_brr(blt, _f);
		case_brf(blt, _f, 32);
		case_brr(ble, _f);
		case_brf(ble, _f, 32);
		case_brr(beq, _f);
		case_brf(beq, _f, 32);
		case_brr(bge, _f);
		case_brf(bge, _f, 32);
		case_brr(bgt, _f);
		case_brf(bgt, _f, 32);
		case_brr(bne, _f);
		case_brf(bne, _f, 32);
		case_brr(bunlt, _f);
		case_brf(bunlt, _f, 32);
		case_brr(bunle, _f);
		case_brf(bunle, _f, 32);
		case_brr(buneq, _f);
		case_brf(buneq, _f, 32);
		case_brr(bunge, _f);
		case_brf(bunge, _f, 32);
		case_brr(bungt, _f);
		case_brf(bungt, _f, 32);
		case_brr(bltgt, _f);
		case_brf(bltgt, _f, 32);
		case_brr(bord, _f);
		case_brf(bord, _f, 32);
		case_brr(bunord, _f);
		case_brf(bunord, _f, 32);
		case_rr(ld, _f);
		case_rw(ld, _f);
		case_rrr(ldx, _f);
		case_rrw(ldx, _f);
#define unldr_x(r0, r1, i0)	fallback_unldr_x(r0, r1, i0)
	    case jit_code_unldr_x:
		unldr_x(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
#define unldi_x(r0, i0, i1)	fallback_unldi_x(r0, i0, i1)
	    case jit_code_unldi_x:
		unldi_x(rn(node->u.w), node->v.w, node->w.w);
		break;
		case_rr(st, _f);
		case_wr(st, _f);
		case_rrr(stx, _f);
		case_wrr(stx, _f);
#define unstr_x(r0, r1, i0)	fallback_unstr_x(r0, r1, i0)
	    case jit_code_unstr_x:
		unstr_x(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
#define unsti_x(i0, r0, i1)	fallback_unsti_x(i0, r0, i1)
	    case jit_code_unsti_x:
		unsti_x(node->u.w, rn(node->v.w), node->w.w);
		break;
		case_rr(mov, _d);
	    case jit_code_movi_d:
		assert(node->flag & jit_flag_data);
		movi_d(rn(node->u.w), (jit_float64_t *)node->v.n->u.w);
		break;
		case_rr(ext, _d);
		case_rr(ext, _f_d);
		case_rr(abs, _d);
		case_rr(neg, _d);
		case_rr(sqrt, _d);
		case_rqr(fma, _d);
		case_rqr(fms, _d);
		case_rqr(fnma, _d);
		case_rqr(fnms, _d);
		case_rrr(add, _d);
		case_rrf(add, _d, 64);
		case_rrr(sub, _d);
		case_rrf(sub, _d, 64);
		case_rrf(rsb, _d, 64);
		case_rrr(mul, _d);
		case_rrf(mul, _d, 64);
		case_rrr(div, _d);
		case_rrf(div, _d, 64);
		case_rrr(lt, _d);
		case_rrf(lt, _d, 64);
		case_rrr(le, _d);
		case_rrf(le, _d, 64);
		case_rrr(eq, _d);
		case_rrf(eq, _d, 64);
		case_rrr(ge, _d);
		case_rrf(ge, _d, 64);
		case_rrr(gt, _d);
		case_rrf(gt, _d, 64);
		case_rrr(ne, _d);
		case_rrf(ne, _d, 64);
		case_rrr(unlt, _d);
		case_rrf(unlt, _d, 64);
		case_rrr(unle, _d);
		case_rrf(unle, _d, 64);
		case_rrr(uneq, _d);
		case_rrf(uneq, _d, 64);
		case_rrr(unge, _d);
		case_rrf(unge, _d, 64);
		case_rrr(ungt, _d);
		case_rrf(ungt, _d, 64);
		case_rrr(ltgt, _d);
		case_rrf(ltgt, _d, 64);
		case_rrr(ord, _d);
		case_rrf(ord, _d, 64);
		case_rrr(unord, _d);
		case_rrf(unord, _d, 64);
		case_brr(blt, _d);
		case_brf(blt, _d, 64);
		case_brr(ble, _d);
		case_brf(ble, _d, 64);
		case_brr(beq, _d);
		case_brf(beq, _d, 64);
		case_brr(bge, _d);
		case_brf(bge, _d, 64);
		case_brr(bgt, _d);
		case_brf(bgt, _d, 64);
		case_brr(bne, _d);
		case_brf(bne, _d, 64);
		case_brr(bunlt, _d);
		case_brf(bunlt, _d, 64);
		case_brr(bunle, _d);
		case_brf(bunle, _d, 64);
		case_brr(buneq, _d);
		case_brf(buneq, _d, 64);
		case_brr(bunge, _d);
		case_brf(bunge, _d, 64);
		case_brr(bungt, _d);
		case_brf(bungt, _d, 64);
		case_brr(bltgt, _d);
		case_brf(bltgt, _d, 64);
		case_brr(bord, _d);
		case_brf(bord, _d, 64);
		case_brr(bunord, _d);
		case_brf(bunord, _d, 64);
		case_rr(ld, _d);
		case_rw(ld, _d);
		case_rrr(ldx, _d);
		case_rrw(ldx, _d);
		case_rr(st, _d);
		case_wr(st, _d);
		case_rrr(stx, _d);
		case_wrr(stx, _d);
	    case jit_code_jmpr:
		jit_check_frame();
		jmpr(rn(node->u.w));
		break;
	    case jit_code_jmpi:
		if (node->flag & jit_flag_node) {
#if _CALL_AIXDESC
		    if (_jit->pc.uc == _jit->code.ptr + sizeof(void*) * 3)
			_jitc->jump = 1;
#endif
		    temp = node->u.n;
		    assert(temp->code == jit_code_label ||
			   temp->code == jit_code_epilog);
		    if (temp->flag & jit_flag_patch)
			jmpi(temp->u.w);
		    else {
			word = _jit->code.length -
			    (_jit->pc.uc - _jit->code.ptr);
			if (can_sign_extend_jump_p(word))
			    word = jmpi(_jit->pc.w);
			else
			    word = jmpi_p(_jit->pc.w);
			patch(word, node);
		    }
		}
		else {
		    jit_check_frame();
		    jmpi(node->u.w);
		}
		break;
	    case jit_code_callr:
#if _CALL_SYSV
#  define xcallr(u, v)		callr(u, v)
#  define xcalli_p(u, v)	calli_p(u, v)
#  define xcalli(u, v)		calli(u, v)
#else
#  define xcallr(u, v)		callr(u)
#  define xcalli_p(u, v)	calli_p(u)
#  define xcalli(u, v)		calli(u)
#endif
		jit_check_frame();
		xcallr(rn(node->u.w), !!(node->flag & jit_flag_varargs));
		break;
	    case jit_code_calli:
		value = !!(node->flag & jit_flag_varargs);
		if (node->flag & jit_flag_node) {
		    _jitc->function->need_return = 1;
		    temp = node->u.n;
		    assert(temp->code == jit_code_label ||
			   temp->code == jit_code_epilog);
		    if (temp->flag & jit_flag_patch)
			xcalli(temp->u.w, value);
		    else {
			word = _jit->code.length -
			    (_jit->pc.uc - _jit->code.ptr);
#if _CALL_SYSV
			if (can_sign_extend_jump_p(word + value * 4))
			    word = xcalli(_jit->pc.w, value);
			else
#endif
			    word = xcalli_p(_jit->pc.w, value);
			patch(word, node);
		    }
		}
		else {
		    jit_check_frame();
		    xcalli(node->u.w, value);
		}
		break;
	    case jit_code_prolog:
		_jitc->function = _jitc->functions.ptr + node->w.w;
		undo.node = node;
		undo.word = _jit->pc.w;
		memcpy(&undo.func, _jitc->function, sizeof(undo.func));
#if DEVEL_DISASSEMBLER
		undo.prevw = prevw;
#endif
		undo.patch_offset = _jitc->patches.offset;
#if _CALL_AIXDESC
		undo.prolog_offset = _jitc->prolog.offset;
#endif
	    restart_function:
		_jitc->again = 0;
#if _CALL_AIXDESC
		if (_jitc->jump && !_jitc->function->assume_frame) {
		    /* remember prolog to hide offset adjustment for a jump
		     * to the start of a function, what is expected to be
		     * a common practice as first jit instruction */
		    if (_jitc->prolog.offset >= _jitc->prolog.length) {
			_jitc->prolog.length += 16;
			jit_realloc((jit_pointer_t *)&_jitc->prolog.ptr,
				    (_jitc->prolog.length - 16) *
				    sizeof(jit_word_t),
				    _jitc->prolog.length * sizeof(jit_word_t));
		    }
		    _jitc->prolog.ptr[_jitc->prolog.offset++] = _jit->pc.w;
		    /* function descriptor */
		    word = _jit->pc.w + sizeof(void*) * 3;
		    iw(word);				/* addr */
		    iw(0);				/* toc */
		    iw(0);				/* env */
		}
#endif
		prolog(node);
		break;
	    case jit_code_epilog:
		assert(_jitc->function == _jitc->functions.ptr + node->w.w);
		if (_jitc->again) {
		    for (temp = undo.node->next;
			 temp != node; temp = temp->next) {
			if (temp->code == jit_code_label ||
			    temp->code == jit_code_epilog)
			    temp->flag &= ~jit_flag_patch;
		    }
		    temp->flag &= ~jit_flag_patch;
		    node = undo.node;
		    _jit->pc.w = undo.word;
		    /* undo.func.self.aoff and undo.func.regset should not
		     * be undone, as they will be further updated, and are
		     * the reason of the undo. */
		    undo.func.self.aoff = _jitc->function->frame +
			_jitc->function->self.aoff;
		    undo.func.need_frame = _jitc->function->need_frame;
		    undo.func.need_stack = _jitc->function->need_stack;
		    undo.func.need_return = _jitc->function->need_return;
		    jit_regset_set(&undo.func.regset, &_jitc->function->regset);
		    /* allocar information also does not need to be undone */
		    undo.func.aoffoff = _jitc->function->aoffoff;
		    undo.func.allocar = _jitc->function->allocar;
		    /* cvt_offset must also not be undone */
		    undo.func.cvt_offset = _jitc->function->cvt_offset;
		    memcpy(_jitc->function, &undo.func, sizeof(undo.func));
#if DEVEL_DISASSEMBLER
		    prevw = undo.prevw;
#endif
		    _jitc->patches.offset = undo.patch_offset;
#if _CALL_AIXDESC
		    _jitc->prolog.offset = undo.prolog_offset;
#endif
		    goto restart_function;
		}
		/* remember label is defined */
		node->flag |= jit_flag_patch;
		node->u.w = _jit->pc.w;
		epilog(node);
		_jitc->function = NULL;
		break;
	    case jit_code_movr_w_f:
		movr_w_f(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_movr_f_w:
		movr_f_w(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_movi_f_w:
		assert(node->flag & jit_flag_data);
		movi_f_w(rn(node->u.w), *(jit_float32_t *)node->v.n->u.w);
		break;
	    case jit_code_movi_w_f:
		movi_w_f(rn(node->u.w), node->v.w);
		break;
#if __WORDSIZE == 64
	    case jit_code_movr_d_w:
		movr_d_w(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_movi_d_w:
		assert(node->flag & jit_flag_data);
		movi_d_w(rn(node->u.w), *(jit_float64_t *)node->v.n->u.w);
		break;
	    case jit_code_movr_w_d:
		movr_w_d(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_movi_w_d:
		movi_w_d(rn(node->u.w), node->v.w);
		break;
#else
	    case jit_code_movr_ww_d:
		movr_ww_d(rn(node->u.w), rn(node->v.w), rn(node->w.w));
		break;
	    case jit_code_movr_d_ww:
		movr_d_ww(rn(node->u.w), rn(node->v.w), rn(node->w.w));
		break;
	    case jit_code_movi_d_ww:
		assert(node->flag & jit_flag_data);
		movi_d_ww(rn(node->u.w), rn(node->v.w),
			  *(jit_float64_t *)node->w.n->u.w);
		break;
	    case jit_code_movi_ww_d:
		movi_ww_d(rn(node->u.w), node->v.w, node->w.w);
		break;
#endif
	    case jit_code_va_start:
		vastart(rn(node->u.w));
		break;
	    case jit_code_va_arg:
		vaarg(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_va_arg_d:
		vaarg_d(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_live:			case jit_code_ellipsis:
	    case jit_code_va_push:
	    case jit_code_allocai:		case jit_code_allocar:
	    case jit_code_arg_c:		case jit_code_arg_s:
	    case jit_code_arg_i:
#  if __WORDSIZE == 64
	    case jit_code_arg_l:
#  endif
	    case jit_code_arg_f:		case jit_code_arg_d:
	    case jit_code_va_end:
	    case jit_code_ret:
	    case jit_code_retr_c:		case jit_code_reti_c:
	    case jit_code_retr_uc:		case jit_code_reti_uc:
	    case jit_code_retr_s:		case jit_code_reti_s:
	    case jit_code_retr_us:		case jit_code_reti_us:
	    case jit_code_retr_i:		case jit_code_reti_i:
#if __WORDSIZE == 64
	    case jit_code_retr_ui:		case jit_code_reti_ui:
	    case jit_code_retr_l:		case jit_code_reti_l:
#endif
	    case jit_code_retr_f:		case jit_code_reti_f:
	    case jit_code_retr_d:		case jit_code_reti_d:
	    case jit_code_getarg_c:		case jit_code_getarg_uc:
	    case jit_code_getarg_s:		case jit_code_getarg_us:
	    case jit_code_getarg_i:
#if __WORDSIZE == 64
	    case jit_code_getarg_ui:		case jit_code_getarg_l:
#endif
	    case jit_code_getarg_f:		case jit_code_getarg_d:
	    case jit_code_putargr_c:		case jit_code_putargi_c:
	    case jit_code_putargr_uc:		case jit_code_putargi_uc:
	    case jit_code_putargr_s:		case jit_code_putargi_s:
	    case jit_code_putargr_us:		case jit_code_putargi_us:
	    case jit_code_putargr_i:		case jit_code_putargi_i:
#if __WORDSIZE == 64
	    case jit_code_putargr_ui:		case jit_code_putargi_ui:
	    case jit_code_putargr_l:		case jit_code_putargi_l:
#endif
	    case jit_code_putargr_f:		case jit_code_putargi_f:
	    case jit_code_putargr_d:		case jit_code_putargi_d:
	    case jit_code_pushargr_c:		case jit_code_pushargi_c:
	    case jit_code_pushargr_uc:		case jit_code_pushargi_uc:
	    case jit_code_pushargr_s:		case jit_code_pushargi_s:
	    case jit_code_pushargr_us:		case jit_code_pushargi_us:
	    case jit_code_pushargr_i:		case jit_code_pushargi_i:
#if __WORDSIZE == 64
	    case jit_code_pushargr_ui:		case jit_code_pushargi_ui:
	    case jit_code_pushargr_l:		case jit_code_pushargi_l:
#endif
	    case jit_code_pushargr_f:		case jit_code_pushargi_f:
	    case jit_code_pushargr_d:		case jit_code_pushargi_d:
	    case jit_code_retval_c:		case jit_code_retval_uc:
	    case jit_code_retval_s:		case jit_code_retval_us:
	    case jit_code_retval_i:
#if __WORDSIZE == 64
	    case jit_code_retval_ui:		case jit_code_retval_l:
#endif
	    case jit_code_retval_f:		case jit_code_retval_d:
	    case jit_code_prepare:
	    case jit_code_finishr:		case jit_code_finishi:
	    case jit_code_negi_f:		case jit_code_absi_f:
	    case jit_code_sqrti_f:		case jit_code_negi_d:
	    case jit_code_absi_d:		case jit_code_sqrti_d:
		break;
	    case jit_code_negi:
		negi(rn(node->u.w), node->v.w);
		break;
	    case jit_code_comi:
		comi(rn(node->u.w), node->v.w);
		break;
	    case jit_code_exti_c:
		exti_c(rn(node->u.w), node->v.w);
		break;
	    case jit_code_exti_uc:
		exti_uc(rn(node->u.w), node->v.w);
		break;
	    case jit_code_exti_s:
		exti_s(rn(node->u.w), node->v.w);
		break;
	    case jit_code_exti_us:
		exti_us(rn(node->u.w), node->v.w);
		break;
	    case jit_code_bswapi_us:
		bswapi_us(rn(node->u.w), node->v.w);
		break;
	    case jit_code_bswapi_ui:
		bswapi_ui(rn(node->u.w), node->v.w);
		break;
	    case jit_code_htoni_us:
		htoni_us(rn(node->u.w), node->v.w);
		break;
	    case jit_code_htoni_ui:
		htoni_ui(rn(node->u.w), node->v.w);
		break;
#if __WORDSIZE == 64
	    case jit_code_exti_i:
		exti_i(rn(node->u.w), node->v.w);
		break;
	    case jit_code_exti_ui:
		exti_ui(rn(node->u.w), node->v.w);
		break;
	    case jit_code_bswapi_ul:
		bswapi_ul(rn(node->u.w), node->v.w);
		break;
	    case jit_code_htoni_ul:
		htoni_ul(rn(node->u.w), node->v.w);
		break;
#endif
	    case jit_code_cloi:
		cloi(rn(node->u.w), node->v.w);
		break;
	    case jit_code_clzi:
		clzi(rn(node->u.w), node->v.w);
		break;
	    case jit_code_ctoi:
		ctoi(rn(node->u.w), node->v.w);
		break;
	    case jit_code_ctzi:
		ctzi(rn(node->u.w), node->v.w);
		break;
	    case jit_code_rbiti:
		rbiti(rn(node->u.w), node->v.w);
		break;
	    case jit_code_popcnti:
		popcnti(rn(node->u.w), node->v.w);
		break;
	    case jit_code_exti:
		exti(rn(node->u.w), node->v.w, node->w.q.l, node->w.q.h);
		break;
	    case jit_code_exti_u:
		exti_u(rn(node->u.w), node->v.w, node->w.q.l, node->w.q.h);
		break;
	    default:
		abort();
	}
	jit_regarg_clr(node, value);
	assert(_jitc->regarg == 0 && _jitc->synth == 0);
	/* update register live state */
	jit_reglive(node);

        no_flag = !(node->flag & jit_flag_patch);
    }
#undef case_brf
#undef case_brw
#undef case_brr
#undef case_wrr
#undef case_rrf
#undef case_rrw
#undef case_xrr
#undef case_Xrr
#undef case_rrx
#undef case_rrX
#undef case_rrr
#undef case_wr
#undef case_rw
#undef case_rr

    for (offset = 0; offset < _jitc->patches.offset; offset++) {
	node = _jitc->patches.ptr[offset].node;
	word = node->code == jit_code_movi ? node->v.n->u.w : node->u.n->u.w;
	patch_at(_jitc->patches.ptr[offset].inst, word);
    }

    jit_flush(_jit->code.ptr, _jit->pc.uc);

    return (_jit->code.ptr);
}

#define CODE				1
#  include "jit_ppc-cpu.c"
#  include "jit_ppc-fpu.c"
#  include "jit_fallback.c"
#undef CODE

void
jit_flush(void *fptr, void *tptr)
{
#if defined(__GNUC__)
    jit_word_t		f, t, s;

    s = sysconf(_SC_PAGE_SIZE);
    f = (jit_word_t)fptr & -s;
    t = (((jit_word_t)tptr) + s - 1) & -s;
    __clear_cache((void *)f, (void *)t);
#endif
}

void
_emit_ldxi(jit_state_t *_jit, jit_int32_t r0, jit_int32_t r1, jit_word_t i0)
{
#if __WORDSIZE == 32
    ldxi_i(rn(r0), rn(r1), i0);
#else
    ldxi_l(rn(r0), rn(r1), i0);
#endif
}

void
_emit_stxi(jit_state_t *_jit, jit_word_t i0, jit_int32_t r0, jit_int32_t r1)
{
#if __WORDSIZE == 32
    stxi_i(i0, rn(r0), rn(r1));
#else
    stxi_l(i0, rn(r0), rn(r1));
#endif
}

void
_emit_ldxi_d(jit_state_t *_jit, jit_int32_t r0, jit_int32_t r1, jit_word_t i0)
{
    ldxi_d(rn(r0), rn(r1), i0);
}

void
_emit_stxi_d(jit_state_t *_jit, jit_word_t i0, jit_int32_t r0, jit_int32_t r1)
{
    stxi_d(i0, rn(r0), rn(r1));
}

static void
_patch(jit_state_t *_jit, jit_word_t instr, jit_node_t *node)
{
    jit_int32_t		 flag;

    assert(node->flag & jit_flag_node);
    if (node->code == jit_code_movi)
	flag = node->v.n->flag;
    else
	flag = node->u.n->flag;
    assert(!(flag & jit_flag_patch));
    if (_jitc->patches.offset >= _jitc->patches.length) {
	jit_realloc((jit_pointer_t *)&_jitc->patches.ptr,
		    _jitc->patches.length * sizeof(jit_patch_t),
		    (_jitc->patches.length + 1024) * sizeof(jit_patch_t));
	_jitc->patches.length += 1024;
    }
    _jitc->patches.ptr[_jitc->patches.offset].inst = instr;
    _jitc->patches.ptr[_jitc->patches.offset].node = node;
    ++_jitc->patches.offset;
}
