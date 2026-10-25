#!/bin/sh
# CDC DMA FIFO lane (see cdc_fifo_test.c): strict C89, then under
# AddressSanitizer + UBSan with no recovery in the compiler's default
# language mode, the one the core builds in - C90 leaves a shift into the
# sign bit defined, so only there does UBSan report one.
# CC=x86_64-w64-mingw32-gcc RUN=wine runs it as a Windows build.
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
SRC=tools/cdc_fifo/cdc_fifo_test.c
OUT=tools/cdc_fifo/cdc_fifo_test
WARN="-O1 -g -Wall -Wextra -Werror -Ilibretro-common/include"
$CC -std=c89 -pedantic -Wno-long-long $WARN -o "$OUT" $SRC
if [ -n "${RUN+x}" ]; then
   [ -f "$OUT.exe" ] && EXE="$OUT.exe" || EXE="$OUT"
   $RUN "$EXE"
else
   "./$OUT"
   $CC $WARN -fsanitize=address,undefined -fno-sanitize-recover=undefined \
      -o "$OUT" $SRC
   "./$OUT"
fi
rm -f "$OUT" "$OUT.exe"
