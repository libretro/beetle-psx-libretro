#!/bin/sh
# MDEC IDCT lane (see mdec_idct_test.c): strict C89, then under
# AddressSanitizer + UBSan with no recovery, then the plain C arm with the
# vector ISA's macro taken away.
# CC=x86_64-w64-mingw32-gcc RUN=wine runs it as a Windows build;
# CC=aarch64-linux-gnu-gcc RUN="qemu-aarch64 -L /usr/aarch64-linux-gnu"
# runs the NEON arm.
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
SRC=tools/mdec_idct/mdec_idct_test.c
OUT=tools/mdec_idct/mdec_idct_test
WARN="-O2 -g -Wall -Wextra -Werror -Ilibretro-common/include"
$CC -std=c89 -pedantic -Wno-long-long $WARN -o "$OUT" $SRC
if [ -n "${RUN+x}" ]; then
   [ -f "$OUT.exe" ] && EXE="$OUT.exe" || EXE="$OUT"
   $RUN "$EXE"
else
   "./$OUT"
   $CC $WARN -fsanitize=address,undefined -fno-sanitize-recover=undefined \
      -o "$OUT" $SRC
   "./$OUT"
   $CC $WARN -U__SSE2__ -o "$OUT" $SRC
   "./$OUT"
fi
rm -f "$OUT" "$OUT.exe"
