#!/bin/sh
# Software-renderer 480i lane (see deint_test.c): every deinterlacer at
# 1x and 2x, uncropped and with the static and smart overscan crops,
# against an already built software core.
#
# usage: tools/deinterlace/run.sh [core]
#   core defaults to the core in the tree root (MSYS2: the .dll)
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"

case "$(uname -s)" in
   MINGW*|MSYS*|CYGWIN*)
      CORE="${1:-mednafen_psx_libretro.dll}"
      HOST=tools/deinterlace/deint_test.exe
      LIBS=""
      ;;
   *)
      CORE="${1:-./mednafen_psx_libretro.so}"
      HOST=tools/deinterlace/deint_test
      LIBS="-ldl"
      ;;
esac

[ -f "$CORE" ] || { echo "no core at $CORE - build it first (make)"; exit 2; }
case "$CORE" in
   /*) ;;
   *) CORE="$(pwd)/$CORE" ;;
esac

$CC -std=c89 -pedantic -Wall -Wextra -Wno-long-long -O1 \
   -Ilibretro-common/include -o "$HOST" tools/deinterlace/deint_test.c $LIBS

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
HOST="$(pwd)/$HOST"

fail=0
for crop in disabled static smart; do
   for res in "1x(native)" 2x; do
      for mode in weave bob bob_offset fastmad off; do
         (cd "$WORK" && "$HOST" "$CORE" "$mode" "$res" "$crop") || fail=1
      done
   done
done
exit $fail
