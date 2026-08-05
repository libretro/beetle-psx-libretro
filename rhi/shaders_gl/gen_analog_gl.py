#!/usr/bin/env python3
"""Translate the analog chain's Vulkan GLSL into GL/GLES GLSL.

The analog shaders live in rhi/shaders_vulkan and are written against Vulkan
GLSL. Everything in them that is actually Vulkan-specific is mechanical:

    #version 450                        -> #version 330 core / 300 es
    #include "analog.h"                 -> textual expansion
    layout(set=,binding=) uniform ...   -> plain uniform
    layout(push_constant) uniform R{}   -> individual uniforms, reg.x -> reg_x
    layout(location=) in/out            -> plain in/out

so the GL backend does not need its own copy of the signal math. That matters:
a forked copy is a copy that silently stops matching, which is exactly what
happened to the offline reference chain before it was moved into the tree. One
source of truth, translated at build time.

The IIR trap (analog_notch.comp) is deliberately not handled here. It is a
compute shader with shared memory and a cross-lane scan; that needs GL 4.3 or
GLES 3.1, well above this renderer's GL/GLES 3.0 floor, so it cannot be a
straight translation and needs a decision about fallback rather than a
transform. See the README.

Run from rhi/shaders_gl:  ./gen_analog_gl.py
"""

import os
import re
import sys

SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'shaders_vulkan')
DST = os.path.dirname(os.path.abspath(__file__))

# (source, output stem, extra -D defines)
SHADERS = [
    ('analog.vert',            'analog_vertex',           []),
    ('analog_downsample.frag', 'analog_downsample',       []),
    ('analog_encode.frag',     'analog_encode',           []),
    ('analog_encode.frag',     'analog_encode_pal',       ['PAL']),
    ('analog_comb.frag',       'analog_comb',             []),
    ('analog_comb.frag',       'analog_comb_pal',         ['PAL']),
    ('analog_demod.frag',      'analog_demod',            []),
    ('analog_demod.frag',      'analog_demod_pal',        ['PAL']),
    ('analog_rgb.frag',        'analog_rgb',              []),
    ('analog_rgb.frag',        'analog_rgb_pal',          ['PAL']),
    ('analog_resolve.frag',    'analog_resolve',          []),
    ('analog_resolve.frag',    'analog_resolve_hdr',      ['HDR']),
    ('analog_yc.frag',         'analog_yc',               []),
    ('analog_yc.frag',         'analog_yc_pal',           ['PAL']),
]

# Compute stages. Separate because they need a higher GL/GLES version than the
# fragment floor: shared memory, barrier() and imageStore mean GL 4.3 / GLES
# 3.1. The backend runs these only when the context reports that, and falls
# back to analog_yc (the trap's own bypass branch) when it does not.
COMPUTE = [
    ('analog_notch.comp',      'analog_notch',            []),
    ('analog_notch.comp',      'analog_notch_pal',        ['PAL']),
]


def expand_includes(path, seen=None):
    """Inline #include "x.h" recursively, relative to the including file."""
    if seen is None:
        seen = set()
    real = os.path.realpath(path)
    if real in seen:
        return ''          # include guards in the headers make this safe
    seen.add(real)
    out = []
    base = os.path.dirname(path)
    for line in open(path).read().splitlines():
        m = re.match(r'\s*#\s*include\s+"([^"]+)"\s*$', line)
        if m:
            out.append('/* ---- begin %s ---- */' % m.group(1))
            out.append(expand_includes(os.path.join(base, m.group(1)), seen))
            out.append('/* ---- end %s ---- */' % m.group(1))
        else:
            out.append(line)
    return '\n'.join(out)


def strip_pragma_guards(text):
    """Drop the headers' #ifndef/#define/#endif guards after inlining.

    They are harmless to the compiler but the surrounding #if defined(PAL)
    blocks are not, so leaving guards in makes the output confusing to read
    when debugging a compile failure. Only the guard triple is removed.
    """
    out = []
    for line in text.splitlines():
        if re.match(r'\s*#\s*(ifndef|define)\s+(ANALOG_H|ANALOG_TAPS_H)\s*$', line):
            continue
        if re.match(r'\s*#\s*endif\s*/\*\s*(ANALOG_H|ANALOG_TAPS_H)\s*\*/\s*$', line):
            continue
        out.append(line)
    return '\n'.join(out)


def collect_push_uniforms(text):
    """Find the push-constant block and return (members, text-without-block).

    Members come back as a list of (type, name) in declaration order so the C
    side can be checked against them.
    """
    m = re.search(r'layout\s*\(\s*push_constant[^)]*\)\s*uniform\s+\w+\s*\{(.*?)\}\s*(\w+)\s*;',
                  text, re.S)
    if not m:
        return [], text
    body, inst = m.group(1), m.group(2)
    # Strip block comments across the whole body first: members are routinely
    # followed by comments that run onto the next line, and a per-line strip
    # would leave the declaration unterminated and silently drop it.
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    members = []
    for line in body.splitlines():
        line = line.strip()
        if not line or line.startswith('//'):
            continue
        mm = re.match(r'(?:highp\s+|mediump\s+|lowp\s+)?(\w+)\s+(.+?);$', line)
        if not mm:
            continue
        ctype = mm.group(1)
        for nm in mm.group(2).split(','):
            members.append((ctype, nm.strip()))
    decls = '\n'.join('uniform %s %s_%s;' % (t, inst, n) for t, n in members)
    text = text[:m.start()] + decls + text[m.end():]
    # reg.foo -> reg_foo
    text = re.sub(r'\b%s\s*\.\s*(\w+)' % inst, r'%s_\1' % inst, text)
    return members, text


def translate(src_name, defines, es, compute=False):
    path = os.path.join(SRC, src_name)
    text = expand_includes(path)
    text = strip_pragma_guards(text)

    # Version + optional defines. The body's own #if defined(PAL)/HDR still
    # works because we inject real #defines rather than pre-resolving them.
    text = re.sub(r'^\s*#version\s+\d+\s*$', '', text, count=1, flags=re.M)

    members, text = collect_push_uniforms(text)

    # Descriptor-set bindings become plain uniforms; GL binds by name.
    # Image bindings keep their format layout - GL needs the format qualifier on
    # an image2D - but lose the descriptor set. Sampler bindings become plain
    # uniforms; GL binds those by name.
    text = re.sub(r'layout\s*\(\s*set\s*=\s*\d+\s*,\s*binding\s*=\s*(\d+)\s*,\s*(\w+)\s*\)\s*uniform',
                  r'layout(binding = \1, \2) uniform', text)
    text = re.sub(r'layout\s*\(\s*set\s*=\s*\d+\s*,\s*binding\s*=\s*(\d+)\s*\)\s*uniform',
                  'uniform', text)
    # Varying/attribute locations: GL 3.0 and GLES 3.0 do not allow location
    # qualifiers on varyings, and the single fragment output is bound by name.
    text = re.sub(r'layout\s*\(\s*location\s*=\s*\d+\s*\)\s*', '', text)

    if compute:
        header = ['#version %s' % ('310 es' if es else '430 core')]
    else:
        header = ['#version %s' % ('300 es' if es else '330 core')]
    if es:
        header.append('precision highp float;\nprecision highp int;')
    else:
        # Desktop GLSL accepts the precision qualifiers used throughout the
        # bodies only if the keywords exist; 330 core has them as no-ops.
        pass
    for d in defines:
        header.append('#define %s 1' % d)
    return '\n'.join(header) + '\n' + text, members


def emit_header(stem, glsl_desktop, glsl_es, members):
    guard = 'GL_%s_GLSL_H' % stem.upper()
    lines = []
    lines.append('/* GENERATED by rhi/shaders_gl/gen_analog_gl.py - do not edit.')
    lines.append(' * Source: rhi/shaders_vulkan/. Regenerate after changing the')
    lines.append(' * signal math there; the GL and Vulkan backends share it. */')
    lines.append('#ifndef %s' % guard)
    lines.append('#define %s' % guard)
    lines.append('')
    if members:
        lines.append('/* Push-constant members, as GL uniform names:')
        for t, n in members:
            lines.append(' *   %-8s reg_%s' % (t, n))
        lines.append(' */')
        lines.append('')
    for suffix, body in (('', glsl_desktop), ('_ES', glsl_es)):
        lines.append('static const char *%s_glsl%s =' % (stem, suffix))
        for ln in body.split('\n'):
            lines.append('   "%s\\n"' % ln.replace('\\', '\\\\').replace('"', '\\"'))
        lines.append('   ;')
        lines.append('')
    lines.append('#endif /* %s */' % guard)
    return '\n'.join(lines) + '\n'


def main():
    for src, stem, defines, is_compute in (
            [(a, b, c, False) for a, b, c in SHADERS] +
            [(a, b, c, True) for a, b, c in COMPUTE]):
        desktop, members = translate(src, defines, es=False, compute=is_compute)
        es, _ = translate(src, defines, es=True, compute=is_compute)
        out = os.path.join(DST, '%s.glsl.h' % stem)
        open(out, 'w').write(emit_header(stem, desktop, es, members))
        print('generated %s (%d uniforms)' % (os.path.basename(out), len(members)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
