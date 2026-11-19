#!/bin/sh
# Hidden frame lane: with frame duping on, a change to the display made in
# a frame the frontend hides (Run-Ahead's kept frames, Preemptive Frames'
# replays) still reaches the screen (see hidden_frames_test.c). Runs
# against an already built core, software or HAVE_HW=1 (on its software
# renderer).
#
# usage: tools/hidden_frames/run.sh [core]
#   core defaults to the core in the tree root (MSYS2: the .dll)
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"

case "$(uname -s)" in
   MINGW*|MSYS*|CYGWIN*)
      CORE="${1:-mednafen_psx_libretro.dll}"
      HOST=tools/hidden_frames/hidden_frames_test.exe
      LIBS=""
      ;;
   *)
      CORE="${1:-./mednafen_psx_libretro.so}"
      HOST=tools/hidden_frames/hidden_frames_test
      LIBS="-ldl"
      ;;
esac

[ -f "$CORE" ] || { echo "no core at $CORE - build it first (make)"; exit 2; }
case "$CORE" in
   /*) ;;
   *) CORE="$(pwd)/$CORE" ;;
esac

$CC -std=c89 -pedantic -Wall -Wno-long-long -O1 -Ilibretro-common/include \
   -o "$HOST" tools/hidden_frames/hidden_frames_test.c $LIBS

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HOST="$(pwd)/$HOST"

fail=0
for run in "plain" "runahead 1" "runahead 3" "preempt"; do
   (cd "$WORK" && "$HOST" "$CORE" $run) || fail=1
done
rm -f "$HOST"
exit $fail
