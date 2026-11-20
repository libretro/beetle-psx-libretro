#!/bin/sh
# SPU silent-voice lane: with the "SPU Silent Voice Optimization" option
# on, voices that keep falling silent and being keyed on again, the SPU
# IRQ and noise mode switched under them and a savestate taken and loaded
# back sound exactly as with every voice mixed, and leave SPU RAM the same
# (see spu_silent_test.c).
# Runs against an already built core.
#
# usage: tools/spu_silent/run.sh [core]
#   core defaults to the core in the tree root (MSYS2: the .dll)
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"

case "$(uname -s)" in
   MINGW*|MSYS*|CYGWIN*)
      CORE="${1:-mednafen_psx_libretro.dll}"
      HOST=tools/spu_silent/spu_silent_test.exe
      LIBS=""
      ;;
   *)
      CORE="${1:-./mednafen_psx_libretro.so}"
      HOST=tools/spu_silent/spu_silent_test
      LIBS="-ldl"
      ;;
esac

[ -f "$CORE" ] || { echo "no core at $CORE - build it first (make)"; exit 2; }
case "$CORE" in
   /*) ;;
   *) CORE="$(pwd)/$CORE" ;;
esac

$CC -std=c89 -pedantic -Wall -Wno-long-long -O1 -Ilibretro-common/include \
   -o "$HOST" tools/spu_silent/spu_silent_test.c $LIBS

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HOST="$(pwd)/$HOST"
cd "$WORK"

off="$("$HOST" "$CORE" off)"
on="$("$HOST" "$CORE" on)"
rm -f "$HOST"
echo "[spu_silent] every voice mixed: $off"
if [ "$on" = "$off" ]; then
   echo "[spu_silent] silent voices skipped: $on OK"
else
   echo "[spu_silent] silent voices skipped: $on FAIL"
   exit 1
fi
