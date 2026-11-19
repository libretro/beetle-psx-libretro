#!/bin/sh
# Run-Ahead lanes for the Vulkan renderer (vkhost, lavapipe).
#
# The host runs RetroArch's Run-Ahead on one instance (VKHOST_RUNAHEAD):
# each frame the kept frame and every speculative frame but the last run
# with video off, reported through GET_AUDIO_VIDEO_ENABLE, and the host
# throws away what they hand it. Content is gen_frame_exe.py with a
# per-buffer drawing area and render-to-texture feedback.
#   - frames run with video off draw no picture;
#   - the frames shown are the frames of the same run on a frontend that
#     reports video on throughout (VKHOST_AV_ALWAYS), which the core draws
#     every frame for, pixel for pixel;
#   - the validation layer reports nothing.
# Each lane prints both runs' wall time.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/vkhost/runahead_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/vk_runahead_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=120

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame.exe" drawarea feedback

now_ms()
{
   echo $(( $(date +%s%N) / 1000000 ))
}

run()
{
   # $1 name, $2 extra environment, $3 core options
   t0=$(now_ms)
   env $2 VKHOST_VARS="${P}_renderer=hardware_vk;$3" \
      tools/vkhost/vkhost "$CORE" "$OUT/frame.exe" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { cat "$OUT/$1.log"; exit 1; }
   ms=$(( $(now_ms) - t0 ))
   if ! grep -q " 0 validation errors" "$OUT/$1.log"; then
      echo "$1: Vulkan validation errors" >&2
      fail=1
   fi
   drawn=$(sed -n 's/.*run-ahead: \([0-9]*\) frames drawn with video off.*/\1/p' "$OUT/$1.log")
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
while IFS='|' read -r name ahead vars; do
   [ -n "$name" ] || continue
   run "${name}_always" "VKHOST_RUNAHEAD=$ahead VKHOST_AV_ALWAYS=1" "$vars"
   always_ms=$ms; always_drawn=$drawn
   run "${name}_hidden" "VKHOST_RUNAHEAD=$ahead" "$vars"
   if same "${name}_hidden" "${name}_always"; then m=identical; else m=DIFFERENT; fail=1; fi
   printf '%-10s shown frames %s; hidden frames drawn %s (video reported on: %s); %s ms (%s ms)\n' \
      "$name" "$m" "${drawn:-?}" "${always_drawn:-?}" "$ms" "$always_ms"
   if [ "${drawn:-x}" != 0 ]; then
      echo "$name: frames run with video off drew a picture" >&2
      fail=1
   fi
   if [ -z "$always_drawn" ] || [ "$always_drawn" -lt $(( (FRAMES - 1) * ahead )) ]; then
      echo "$name: with video reported on, the hidden frames were not drawn" >&2
      fail=1
   fi
done <<LANES
2x|1|${P}_internal_resolution=2x;${P}_renderer_software_fb=disabled
2x_ahead3|3|${P}_internal_resolution=2x;${P}_renderer_software_fb=disabled
4x_ssaa|1|${P}_internal_resolution=4x;${P}_super_sampling=enabled;${P}_renderer_software_fb=disabled
smoothing|1|${P}_internal_resolution=2x;${P}_adaptive_smoothing=enabled;${P}_renderer_software_fb=disabled
hdr|1|${P}_internal_resolution=2x;${P}_color_format=30bit_hdr;${P}_renderer_software_fb=disabled
1x|1|${P}_internal_resolution=1x
LANES

[ "$fail" = 0 ] || exit 1
echo "vkhost: run-ahead lanes passed"
