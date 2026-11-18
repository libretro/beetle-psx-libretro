#!/bin/sh
# Software rasterizer span lane: a fixed GP0 stream covering every textured
# span variant hashes to the scalar renderer's VRAM and frame at 1x, 2x and
# 4x, with the same emulated GPU timing at each (see gpu_span_test.c).  Runs
# against an already built core; for an ARM
# build, CC=aarch64-linux-gnu-gcc RUN="qemu-aarch64 -L /usr/aarch64-linux-gnu".
#
# usage: tools/gpu_span/run.sh [core]
#   core defaults to the core in the tree root (MSYS2: the .dll)
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"

case "$(uname -s)" in
   MINGW*|MSYS*|CYGWIN*)
      CORE="${1:-mednafen_psx_libretro.dll}"
      HOST=tools/gpu_span/gpu_span_test.exe
      LIBS=""
      ;;
   *)
      CORE="${1:-./mednafen_psx_libretro.so}"
      HOST=tools/gpu_span/gpu_span_test
      LIBS="-ldl"
      ;;
esac

[ -f "$CORE" ] || { echo "no core at $CORE - build it first (make)"; exit 2; }
case "$CORE" in
   /*) ;;
   *) CORE="$(pwd)/$CORE" ;;
esac

$CC -std=c89 -pedantic -Wall -Wno-long-long -O1 -Ilibretro-common/include \
   -o "$HOST" tools/gpu_span/gpu_span_test.c $LIBS

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HOST="$(pwd)/$HOST"

fail=0
for res in 1x 2x 4x; do
   (cd "$WORK" && ${RUN:-} "$HOST" "$CORE" "$res") || fail=1
done
rm -f "$HOST"
exit $fail
