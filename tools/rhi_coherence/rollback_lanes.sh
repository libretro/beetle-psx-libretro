#!/bin/sh
# Rollback lanes for the VRAM readback coherence map, on the Vulkan renderer
# (vkhost, lavapipe) and the OpenGL renderer (glhost, llvmpipe), with the
# Software Framebuffer off so savestates read VRAM back from the GPU.
#
# Content is gen_frame_exe.py with a per-buffer drawing area and
# render-to-texture feedback; the host rolls back one frame every frame as
# RetroArch's Preemptive Frames does, so every frame takes a savestate - a
# whole-VRAM read the coherence map skips or splits. The core runs with
# BEETLE_RHI_COHERENCE_CHECK, which re-reads every requested rect whole and
# logs any pixel the map left stale. Every lane must log none, at 1x and
# 2x, and must actually have split reads to check.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/rhi_coherence/rollback_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/rhi_rollback_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=360

make -s -C tools/vkhost vkhost
make -s -C tools/glhost glhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame.exe" drawarea feedback

export BEETLE_RHI_COHERENCE_CHECK=1
fail=0
for res in 1x 2x; do
   V="${P}_renderer_software_fb=disabled;${P}_internal_resolution=$res"
   VKHOST_PREEMPT=1 VKHOST_VARS="${P}_renderer=hardware_vk;$V" \
      tools/vkhost/vkhost "$CORE" "$OUT/frame.exe" - "$FRAMES" "$OUT/vk_$res" \
      > "$OUT/vk_$res.log" 2>&1 || { cat "$OUT/vk_$res.log"; exit 1; }
   GLHOST_PREEMPT=2 GLHOST_VARS="${P}_renderer=hardware_gl;$V" \
      tools/glhost/glhost "$CORE" "$OUT/frame.exe" - "$FRAMES" "$OUT/gl_$res" \
      > "$OUT/gl_$res.log" 2>&1 || { cat "$OUT/gl_$res.log"; exit 1; }
   for r in vk gl; do
      log="$OUT/${r}_$res.log"
      stale=$(grep -c "\[rhi_coherence\] stale" "$log" || true)
      split=$(grep -c "\[rhi_coherence\] read (0,0 1024x512) in [1-8] part" "$log" || true)
      skipped=$(grep -c "\[rhi_coherence\] read (0,0 1024x512) in 0 part" "$log" || true)
      printf '%s %s: %s stale reads, %s savestate reads split, %s skipped\n' \
         "$r" "$res" "$stale" "$split" "$skipped"
      if [ "$stale" != 0 ]; then
         grep "\[rhi_coherence\] stale" "$log" | head -5
         fail=1
      fi
      if [ "$split" -lt 20 ]; then
         echo "$r $res: too few checked savestate reads" >&2
         fail=1
      fi
      if [ "$r" = vk ] && ! grep -q " 0 validation errors" "$log"; then
         echo "$r $res: Vulkan validation errors" >&2
         fail=1
      fi
   done
done
[ "$fail" = 0 ] || exit 1
echo "rhi_coherence: rollback lanes passed"
