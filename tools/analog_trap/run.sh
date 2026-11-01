#!/bin/sh
# Builds and runs trap_test as strict C89 against the shipping
# rhi/analog_trap.h, then under AddressSanitizer + UBSan where available.
# CC=x86_64-w64-mingw32-gcc RUN=wine runs it as a Windows build.
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
OUT=tools/analog_trap/trap_test
FLAGS="-std=c89 -pedantic -O2 -g -Wall -Werror -I."
$CC $FLAGS -o "$OUT" tools/analog_trap/trap_test.c -lm
if [ -n "${RUN+x}" ]; then
   [ -f "$OUT.exe" ] && EXE="$OUT.exe" || EXE="$OUT"
   $RUN "$EXE"
else
   "./$OUT"
fi
if [ -z "${RUN+x}" ] && $CC $FLAGS -fsanitize=address,undefined \
      -fno-sanitize-recover=undefined -o "${OUT}_asan" tools/analog_trap/trap_test.c -lm 2>/dev/null; then
   "./${OUT}_asan"
fi
rm -f "$OUT" "$OUT.exe" "${OUT}_asan"
