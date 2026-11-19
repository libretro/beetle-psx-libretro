#!/bin/sh
# Memory card lane: under Run-Ahead (1 and 3 speculative frames, with and
# without the lifetime callbacks) and Preemptive Frames, a card the game
# writes reaches its file during play and is not written again unchanged;
# on a frontend that reports neither savestate context nor execution phase
# it still reaches its file by unload (see memcard_rollback_test.c). Runs
# against an already built core, software or HAVE_HW=1 (on its software
# renderer).
#
# usage: tools/memcard_rollback/run.sh [core]
#   core defaults to the core in the tree root (MSYS2: the .dll)
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"

case "$(uname -s)" in
   MINGW*|MSYS*|CYGWIN*)
      CORE="${1:-mednafen_psx_libretro.dll}"
      HOST=tools/memcard_rollback/memcard_rollback_test.exe
      LIBS=""
      ;;
   *)
      CORE="${1:-./mednafen_psx_libretro.so}"
      HOST=tools/memcard_rollback/memcard_rollback_test
      LIBS="-ldl"
      ;;
esac

[ -f "$CORE" ] || { echo "no core at $CORE - build it first (make)"; exit 2; }
case "$CORE" in
   /*) ;;
   *) CORE="$(pwd)/$CORE" ;;
esac

$CC -std=c89 -pedantic -Wall -Wno-long-long -O1 -Ilibretro-common/include \
   -o "$HOST" tools/memcard_rollback/memcard_rollback_test.c $LIBS

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HOST="$(pwd)/$HOST"

fail=0
for run in "plain" "runahead 1" "runahead 3" "managed 1" "preempt" "legacy 1"; do
   (cd "$WORK" && "$HOST" "$CORE" $run) || fail=1
done
rm -f "$HOST"
exit $fail
