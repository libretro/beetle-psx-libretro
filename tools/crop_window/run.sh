#!/bin/sh
# Overscan crop window lane: the cropped picture keeps the game's
# horizontal display range and the requested aspect ratio follows it
# (see crop_window_test.c).  Runs against an already built software core.
#
# usage: tools/crop_window/run.sh [core]
#   core defaults to the core in the tree root (MSYS2: the .dll)
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"

case "$(uname -s)" in
   MINGW*|MSYS*|CYGWIN*)
      CORE="${1:-mednafen_psx_libretro.dll}"
      HOST=tools/crop_window/crop_window_test.exe
      LIBS="-lm"
      ;;
   *)
      CORE="${1:-./mednafen_psx_libretro.so}"
      HOST=tools/crop_window/crop_window_test
      LIBS="-lm -ldl"
      ;;
esac

[ -f "$CORE" ] || { echo "no core at $CORE - build it first (make)"; exit 2; }
case "$CORE" in
   /*) ;;
   *) CORE="$(pwd)/$CORE" ;;
esac

$CC -std=c89 -pedantic -Wall -Wno-long-long -O1 -Ilibretro-common/include \
   -o "$HOST" tools/crop_window/crop_window_test.c $LIBS

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HOST="$(pwd)/$HOST"

fail=0
for c in std320 std368 wide368 wide368_static wide368_off switch368 \
         switch368_dupe; do
   (cd "$WORK" && "$HOST" "$CORE" "$c") || fail=1
done
exit $fail
