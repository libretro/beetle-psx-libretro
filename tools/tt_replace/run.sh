#!/bin/sh
# Builds and runs tt_replace_latch_test as strict C89, then under
# AddressSanitizer + UBSan. Runs on Linux and on Windows under MSYS2 MINGW64
# (where the sanitizer pass is skipped if the toolchain lacks it).
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
SRC=tools/tt_replace/tt_replace_latch_test.c
OUT=tools/tt_replace/tt_replace_latch_test
FLAGS="-std=c89 -pedantic -Wno-long-long -Wno-variadic-macros -O1 -g -Wall -Werror -I. -Ilibretro-common/include"
$CC $FLAGS -o "$OUT" $SRC
"./$OUT"
if $CC $FLAGS -fsanitize=address,undefined -fno-sanitize-recover=undefined \
      -o "${OUT}_asan" $SRC 2>/dev/null; then
   ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 "./${OUT}_asan"
fi
rm -f "$OUT" "$OUT.exe" "${OUT}_asan" "${OUT}_asan.exe"
