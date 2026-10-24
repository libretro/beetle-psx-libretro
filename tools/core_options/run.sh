#!/bin/sh
# Builds tools/core_options/dt_visibility (strict C89) and runs it against
# a hardware build of the core.
#   sh tools/core_options/run.sh [mednafen_psx_hw_libretro.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=tools/core_options/dt_visibility
${CC:-cc} -std=c89 -pedantic -Wall -Werror -O1 -g -Ilibretro-common/include \
   -o "$OUT" tools/core_options/dt_visibility.c -ldl
"./$OUT" "$CORE"
rm -f "$OUT"
