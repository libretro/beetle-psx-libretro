#!/bin/sh
# Steady-state resource lanes for the Vulkan renderer: after warm-up, a
# frame must not create Vulkan objects. vkhost counts every image, image
# view, buffer, buffer view, framebuffer and memory allocation the core
# makes and reports how many fell in the second half of the run. Uploads go
# through a per-frame staging ring, vertex data through recycled blocks,
# and the scanout and the analog and HDR post intermediates through the
# renderer's image cache, so every display path must stay at (nearly)
# zero: a regression shows up as thousands.
#
# The frame's fence rides on its last submit to the frontend's queue
# rather than on a submit of its own, so a frame submits once: each lane
# is held to its submits per two frames (the last field: 2 for a plain
# frame; rollback replays a frame and saves and restores a state, 10), plus
# teardown's few.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/vkhost/steady_state.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/vk_steady_state
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=600
LIMIT=8

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame.exe" drawarea feedback

fail=0
while IFS='|' read -r name preempt subs vars; do
   [ -n "$name" ] || continue
   env ${preempt:+VKHOST_PREEMPT=1} VKHOST_VARS="${P}_renderer=hardware_vk;$vars" \
      tools/vkhost/vkhost "$CORE" "$OUT/frame.exe" - "$FRAMES" "$OUT/$name" \
      > "$OUT/$name.log" 2>&1 || { cat "$OUT/$name.log"; exit 1; }
   n=$(sed -n 's/.*steady state: \([0-9]*\) Vulkan objects.*/\1/p' "$OUT/$name.log")
   printf '%-10s %s objects created in the last %d frames\n' "$name" "${n:-?}" $((FRAMES / 2))
   if [ -z "$n" ] || [ "$n" -gt "$LIMIT" ]; then
      fail=1
   fi
   q=$(sed -n 's/.*steady state: \([0-9]*\) queue submits.*/\1/p' "$OUT/$name.log")
   printf '%-10s %s queue submits in the last %d frames\n' "$name" "${q:-?}" $((FRAMES / 2))
   if [ -z "$q" ] || [ "$q" -gt $((FRAMES / 4 * subs + LIMIT)) ]; then
      echo "$name: more queue submits than $subs per two frames" >&2
      fail=1
   fi
   if ! grep -q " 0 validation errors" "$OUT/$name.log"; then
      echo "$name: Vulkan validation errors" >&2
      fail=1
   fi
done <<LANES
plain||2|
2x||2|${P}_internal_resolution=2x
smoothing||2|${P}_internal_resolution=2x;${P}_adaptive_smoothing=enabled
composite||2|${P}_video_cable=composite
hdr_post||2|${P}_color_format=30bit_hdr;${P}_hdr_bloom=standard;${P}_hdr_bloom_convolution=enabled;${P}_hdr_chromatic=heavy
rollback|1|10|${P}_renderer_software_fb=disabled
LANES
[ "$fail" = 0 ] || exit 1
echo "vkhost: steady state lanes passed"
