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

#if defined(__linux__)
#  include <stdio.h>
#endif

#define stack_framesize			48

#define jit_arg_reg_p(i)		((i) >= 0 && (i) < 4)
#define jit_arg_f_reg_p(i)		((i) >= 0 && (i) < 16)
#define jit_arg_d_reg_p(i)		((i) >= 0 && (i) < 15)

#define arm_patch_node			0x80000000
#define arm_patch_word			0x40000000
#define arm_patch_jump			0x20000000
#define arm_patch_load			0x10000000
#define arm_patch_call			0x08000000

#define jit_fpr_p(rn)			((rn) > 15)

#define arg_base()			(stack_framesize - 16)
#define arg_offset(n)							\
    ((n) < 4 ? arg_base() + ((n) << 2) : (n))

/* Assume functions called never match jit instruction set, that is
 * libc, gmp, mpfr, etc functions are in thumb mode and jit is in
 * arm mode, what may cause a crash upon return of that function
 * if generating jit for a relative jump.
 */
#define jit_exchange_p()		jit_cpu.exchange

/* FIXME is it really required to not touch _R10? */

#define CHECK_REG_ARGS()						\
    do {								\
	if (!_jitc->function->save_reg_args)				\
	    _jitc->again = _jitc->function->save_reg_args = 1;		\
    } while (0)

#define CHECK_SWF_OFFSET()						\
    do {								\
	if (!_jitc->function->swf_offset) {				\
	    _jitc->again = _jitc->function->save_reg_args =		\
		_jitc->function->swf_offset = 1;			\
	    _jitc->function->self.aoff = -64;				\
	}								\
    } while (0)

#define CHECK_RETURN()							\
    do {								\
	if (!_jitc->function->need_frame &&				\
	    !_jitc->function->need_return)				\
	    _jitc->again = _jitc->function->need_return = 1;		\
    } while (0)

/*
 * Types
 */
typedef union _jit_thumb_t {
    jit_int32_t		i;
    jit_int16_t		s[2];
} jit_thumb_t;

typedef jit_pointer_t	jit_va_list;

/*
 * Prototypes
 */
#define jit_make_arg(node,code)		_jit_make_arg(_jit,node,code)
static jit_node_t *_jit_make_arg(jit_state_t*,jit_node_t*,jit_code_t);
#define jit_make_arg_f(node)		_jit_make_arg_f(_jit,node)
static jit_node_t *_jit_make_arg_f(jit_state_t*,jit_node_t*);
#define jit_make_arg_d(node)		_jit_make_arg_d(_jit,node)
static jit_node_t *_jit_make_arg_d(jit_state_t*,jit_node_t*);
#define jit_get_reg_pair()		_jit_get_reg_pair(_jit)
static jit_int32_t _jit_get_reg_pair(jit_state_t*);
#define jit_unget_reg_pair(rn)		_jit_unget_reg_pair(_jit,rn)
static void _jit_unget_reg_pair(jit_state_t*,jit_int32_t);
# define must_align_p(node)		_must_align_p(_jit, node)
static jit_bool_t _must_align_p(jit_state_t*,jit_node_t*);
#define load_const(uniq,r0,i0)		_load_const(_jit,uniq,r0,i0)
static void _load_const(jit_state_t*,jit_bool_t,jit_int32_t,jit_word_t);
#define flush_consts()			_flush_consts(_jit)
static void _flush_consts(jit_state_t*);
#define invalidate_consts()		_invalidate_consts(_jit)
static void _invalidate_consts(jit_state_t*);
#define compute_framesize()		_compute_framesize(_jit)
static void _compute_framesize(jit_state_t*);
#define patch(instr, node, kind)	_patch(_jit, instr, node, kind)
static void _patch(jit_state_t*,jit_word_t,jit_node_t*,jit_int32_t);

#if defined(__GNUC__)
/* libgcc */
extern void __clear_cache(void *, void *);
#endif

#define PROTO				1
#  include "jit_rewind.c"
#  include "jit_arm-cpu.c"
#  include "jit_arm-swf.c"
#  include "jit_arm-vfp.c"
#  include "jit_fallback.c"
#undef PROTO

/*
 * Initialization
 */
jit_cpu_t		jit_cpu;
jit_register_t		_rvs[] = {
    { rc(gpr) | 0x0c,			"ip" },
    { rc(sav) | rc(gpr) | 0x04,		"r4" },
    { rc(sav) | rc(gpr) | 0x05,		"r5" },
    { rc(sav) | rc(gpr) | 0x06,		"r6" },
    { rc(sav) | rc(gpr) | 0x07,		"r7" },
    { rc(sav) | rc(gpr) | 0x08,		"r8" },
    { rc(sav) | rc(gpr) | 0x09,		"r9" },
    { rc(sav) | 0x0a,			"sl" },
    { rc(sav) | 0x0b,			"fp" },
    { rc(sav) | 0x0d,			"sp" },
    { rc(sav) | 0x0e,			"lr" },
    { 0x0f,				"pc" },
    { rc(arg) | rc(gpr) | 0x03,		"r3" },
    { rc(arg) | rc(gpr) | 0x02,		"r2" },
    { rc(arg) | rc(gpr) | 0x01,		"r1" },
    { rc(arg) | rc(gpr) | 0x00,		"r0" },
    { rc(fpr) | 0x20,			"d8" },
    { 0x21,				"s17" },
    { rc(fpr) | 0x22,			"d9" },
    { 0x23,				"s19" },
    { rc(fpr) | 0x24,			"d10" },
    { 0x25,				"s21" },
    { rc(fpr) | 0x26,			"d11" },
    { 0x27,				"s23" },
    { rc(fpr) | 0x28,			"d12" },
    { 0x29,				"s25" },
    { rc(fpr) | 0x2a,			"d13" },
    { 0x2b,				"s27" },
    { rc(fpr) | 0x2c,			"d14" },
    { 0x2d,				"s29" },
    { rc(fpr) | 0x2e,			"d15" },
    { 0x2f,				"s31" },
    { rc(arg) | 0x1f,			"s15" },
    { rc(arg)|rc(sft)|rc(fpr)|0x1e,	"d7" },
    { rc(arg) | 0x1d,			"s13" },
    { rc(arg)|rc(sft)|rc(fpr)|0x1c,	"d6" },
    { rc(arg) | 0x1b,			"s11" },
    { rc(arg)|rc(sft)|rc(fpr)|0x1a,	"d5" },
    { rc(arg) | 0x19,			"s9" },
    { rc(arg)|rc(sft)|rc(fpr)|0x18,	"d4" },
    { rc(arg) | 0x17,			"s7" },
    { rc(arg)|rc(sft)|rc(fpr)|0x16,	"d3" },
    { rc(arg) | 0x15,			"s5" },
    { rc(arg)|rc(sft)|rc(fpr)|0x14,	"d2" },
    { rc(arg) | 0x13,			"s3" },
    { rc(arg)|rc(sft)|rc(fpr)|0x12,	"d1" },
    { rc(arg) | 0x11,			"s1" },
    { rc(arg)|rc(sft)|rc(fpr)|0x10,	"d0" },
    { _NOREG,				"<none>" },
};

static jit_int32_t iregs[] = {
    _R4, _R5, _R6, _R7, _R8, _R9,
};

/*
 * Implementation
 */
void
jit_get_cpu(void)
{
#if defined(__linux__)
    FILE	*fp;
    char	*ptr;
    char	 buf[128];

    if ((fp = fopen("/proc/cpuinfo", "r")) != NULL) {
	while (fgets(buf, sizeof(buf), fp)) {
	    if (strncmp(buf, "CPU architecture:", 17) == 0) {
		jit_cpu.version = strtol(buf + 17, &ptr, 10);
		while (*ptr) {
		    if (*ptr == 'T' || *ptr == 't') {
			++ptr;
			jit_cpu.thumb = 1;
		    }
		    else if (*ptr == 'E' || *ptr == 'e') {
			jit_cpu.extend = 1;
			++ptr;
		    }
		    else
			++ptr;
		}
	    }
	    else if (strncmp(buf, "Features\t:", 10) == 0) {
		if ((ptr = strstr(buf + 10, "vfpv")))
		    jit_cpu.vfp = strtol(ptr + 4, NULL, 0);
		if ((ptr = strstr(buf + 10, "neon")))
		    jit_cpu.neon = 1;
		if ((ptr = strstr(buf + 10, "thumb")))
		    jit_cpu.thumb = 1;
	    }
	}
	fclose(fp);
    }
#endif
#if defined(__ARM_PCS_VFP)
    if (!jit_cpu.vfp)
	jit_cpu.vfp = 3;
    if (!jit_cpu.version)
	jit_cpu.version = 7;
    jit_cpu.abi = 1;
#endif
#if defined(__thumb2__)
    jit_cpu.thumb = 1;
#endif
    /* armv6t2 todo (software float and thumb2) */
    if (!jit_cpu.vfp && jit_cpu.thumb)
	jit_cpu.thumb = 0;
    /* FIXME need test environments for the below. For the moment just
     * be very conservative */
    /* force generation of code assuming jit and function libraries called
     * instruction set do not match */
    jit_cpu.exchange = 1;
    /* do not generate hardware integer division by default */
    if (jit_cpu.version == 7)
	jit_cpu.extend = 0;

    /* By default generate extra instructions for unaligned load/store. */
    jit_cpu.unaligned = 1;
    /* Linux should only not handle unaligned vfp load/store */
    jit_cpu.vfp_unaligned = 1;
}

void
_jit_init(jit_state_t *_jit)
{
    jit_int32_t		regno;
    static jit_bool_t	first = 1;

    _jitc->reglen = jit_size(_rvs) - 1;
    if (first) {
	/* jit_get_cpu() should have been already called, and only once */
	if (!jit_cpu.vfp) {
	    /* cause register to never be allocated, because simple
	     * software float only allocates stack space for 8 slots  */
	    for (regno = _D8; regno < _D7; regno++)
		_rvs[regno].spec = 0;
	}
	if (!jit_cpu.abi) {
	    for (regno = _S15; regno <= _D0; regno++)
		_rvs[regno].spec &= ~rc(arg);
	}
	first = 0;
    }
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
    _jitc->function->self.size = stack_framesize;
    _jitc->function->self.argi = _jitc->function->self.argf =
	_jitc->function->self.alen = _jitc->function->self.aoff = 0;
    _jitc->function->swf_offset = _jitc->function->save_reg_args =
	_jitc->function->need_return = 0;
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
    if (jit_swf_p())
	CHECK_SWF_OFFSET();
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
    jit_int32_t		 reg;
    assert(_jitc->function);
    jit_inc_synth_ww(allocar, u, v);
    if (!_jitc->function->allocar) {
	_jitc->function->aoffoff = jit_allocai(sizeof(jit_int32_t));
	_jitc->function->allocar = 1;
    }
    reg = jit_get_reg(jit_class_gpr);
    jit_negr(reg, v);
    jit_andi(reg, reg, -8);
    jit_ldxi_i(u, JIT_FP, _jitc->function->aoffoff);
    jit_addr(u, u, reg);
    jit_addr(JIT_SP, JIT_SP, reg);
    jit_stxi_i(_jitc->function->aoffoff, JIT_FP, u);
    jit_unget_reg(reg);
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
    if (jit_cpu.abi) {
	if (u != JIT_FRET)
	    jit_movr_f(JIT_FRET, u);
	else
	    jit_live(JIT_FRET);
    }
    else {
	if (u != JIT_RET)
	    jit_movr_f_w(JIT_RET, u);
	else
	    jit_live(JIT_RET);
    }
    jit_ret();
    jit_dec_synth();
}

void
_jit_reti_f(jit_state_t *_jit, jit_float32_t u)
{
    jit_inc_synth_f(reti_f, u);
    if (jit_cpu.abi)
	jit_movi_f(JIT_FRET, u);
    else
	jit_movi_f_w(JIT_RET, u);
    jit_ret();
    jit_dec_synth();
}

void
_jit_retr_d(jit_state_t *_jit, jit_int32_t u)
{
    jit_inc_synth_w(retr_d, u);
    if (jit_cpu.abi) {
	if (u != JIT_FRET)
	    jit_movr_d(JIT_FRET, u);
	else
	    jit_live(JIT_FRET);
    }
    else {
	if (u != JIT_RET)
	    jit_movr_d_ww(JIT_RET, _R1, u);
	else
	    jit_live(JIT_RET);
    }
    jit_ret();
    jit_dec_synth();
}

void
_jit_reti_d(jit_state_t *_jit, jit_float64_t u)
{
    jit_inc_synth_d(reti_d, u);
    if (jit_cpu.abi)
	jit_movi_d(JIT_FRET, u);
    else
	jit_movi_d_ww(JIT_RET, _R1, u);
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
    if (!(u->code >= jit_code_arg_c && u->code <= jit_code_arg)) {
	if (u->code == jit_code_arg_f) {
	    if (jit_cpu.abi)
		return (jit_arg_f_reg_p(u->u.w));
	}
	else {
	    assert(u->code == jit_code_arg_d);
	    if (jit_cpu.abi)
		return (jit_arg_d_reg_p(u->u.w));
	}
    }
    return (jit_arg_reg_p(u->u.w));
}

static jit_node_t *
_jit_make_arg(jit_state_t *_jit, jit_node_t *node, jit_code_t code)
{
    jit_int32_t		 offset;
    if (jit_arg_reg_p(_jitc->function->self.argi))
	offset = _jitc->function->self.argi++;
    else {
	offset = _jitc->function->self.size;
	_jitc->function->self.size += sizeof(jit_word_t);
    }
    if (node == (jit_node_t *)0)
	node = jit_new_node(code);
    else
	link_node(node);
    node->u.w = offset;
    node->v.w = ++_jitc->function->self.argn;
    jit_link_prolog();
    return (node);
}

jit_node_t *
_jit_make_arg_f(jit_state_t *_jit, jit_node_t *node)
{
    jit_int32_t		 offset;
    if (jit_cpu.abi && !(_jitc->function->self.call & jit_call_varargs)) {
	if (jit_arg_f_reg_p(_jitc->function->self.argf)) {
	    offset = _jitc->function->self.argf++;
	    goto done;
	}
    }
    else {
	if (jit_arg_reg_p(_jitc->function->self.argi)) {
	    offset = _jitc->function->self.argi++;
	    goto done;
	}
    }
    offset = _jitc->function->self.size;
    _jitc->function->self.size += sizeof(jit_float32_t);
done:
    if (node == (jit_node_t *)0)
	node = jit_new_node(jit_code_arg_f);
    else
	link_node(node);
    node->u.w = offset;
    node->v.w = ++_jitc->function->self.argn;
    jit_link_prolog();
    return (node);
}

jit_node_t *
_jit_make_arg_d(jit_state_t *_jit, jit_node_t *node)
{
    jit_int32_t		 offset;
    if (jit_cpu.abi && !(_jitc->function->self.call & jit_call_varargs)) {
	if (jit_arg_d_reg_p(_jitc->function->self.argf)) {
	    if (_jitc->function->self.argf & 1)
		++_jitc->function->self.argf;
	    offset = _jitc->function->self.argf;
	    _jitc->function->self.argf += 2;
	    goto done;
	}
    }
    else {
	if (_jitc->function->self.argi & 1)
	    ++_jitc->function->self.argi;
	if (jit_arg_reg_p(_jitc->function->self.argi)) {
	    offset = _jitc->function->self.argi;
	    _jitc->function->self.argi += 2;
	    goto done;
	}
    }
    if (_jitc->function->self.size & 7)
	_jitc->function->self.size += 4;
    offset = _jitc->function->self.size;
    _jitc->function->self.size += sizeof(jit_float64_t);
done:
    if (node == (jit_node_t *)0)
	node = jit_new_node(jit_code_arg_d);
    else
	link_node(node);
    node->u.w = offset;
    node->v.w = ++_jitc->function->self.argn;
    jit_link_prolog();
    return (node);
}

void
_jit_ellipsis(jit_state_t *_jit)
{
    if (_jitc->prepare) {
	assert(!(_jitc->function->call.call & jit_call_varargs));
	_jitc->function->call.call |= jit_call_varargs;
	if (jit_cpu.abi && _jitc->function->call.argf)
	    rewind_prepare();
    }
    else {
	assert(!(_jitc->function->self.call & jit_call_varargs));
	_jitc->function->self.call |= jit_call_varargs;
	CHECK_REG_ARGS();
	if (jit_cpu.abi &&  _jitc->function->self.argf)
	    rewind_prolog();
	/* First 4 stack addresses need to be spilled r0-r3 */
	if (jit_arg_reg_p(_jitc->function->self.argi))
	    _jitc->function->vagp = _jitc->function->self.argi * 4;
	else
	    _jitc->function->vagp = 16;
    }
    jit_inc_synth(ellipsis);
    if (_jitc->prepare)
	jit_link_prepare();
    else
	jit_link_prolog();
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
    assert(_jitc->function);
    assert(!(_jitc->function->self.call & jit_call_varargs));
#if STRONG_TYPE_CHECKING
    assert(code >= jit_code_arg_c && code <= jit_code_arg);
#endif
    return (jit_make_arg((jit_node_t*)0, code));
}

jit_node_t *
_jit_arg_f(jit_state_t *_jit)
{
    assert(_jitc->function);
    assert(!(_jitc->function->self.call & jit_call_varargs));
    return (jit_make_arg_f((jit_node_t*)0));
}

jit_node_t *
_jit_arg_d(jit_state_t *_jit)
{
    assert(_jitc->function);
    assert(!(_jitc->function->self.call & jit_call_varargs));
    return (jit_make_arg_d((jit_node_t*)0));
}

void
_jit_getarg_c(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    jit_node_t		*node = NULL;
    assert_arg_type(v->code, jit_code_arg_c);
    jit_inc_synth_wp(getarg_c, u, v);
    if (jit_swf_p())
	node = jit_ldxi_c(u, JIT_FP, arg_offset(v->u.w));
    else if (jit_arg_reg_p(v->u.w))
	jit_extr_c(u, JIT_RA0 - v->u.w);
    else
	node = jit_ldxi_c(u, JIT_FP, v->u.w);
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_getarg_uc(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    jit_node_t		*node = NULL;
    assert_arg_type(v->code, jit_code_arg_c);
    jit_inc_synth_wp(getarg_uc, u, v);
    if (jit_swf_p())
	node = jit_ldxi_uc(u, JIT_FP, arg_offset(v->u.w));
    else if (jit_arg_reg_p(v->u.w))
	jit_extr_uc(u, JIT_RA0 - v->u.w);
    else
	node = jit_ldxi_uc(u, JIT_FP, v->u.w);
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_getarg_s(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    jit_node_t		*node = NULL;
    assert_arg_type(v->code, jit_code_arg_s);
    jit_inc_synth_wp(getarg_s, u, v);
    if (jit_swf_p())
	node = jit_ldxi_s(u, JIT_FP, arg_offset(v->u.w));
    else if (jit_arg_reg_p(v->u.w))
	jit_extr_s(u, JIT_RA0 - v->u.w);
    else
	node = jit_ldxi_s(u, JIT_FP, v->u.w);
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_getarg_us(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    jit_node_t		*node = NULL;
    assert_arg_type(v->code, jit_code_arg_s);
    jit_inc_synth_wp(getarg_us, u, v);
    if (jit_swf_p())
	node = jit_ldxi_us(u, JIT_FP, arg_offset(v->u.w));
    else if (jit_arg_reg_p(v->u.w))
	jit_extr_us(u, JIT_RA0 - v->u.w);
    else
	node = jit_ldxi_us(u, JIT_FP, v->u.w);
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_getarg_i(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    jit_node_t		*node = NULL;
    assert_arg_type(v->code, jit_code_arg_i);
    jit_inc_synth_wp(getarg_i, u, v);
    if (jit_swf_p())
	node = jit_ldxi_i(u, JIT_FP, arg_offset(v->u.w));
    else if (jit_arg_reg_p(v->u.w))
	jit_movr(u, JIT_RA0 - v->u.w);
    else
	node = jit_ldxi_i(u, JIT_FP, v->u.w);
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_putargr(jit_state_t *_jit, jit_int32_t u, jit_node_t *v, jit_code_t code)
{
    jit_node_t		*node = NULL;
    assert_putarg_type(code, v->code);
    jit_code_inc_synth_wp(code, u, v);
    if (jit_swf_p())
	node = jit_stxi(arg_offset(v->u.w), JIT_FP, u);
    else if (jit_arg_reg_p(v->u.w))
	jit_movr(JIT_RA0 - v->u.w, u);
    else
	node = jit_stxi(v->u.w, JIT_FP, u);
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_putargi(jit_state_t *_jit, jit_word_t u, jit_node_t *v, jit_code_t code)
{
    jit_int32_t		 regno;
    jit_node_t		*node = NULL;
    assert_putarg_type(code, v->code);
    jit_code_inc_synth_wp(code, u, v);
    if (jit_swf_p()) {
	regno = jit_get_reg(jit_class_gpr);
	jit_movi(regno, u);
	node = jit_stxi(arg_offset(v->u.w), JIT_FP, regno);
	jit_unget_reg(regno);
    }
    else if (jit_arg_reg_p(v->u.w))
	jit_movi(JIT_RA0 - v->u.w, u);
    else {
	regno = jit_get_reg(jit_class_gpr);
	jit_movi(regno, u);
	node = jit_stxi(v->u.w, JIT_FP, regno);
	jit_unget_reg(regno);
    }
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_getarg_f(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    jit_node_t		*node = NULL;
    assert(v->code == jit_code_arg_f);
    jit_inc_synth_wp(getarg_f, u, v);
    if (jit_cpu.abi && !(_jitc->function->self.call & jit_call_varargs)) {
	if (jit_arg_f_reg_p(v->u.w))
	    jit_movr_f(u, JIT_FA0 - v->u.w);
	else
	    node = jit_ldxi_f(u, JIT_FP, v->u.w);
    }
    else if (jit_swf_p())
	node = jit_ldxi_f(u, JIT_FP, arg_offset(v->u.w));
    else {
	if (jit_arg_reg_p(v->u.w))
	    jit_movr_w_f(u, JIT_RA0 - v->u.w);
	else
	    node = jit_ldxi_f(u, JIT_FP, v->u.w);
    }
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_putargr_f(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    jit_node_t		*node = NULL;
    assert(v->code == jit_code_arg_f);
    jit_inc_synth_wp(putargr_f, u, v);
    if (jit_cpu.abi) {
	if (jit_arg_f_reg_p(v->u.w))
	    jit_movr_f(JIT_FA0 - v->u.w, u);
	else
	    node = jit_stxi_f(v->u.w, JIT_FP, u);
    }
    else if (jit_swf_p())
	node = jit_stxi_f(arg_offset(v->u.w), JIT_FP, u);
    else {
	if (jit_arg_reg_p(v->u.w))
	    jit_movr_f_w(JIT_RA0 - v->u.w, u);
	else
	    node = jit_stxi_f(v->u.w, JIT_FP, u);
    }
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_putargi_f(jit_state_t *_jit, jit_float32_t u, jit_node_t *v)
{
    jit_int32_t		 regno;
    jit_node_t		*node = NULL;
    assert(v->code == jit_code_arg_f);
    jit_inc_synth_fp(putargi_f, u, v);
    if (jit_cpu.abi) {
	if (jit_arg_f_reg_p(v->u.w))
	    jit_movi_f(JIT_FA0 - v->u.w, u);
	else {
	    regno = jit_get_reg(jit_class_fpr);
	    jit_movi_f(regno, u);
	    node = jit_stxi_f(v->u.w, JIT_FP, regno);
	    jit_unget_reg(regno);
	}
    }
    else if (jit_swf_p()) {
	regno = jit_get_reg(jit_class_fpr);
	jit_movi_f(regno, u);
	node = jit_stxi_f(arg_offset(v->u.w), JIT_FP, regno);
	jit_unget_reg(regno);
    }
    else {
	regno = jit_get_reg(jit_class_fpr);
	jit_movi_f(regno, u);
	if (jit_arg_reg_p(v->u.w))
	    jit_movr_f_w(JIT_RA0 - v->u.w, regno);
	else
	    node = jit_stxi_f(v->u.w, JIT_FP, regno);
	jit_unget_reg(regno);
    }
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_getarg_d(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    jit_node_t		*node = NULL;
    assert(v->code == jit_code_arg_d);
    jit_inc_synth_wp(getarg_d, u, v);
    if (jit_cpu.abi && !(_jitc->function->self.call & jit_call_varargs)) {
	if (jit_arg_f_reg_p(v->u.w))
	    jit_movr_d(u, JIT_FA0 - v->u.w);
	else
	    node = jit_ldxi_d(u, JIT_FP, v->u.w);
    }
    else if (jit_swf_p())
	node = jit_ldxi_d(u, JIT_FP, arg_offset(v->u.w));
    else {
	if (jit_arg_reg_p(v->u.w))
	    jit_movr_ww_d(u, JIT_RA0 - v->u.w, JIT_RA0 - (v->u.w + 1));
	else
	    node = jit_ldxi_d(u, JIT_FP, v->u.w);
    }
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_putargr_d(jit_state_t *_jit, jit_int32_t u, jit_node_t *v)
{
    jit_node_t		*node = NULL;
    assert(v->code == jit_code_arg_d);
    jit_inc_synth_wp(putargr_d, u, v);
    if (jit_cpu.abi) {
	if (jit_arg_f_reg_p(v->u.w))
	    jit_movr_d(JIT_FA0 - v->u.w, u);
	else
	    node = jit_stxi_d(v->u.w, JIT_FP, u);
    }
    else if (jit_swf_p())
	node = jit_stxi_d(arg_offset(v->u.w), JIT_FP, u);
    else {
	if (jit_arg_reg_p(v->u.w))
	    jit_movr_d_ww(JIT_RA0 - v->u.w, JIT_RA0 - (v->u.w + 1), u);
	else
	    node = jit_stxi_d(v->u.w, JIT_FP, u);
    }
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_putargi_d(jit_state_t *_jit, jit_float64_t u, jit_node_t *v)
{
    jit_int32_t		 regno;
    jit_node_t		*node = NULL;
    assert(v->code == jit_code_arg_d);
    jit_inc_synth_dp(putargi_d, u, v);
    if (jit_cpu.abi) {
	if (jit_arg_f_reg_p(v->u.w))
	    jit_movi_d(JIT_FA0 - v->u.w, u);
	else {
	    regno = jit_get_reg(jit_class_fpr);
	    jit_movi_d(regno, u);
	    node = jit_stxi_d(v->u.w, JIT_FP, regno);
	    jit_unget_reg(regno);
	}
    }
    else if (jit_swf_p()) {
	regno = jit_get_reg(jit_class_fpr);
	jit_movi_d(regno, u);
	node = jit_stxi_d(arg_offset(v->u.w), JIT_FP, regno);
	jit_unget_reg(regno);
    }
    else {
	regno = jit_get_reg(jit_class_fpr);
	jit_movi_d(regno, u);
	if (jit_arg_reg_p(v->u.w))
	    jit_movr_d_ww(JIT_RA0 - v->u.w, JIT_RA0 - (v->u.w + 1), regno);
	else
	    node = jit_stxi_d(v->u.w, JIT_FP, regno);
	jit_unget_reg(regno);
    }
    if (node) {
	CHECK_REG_ARGS();
	jit_link_alist(node);
	jit_check_frame();
    }
    jit_dec_synth();
}

void
_jit_pushargr(jit_state_t *_jit, jit_int32_t u, jit_code_t code)
{
    assert(_jitc->function);
    jit_code_inc_synth_w(code, u);
    jit_link_prepare();
    if (jit_arg_reg_p(_jitc->function->call.argi)) {
	jit_movr(JIT_RA0 - _jitc->function->call.argi, u);
	++_jitc->function->call.argi;
    }
    else {
	jit_stxi(_jitc->function->call.size, JIT_SP, u);
	_jitc->function->call.size += sizeof(jit_word_t);
    }
    jit_dec_synth();
}

void
_jit_pushargi(jit_state_t *_jit, jit_word_t u, jit_code_t code)
{
    jit_int32_t		 regno;
    assert(_jitc->function);
    jit_code_inc_synth_w(code, u);
    jit_link_prepare();
    if (jit_arg_reg_p(_jitc->function->call.argi)) {
	jit_movi(JIT_RA0 - _jitc->function->call.argi, u);
	++_jitc->function->call.argi;
    }
    else {
	regno = jit_get_reg(jit_class_gpr);
	jit_movi(regno, u);
	jit_stxi(_jitc->function->call.size, JIT_SP, regno);
	jit_unget_reg(regno);
	_jitc->function->call.size += sizeof(jit_word_t);
    }
    jit_dec_synth();
}

void
_jit_pushargr_f(jit_state_t *_jit, jit_int32_t u)
{
    assert(_jitc->function);
    jit_inc_synth_w(pushargr_f, u);
    jit_link_prepare();
    if (jit_cpu.abi && !(_jitc->function->call.call & jit_call_varargs)) {
	if (jit_arg_f_reg_p(_jitc->function->call.argf)) {
	    jit_movr_f(JIT_FA0 - _jitc->function->call.argf, u);
	    ++_jitc->function->call.argf;
	    goto done;
	}
    }
    else {
	if (jit_arg_reg_p(_jitc->function->call.argi)) {
	    jit_movr_f_w(JIT_RA0 - _jitc->function->call.argi, u);
	    ++_jitc->function->call.argi;
	    goto done;
	}
    }
    jit_stxi_f(_jitc->function->call.size, JIT_SP, u);
    _jitc->function->call.size += sizeof(jit_word_t);
done:
    jit_dec_synth();
}

void
_jit_pushargi_f(jit_state_t *_jit, jit_float32_t u)
{
    jit_int32_t		regno;
    assert(_jitc->function);
    jit_inc_synth_f(pushargi_f, u);
    jit_link_prepare();
    if (jit_cpu.abi && !(_jitc->function->call.call & jit_call_varargs)) {
	if (jit_arg_f_reg_p(_jitc->function->call.argf)) {
	    /* cannot jit_movi_f in the argument register because
	     * float arguments are packed, and that would cause
	     * either an assertion in debug mode, or overwritting
	     * two registers */
	    regno = jit_get_reg(jit_class_fpr);
	    jit_movi_f(regno, u);
	    jit_movr_f(JIT_FA0 - _jitc->function->call.argf, regno);
	    jit_unget_reg(regno);
	    ++_jitc->function->call.argf;
	    goto done;
	}
    }
    else {
	if (jit_arg_reg_p(_jitc->function->call.argi)) {
	    jit_movi_f_w(JIT_RA0 - _jitc->function->call.argi, u);
	    ++_jitc->function->call.argi;
	    goto done;
	}
    }
    regno = jit_get_reg(jit_class_fpr);
    jit_movi_f(regno, u);
    jit_stxi_f(_jitc->function->call.size, JIT_SP, regno);
    jit_unget_reg(regno);
    _jitc->function->call.size += sizeof(jit_word_t);
done:
    jit_dec_synth();
}

void
_jit_pushargr_d(jit_state_t *_jit, jit_int32_t u)
{
    assert(_jitc->function);
    jit_inc_synth_w(pushargr_d, u);
    jit_link_prepare();
    if (jit_cpu.abi && !(_jitc->function->call.call & jit_call_varargs)) {
	if (jit_arg_d_reg_p(_jitc->function->call.argf)) {
	    if (_jitc->function->call.argf & 1)
		++_jitc->function->call.argf;
	    jit_movr_d(JIT_FA0 - _jitc->function->call.argf, u);
	    _jitc->function->call.argf += 2;
	    goto done;
	}
    }
    else {
	if (_jitc->function->call.argi & 1)
	    ++_jitc->function->call.argi;
	if (jit_arg_reg_p(_jitc->function->call.argi)) {
	    jit_movr_d_ww(JIT_RA0 - _jitc->function->call.argi,
			  JIT_RA0 - (_jitc->function->call.argi + 1),
			  u);
	    _jitc->function->call.argi += 2;
	    goto done;
	}
    }
    if (_jitc->function->call.size & 7)
	_jitc->function->call.size += 4;
    jit_stxi_d(_jitc->function->call.size, JIT_SP, u);
    _jitc->function->call.size += sizeof(jit_float64_t);
done:
    jit_dec_synth();
}

void
_jit_pushargi_d(jit_state_t *_jit, jit_float64_t u)
{
    jit_int32_t		regno;
    assert(_jitc->function);
    jit_inc_synth_d(pushargi_d, u);
    jit_link_prepare();
    if (jit_cpu.abi && !(_jitc->function->call.call & jit_call_varargs)) {
	if (jit_arg_d_reg_p(_jitc->function->call.argf)) {
	    if (_jitc->function->call.argf & 1)
		++_jitc->function->call.argf;
	    jit_movi_d(JIT_FA0 - _jitc->function->call.argf, u);
	    _jitc->function->call.argf += 2;
	    goto done;
	}
    }
    else {
	if (_jitc->function->call.argi & 1)
	    ++_jitc->function->call.argi;
	if (jit_arg_reg_p(_jitc->function->call.argi)) {
	    jit_movi_d_ww(JIT_RA0 - _jitc->function->call.argi,
			  JIT_RA0 - (_jitc->function->call.argi + 1),
			  u);
	    _jitc->function->call.argi += 2;
	    goto done;
	}
    }
    if (_jitc->function->call.size & 7)
	_jitc->function->call.size += 4;
    regno = jit_get_reg(jit_class_fpr);
    jit_movi_d(regno, u);
    jit_stxi_d(_jitc->function->call.size, JIT_SP, regno);
    jit_unget_reg(regno);
    _jitc->function->call.size += sizeof(jit_float64_t);
done:
    jit_dec_synth();
}

jit_bool_t
_jit_regarg_p(jit_state_t *_jit, jit_node_t *node, jit_int32_t regno)
{
    jit_int32_t		spec;
    spec = jit_class(_rvs[regno].spec);
    if (spec & jit_class_arg) {
	regno = JIT_RA0 - regno;
	if (regno >= 0 && regno < node->v.w)
	    return (1);
	if (jit_cpu.abi && spec & jit_class_fpr) {
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
    jit_node_t		*node;
    assert(_jitc->function);
    jit_inc_synth_w(finishr, r0);
    if (_jitc->function->self.alen < _jitc->function->call.size)
	_jitc->function->self.alen = _jitc->function->call.size;
    node = jit_callr(r0);
    node->v.w = _jitc->function->self.argi;
    node->w.w = _jitc->function->call.argf;
    _jitc->function->call.argi = _jitc->function->call.argf =
	_jitc->function->call.size = 0;
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
    _jitc->function->call.argi = _jitc->function->call.argf =
	_jitc->function->call.size = 0;
    _jitc->prepare = 0;
    jit_dec_synth();
    return (node);
}

void
_jit_retval_c(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth_w(retval_c, r0);
    jit_extr_c(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_uc(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth_w(retval_uc, r0);
    jit_extr_uc(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_s(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth_w(retval_s, r0);
    jit_extr_s(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_us(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth_w(retval_us, r0);
    jit_extr_us(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_i(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth_w(retval_i, r0);
    if (r0 != JIT_RET)
	jit_movr(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_f(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth_w(retval_f, r0);
    if (jit_cpu.abi) {
	if (r0 != JIT_FRET)
	    jit_movr_f(r0, JIT_FRET);
    }
    else if (r0 != JIT_RET)
	jit_movr_w_f(r0, JIT_RET);
    jit_dec_synth();
}

void
_jit_retval_d(jit_state_t *_jit, jit_int32_t r0)
{
    jit_inc_synth_w(retval_d, r0);
    if (jit_cpu.abi) {
	if (r0 != JIT_FRET)
	    jit_movr_d(r0, JIT_FRET);
    }
    else if (r0 != JIT_RET)
	jit_movr_ww_d(r0, JIT_RET, _R1);
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
    struct {
	jit_node_t	*node;
	jit_uint8_t	*data;
	jit_word_t	 word;
	jit_function_t	 func;
#if DEVEL_DISASSEMBLER
	jit_word_t	 prevw;
#endif
	jit_uword_t	 thumb;
#if DISASSEMBLER
	jit_int32_t	 info_offset;
#endif
	jit_int32_t	 const_offset;
	jit_int32_t	 patch_offset;
    } undo;
#if DEVEL_DISASSEMBLER
    jit_word_t		 prevw;
#endif

    _jitc->function = NULL;
    _jitc->thumb = 0;

    jit_reglive_setup();

    _jitc->consts.data = NULL;
    _jitc->consts.offset = _jitc->consts.length = 0;

    undo.word = 0;
    undo.node = NULL;
    undo.data = NULL;
    undo.thumb = 0;
#if DISASSEMBLER
    undo.info_offset =
#endif
	undo.const_offset = undo.patch_offset = 0;
#  define assert_data(node)		/**/
#define case_rr(name, type)						\
	    case jit_code_##name##r##type:				\
		name##r##type(rn(node->u.w), rn(node->v.w));		\
		break
#define case_rw(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(rn(node->u.w), node->v.w);		\
		break
#define case_vv(name, type)						\
	    case jit_code_##name##r##type:				\
		if (jit_swf_p())					\
		    swf_##name##r##type(rn(node->u.w), rn(node->v.w));	\
		else							\
		    vfp_##name##r##type(rn(node->u.w), rn(node->v.w));	\
		break
#define case_vw(name, type)						\
	    case jit_code_##name##i##type:				\
		if (jit_swf_p())					\
		    swf_##name##i##type(rn(node->u.w), node->v.w);	\
		else							\
		    vfp_##name##i##type(rn(node->u.w), node->v.w);	\
		break
#define case_wr(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(node->u.w, rn(node->v.w));		\
		break
#define case_wv(name, type)						\
	    case jit_code_##name##i##type:				\
		if (jit_swf_p())					\
		    swf_##name##i##type(node->u.w, rn(node->v.w));	\
		else							\
		    vfp_##name##i##type(node->u.w, rn(node->v.w));	\
		break
#define case_rrr(name, type)						\
	    case jit_code_##name##r##type:				\
		name##r##type(rn(node->u.w),				\
			      rn(node->v.w), rn(node->w.w));		\
		break
#define case_rqr(name, type)						\
	    case jit_code_##name##r##type:				\
		if (jit_swf_p())					\
		    swf_##name##r##type(rn(node->u.w), rn(node->v.q.l),	\
					rn(node->v.q.h), rn(node->w.w));\
		else							\
		    vfp_##name##r##type(rn(node->u.w), rn(node->v.q.l),	\
					rn(node->v.q.h), rn(node->w.w));\
	    case jit_code_##name##i##type:				\
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
#define case_vvv(name, type)						\
	    case jit_code_##name##r##type:				\
		if (jit_swf_p())					\
		    swf_##name##r##type(rn(node->u.w),			\
				        rn(node->v.w), rn(node->w.w));	\
		else							\
		    vfp_##name##r##type(rn(node->u.w),			\
				        rn(node->v.w), rn(node->w.w));	\
		break
#define case_rrw(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(rn(node->u.w), rn(node->v.w), node->w.w);	\
		break
#define case_rrrw(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(rn(node->u.q.l), rn(node->u.q.h),		\
			      rn(node->v.w), node->w.w);		\
		break
#define case_vvw(name, type)						\
	    case jit_code_##name##i##type:				\
		if (jit_swf_p())					\
		    swf_##name##i##type(rn(node->u.w),			\
				        rn(node->v.w), node->w.w);	\
		else							\
		    vfp_##name##i##type(rn(node->u.w),			\
				        rn(node->v.w), node->w.w);	\
		break
#define case_vvf(name)							\
	    case jit_code_##name##i_f:					\
		assert_data(node);					\
		if (jit_swf_p())					\
		    swf_##name##i_f(rn(node->u.w), rn(node->v.w),	\
				    node->w.f);				\
		else							\
		    vfp_##name##i_f(rn(node->u.w), rn(node->v.w),	\
				    node->w.f);				\
		break
#define case_vvd(name)							\
	    case jit_code_##name##i_d:					\
		assert_data(node);					\
		if (jit_swf_p())					\
		    swf_##name##i_d(rn(node->u.w), rn(node->v.w),	\
				    node->w.d);				\
		else							\
		    vfp_##name##i_d(rn(node->u.w), rn(node->v.w),	\
				    node->w.d);				\
		break
#define case_wrr(name, type)						\
	    case jit_code_##name##i##type:				\
		name##i##type(node->u.w, rn(node->v.w), rn(node->w.w));	\
		break
#define case_wvv(name, type)						\
	    case jit_code_##name##i##type:				\
		if (jit_swf_p())					\
		    swf_##name##i##type(node->u.w,			\
				        rn(node->v.w), rn(node->w.w));	\
		else							\
		    vfp_##name##i##type(node->u.w,			\
				        rn(node->v.w), rn(node->w.w));	\
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
		    patch(word, node, arm_patch_jump);			\
		}							\
		break
#define case_bvv(name, type)						\
	    case jit_code_##name##r##type:				\
		temp = node->u.n;					\
		assert(temp->code == jit_code_label ||			\
		       temp->code == jit_code_epilog);			\
		if (temp->flag & jit_flag_patch) {			\
		    if (jit_swf_p())					\
			swf_##name##r##type(temp->u.w, rn(node->v.w),	\
					    rn(node->w.w));		\
		    else						\
			vfp_##name##r##type(temp->u.w, rn(node->v.w),	\
					    rn(node->w.w));		\
		}							\
		else {							\
		    if (jit_swf_p())					\
			word = swf_##name##r##type(_jit->pc.w,		\
						   rn(node->v.w),	\
						   rn(node->w.w));	\
		    else						\
			word = vfp_##name##r##type(_jit->pc.w,		\
						   rn(node->v.w),	\
						   rn(node->w.w));	\
		    patch(word, node, arm_patch_jump);			\
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
		    patch(word, node, arm_patch_jump);			\
		}							\
		break;
#define case_bvf(name)							\
	    case jit_code_##name##i_f:					\
		temp = node->u.n;					\
		assert(temp->code == jit_code_label ||			\
		       temp->code == jit_code_epilog);			\
		if (temp->flag & jit_flag_patch) {			\
		    if (jit_swf_p())					\
			swf_##name##i_f(temp->u.w, rn(node->v.w),	\
					node->w.f);			\
		    else						\
			vfp_##name##i_f(temp->u.w, rn(node->v.w),	\
					node->w.f);			\
		}							\
		else {							\
		    if (jit_swf_p())					\
			word = swf_##name##i_f(_jit->pc.w,		\
					       rn(node->v.w),		\
					       node->w.f);		\
		    else						\
			word = vfp_##name##i_f(_jit->pc.w,		\
					       rn(node->v.w),		\
					       node->w.f);		\
		    patch(word, node, arm_patch_jump);			\
		}							\
		break
#define case_bvd(name)							\
	    case jit_code_##name##i_d:					\
		temp = node->u.n;					\
		assert(temp->code == jit_code_label ||			\
		       temp->code == jit_code_epilog);			\
		if (temp->flag & jit_flag_patch) {			\
		    if (jit_swf_p())					\
			swf_##name##i_d(temp->u.w, rn(node->v.w),	\
					node->w.d);			\
		    else						\
			vfp_##name##i_d(temp->u.w, rn(node->v.w),	\
					node->w.d);			\
		}							\
		else {							\
		    if (jit_swf_p())					\
			word = swf_##name##i_d(_jit->pc.w,		\
					       rn(node->v.w),		\
					       node->w.d);		\
		    else						\
			word = vfp_##name##i_d(_jit->pc.w,		\
					       rn(node->v.w),		\
					       node->w.d);		\
		    patch(word, node, arm_patch_jump);			\
		}							\
		break
#if DEVEL_DISASSEMBLER
    prevw = _jit->pc.w;
#endif
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
		if (jit_thumb_p())
		    nop((node->u.w + 1) & ~1);
		else
		    nop((node->u.w + 3) & ~3);
		break;
	    case jit_code_note:		case jit_code_name:
		if (must_align_p(node->next))
		    nop(2);
		node->u.w = _jit->pc.w;
		break;
	    case jit_code_label:
		if (must_align_p(node->next))
		    nop(2);
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
		case_rrr(lsh,);
		case_rrw(lsh,);
#define qlshr(r0, r1, r2, r3)	fallback_qlshr(r0, r1, r2, r3)
#define qlshi(r0, r1, r2, i0)	fallback_qlshi(r0, r1, r2, i0)
#define qlshr_u(r0, r1, r2, r3)	fallback_qlshr_u(r0, r1, r2, r3)
#define qlshi_u(r0, r1, r2, i0)	fallback_qlshi_u(r0, r1, r2, i0)
		case_rrrr(qlsh,);
		case_rrrw(qlsh,);
		case_rrrr(qlsh, _u);
		case_rrrw(qlsh, _u);
		case_rrr(rsh,);
		case_rrw(rsh,);
		case_rrr(rsh, _u);
		case_rrw(rsh, _u);
#define qrshr(r0, r1, r2, r3)	fallback_qrshr(r0, r1, r2, r3)
#define qrshi(r0, r1, r2, i0)	fallback_qrshi(r0, r1, r2, i0)
#define qrshr_u(r0, r1, r2, r3)	fallback_qrshr_u(r0, r1, r2, r3)
#define qrshi_u(r0, r1, r2, i0)	fallback_qrshi_u(r0, r1, r2, i0)
		case_rrrr(qrsh,);
		case_rrrw(qrsh,);
		case_rrrr(qrsh, _u);
		case_rrrw(qrsh, _u);
		case_rrr(lrot,);
		case_rrw(lrot,);
		case_rrr(rrot,);
		case_rrw(rrot,);
		case_rr(neg,);
		case_rr(com,);
		case_rr(clo,);
		case_rr(clz,);
		case_rr(cto,);
		case_rr(ctz,);
		case_rr(rbit,);
		case_rr(popcnt,);
		case_rrr(and,);
		case_rrw(and,);
		case_rrr(or,);
		case_rrw(or,);
		case_rrr(xor,);
		case_rrw(xor,);
		case_vv(trunc, _f_i);
		case_vv(trunc, _d_i);
		case_rr(ld, _c);
		case_rw(ld, _c);
		case_rr(ld, _uc);
		case_rw(ld, _uc);
		case_rr(ld, _s);
		case_rw(ld, _s);
		case_rr(ld, _us);
		case_rw(ld, _us);
		case_rr(ld, _i);
		case_rw(ld, _i);
		case_rrr(ldx, _c);
		case_rrw(ldx, _c);
		case_rrr(ldx, _uc);
		case_rrw(ldx, _uc);
		case_rrr(ldx, _s);
		case_rrw(ldx, _s);
		case_rrr(ldx, _us);
		case_rrw(ldx, _us);
		case_rrr(ldx, _i);
		case_rrw(ldx, _i);
	    case jit_code_unldr:
		unldr(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
	    case jit_code_unldi:
		unldi(rn(node->u.w), node->v.w, node->w.w);
		break;
	    case jit_code_unldr_u:
		unldr_u(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
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
	    case jit_code_ldxbr_f:
		addr(rn(node->v.w), rn(node->v.w), rn(node->w.w));
		goto L_ldxbi_f;
	    case jit_code_ldxbi_f:
		addi(rn(node->v.w), rn(node->v.w), node->w.w);
	    L_ldxbi_f:
		if (jit_swf_p())
		    swf_ldr_f(rn(node->u.w), rn(node->v.w));
		else
		    vfp_ldr_f(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_ldxar_f:
		if (jit_swf_p())
		    swf_ldr_f(rn(node->u.w), rn(node->v.w));
		else
		    vfp_ldr_f(rn(node->u.w), rn(node->v.w));
		addr(rn(node->v.w), rn(node->v.w), rn(node->w.w));
		break;
	    case jit_code_ldxai_f:
		if (jit_swf_p()) {
		    swf_ldr_f(rn(node->u.w), rn(node->v.w));
		    addi(rn(node->v.w), rn(node->v.w), node->w.w);
		}
		else
		    vfp_ldxai_f(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
	    case jit_code_ldxbr_d:
		addr(rn(node->v.w), rn(node->v.w), rn(node->w.w));
		goto L_ldxbi_d;
	    case jit_code_ldxbi_d:
		addi(rn(node->v.w), rn(node->v.w), node->w.w);
	    L_ldxbi_d:
		if (jit_swf_p())
		    swf_ldr_d(rn(node->u.w), rn(node->v.w));
		else
		    vfp_ldr_d(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_ldxar_d:
		if (jit_swf_p())
		    swf_ldr_d(rn(node->u.w), rn(node->v.w));
		else
		    vfp_ldr_d(rn(node->u.w), rn(node->v.w));
		addr(rn(node->v.w), rn(node->v.w), rn(node->w.w));
		break;
	    case jit_code_ldxai_d:
		if (jit_swf_p()) {
		    swf_ldr_d(rn(node->u.w), rn(node->v.w));
		    addi(rn(node->v.w), rn(node->v.w), node->w.w);
		}
		else
		    vfp_ldxai_d(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
		case_rr(st, _c);
		case_wr(st, _c);
		case_rr(st, _s);
		case_wr(st, _s);
		case_rr(st, _i);
		case_wr(st, _i);
		case_rrr(stx, _c);
		case_wrr(stx, _c);
		case_rrr(stx, _s);
		case_wrr(stx, _s);
		case_rrr(stx, _i);
		case_wrr(stx, _i);
	    case jit_code_unstr:
		unstr(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
	    case jit_code_unsti:
		unsti(node->u.w, rn(node->v.w), node->w.w);
		break;
		case_xrr(stxb, _c);	case_Xrr(stxb, _c);
		case_xrr(stxa, _c);	case_Xrr(stxa, _c);
		case_xrr(stxb, _s);	case_Xrr(stxb, _s);
		case_xrr(stxa, _s);	case_Xrr(stxa, _s);
		case_xrr(stxb, _i);	case_Xrr(stxb, _i);
		case_xrr(stxa, _i);	case_Xrr(stxa, _i);
	    case jit_code_stxbr_f:
		addr(rn(node->v.w), rn(node->v.w), rn(node->u.w));
		goto L_stxbi_f;
	    case jit_code_stxbi_f:
		addi(rn(node->v.w), rn(node->v.w), node->u.w);
	    L_stxbi_f:
		if (jit_swf_p())
		    swf_str_f(rn(node->v.w), rn(node->w.w));
		else
		    vfp_str_f(rn(node->v.w), rn(node->w.w));
		break;
	    case jit_code_stxar_f:
		if (jit_swf_p())
		    swf_str_f(rn(node->v.w), rn(node->w.w));
		else
		    vfp_str_f(rn(node->v.w), rn(node->w.w));
		addr(rn(node->v.w), rn(node->v.w), rn(node->u.w));
		break;
	    case jit_code_stxai_f:
		if (jit_swf_p()) {
		    swf_str_f(rn(node->v.w), rn(node->w.w));
		    addi(rn(node->v.w), rn(node->v.w), node->u.w);
		}
		else
		    vfp_stxai_f(node->u.w, rn(node->v.w), rn(node->w.w));
		break;
	    case jit_code_stxbr_d:
		addr(rn(node->v.w), rn(node->v.w), rn(node->u.w));
		goto L_stxbi_d;
	    case jit_code_stxbi_d:
		addi(rn(node->v.w), rn(node->v.w), node->u.w);
	    L_stxbi_d:
		if (jit_swf_p())
		    swf_str_d(rn(node->v.w), rn(node->w.w));
		else
		    vfp_str_d(rn(node->v.w), rn(node->w.w));
		break;
	    case jit_code_stxar_d:
		if (jit_swf_p())
		    swf_str_d(rn(node->v.w), rn(node->w.w));
		else
		    vfp_str_d(rn(node->v.w), rn(node->w.w));
		addr(rn(node->v.w), rn(node->v.w), rn(node->u.w));
		break;
	    case jit_code_stxai_d:
		if (jit_swf_p()) {
		    swf_str_d(rn(node->v.w), rn(node->w.w));
		    addi(rn(node->v.w), rn(node->v.w), node->u.w);
		}
		else
		    vfp_stxai_d(node->u.w, rn(node->v.w), rn(node->w.w));
		break;
		case_rr(hton, _us);
		case_rr(hton, _ui);
		case_rr(bswap, _us);
		case_rr(bswap, _ui);
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
	    case jit_code_casr:
		casr(rn(node->u.w), rn(node->v.w),
		     rn(node->w.q.l), rn(node->w.q.h));
		break;
	    case jit_code_casi:
		casi(rn(node->u.w), node->v.w,
		     rn(node->w.q.l), rn(node->w.q.h));
		break;
		case_rr(mov,);
		case_rrr(movn,);
		case_rrr(movz,);
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
			word = movi_p(rn(node->u.w), temp->u.w);
			patch(word, node, arm_patch_word);
		    }
		}
		else
		    movi(rn(node->u.w), node->v.w);
		break;
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
		case_brr(bms,);
		case_brw(bms,);
		case_brr(bmc,);
		case_brw(bmc,);
		case_vvv(add, _f);
		case_vvf(add);
		case_vvv(sub, _f);
		case_vvf(sub);
		case_vvf(rsb);
		case_vvv(mul, _f);
		case_vvf(mul);
		case_vvv(div, _f);
		case_vvf(div);
		case_vv(abs, _f);
		case_vv(neg, _f);
		case_vv(sqrt, _f);
		case_rqr(fma, _f);
		case_rqr(fms, _f);
		case_rqr(fnma, _f);
		case_rqr(fnms, _f);
		case_vv(ext, _f);
		case_vv(ld, _f);
		case_vw(ld, _f);
		case_vvv(ldx, _f);
		case_vvw(ldx, _f);
	    case jit_code_unldr_x:
		if (jit_swf_p())
		    swf_unldr_x(rn(node->u.w), rn(node->v.w), node->w.w);
		else
		    vfp_unldr_x(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
	    case jit_code_unldi_x:
		if (jit_swf_p())
		    swf_unldi_x(rn(node->u.w), node->v.w, node->w.w);
		else
		    vfp_unldi_x(rn(node->u.w), node->v.w, node->w.w);
		break;
		case_vv(st, _f);
		case_wv(st, _f);
		case_vvv(stx, _f);
		case_wvv(stx, _f);
	    case jit_code_unstr_x:
		if (jit_swf_p())
		    swf_unstr_x(rn(node->u.w), rn(node->v.w), node->w.w);
		else
		    vfp_unstr_x(rn(node->u.w), rn(node->v.w), node->w.w);
		break;
	    case jit_code_unsti_x:
		if (jit_swf_p())
		    swf_unsti_x(node->u.w, rn(node->v.w), node->w.w);
		else
		    vfp_unsti_x(node->u.w, rn(node->v.w), node->w.w);
		break;
		case_vv(mov, _f);
	    case jit_code_movi_f:
		assert_data(node);
		if (jit_swf_p())
		    swf_movi_f(rn(node->u.w), node->v.f);
		else
		    vfp_movi_f(rn(node->u.w), node->v.f);
		break;
		case_vv(ext, _d_f);
		case_vvv(lt, _f);
		case_vvf(lt);
		case_vvv(le, _f);
		case_vvf(le);
		case_vvv(eq, _f);
		case_vvf(eq);
		case_vvv(ge, _f);
		case_vvf(ge);
		case_vvv(gt, _f);
		case_vvf(gt);
		case_vvv(ne, _f);
		case_vvf(ne);
		case_vvv(unlt, _f);
		case_vvf(unlt);
		case_vvv(unle, _f);
		case_vvf(unle);
		case_vvv(uneq, _f);
		case_vvf(uneq);
		case_vvv(unge, _f);
		case_vvf(unge);
		case_vvv(ungt, _f);
		case_vvf(ungt);
		case_vvv(ltgt, _f);
		case_vvf(ltgt);
		case_vvv(ord, _f);
		case_vvf(ord);
		case_vvv(unord, _f);
		case_vvf(unord);
		case_bvv(blt, _f);
		case_bvf(blt);
		case_bvv(ble, _f);
		case_bvf(ble);
		case_bvv(beq, _f);
		case_bvf(beq);
		case_bvv(bge, _f);
		case_bvf(bge);
		case_bvv(bgt, _f);
		case_bvf(bgt);
		case_bvv(bne, _f);
		case_bvf(bne);
		case_bvv(bunlt, _f);
		case_bvf(bunlt);
		case_bvv(bunle, _f);
		case_bvf(bunle);
		case_bvv(buneq, _f);
		case_bvf(buneq);
		case_bvv(bunge, _f);
		case_bvf(bunge);
		case_bvv(bungt, _f);
		case_bvf(bungt);
		case_bvv(bltgt, _f);
		case_bvf(bltgt);
		case_bvv(bord, _f);
		case_bvf(bord);
		case_bvv(bunord, _f);
		case_bvf(bunord);
		case_vvv(add, _d);
		case_vvd(add);
		case_vvv(sub, _d);
		case_vvd(sub);
		case_vvd(rsb);
		case_vvv(mul, _d);
		case_vvd(mul);
		case_vvv(div, _d);
		case_vvd(div);
		case_vv(abs, _d);
		case_vv(neg, _d);
		case_vv(sqrt, _d);
		case_rqr(fma, _d);
		case_rqr(fms, _d);
		case_rqr(fnma, _d);
		case_rqr(fnms, _d);
		case_vv(ext, _d);
		case_vv(ld, _d);
		case_vw(ld, _d);
		case_vvv(ldx, _d);
		case_vvw(ldx, _d);
		case_vv(st, _d);
		case_wv(st, _d);
		case_vvv(stx, _d);
		case_wvv(stx, _d);
		case_vv(mov, _d);
	    case jit_code_movi_d:
		assert_data(node);
		if (jit_swf_p())
		    swf_movi_d(rn(node->u.w), node->v.d);
		else
		    vfp_movi_d(rn(node->u.w), node->v.d);
		break;
		case_vv(ext, _f_d);
		case_vvv(lt, _d);
		case_vvd(lt);
		case_vvv(le, _d);
		case_vvd(le);
		case_vvv(eq, _d);
		case_vvd(eq);
		case_vvv(ge, _d);
		case_vvd(ge);
		case_vvv(gt, _d);
		case_vvd(gt);
		case_vvv(ne, _d);
		case_vvd(ne);
		case_vvv(unlt, _d);
		case_vvd(unlt);
		case_vvv(unle, _d);
		case_vvd(unle);
		case_vvv(uneq, _d);
		case_vvd(uneq);
		case_vvv(unge, _d);
		case_vvd(unge);
		case_vvv(ungt, _d);
		case_vvd(ungt);
		case_vvv(ltgt, _d);
		case_vvd(ltgt);
		case_vvv(ord, _d);
		case_vvd(ord);
		case_vvv(unord, _d);
		case_vvd(unord);
		case_bvv(blt, _d);
		case_bvd(blt);
		case_bvv(ble, _d);
		case_bvd(ble);
		case_bvv(beq, _d);
		case_bvd(beq);
		case_bvv(bge, _d);
		case_bvd(bge);
		case_bvv(bgt, _d);
		case_bvd(bgt);
		case_bvv(bne, _d);
		case_bvd(bne);
		case_bvv(bunlt, _d);
		case_bvd(bunlt);
		case_bvv(bunle, _d);
		case_bvd(bunle);
		case_bvv(buneq, _d);
		case_bvd(buneq);
		case_bvv(bunge, _d);
		case_bvd(bunge);
		case_bvv(bungt, _d);
		case_bvd(bungt);
		case_bvv(bltgt, _d);
		case_bvd(bltgt);
		case_bvv(bord, _d);
		case_bvd(bord);
		case_bvv(bunord, _d);
		case_bvd(bunord);
	    case jit_code_jmpr:
		jit_check_frame();
		jmpr(rn(node->u.w));
		flush_consts();
		break;
	    case jit_code_jmpi:
		if (node->flag & jit_flag_node) {
		    temp = node->u.n;
		    assert(temp->code == jit_code_label ||
			   temp->code == jit_code_epilog);
		    if (temp->flag & jit_flag_patch)
			jmpi(temp->u.w);
		    else {
			word = _jit->code.length -
			    (_jit->pc.uc - _jit->code.ptr);
			if (jit_thumb_p())	word >>= 1;
			else			word >>= 2;
			word -= 2;
			value = _s24P(word);
			word = jmpi_p(_jit->pc.w, value);
			patch(word, node, value ?
			      arm_patch_jump : arm_patch_word);
		    }
		}
		else {
		    jit_check_frame();
		    jmpi(node->u.w);
		}
		flush_consts();
		break;
	    case jit_code_callr:
		jit_check_frame();
		callr(rn(node->u.w));
		break;
	    case jit_code_calli:
		if (node->flag & jit_flag_node) {
		    CHECK_RETURN();
		    temp = node->u.n;
		    assert(temp->code == jit_code_label ||
			   temp->code == jit_code_epilog);
		    if (temp->flag & jit_flag_patch)
			calli(temp->u.w, 0);
		    else {
			word = _jit->code.length -
			    (_jit->pc.uc - _jit->code.ptr);
			if (jit_exchange_p())
			    word -= 8;
			if (jit_thumb_p())	word >>= 1;
			else			word >>= 2;
			word -= 2;
			value = _s24P(word);
			word = calli_p(_jit->pc.w, value);
			patch(word, node, value ?
			      arm_patch_call : arm_patch_word);
		    }
		}
		else {
		    jit_check_frame();
		    calli(node->u.w, jit_exchange_p());
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
		undo.data = _jitc->consts.data;
		undo.thumb = _jitc->thumb;
		undo.const_offset = _jitc->consts.offset;
		undo.patch_offset = _jitc->patches.offset;
#if DISASSEMBLER
		if (_jitc->data_info.ptr)
		    undo.info_offset = _jitc->data_info.offset;
#endif
	    restart_function:
		_jitc->again = 0;
		compute_framesize();
		patch_alist(0);
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
		    undo.func.need_return = _jitc->function->need_return;
		    jit_regset_set(&undo.func.regset, &_jitc->function->regset);
		    /* allocar information also does not need to be undone */
		    undo.func.aoffoff = _jitc->function->aoffoff;
		    undo.func.allocar = _jitc->function->allocar;
		    /* swf_offset and check_reg_args must also not be undone */
		    undo.func.swf_offset = _jitc->function->swf_offset;
		    undo.func.save_reg_args = _jitc->function->save_reg_args;
		    memcpy(_jitc->function, &undo.func, sizeof(undo.func));
#if DEVEL_DISASSEMBLER
		    prevw = undo.prevw;
#endif
		    invalidate_consts();
		    _jitc->consts.data = undo.data;
		    _jitc->thumb = undo.thumb;
		    _jitc->consts.offset = undo.const_offset;
		    _jitc->patches.offset = undo.patch_offset;
#if DISASSEMBLER
		    if (_jitc->data_info.ptr)
			_jitc->data_info.offset = undo.info_offset;
#endif
		    patch_alist(1);
		    goto restart_function;
		}
		/* remember label is defined */
		node->flag |= jit_flag_patch;
		node->u.w = _jit->pc.w;
		epilog(node);
		_jitc->function = NULL;
		flush_consts();
		break;
	    case jit_code_movr_w_f:
		if (jit_swf_p())
		    swf_movr_w_f(rn(node->u.w), rn(node->v.w));
		else
		    vfp_movr_w_f(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_movr_f_w:
		if (jit_swf_p())
		    swf_movr_f_w(rn(node->u.w), rn(node->v.w));
		else
		    vfp_movr_f_w(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_movi_f_w:
		assert_data(node);
		movi_f_w(rn(node->u.w), node->v.f);
		break;
	    case jit_code_movi_w_f:
		if (jit_swf_p())
		    swf_movi_w_f(rn(node->u.w), node->v.w);
		else
		    vfp_movi_w_f(rn(node->u.w), node->v.w);
		break;
	    case jit_code_movr_ww_d:
		if (jit_swf_p())
		    swf_movr_ww_d(rn(node->u.w), rn(node->v.w), rn(node->w.w));
		else
		    vfp_movr_ww_d(rn(node->u.w), rn(node->v.w), rn(node->w.w));
		break;
	    case jit_code_movr_d_ww:
		if (jit_swf_p())
		    swf_movr_d_ww(rn(node->u.w), rn(node->v.w), rn(node->w.w));
		else
		    vfp_movr_d_ww(rn(node->u.w), rn(node->v.w), rn(node->w.w));
		break;
	    case jit_code_movi_d_ww:
		movi_d_ww(rn(node->u.w), rn(node->v.w), node->w.d);
		break;
	    case jit_code_movi_ww_d:
		if (jit_swf_p())
		    swf_movi_ww_d(rn(node->u.w), node->v.w, node->w.w);
		else
		    vfp_movi_ww_d(rn(node->u.w), node->v.w, node->w.w);
		break;
	    case jit_code_va_start:
		vastart(rn(node->u.w));
		break;
	    case jit_code_va_arg:
		vaarg(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_va_arg_d:
		if (jit_swf_p())
		    swf_vaarg_d(rn(node->u.w), rn(node->v.w));
		else
		    vfp_vaarg_d(rn(node->u.w), rn(node->v.w));
		break;
	    case jit_code_live:			case jit_code_ellipsis:
	    case jit_code_va_push:
	    case jit_code_allocai:		case jit_code_allocar:
	    case jit_code_arg_c:		case jit_code_arg_s:
	    case jit_code_arg_i:
	    case jit_code_arg_f:		case jit_code_arg_d:
	    case jit_code_va_end:
	    case jit_code_ret:
	    case jit_code_retr_c:		case jit_code_reti_c:
	    case jit_code_retr_uc:		case jit_code_reti_uc:
	    case jit_code_retr_s:		case jit_code_reti_s:
	    case jit_code_retr_us:		case jit_code_reti_us:
	    case jit_code_retr_i:		case jit_code_reti_i:
	    case jit_code_retr_f:		case jit_code_reti_f:
	    case jit_code_retr_d:		case jit_code_reti_d:
	    case jit_code_getarg_c:		case jit_code_getarg_uc:
	    case jit_code_getarg_s:		case jit_code_getarg_us:
	    case jit_code_getarg_i:
	    case jit_code_getarg_f:		case jit_code_getarg_d:
	    case jit_code_putargr_c:		case jit_code_putargi_c:
	    case jit_code_putargr_uc:		case  jit_code_putargi_uc:
	    case jit_code_putargr_s:		case jit_code_putargi_s:
	    case jit_code_putargr_us:		case jit_code_putargi_us:
	    case jit_code_putargr_i:		case jit_code_putargi_i:
	    case jit_code_putargr_f:		case jit_code_putargi_f:
	    case jit_code_putargr_d:		case jit_code_putargi_d:
	    case jit_code_pushargr_c:		case jit_code_pushargi_c:
	    case jit_code_pushargr_uc:		case jit_code_pushargi_uc:
	    case jit_code_pushargr_s:		case jit_code_pushargi_s:
	    case jit_code_pushargr_us:		case jit_code_pushargi_us:
	    case jit_code_pushargr_i:		case jit_code_pushargi_i:
	    case jit_code_pushargr_f:		case jit_code_pushargi_f:
	    case jit_code_pushargr_d:		case jit_code_pushargi_d:
	    case jit_code_retval_c:		case jit_code_retval_uc:
	    case jit_code_retval_s:		case jit_code_retval_us:
	    case jit_code_retval_i:
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

#if defined JIT_INSTR_MAX
	word = 4096 - JIT_INSTR_MAX;
#else
	word = 3968;
#endif
	/* longest sequence should be 64 bytes, but preventively
	 * do not let it go past 256 remaining bytes before a flush */
	if (word > 3968)
	    word = 3968;
	if (_jitc->consts.length &&
	    (_jit->pc.uc - _jitc->consts.data >= word ||
	     (jit_uword_t)_jit->pc.uc -
	     (jit_uword_t)_jitc->consts.patches[0] >= word)) {
	    if (node->next &&
		node->next->code != jit_code_jmpi &&
		node->next->code != jit_code_jmpr &&
		node->next->code != jit_code_epilog) {
		/* insert a jump, flush constants and continue */
		word = _jit->pc.w;
		assert(!jit_thumb_p());
		B(0);
		flush_consts();
		patch_at(arm_patch_jump, word, _jit->pc.w);
	    }
	}
    }
#undef case_bvd
#undef case_bvf
#undef case_brw
#undef case_bvv
#undef case_brr
#undef case_wvv
#undef case_wrr
#undef case_vvd
#undef case_vvf
#undef case_vvw
#undef case_rrw
#undef case_vvv
#undef case_rrx
#undef case_rrX
#undef case_xrr
#undef case_Xrr
#undef case_rrr
#undef case_wv
#undef case_wr
#undef case_vw
#undef case_vv
#undef case_rw
#undef case_rr

    flush_consts();
    for (offset = 0; offset < _jitc->patches.offset; offset++) {
	assert(_jitc->patches.ptr[offset].kind & arm_patch_node);
	node = _jitc->patches.ptr[offset].node;
	word = _jitc->patches.ptr[offset].inst;
	if (!jit_thumb_p() &&
	    (node->code == jit_code_movi ||
	     (node->code == jit_code_calli &&
	      (_jitc->patches.ptr[offset].kind & ~arm_patch_node) ==
	      arm_patch_word))) {
	    /* calculate where to patch word */
	    value = *(jit_int32_t *)word;
	    assert((value & 0x0f700000) == ARM_LDRI);
	    /* offset may become negative (-4) if last instruction
	     * before unconditional branch and data following
	     * FIXME can this cause issues in the preprocessor prefetch
	     * or something else? should not, as the constants are after
	     * an unconditional jump */
	    if (value & ARM_U)	value =   value & 0x00000fff;
	    else		value = -(value & 0x00000fff);
	    word = word + 8 + value;
 	}
	value = node->code == jit_code_movi ? node->v.n->u.w : node->u.n->u.w;
	patch_at(_jitc->patches.ptr[offset].kind & ~arm_patch_node, word, value);
    }

    jit_flush(_jit->code.ptr, _jit->pc.uc);

    return (_jit->code.ptr);
}

#define CODE				1
#  include "jit_rewind.c"
#  include "jit_arm-cpu.c"
#  include "jit_arm-swf.c"
#  include "jit_arm-vfp.c"
#  include "jit_fallback.c"
#undef CODE

void
jit_flush(void *fptr, void *tptr)
{
#if defined(__GNUC__)
    jit_uword_t		i, f, t, s;

    s = sysconf(_SC_PAGE_SIZE);
    f = (jit_uword_t)fptr & -s;
    t = (((jit_uword_t)tptr) + s - 1) & -s;
    for (i = f; i < t; i += s)
	__clear_cache((void *)i, (void *)(i + s));
#endif
}

void
_emit_ldxi(jit_state_t *_jit, jit_int32_t r0, jit_int32_t r1, jit_word_t i0)
{
    ldxi_i(rn(r0), rn(r1), i0);
}

void
_emit_stxi(jit_state_t *_jit, jit_word_t i0, jit_int32_t r0, jit_int32_t r1)
{
    stxi_i(i0, rn(r0), rn(r1));
}

void
_emit_ldxi_d(jit_state_t *_jit, jit_int32_t r0, jit_int32_t r1, jit_word_t i0)
{
    if (jit_swf_p())
	swf_ldxi_d(rn(r0), rn(r1), i0);
    else
	vfp_ldxi_d(rn(r0), rn(r1), i0);
}

void
_emit_stxi_d(jit_state_t *_jit, jit_word_t i0, jit_int32_t r0, jit_int32_t r1)
{
    if (jit_swf_p())
	swf_stxi_d(i0, rn(r0), rn(r1));
    else
	vfp_stxi_d(i0, rn(r0), rn(r1));
}

static jit_int32_t
_jit_get_reg_pair(jit_state_t *_jit)
{
    /*   bypass jit_get_reg() with argument or'ed with jit_class_chk
     * and try to find an consecutive, even free register pair, or
     * return JIT_NOREG if fail, as the cost of spills is greater
     * than splitting a double load/store in two operations. */
    if (jit_reg_free_p(_R0) && jit_reg_free_p(_R1)) {
	jit_regset_setbit(&_jitc->regarg, _R0);
	jit_regset_setbit(&_jitc->regarg, _R1);
	return (_R0);
    }
    if (jit_reg_free_p(_R2) && jit_reg_free_p(_R3)) {
	jit_regset_setbit(&_jitc->regarg, _R2);
	jit_regset_setbit(&_jitc->regarg, _R3);
	return (_R2);
    }
    if (jit_reg_free_p(_R4) && jit_reg_free_p(_R5)) {
	jit_regset_setbit(&_jitc->regarg, _R4);
	jit_regset_setbit(&_jitc->regarg, _R5);
	return (_R4);
    }
    if (jit_reg_free_p(_R6) && jit_reg_free_p(_R7)) {
	jit_regset_setbit(&_jitc->regarg, _R6);
	jit_regset_setbit(&_jitc->regarg, _R7);
	return (_R6);
    }
    if (jit_reg_free_p(_R8) && jit_reg_free_p(_R9)) {
	jit_regset_setbit(&_jitc->regarg, _R8);
	jit_regset_setbit(&_jitc->regarg, _R9);
	return (_R8);
    }
    return (JIT_NOREG);
}

static void
_jit_unget_reg_pair(jit_state_t *_jit, jit_int32_t reg)
{
    jit_unget_reg(reg);
    switch (reg) {
	case _R0:	jit_unget_reg(_R1);	break;
	case _R2:	jit_unget_reg(_R3);	break;
	case _R4:	jit_unget_reg(_R5);	break;
	case _R6:	jit_unget_reg(_R7);	break;
	case _R8:	jit_unget_reg(_R9);	break;
	default:	abort();
    }
}

/*   A prolog must be aligned at mod 4 bytes boundary.
 *   This condition was not being required to be tested by
 * accident previously, but with the jit_frame and jit_tramp
 * code it is required */
static jit_bool_t
_must_align_p(jit_state_t *_jit, jit_node_t *node)
{
    if (jit_thumb_p() && (_jit->pc.w & 3)) {
	for (; node; node = node->next) {
	    switch (node->code) {
		case jit_code_note:
		case jit_code_name:
		case jit_code_label:
		    break;
		case jit_code_prolog:
		    return (1);
		default:
		    return (0);
	    }
	}
    }
    return (0);
}

static void
_load_const(jit_state_t *_jit, jit_bool_t uniq, jit_int32_t r0, jit_word_t i0)
{
    jit_word_t		 w;
    jit_word_t		 d;
    jit_word_t		 base;
    jit_int32_t		*data;
    jit_int32_t		 size;
    jit_int32_t		 offset;

    assert(!jit_thumb_p());
    if (!uniq) {
	/* use zero, a valid directly encoded immediate, to avoid the
	 * need of a bitmask to know what offsets will be patched, so
	 * that comparison will always fail for constants that cannot
	 * be encoded */
	assert(i0 != 0);

	/* Actually, code is (currently at least) not self modifying,
	 * so, any value reachable backwards is valid as a constant. */

	/* FIXME a quickly updateable/mutable hash table could be
	 * better here, but most times only a few comparisons
	 * should be done
	 */

	/* search in previous constant pool */
	if ((data = (jit_int32_t *)_jitc->consts.data)) {
	    w = (jit_word_t)data;
	    /* maximum backwards offset */
	    base = (_jit->pc.w + 8) - 4092;
	    if (base <= w)
		/* can scan all possible available backward constants */
		base = 0;
	    else
		base = (base - w) >> 2;
	    size = _jitc->consts.size >> 2;
	    for (offset = size - 1; offset >= base; offset--) {
		if (data[offset] == i0) {
		    w = (jit_word_t)(data + offset);
		    d = (_jit->pc.w + 8) - w;
		    LDRIN(r0, _R15_REGNO, d);
		    return;
		}
	    }
	}
    }
    else
	assert(i0 == 0);

    _jitc->consts.patches[_jitc->consts.offset++] = _jit->pc.w;
    /* (probably) positive forward offset */
    LDRI(r0, _R15_REGNO, 0);

    if (!uniq) {
	/* search already requested values */
	for (offset = 0; offset < _jitc->consts.length; offset++) {
	    if (_jitc->consts.values[offset] == i0) {
		_jitc->consts.patches[_jitc->consts.offset++] = offset;
		return;
	    }
	}
    }

#if DEBUG
    /* cannot run out of space because of limited range
     * but assert anyway to catch logic errors */
    assert(_jitc->consts.length < 1024);
    assert(_jitc->consts.offset < 2048);
#endif
    _jitc->consts.patches[_jitc->consts.offset++] = _jitc->consts.length;
    _jitc->consts.values[_jitc->consts.length++] = i0;
}

static void
_flush_consts(jit_state_t *_jit)
{
    jit_word_t		 word;
    jit_int32_t		 offset;

    /* if no forward constants */
    if (!_jitc->consts.length)
	return;
    assert(!jit_thumb_p());
    word = _jit->pc.w;
    _jitc->consts.data = _jit->pc.uc;
    _jitc->consts.size = _jitc->consts.length << 2;
    /* FIXME check will not overrun, otherwise, need to reallocate
     * code buffer and start over */
    jit_memcpy(_jitc->consts.data, _jitc->consts.values, _jitc->consts.size);
    _jit->pc.w += _jitc->consts.size;

#if DISASSEMBLER
    if (_jitc->data_info.ptr) {
	if (_jitc->data_info.offset >= _jitc->data_info.length) {
	    jit_realloc((jit_pointer_t *)&_jitc->data_info.ptr,
			_jitc->data_info.length * sizeof(jit_data_info_t),
			(_jitc->data_info.length + 1024) *
			sizeof(jit_data_info_t));
	    _jitc->data_info.length += 1024;
	}
	_jitc->data_info.ptr[_jitc->data_info.offset].code = word;
	_jitc->data_info.ptr[_jitc->data_info.offset].length = _jitc->consts.size;
	++_jitc->data_info.offset;
    }
#endif

    for (offset = 0; offset < _jitc->consts.offset; offset += 2)
	patch_at(arm_patch_load, _jitc->consts.patches[offset],
		 word + (_jitc->consts.patches[offset + 1] << 2));
    _jitc->consts.length = _jitc->consts.offset = 0;
}

/* to be called if needing to start over a function */
static void
_invalidate_consts(jit_state_t *_jit)
{
    /* if no forward constants */
    if (_jitc->consts.length)
	_jitc->consts.length = _jitc->consts.offset = 0;
}

static void
_compute_framesize(jit_state_t *_jit)
{
    jit_int32_t		reg;
    _jitc->framesize = sizeof(jit_word_t) * 2;	/* lr+fp */
    for (reg = 0; reg < jit_size(iregs); reg++)
	if (jit_regset_tstbit(&_jitc->function->regset, iregs[reg]))
	    _jitc->framesize += sizeof(jit_word_t);

    if (_jitc->function->save_reg_args)
	_jitc->framesize += 16;

    /* Make sure functions called have a 8 byte aligned stack */
    _jitc->framesize = (_jitc->framesize + 7) & -8;
}

static void
_patch(jit_state_t *_jit, jit_word_t instr, jit_node_t *node, jit_int32_t kind)
{
    jit_int32_t		 flag;

    assert(node->flag & jit_flag_node);
    if (node->code == jit_code_movi)
	flag = node->v.n->flag;
    else
	flag = node->u.n->flag;
    assert(!(flag & jit_flag_patch));
    kind |= arm_patch_node;
    if (_jitc->patches.offset >= _jitc->patches.length) {
	jit_realloc((jit_pointer_t *)&_jitc->patches.ptr,
		    _jitc->patches.length * sizeof(jit_patch_t),
		    (_jitc->patches.length + 1024) * sizeof(jit_patch_t));
	_jitc->patches.length += 1024;
    }
    _jitc->patches.ptr[_jitc->patches.offset].kind = kind;
    _jitc->patches.ptr[_jitc->patches.offset].inst = instr;
    _jitc->patches.ptr[_jitc->patches.offset].node = node;
    ++_jitc->patches.offset;
}
