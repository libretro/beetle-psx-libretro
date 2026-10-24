#!/bin/sh
# Steady-state resource lanes for the OpenGL renderer: after warm-up, a
# frame must not re-specify buffers (glBufferData) or create GL objects.
# glhost counts both and reports how many fell in the second half of the
# run. Indices go through a ring that only re-specifies when it wraps, and
# the Display Transform's uniform buffer only when its contents change, so
# every display path must stay at (nearly) zero: per-flush or per-draw
# re-specification shows up as hundreds.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/glhost/steady_state.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/gl_steady_state
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=600
LIMIT=16

make -s -C tools/glhost glhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame.exe" drawarea feedback

fail=0
while IFS='|' read -r name preempt vars; do
   [ -n "$name" ] || continue
   env ${preempt:+GLHOST_PREEMPT=$preempt} GLHOST_VARS="${P}_renderer=hardware_gl;$vars" \
      tools/glhost/glhost "$CORE" "$OUT/frame.exe" - "$FRAMES" "$OUT/$name" \
      > "$OUT/$name.log" 2>&1 || { cat "$OUT/$name.log"; exit 1; }
   r=$(sed -n 's/.*steady state: \([0-9]*\) buffer re-specifications.*/\1/p' "$OUT/$name.log")
   o=$(sed -n 's/.*re-specifications, \([0-9]*\) GL objects.*/\1/p' "$OUT/$name.log")
   printf '%-10s %s buffer re-specifications, %s objects created in the last %d frames\n' \
      "$name" "${r:-?}" "${o:-?}" $((FRAMES / 2))
   if [ -z "$r" ] || [ -z "$o" ] || [ "$r" -gt "$LIMIT" ] || [ "$o" -gt "$LIMIT" ]; then
      fail=1
   fi
done <<LANES
plain||
4x||${P}_internal_resolution=4x
hdr_aces2||${P}_color_format=30bit_hdr;${P}_hdr_display_transform=aces2full
hdr_post||${P}_color_format=30bit_hdr;${P}_hdr_bloom=standard;${P}_hdr_chromatic=heavy
composite||${P}_video_cable=composite
rollback|2|${P}_renderer_software_fb=disabled
LANES
[ "$fail" = 0 ] || exit 1
echo "glhost: steady state lanes passed"
