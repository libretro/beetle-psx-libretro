#!/bin/sh
# Savestate-load lanes for the hardware renderers with the Software
# Framebuffer off, on Vulkan (vkhost, lavapipe) and OpenGL (glhost,
# llvmpipe).
#
# The host rolls back one frame every frame, as Preemptive Frames does, so
# every frame loads a state. A load hands the GPU what it changed in the
# CPU copy of VRAM and the blocks the coherence map does not know to match
# it; BEETLE_FULL_VRAM_UPLOAD=1 uploads all of VRAM instead. Each lane
# checks that
#   - the output is exactly that of the full upload: every frame at 1x,
#     the presented ones at 2x, where a replayed frame may show the
#     internal-resolution pixels the full upload throws away,
#   - the loads do at most 3/4 of the full upload's work.
# Content is gen_frame_exe.py with a per-buffer drawing area and
# render-to-texture feedback. The output is compared on content light
# enough that every frame starts with the GPU idle: a state saved while it
# draws holds VRAM the renderer has only partly drawn, which neither load
# can give back as it was. The work is measured on the full content, as a
# game redraws only part of VRAM a frame.
#
# A frontend that registers the core's savestate lifetime callbacks, as
# RetroArch does, gets the renderer's own VRAM back instead: on Vulkan,
# rolling back with the full content, which keeps the GPU drawing across
# every frame boundary, must show exactly what running without rollback
# shows, with the Software Framebuffer on and off.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/rhi_coherence/hw_load_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/rhi_hw_load_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=120

make -s -C tools/vkhost vkhost
make -s -C tools/glhost glhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame.exe" drawarea feedback light
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame_full.exe" drawarea feedback

# vk_run <name> <res> <exe> [env...]
vk_run() {
   n=$1; r=$2; exe=$3; shift 3
   env "$@" VKHOST_DUMP_INTERVAL=2 \
      VKHOST_VARS="${P}_renderer=hardware_vk;${P}_renderer_software_fb=disabled;${P}_internal_resolution=$r" \
      tools/vkhost/vkhost "$CORE" "$OUT/$exe" - "$FRAMES" "$OUT/$n" \
      > "$OUT/$n.log" 2>&1 || { cat "$OUT/$n.log"; exit 1; }
   if ! grep -q " 0 validation errors" "$OUT/$n.log"; then
      echo "$n: Vulkan validation errors" >&2
      fail=1
   fi
}

# vk_managed <name> <res> <software_fb> [env...]: the full content
vk_managed() {
   n=$1; r=$2; fb=$3; shift 3
   env "$@" VKHOST_DUMP_INTERVAL=2 \
      VKHOST_VARS="${P}_renderer=hardware_vk;${P}_renderer_software_fb=$fb;${P}_internal_resolution=$r" \
      tools/vkhost/vkhost "$CORE" "$OUT/frame_full.exe" - "$FRAMES" "$OUT/$n" \
      > "$OUT/$n.log" 2>&1 || { cat "$OUT/$n.log"; exit 1; }
}

# gl_run <name> <res> <exe> [env...]
gl_run() {
   n=$1; r=$2; exe=$3; shift 3
   env "$@" GLHOST_FRAMEHASH=1 \
      GLHOST_VARS="${P}_renderer=hardware_gl;${P}_renderer_software_fb=disabled;${P}_internal_resolution=$r" \
      tools/glhost/glhost "$CORE" "$OUT/$exe" - "$FRAMES" "$OUT/$n" \
      > "$OUT/$n.log" 2>&1 || { cat "$OUT/$n.log"; exit 1; }
   grep "\[glhost\] frame .* hash" "$OUT/$n.log" > "$OUT/$n.hash" || true
   grep -v "(replay)" "$OUT/$n.hash" > "$OUT/$n.shown" || true
}

# vk_same <a> <b>: every dumped frame of a equals b's.
vk_same() {
   d=0; k=0
   for f in "$OUT/$1"/*.ppm; do
      k=$((k + 1))
      cmp -s "$f" "$OUT/$2/$(basename "$f")" || d=$((d + 1))
   done
   printf '%s vs %s: %s of %s frames differ\n' "$1" "$2" "$d" "$k"
   [ "$d" = 0 ] && [ "$k" -gt 0 ] || fail=1
}

# gl_same <a> <b> <hash|shown>: every frame hash of a equals b's.
gl_same() {
   d=$(diff "$OUT/$1.$3" "$OUT/$2.$3" | grep -c '^<' || true)
   k=$(wc -l < "$OUT/$1.$3")
   printf '%s vs %s: %s of %s frames differ (%s)\n' "$1" "$2" "$d" "$k" "$3"
   [ "$d" = 0 ] && [ "$k" -gt 0 ] || fail=1
}

# fraction <label> <diff> <full>: the loads must do at most 3/4 of the work.
fraction() {
   printf '%s: %s with diff loads, %s with full uploads\n' "$1" "$2" "$3"
   if [ -z "$2" ] || [ -z "$3" ] || [ $(( $2 * 4 )) -gt $(( $3 * 3 )) ]; then
      echo "$1: loads re-uploaded VRAM" >&2
      fail=1
   fi
}

steady() { sed -n "s/.*steady state: \([0-9]*\) $2.*/\1/p" "$OUT/$1.log"; }
fail=0
for res in 1x 2x; do
   vk_run "vk_${res}_diff" $res frame.exe VKHOST_PREEMPT=1
   vk_run "vk_${res}_full" $res frame.exe VKHOST_PREEMPT=1 BEETLE_FULL_VRAM_UPLOAD=1
   vk_same "vk_${res}_diff" "vk_${res}_full"
   vk_run "vk_${res}_wdiff" $res frame_full.exe VKHOST_PREEMPT=1
   vk_run "vk_${res}_wfull" $res frame_full.exe VKHOST_PREEMPT=1 BEETLE_FULL_VRAM_UPLOAD=1
   fraction "vk $res compute workgroups" \
      "$(steady "vk_${res}_wdiff" compute)" "$(steady "vk_${res}_wfull" compute)"

   gl_run "gl_${res}_diff" $res frame.exe GLHOST_PREEMPT=2
   gl_run "gl_${res}_full" $res frame.exe GLHOST_PREEMPT=2 BEETLE_FULL_VRAM_UPLOAD=1
   if [ $res = 1x ]; then
      gl_same "gl_${res}_diff" "gl_${res}_full" hash
   else
      gl_same "gl_${res}_diff" "gl_${res}_full" shown
   fi
   gl_run "gl_${res}_wdiff" $res frame_full.exe GLHOST_PREEMPT=2
   gl_run "gl_${res}_wfull" $res frame_full.exe GLHOST_PREEMPT=2 BEETLE_FULL_VRAM_UPLOAD=1
   fraction "gl $res texels uploaded" \
      "$(steady "gl_${res}_wdiff" texels)" "$(steady "gl_${res}_wfull" texels)"
done
for res in 1x 2x; do
   for fb in enabled disabled; do
      vk_managed "vk_${res}_${fb}_plain" $res $fb
      vk_managed "vk_${res}_${fb}_managed" $res $fb VKHOST_PREEMPT=1 VKHOST_LIFETIME=1
      vk_same "vk_${res}_${fb}_managed" "vk_${res}_${fb}_plain"
      if ! grep -q " 0 refused loads" "$OUT/vk_${res}_${fb}_managed.log"; then
         echo "vk_${res}_${fb}_managed: managed loads refused" >&2
         fail=1
      fi
   done
done
[ "$fail" = 0 ] || exit 1
echo "rhi_coherence: hardware load lanes passed"
