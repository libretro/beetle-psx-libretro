#!/bin/sh
# Builds and runs upscaler_test (the temporal upscaler's geometry,
# rhi/rhi_upscaler.c) as strict C89, then under AddressSanitizer + UBSan
# (skipped where the toolchain lacks them).
# CC=x86_64-w64-mingw32-gcc RUN=wine runs it as a Windows build.
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
SRC="tools/upscaler/upscaler_test.c rhi/rhi_upscaler.c"
OUT=tools/upscaler/upscaler_test
FLAGS="-std=c89 -pedantic -Wno-long-long -Wno-variadic-macros -O2 -g -Wall -Werror -I. -Ilibretro-common/include"
$CC $FLAGS -o "$OUT" $SRC -lm
if [ -n "${RUN+x}" ]; then
   [ -f "$OUT.exe" ] && EXE="$OUT.exe" || EXE="$OUT"
   $RUN "$EXE"
else
   "./$OUT"
fi
if [ -z "${RUN+x}" ] && $CC $FLAGS -fsanitize=address,undefined \
      -fno-sanitize-recover=undefined -o "${OUT}_asan" $SRC -lm 2>/dev/null; then
   ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 "./${OUT}_asan"
fi
rm -f "$OUT" "$OUT.exe" "${OUT}_asan" "${OUT}_asan.exe"
