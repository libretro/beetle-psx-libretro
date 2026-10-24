#!/bin/sh
# Builds and runs diff_load_test against mednafen/state.c: the test as
# strict C89, then under AddressSanitizer + UBSan (skipped where the
# toolchain lacks them). CC=x86_64-w64-mingw32-gcc RUN=wine runs it as a
# Windows build.
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
OUT=tools/savestate/diff_load_test
INC="-I. -Ilibretro-common/include"
DEFS="-DMEDNAFEN_VERSION_NUMERIC=9386"
$CC -std=c89 -pedantic -Wno-long-long -Wall -Werror $INC -fsyntax-only \
   tools/savestate/diff_load_test.c
build() {
   $CC -O1 -g -Wall $INC $DEFS "$@" -o "$OUT" \
      tools/savestate/diff_load_test.c mednafen/state.c \
      libretro-common/compat/compat_strl.c
}
build
if [ -n "${RUN+x}" ]; then
   [ -f "$OUT.exe" ] && EXE="$OUT.exe" || EXE="$OUT"
   $RUN "$EXE"
else
   "./$OUT"
   if build -fsanitize=address,undefined -fno-sanitize-recover=undefined \
         2>/dev/null; then
      ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 "./$OUT"
   fi
fi
rm -f "$OUT" "$OUT.exe"
