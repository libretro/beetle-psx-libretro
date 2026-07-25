#!/usr/bin/env python3
"""Check that every ImageHandle member of Renderer is released in renderer_fini.

The Renderer is a plain C struct, so nothing drops its handles automatically -
renderer_fini names each one by hand. That list sits some two thousand lines
from the declarations and is not referenced by anything, so a handle added
later is silently never released. That is not a per-frame leak, because
ih_move drops the previous handle when a target is recreated, but the last set
survives teardown.

It has already happened once: the analog path grew five render targets over
several restructures and none of them reached renderer_fini, holding about
15 MB after the renderer went away.

Exits non-zero and names the offenders, so it can be wired into CI or a
pre-commit hook. Pure text matching - no compiler, no dependencies.
"""

import re
import sys
import os

SRC = os.path.join(os.path.dirname(__file__), "..", "rhi", "rhi_lib_vulkan.c")


def main():
    try:
        with open(SRC, encoding="utf-8", errors="replace") as f:
            src = f.read()
    except OSError as e:
        print("cannot read %s: %s" % (SRC, e))
        return 2

    # The Renderer struct: from its opening to the matching close at the same
    # indent. Brace counting rather than a regex, since the body nests.
    m = re.search(r'^(\s*)struct Renderer\s*$\n\s*\{', src, re.M)
    if not m:
        print("could not locate 'struct Renderer' - has it been renamed?")
        return 2
    start = src.index('{', m.start())
    depth, i = 0, start
    while i < len(src):
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0:
                break
        i += 1
    body = src[start:i]

    declared = re.findall(r'^\s*ImageHandle\s+(\w+)\s*;', body, re.M)
    if not declared:
        print("found no ImageHandle members - the struct layout has changed, "
              "so this check is no longer looking at the right thing")
        return 2

    fm = re.search(r'^static void renderer_fini\(Renderer \*self\)\s*$\n\{', src, re.M)
    if not fm:
        print("could not locate renderer_fini - has it been renamed?")
        return 2
    fstart = src.index('{', fm.start())
    depth, j = 0, fstart
    while j < len(src):
        if src[j] == '{':
            depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0:
                break
        j += 1
    fini = src[fstart:j]

    released = set(re.findall(r'ih_reset\(&self->(\w+)\)', fini))
    missing = [n for n in declared if n not in released]

    print("Renderer ImageHandle members : %d" % len(declared))
    print("released in renderer_fini    : %d" % len(released & set(declared)))

    if missing:
        print("\nnot released in renderer_fini:")
        for n in missing:
            print("    self->%s" % n)
        print("\nAdd an ih_reset(&self->NAME) for each, or this memory outlives "
              "the renderer.")
        return 1

    print("\nall released.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
