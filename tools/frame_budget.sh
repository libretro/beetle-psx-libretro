#!/bin/sh
# Stack-frame budget for core functions held to the 4 KiB limit.
#
# Compiles libretro.c with -fstack-usage using the flags the HW build uses
# and fails if any function named below has a frame over 4096 bytes. A
# function joins the list once it is under budget, so it cannot creep back.
#
# Usage, from the repo root:
#     sh tools/frame_budget.sh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

BUDGET=4096
FUNCS="retro_run CloseGame memcard_swap_index memcard_save_index_async refresh_hdr_metadata"

cd "$root"
${CC:-cc} -c -O3 -fstack-usage -o "$work/libretro.o" libretro.c \
   -DHAVE_OPENGL -DCORE -DHAVE_CDROM -DNDEBUG -DHAVE_HW -DHAVE_MMAP \
   -D_GNU_SOURCE -DHAVE_SHM -DHAVE_PBP -DWANT_THREADING -DHAVE_THREADS \
   -DNEED_DEINTERLACER -DWANT_32BPP -DNEED_CD -DHAVE_CHD -DHAVE_RCHD \
   -DHAVE_LIGHTREC -DTEXTURE_DUMPING_ENABLED -DHAVE_VULKAN \
   -D__LIBRETRO__ -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS \
   -fwrapv -fsigned-char \
   -I. -I./mednafen -I./mednafen/include -I./libretro-common/include \
   -I./deps/lightning/include -I./deps/lightrec -I./include \
   -I./parallel-psx/khronos/include -include ./include/debug.h \
   2>"$work/cc.log" || { cat "$work/cc.log"; exit 1; }

fail=0
for f in $FUNCS; do
   size=$(awk -F'\t' -v f="$f" '{ split($1, a, ":"); n = a[4]; sub(/\..*$/, "", n);
                                  if (n == f) print $2 }' "$work/libretro.su" | sort -n | tail -1)
   if [ -z "$size" ]; then
      echo "frame_budget: $f not found (renamed? update FUNCS)"; fail=1
   elif [ "$size" -gt "$BUDGET" ]; then
      echo "frame_budget: $f is $size bytes, over $BUDGET"; fail=1
   else
      echo "   $f: $size"
   fi
done
[ "$fail" = 0 ] && echo "frame_budget: OK"
exit $fail
