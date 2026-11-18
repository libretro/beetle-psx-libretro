#!/bin/sh
# GTE register lane: every GTE data register comes back from a loaded
# state, survives switching between the interpreter and the dynarec, and
# starts from its power-on value after a reset or a second load in the same
# process (see gte_state_test.c).  Runs against an already built software
# core, with the interpreter and, where the core has lightrec, both dynarec
# modes.
#
# usage: tools/gte_state/run.sh [core]
#   core defaults to the core in the tree root (MSYS2: the .dll)
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"

case "$(uname -s)" in
   MINGW*|MSYS*|CYGWIN*)
      CORE="${1:-mednafen_psx_libretro.dll}"
      HOST=tools/gte_state/gte_state_test.exe
      LIBS=""
      ;;
   *)
      CORE="${1:-./mednafen_psx_libretro.so}"
      HOST=tools/gte_state/gte_state_test
      LIBS="-ldl"
      ;;
esac

[ -f "$CORE" ] || { echo "no core at $CORE - build it first (make)"; exit 2; }
case "$CORE" in
   /*) ;;
   *) CORE="$(pwd)/$CORE" ;;
esac

$CC -std=c89 -pedantic -Wall -Wno-long-long -O1 -Ilibretro-common/include \
   -o "$HOST" tools/gte_state/gte_state_test.c $LIBS

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HOST="$(pwd)/$HOST"

fail=0
for case in load reset reload; do
   for mode in disabled execute run_interpreter; do
      (cd "$WORK" && "$HOST" "$CORE" "$case" "$mode") || fail=1
   done
done
(cd "$WORK" && "$HOST" "$CORE" toggle execute) || fail=1
rm -f "$HOST"
exit $fail
