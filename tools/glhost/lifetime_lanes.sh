#!/bin/sh
# Compare upscaled rollback with and without managed states.
# Usage: sh tools/glhost/lifetime_lanes.sh [core.so]
set -eu
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=$(mktemp -d)
mkdir "$OUT/system" "$OUT/save"
head -c 524288 /dev/zero > "$OUT/system/scph5501.bin"

make -s -C tools/glhost glhost
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame.exe" drawarea feedback readback

run()
{
   name=$1
   scale=$2
   shift 2
   env GLHOST_SYSTEM_DIR="$OUT/system" GLHOST_SAVE_DIR="$OUT/save" \
      GLHOST_VARS="beetle_psx_hw_internal_resolution=$scale;beetle_psx_hw_renderer_software_fb=disabled;beetle_psx_hw_renderer=$renderer;beetle_psx_hw_gpu_thread=$gpu_thread" \
      GLHOST_FRAMEHASH=1 GLHOST_RAM_OUT="$OUT/$name.ram:1f0000:200" \
      BEETLE_RHI_COHERENCE_CHECK=1 "$@" \
      tools/glhost/glhost "$CORE" "$OUT/frame.exe" - 60 "$OUT/$name" \
      > "$OUT/$name.log" 2>&1
   grep -E 'frame [0-9]+ hash [0-9a-f]+$' "$OUT/$name.log" > "$OUT/$name.hash"
   test "$(wc -l < "$OUT/$name.hash")" -eq 60
}

echo "Logs and captures in $OUT"
renderer=hardware_gl
gpu_thread=disabled
run plain 4x
run managed 4x GLHOST_PREEMPT=2 GLHOST_LIFETIME=1
run legacy 4x GLHOST_PREEMPT=2
grep -q 'lifetime: 60 saves, 60 releases' "$OUT/managed.log"
grep -q 'read (0,0 16x16) in [1-9]' "$OUT/managed.log"
if grep -q 'rhi_coherence.*stale' "$OUT/managed.log"; then
   grep 'rhi_coherence.*stale' "$OUT/managed.log"
   exit 1
fi
cmp "$OUT/plain.hash" "$OUT/managed.hash"
cmp "$OUT/plain.ram" "$OUT/managed.ram"
if cmp -s "$OUT/plain.hash" "$OUT/legacy.hash"; then
   echo 'Expected ordinary rollback to lose 4x detail' >&2
   exit 1
fi

run native 1x
run native_managed 1x GLHOST_PREEMPT=2 GLHOST_LIFETIME=1
cmp "$OUT/native.hash" "$OUT/native_managed.hash"
cmp "$OUT/native.ram" "$OUT/native_managed.ram"
echo 'OpenGL managed savestate checks passed'

renderer=software
run sw_plain 4x
run sw_legacy 4x GLHOST_PREEMPT=2
if cmp -s "$OUT/sw_plain.hash" "$OUT/sw_legacy.hash"; then
   echo 'Expected ordinary Software rollback to lose 4x detail' >&2
   exit 1
fi
for gpu_thread in disabled 3; do
   run sw_managed_$gpu_thread 4x GLHOST_PREEMPT=2 GLHOST_LIFETIME=1
   grep -q 'software-renderer mode' "$OUT/sw_managed_$gpu_thread.log"
   grep -q 'lifetime: 60 saves, 60 releases' "$OUT/sw_managed_$gpu_thread.log"
   cmp "$OUT/sw_plain.hash" "$OUT/sw_managed_$gpu_thread.hash"
   cmp "$OUT/sw_plain.ram" "$OUT/sw_managed_$gpu_thread.ram"
done
echo 'Software managed savestate checks passed'
