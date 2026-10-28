#!/bin/sh
# Temporal upscaler lanes for the Vulkan renderer: vkhost offers the core
# a frontend upscaler (VKHOST_UPSCALER) over gen_motion_exe.py's scene and
# check.py checks what the core hands it: inputs with every frame, the
# Halton jitter, motion vectors that take each changed pixel back to where
# the frame before had it, the transparency mask on the translucent quad
# only and depth on what is drawn (PGXP's on the GTE triangle in the
# pgxp lane, drawn with PGXP on). The core must ask for the upscaler in
# retro_load_game, draw at the factor the request fits (2x for 640x480,
# 1x for 320x240, 4x for 1280x960, whatever Internal GPU Resolution says),
# and, as the other Vulkan paths, create no Vulkan objects once warm.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/upscaler/lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/vk_upscaler
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=90
FIRST=60
LIMIT=8

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/upscaler/gen_motion_exe.py "$OUT/motion.exe"

fail=0
# name|request|resolution option|expected drawing line|check flags|more options
while IFS='|' read -r name request res expect flags vars; do
   [ -n "$name" ] || continue
   mkdir -p "$OUT/$name"
   VKHOST_UPSCALER="$request" VKHOST_UPSCALER_LOG="$OUT/$name/upscaler.txt" \
      VKHOST_DUMP_INTERVAL=1 VKHOST_DUMP_RAW=1 \
      VKHOST_VARS="${P}_renderer=hardware_vk;${P}_internal_resolution=$res;${P}_msaa=2x$vars" \
      tools/vkhost/vkhost "$CORE" "$OUT/motion.exe" - "$FRAMES" "$OUT/$name" \
      > "$OUT/$name.log" 2>&1 || { cat "$OUT/$name.log"; exit 1; }
   if ! grep -q "asked for in retro_load_game 1" "$OUT/$name.log"; then
      echo "$name: the core did not ask for the upscaler in retro_load_game" >&2
      fail=1
   fi
   if ! grep -q "Temporal upscaler: drawing at $expect" "$OUT/$name.log"; then
      echo "$name: the core did not say it draws at $expect" >&2
      grep "Temporal upscaler" "$OUT/$name.log" >&2 || true
      fail=1
   fi
   if ! python3 tools/upscaler/check.py "$OUT/$name" "$OUT/$name/upscaler.txt" "$FIRST" "$FRAMES" $flags \
         > "$OUT/$name.check" 2>&1; then
      cat "$OUT/$name.check"
      echo "$name: check.py failed" >&2
      fail=1
   fi
   n=$(sed -n 's/.*steady state: \([0-9]*\) Vulkan objects.*/\1/p' "$OUT/$name.log")
   printf '%-8s %s objects created in the last %d frames\n' "$name" "${n:-?}" $((FRAMES / 2))
   if [ -z "$n" ] || [ "$n" -gt "$LIMIT" ]; then
      fail=1
   fi
   if ! grep -q " 0 validation errors" "$OUT/$name.log"; then
      echo "$name: Vulkan validation errors" >&2
      fail=1
   fi
done <<LANES
2x|640,480|4x|2x, 640x480|--scene
1x|320,240,160,120,640,480|2x|1x, 320x240|--scene
4x|1280,960|1x|4x, 1280x960|--scene
pgxp|640,480|2x|2x, 640x480|--scene --gte-depth 0.5|;${P}_pgxp_mode=memory only;${P}_pgxp_texture=enabled
LANES

# a request no factor fits: the core says so and renders as the options say
mkdir -p "$OUT/unfit"
VKHOST_UPSCALER="1000,700,900,650,1100,750" VKHOST_UPSCALER_LOG="$OUT/unfit/upscaler.txt" \
   VKHOST_VARS="${P}_renderer=hardware_vk;${P}_internal_resolution=2x" \
   tools/vkhost/vkhost "$CORE" "$OUT/motion.exe" - 30 "$OUT/unfit" > "$OUT/unfit.log" 2>&1 || { cat "$OUT/unfit.log"; exit 1; }
if ! grep -q "no factor of 320x240 fits" "$OUT/unfit.log" || ! grep -q " none$" "$OUT/unfit/upscaler.txt"; then
   echo "unfit: a request no factor fits was not said, or got inputs" >&2
   fail=1
fi

exit $fail
