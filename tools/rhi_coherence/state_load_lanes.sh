#!/bin/sh
# Savestate-load lanes for the hardware renderers, with the Software
# Framebuffer on (the default), on Vulkan (vkhost, lavapipe) and OpenGL
# (glhost, llvmpipe).
#
# Runahead and Preemptive Frames load a savestate every frame. A load must
# hand the GPU only the VRAM it changed: the GPU already holds the rest, at
# its internal resolution. Each lane saves a state and loads it straight
# back before every frame, which changes nothing, and checks that
#   - the output is exactly that of a run without the loads, and
#   - the loads add at most a little upload work (a CPU->VRAM transfer
#     caught half way still goes up whole), where re-uploading all of VRAM
#     every frame multiplies it a hundredfold.
# Content is gen_frame_exe.py, whose per-frame CPU->VRAM stream puts some
# of the loads in the middle of a transfer.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/rhi_coherence/state_load_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/rhi_state_load_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=120

make -s -C tools/vkhost vkhost
make -s -C tools/glhost glhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame.exe" drawarea feedback

# work <plain> <loaded> <label>: loads may add at most half again.
work() {
   if [ -z "$1" ] || [ -z "$2" ] || [ "$2" -gt $(( $1 + $1 / 2 )) ]; then
      echo "$3: loads re-uploaded VRAM ($1 without loads, $2 with)" >&2
      return 1
   fi
}

fail=0
for res in 1x 2x; do
   V="${P}_internal_resolution=$res"

   for m in plain load; do
      env $( [ $m = load ] && echo VKHOST_RELOAD=1 ) \
         VKHOST_DUMP_INTERVAL=10 VKHOST_VARS="${P}_renderer=hardware_vk;$V" \
         tools/vkhost/vkhost "$CORE" "$OUT/frame.exe" - "$FRAMES" "$OUT/vk_${res}_$m" \
         > "$OUT/vk_${res}_$m.log" 2>&1 || { cat "$OUT/vk_${res}_$m.log"; exit 1; }
      GLHOST_FRAMEHASH=1 GLHOST_PREEMPT=$( [ $m = load ] && echo 1 || echo 0 ) \
         GLHOST_VARS="${P}_renderer=hardware_gl;$V" \
         tools/glhost/glhost "$CORE" "$OUT/frame.exe" - "$FRAMES" "$OUT/gl_${res}_$m" \
         > "$OUT/gl_${res}_$m.log" 2>&1 || { cat "$OUT/gl_${res}_$m.log"; exit 1; }
   done

   d=0
   for f in "$OUT/vk_${res}_plain"/*.ppm; do
      cmp -s "$f" "$OUT/vk_${res}_load/$(basename "$f")" || d=$((d + 1))
   done
   a=$(sed -n 's/.*steady state: \([0-9]*\) compute.*/\1/p' "$OUT/vk_${res}_plain.log")
   b=$(sed -n 's/.*steady state: \([0-9]*\) compute.*/\1/p' "$OUT/vk_${res}_load.log")
   printf 'vk %s: %s frames differ, %s workgroups without loads, %s with\n' "$res" "$d" "$a" "$b"
   [ "$d" = 0 ] || fail=1
   work "$a" "$b" "vk $res" || fail=1
   for m in plain load; do
      if ! grep -q " 0 validation errors" "$OUT/vk_${res}_$m.log"; then
         echo "vk $res $m: Vulkan validation errors" >&2
         fail=1
      fi
   done

   grep "\[glhost\] frame .* hash" "$OUT/gl_${res}_plain.log" > "$OUT/gl_${res}_plain.hash"
   grep "\[glhost\] frame .* hash" "$OUT/gl_${res}_load.log" > "$OUT/gl_${res}_load.hash"
   d=$(diff "$OUT/gl_${res}_plain.hash" "$OUT/gl_${res}_load.hash" | grep -c '^<' || true)
   n=$(wc -l < "$OUT/gl_${res}_plain.hash")
   a=$(sed -n 's/.*steady state: \([0-9]*\) texels.*/\1/p' "$OUT/gl_${res}_plain.log")
   b=$(sed -n 's/.*steady state: \([0-9]*\) texels.*/\1/p' "$OUT/gl_${res}_load.log")
   printf 'gl %s: %s of %s frames differ, %s texels uploaded without loads, %s with\n' "$res" "$d" "$n" "$a" "$b"
   [ "$d" = 0 ] && [ "$n" -gt 0 ] || fail=1
   work "$a" "$b" "gl $res" || fail=1
done
[ "$fail" = 0 ] || exit 1
echo "rhi_coherence: state load lanes passed"
