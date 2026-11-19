#!/bin/sh
# GPU thread lane: the software renderer's VRAM, pictures, VRAM read back
# through GPUREAD and emulated GPU timing are the same with the GPU thread
# off, on, switched on and off mid-run, and with 2, 3 and 4 band lanes, at
# 1x, 2x and 4x, with PGXP
# off and on, and with the 480i "Off" deinterlacer (see gpu_thread_test.c).
# Runs against an already built core; for an ARM build,
# CC=aarch64-linux-gnu-gcc RUN="qemu-aarch64 -L /usr/aarch64-linux-gnu".
#
# usage: tools/gpu_thread/run.sh [core]
#   core defaults to the core in the tree root (MSYS2: the .dll)
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"

case "$(uname -s)" in
   MINGW*|MSYS*|CYGWIN*)
      CORE="${1:-mednafen_psx_libretro.dll}"
      HOST=tools/gpu_thread/gpu_thread_test.exe
      LIBS=""
      ;;
   *)
      CORE="${1:-./mednafen_psx_libretro.so}"
      HOST=tools/gpu_thread/gpu_thread_test
      LIBS="-ldl"
      ;;
esac

[ -f "$CORE" ] || { echo "no core at $CORE - build it first (make)"; exit 2; }
case "$CORE" in
   /*) ;;
   *) CORE="$(pwd)/$CORE" ;;
esac

$CC -std=c89 -pedantic -Wall -Wno-long-long -O1 -Ilibretro-common/include \
   -o "$HOST" tools/gpu_thread/gpu_thread_test.c $LIBS

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HOST="$(pwd)/$HOST"

fail=0
for cfg in "1x nopgxp" "2x nopgxp" "4x nopgxp" "1x pgxp" "2x pgxp" \
           "4x pgxp" "1x nopgxp deint" "2x nopgxp deint"; do
   set -- $cfg
   ref="$(cd "$WORK" && ${RUN:-} "$HOST" "$CORE" "$1" off $2 $3)" || { fail=1; continue; }
   for mode in on toggle 2 3 4; do
      got="$(cd "$WORK" && ${RUN:-} "$HOST" "$CORE" "$1" $mode $2 $3)" || got="(failed)"
      if [ "$got" = "$ref" ]; then
         echo "[gpu_thread] $cfg $mode: $got OK"
      else
         echo "[gpu_thread] $cfg $mode: $got, thread off gives $ref FAIL"
         fail=1
      fi
   done
done
rm -f "$HOST"
exit $fail
