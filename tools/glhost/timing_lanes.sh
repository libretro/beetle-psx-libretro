#!/bin/sh
# Emulated GPU timing lanes for the hardware renderers: the GPU's busy time
# a game sees does not depend on the renderer or on the Software
# Framebuffer option. Content is the gpu_thread probe
# (tools/gpu_thread/gpu_thread_test.c), which counts its waits for the
# GPU's DMA into RAM; every lane must count what the software renderer
# counts. With PGXP the software renderer draws, and charges, sub-pixel
# geometry, so there the lanes must count what OpenGL with the software
# framebuffer on counts.
#
#   sw         software renderer
#   gl*, vk*   OpenGL and Vulkan, software framebuffer on and off, at 1x
#              and 2x, with and without PGXP
#
# usage: tools/glhost/timing_lanes.sh [hw core]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/glhost_timing_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=24
WAITS="F0000:4"

make -s -C tools/glhost glhost
make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
${CC:-cc} -std=c89 -pedantic -Wall -Wno-long-long -O1 \
   -Ilibretro-common/include -o "$OUT/gpu_thread_test" \
   tools/gpu_thread/gpu_thread_test.c -ldl
(cd "$OUT" && ./gpu_thread_test probe)
EXE="$OUT/gpu_thread_probe.exe"

gl()
{
   # $1 name, $2 GLHOST_VARS
   GLHOST_VARS="$2" GLHOST_RAM_OUT="$OUT/$1.bin:$WAITS" \
      tools/glhost/glhost "$CORE" "$EXE" - "$FRAMES" "$OUT/$1" \
      2> "$OUT/$1.log" || { cat "$OUT/$1.log"; exit 1; }
}
vk()
{
   # $1 name, $2 VKHOST_VARS
   VKHOST_VARS="${P}_renderer=hardware_vk;$2" VKHOST_RAM_OUT="$OUT/$1.bin:$WAITS" \
      tools/vkhost/vkhost "$CORE" "$EXE" - "$FRAMES" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { cat "$OUT/$1.log"; exit 1; }
}

OFF="${P}_renderer_software_fb=disabled"
ON="${P}_renderer_software_fb=enabled"
PGXP="${P}_pgxp_mode=memory only;${P}_pgxp_texture=enabled"
X2="${P}_internal_resolution=2x"
gl sw        "${P}_renderer=software"
gl gl_on     "$ON"
gl gl_off    "$OFF"
gl gl_off2x  "$OFF;$X2"
gl gl_onpgxp "$ON;$PGXP"
gl gl_offpgxp "$OFF;$PGXP"
vk vk_on     "$ON"
vk vk_off    "$OFF"
vk vk_off2x  "$OFF;$X2"
vk vk_offpgxp "$OFF;$PGXP"

waits() { od -An -tu4 "$OUT/$1.bin" | tr -d ' '; }
fail=0
check()
{
   # $1 reference, then the lanes that must match it
   r=$1; ref=$(waits "$r"); shift
   for n in "$@"; do
      got=$(waits "$n")
      if [ "$got" = "$ref" ]; then
         printf '%-11s waits %s  OK\n' "$n" "$got"
      else
         printf '%-11s waits %s, %s %s  FAIL\n' "$n" "$got" "$r" "$ref"
         fail=1
      fi
   done
}
check sw gl_on gl_off gl_off2x vk_on vk_off vk_off2x
check gl_onpgxp gl_offpgxp vk_offpgxp
[ "$fail" = 0 ] && echo "timing_lanes: PASS" || echo "timing_lanes: FAIL"
exit $fail
