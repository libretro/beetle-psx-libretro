#!/bin/sh
# Managed savestate lanes for the Vulkan renderer (vkhost, lavapipe).
#
# The host rolls back one frame every frame as RetroArch's Preemptive
# Frames does, and with VKHOST_LIFETIME takes the core's savestate
# lifetime callbacks as RetroArch does. A managed state names a snapshot of
# VRAM as the renderer held it, so the rollback puts the renderer back as
# it was at its internal resolution. Content is gen_frame_exe.py with a
# per-buffer drawing area and render-to-texture feedback.
#   - each lane's frames under rollback with lifetime callbacks are the
#     frames of the same run without rollback, pixel for pixel;
#   - the 2x lane without lifetime callbacks differs from the run without
#     rollback: an ordinary state reloads 1x VRAM, which is what the
#     snapshots are for, and the content shows it;
#   - every managed save is released, by teardown at the latest;
#   - with the Software Framebuffer off, a managed save that got a snapshot
#     reads no VRAM back from the GPU (an ordinary one reads it all, less
#     what the coherence map skips), and with BEETLE_RHI_COHERENCE_CHECK no
#     read of VRAM the core does make finds a stale pixel;
#   - a state released before it is loaded is refused when it left VRAM on
#     the GPU, and loads when it carries VRAM;
#   - the second half of each run makes no Vulkan objects: the snapshots'
#     images are made once;
#   - the validation layer reports nothing.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/vkhost/lifetime_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/vk_lifetime_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=180
LIMIT=8

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame.exe" drawarea feedback

run()
{
   # $1 name, $2 extra environment, $3 core options
   env $2 VKHOST_VARS="${P}_renderer=hardware_vk;$3" \
      tools/vkhost/vkhost "$CORE" "$OUT/frame.exe" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { cat "$OUT/$1.log"; exit 1; }
   if ! grep -q " 0 validation errors" "$OUT/$1.log"; then
      echo "$1: Vulkan validation errors" >&2
      fail=1
   fi
}

same()
{
   # $1 run, $2 reference: every dumped frame identical
   for f in "$OUT/$2"/frame_*.ppm; do
      cmp -s "$f" "$OUT/$1/${f##*/}" || return 1
   done
   ls "$OUT/$2"/frame_*.ppm > /dev/null 2>&1
}

fail=0
while IFS='|' read -r name vars; do
   [ -n "$name" ] || continue
   run "${name}_plain" "" "$vars"
   run "${name}_lifetime" "VKHOST_PREEMPT=1 VKHOST_LIFETIME=1" "$vars"
   log="$OUT/${name}_lifetime.log"
   saves=$(sed -n 's/.*lifetime: \([0-9]*\) managed saves.*/\1/p' "$log")
   rels=$(sed -n 's/.*managed saves, \([0-9]*\) releases.*/\1/p' "$log")
   objs=$(sed -n 's/.*steady state: \([0-9]*\) Vulkan objects.*/\1/p' "$log")
   if same "${name}_lifetime" "${name}_plain"; then m=identical; else m=DIFFERENT; fail=1; fi
   printf '%-10s rollback frames %s to the plain run; %s managed saves, %s released; %s objects in the second half\n' \
      "$name" "$m" "${saves:-?}" "${rels:-?}" "${objs:-?}"
   if [ -z "$saves" ] || [ "$saves" -lt $((FRAMES - 2)) ] || [ "$saves" != "$rels" ]; then
      echo "$name: managed saves not all made and released" >&2
      fail=1
   fi
   if [ -z "$objs" ] || [ "$objs" -gt "$LIMIT" ]; then
      echo "$name: Vulkan objects made in the second half" >&2
      fail=1
   fi
done <<LANES
2x|${P}_internal_resolution=2x;${P}_renderer_software_fb=disabled
4x|${P}_internal_resolution=4x;${P}_renderer_software_fb=disabled
swfb|${P}_internal_resolution=2x
smoothing|${P}_internal_resolution=2x;${P}_adaptive_smoothing=enabled;${P}_renderer_software_fb=disabled
hdr|${P}_internal_resolution=2x;${P}_color_format=30bit_hdr;${P}_renderer_software_fb=disabled
1x|${P}_internal_resolution=1x;${P}_renderer_software_fb=disabled
LANES

# The control: the same rollback with ordinary states loses the 2x picture
run 2x_ordinary "VKHOST_PREEMPT=1 BEETLE_RHI_COHERENCE_CHECK=1" "${P}_internal_resolution=2x;${P}_renderer_software_fb=disabled"
if same 2x_ordinary 2x_plain; then
   echo "2x: rollback with ordinary states matches the plain run; the content does not show what snapshots fix" >&2
   fail=1
else
   echo "2x_ordinary rollback frames differ from the plain run, as they should"
fi

# Readbacks: the managed run against the ordinary one, every read checked
run 2x_coherence "VKHOST_PREEMPT=1 VKHOST_LIFETIME=1 BEETLE_RHI_COHERENCE_CHECK=1" "${P}_internal_resolution=2x;${P}_renderer_software_fb=disabled"
for r in 2x_ordinary 2x_coherence; do
   whole=$(grep -c "\[rhi_coherence\] read (0,0 1024x512)" "$OUT/$r.log" || true)
   stale=$(grep -c "\[rhi_coherence\] stale" "$OUT/$r.log" || true)
   printf '%-12s %s whole-VRAM reads, %s stale\n' "$r" "$whole" "$stale"
   if [ "$stale" != 0 ]; then
      grep "\[rhi_coherence\] stale" "$OUT/$r.log" | head -5
      fail=1
   fi
   case $r in
      2x_ordinary)  [ "$whole" -ge $((FRAMES - 2)) ] || { echo "$r: the ordinary saves did not read VRAM back" >&2; fail=1; } ;;
      2x_coherence) [ "$whole" = 0 ] || { echo "$r: managed saves read VRAM back" >&2; fail=1; } ;;
   esac
done
if ! same 2x_coherence 2x_plain; then
   echo "2x_coherence: frames differ from the plain run" >&2
   fail=1
fi

# A frontend that releases a state before loading it: with the Software
# Framebuffer off the state left VRAM on the GPU, so the core must refuse
# the load; with it on the state carries VRAM and the load goes ahead
for r in "early_hw|disabled|1" "early_swfb|enabled|0"; do
   name=${r%%|*}; rest=${r#*|}; swfb=${rest%%|*}; want=${rest#*|}
   run "$name" "VKHOST_PREEMPT=1 VKHOST_LIFETIME=1 VKHOST_LIFETIME_EARLY_RELEASE=60" \
      "${P}_internal_resolution=2x;${P}_renderer_software_fb=$swfb"
   got=$(sed -n 's/.*releases, \([0-9]*\) refused loads.*/\1/p' "$OUT/$name.log")
   printf '%-12s %s refused loads\n' "$name" "$got"
   [ "$got" = "$want" ] || { echo "$name: expected $want refused loads" >&2; fail=1; }
done
[ "$fail" = 0 ] || exit 1
echo "vkhost: lifetime lanes passed"
