#!/bin/sh
# Builds tools/hdr_transform/aces2_gpu (strict C89) against the shipping
# rhi/hdr_transform.c and rhi/hdr_aces2.c and runs it on an EGL surfaceless
# desktop GL context (llvmpipe works): ACES 2.0 (full) through the baked
# cube and the per-pixel solve, each against the double-precision reference.
set -e
cd "$(dirname "$0")/../.."
OUT=tools/hdr_transform/aces2_gpu
${CC:-cc} -std=c89 -pedantic -Wall -Werror -Wno-long-long -Wno-overlength-strings \
   -O2 -g -I. -Irhi -Ilibretro-common/include -o "$OUT" \
   tools/hdr_transform/aces2_gpu.c rhi/hdr_transform.c rhi/hdr_aces2.c -lEGL -lGL -lm
"./$OUT"
rm -f "$OUT"
