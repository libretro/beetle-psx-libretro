#!/bin/sh
# Fused HD pages through the Vulkan renderer, under the validation layer's
# synchronization checks, run headless through vkhost (lavapipe works).
# Content is gen_clut_exe.py: a 4 bpp page uploaded as two halves, both
# replaced, so draws across it are served a fused page, rebuilt in place
# each frame as the halves are sent again. Builds a synthetic pack from a
# dump of that content (make_pack.py) and checks:
#   - the dump is named by the hashes HD packs are keyed by, which must
#     not move (an upload's CRC over its packed rows, then its palette's);
#   - the pack is used (frames differ from the run without it);
#   - the page clear, the blits into it and its layout changes are ordered:
#     no synchronization hazard on a transfer command or a barrier.
# Hazards the layer reports on dispatches and draws are listed, not failed:
# it takes an image a descriptor names as wholly accessed, while the
# renderer orders VRAM per 8x8 block, so disjoint blocks read as hazards.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/tt_replace/fused_page_sync.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/tt_fused_page_sync
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=30

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT/content" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/tt_replace/gen_clut_exe.py "$OUT/content/hd.exe" static

TT="${P}_renderer=hardware_vk;${P}_track_textures=enabled"
VKHOST_VARS="$TT;${P}_dump_textures=enabled;${P}_replace_textures=disabled" \
   tools/vkhost/vkhost "$CORE" "$OUT/content/hd.exe" - "$FRAMES" "$OUT/native" \
   > "$OUT/native.log" 2>&1 || { tail -20 "$OUT/native.log"; exit 1; }
names=$(ls "$OUT/content/hd-texture-dump" | tr '\n' ' ')
if [ "$names" != "7041fe9a-13893d6f.png 8bb0ea93-13893d6f.png " ]; then
   echo "fused_page_sync: dump names moved: $names"; exit 1
fi
python3 tools/tt_replace/make_pack.py "$OUT/content/hd-texture-dump" \
   "$OUT/content/hd-texture-replacements" > /dev/null

fail=0
for m in lazy_sync lazy; do
   # vkhost exits 5 when the layer reported anything; sorted out below.
   rc=0
   VKHOST_SYNC_VALIDATION=1 \
   VKHOST_VARS="$TT;${P}_dump_textures=disabled;${P}_replace_textures=enabled;${P}_hd_caching_method=$m" \
      tools/vkhost/vkhost "$CORE" "$OUT/content/hd.exe" - "$FRAMES" "$OUT/$m" \
      > "$OUT/$m.log" 2>&1 || rc=$?
   [ $rc -eq 0 ] || [ $rc -eq 5 ] || { tail -20 "$OUT/$m.log"; exit 1; }
   ordered=$(grep 'SYNC-HAZARD' "$OUT/$m.log" |
      grep -cE 'vkCmd(BlitImage|ClearColorImage|CopyImage|CopyBufferToImage|PipelineBarrier)\(' || true)
   other=$(grep -c 'SYNC-HAZARD' "$OUT/$m.log" || true)
   other=$((other - ordered))
   used=yes
   cmp -s "$OUT/$m/frame_$(printf %04d $FRAMES).ppm" \
      "$OUT/native/frame_$(printf %04d $FRAMES).ppm" && used=no
   echo "$m: pack used: $used, transfer/barrier hazards: $ordered," \
        "descriptor hazards (not checked): $other"
   if [ "$used" = no ] || [ "$ordered" -ne 0 ]; then
      grep 'SYNC-HAZARD' "$OUT/$m.log" |
         grep -E 'vkCmd(BlitImage|ClearColorImage|CopyImage|CopyBufferToImage|PipelineBarrier)\(' |
         head -3 | cut -c1-400
      fail=1
   fi
done
[ $fail -eq 0 ] && echo "fused_page_sync: PASS" || { echo "fused_page_sync: FAIL"; exit 1; }
