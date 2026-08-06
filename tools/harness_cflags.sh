#!/bin/sh
# Print the preprocessor flags the core is actually built with.
#
# The harnesses under tools/ compile core sources outside the core's own
# build, and every one of them used to carry a hand-written -D list. That
# list drifted, and the drift was not visible: a harness that opens the same
# disc images the core opens, built without -DHAVE_MMAP, never takes the file
# mapping path, so a heap corruption that fired on every mapped disc image
# was unreachable in test while looking fully covered.
#
# Rather than copy the list again, this reads it back out of the real build.
# `make -n` on one object prints the command line the core would use; the -D
# flags are lifted from it. If the core's flags change, so do the harnesses',
# with no second place to update.
#
# Usage, from the repo root:
#     CFLAGS="$(tools/harness_cflags.sh)"
#     gcc $CFLAGS -Ilibretro-common/include -Imednafen -I. ...
#
# Pass an object path to sample a different translation unit; the default is
# a core TU that carries the full set.

set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OBJ=${1:-mednafen/psx/gpu.o}

cd "$ROOT"

# -n so nothing is built, and -B so the rule is printed even when the object
# is already up to date -- without it this prints nothing at all against a
# tree that has been built, which is most of them. GIT_VERSION is dropped: it
# embeds quotes that do not survive re-quoting through a shell variable, and
# nothing under test reads it.
make -Bn "$OBJ" 2>/dev/null \
  | grep -m1 -- "-c .*$(basename "$OBJ" .o)\.c" \
  | tr ' ' '\n' \
  | grep '^-D' \
  | grep -v '^-DGIT_VERSION' \
  | sort -u \
  | tr '\n' ' '
echo
