#!/bin/sh
# Fused HD pages under AddressSanitizer, UndefinedBehaviorSanitizer and
# LeakSanitizer, on both hardware renderers (lavapipe, llvmpipe). Content
# and pack as in fused_page_sync.sh: paletted uploads served as fused
# pages, created and rebuilt while the game runs. Every page, the rects it
# fuses and the uploads behind them must be released; the drivers' own
# allocations are suppressed (lsan.supp).
#
# Usage (from the repository root, after
#   make HAVE_HW=1 SANITIZER=address,undefined):
#   sh tools/tt_replace/fused_page_leaks.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/tt_fused_page_leaks
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=40
LIBASAN=$(${CC:-cc} -print-file-name=libasan.so)

make -s -C tools/vkhost vkhost
make -s -C tools/glhost glhost
rm -rf "$OUT"
mkdir -p "$OUT/content" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/tt_replace/gen_clut_exe.py "$OUT/content/hd.exe" static

TT="${P}_track_textures=enabled"
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0
export LSAN_OPTIONS=suppressions="$PWD/tools/tt_replace/lsan.supp":print_suppressions=0
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
LD_PRELOAD="$LIBASAN" VKHOST_NO_VALIDATION=1 \
VKHOST_VARS="${P}_renderer=hardware_vk;$TT;${P}_dump_textures=enabled;${P}_replace_textures=disabled" \
   tools/vkhost/vkhost "$CORE" "$OUT/content/hd.exe" - "$FRAMES" "$OUT/dump" \
   > "$OUT/dump.log" 2>&1 || { tail -40 "$OUT/dump.log"; exit 1; }
python3 tools/tt_replace/make_pack.py "$OUT/content/hd-texture-dump" \
   "$OUT/content/hd-texture-replacements" > /dev/null

fail=0
for r in vk gl; do
   rc=0
   if [ $r = vk ]; then
      LD_PRELOAD="$LIBASAN" VKHOST_NO_VALIDATION=1 \
      VKHOST_VARS="${P}_renderer=hardware_vk;$TT;${P}_replace_textures=enabled" \
         tools/vkhost/vkhost "$CORE" "$OUT/content/hd.exe" - "$FRAMES" "$OUT/$r" \
         > "$OUT/$r.log" 2>&1 || rc=$?
   else
      LD_PRELOAD="$LIBASAN" \
      GLHOST_VARS="${P}_renderer=hardware_gl;$TT;${P}_replace_textures=enabled" \
         tools/glhost/glhost "$CORE" "$OUT/content/hd.exe" - "$FRAMES" "$OUT/$r" \
         > "$OUT/$r.log" 2>&1 || rc=$?
   fi
   n=$(grep -cE 'ERROR: (Address|Leak)Sanitizer|runtime error' "$OUT/$r.log" || true)
   echo "$r: exit $rc, sanitizer reports $n"
   if [ $rc -ne 0 ] || [ "$n" -ne 0 ]; then
      grep -A8 -E 'ERROR: (Address|Leak)Sanitizer|runtime error|Direct leak' "$OUT/$r.log" | head -30
      fail=1
   fi
done
[ $fail -eq 0 ] && echo "fused_page_leaks: PASS" || { echo "fused_page_leaks: FAIL"; exit 1; }
