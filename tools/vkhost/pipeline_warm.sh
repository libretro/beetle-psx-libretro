#!/bin/sh
# Pipeline warm-up lanes for the Vulkan renderer, run headless through
# vkhost (lavapipe works). Content is tools/rhi_coherence/gen_frame_exe.py
# (textured, semi-transparent and feedback draws, VRAM copies).
#
# For each configuration a first run starts with an empty save directory,
# so it builds its pipelines on the frame's thread and writes the
# pipeline list. The driver's cache blob is then deleted, as a driver
# update makes it useless, and a second run must build none on the
# frame's thread: the list and the warm-up thread give it every one. Both
# runs must hand back the same frames. The runs leave the warm-up thread
# the time a frontend leaves between loading content and running it
# (VKHOST_LOAD_PAUSE_MS): this content draws from its first frame, and
# lavapipe takes tens of milliseconds a pipeline.
#
# A second session in the same process (the content unloaded and loaded
# again, as a statically linked frontend does) makes the device again,
# which reads the list the first one wrote as it was torn down: it must
# build none on the frame's thread. A session torn down one frame in,
# while the warm-up thread is still building, must end clean (the
# validation layer reports any pipeline left at vkDestroyDevice) and keep
# the list.
#
# Then the list file is damaged four ways (cut short, a byte changed,
# another version, not a list at all): each must be ignored - the run
# builds what a first run builds and draws the same frames - and replaced
# by a good one, which the run after that uses.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/vkhost/pipeline_warm.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/vkhost_pipeline_warm
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=90

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/rhi_coherence/gen_frame_exe.py "$OUT/frame.exe" additive drawarea feedback

fail=0
run()
{
   # $1 name, $2 save directory, $3 options, $4 content for a second session
   if [ -n "$4" ]; then export VKHOST_NEXT_CONTENT="$4"; else unset VKHOST_NEXT_CONTENT; fi
   VKHOST_SAVE_DIR="$2" VKHOST_VARS="$3" VKHOST_DUMP_INTERVAL=15 \
   VKHOST_LOAD_PAUSE_MS=${PAUSE_MS:-3000} \
      tools/vkhost/vkhost "$CORE" "$OUT/frame.exe" - "${RUN_FRAMES:-$FRAMES}" "$OUT/$1" \
      > "$OUT/$1.log" 2>&1 || { cat "$OUT/$1.log"; exit 1; }
}
# (with a second session, the counts at the end of each, the second's
# counting both)
counts()  { sed -n "s/.*pipelines: \([0-9]*\) built on the frame's thread.*/\1/p" "$OUT/$1.log"; }
built()   { counts "$1" | tail -n 1; }
behind()  { sed -n "s/.*, \([0-9]*\) on the core's own threads.*/\1/p" "$OUT/$1.log" | tail -n 1; }
same_frames()
{
   # $1 reference run, $2 run
   n=0
   for f in "$OUT/$1"/frame_*.ppm; do
      n=$((n + 1))
      if ! cmp -s "$f" "$OUT/$2/$(basename "$f")"; then
         echo "$2: $(basename "$f") differs from $1" >&2
         fail=1
      fi
   done
   [ "$n" -gt 0 ] || { echo "$1: no frames dumped" >&2; fail=1; }
}
list()  { ls "$1/Beetle PSX HW"/vulkan_pipeline_list_*.bin 2>/dev/null | head -n 1; }
blobs() { rm -f "$1/Beetle PSX HW"/vulkan_pipeline_cache_*.bin; }

lane()
{
   # $1 name, $2 options
   save="$OUT/save_$1"
   run "$1_cold" "$save" "$2"
   if [ -z "$(list "$save")" ]; then
      echo "$1: no pipeline list written" >&2
      fail=1
      return
   fi
   blobs "$save"
   run "$1_warm" "$save" "$2"
   cold=$(built "$1_cold"); warm=$(built "$1_warm")
   printf '%s: %s pipelines built on the frame thread cold, %s with the list (%s by the warm-up thread)\n' \
      "$1" "$cold" "$warm" "$(behind "$1_warm")"
   [ "$cold" -gt 0 ] || { echo "$1: the first run built no pipeline" >&2; fail=1; }
   [ "$warm" = 0 ] || { echo "$1: the frame thread still built $warm" >&2; fail=1; }
   same_frames "$1_cold" "$1_warm"
}

lane plain   "${P}_internal_resolution=1x"
lane scaled  "${P}_internal_resolution=2x;${P}_msaa=4x;${P}_filter=bilinear"

# The device made again in the same process.
run session "$OUT/save_session" "${P}_internal_resolution=1x" "$OUT/frame.exe"
first=$(counts session | head -n 1); both=$(built session)
printf 'second session: %s pipelines built on the frame thread in the first, %s in the second\n' \
   "$first" $((both - first))
[ "$first" -gt 0 ] && [ "$both" = "$first" ] \
   || { echo "session: the second session built $((both - first)) on the frame thread" >&2; fail=1; }
same_frames session/next session

# Torn down one frame in, the warm-up thread still busy: a clean end, and
# the list kept.
PAUSE_MS=0 RUN_FRAMES=1 run early "$OUT/save_plain" "${P}_internal_resolution=1x"
grep -q " 0 validation errors" "$OUT/early.log" \
   || { echo "early: validation errors at teardown" >&2; fail=1; }
run after_early "$OUT/save_plain" "${P}_internal_resolution=1x"
[ "$(built after_early)" = 0 ] \
   || { echo "after_early: the list was lost" >&2; fail=1; }
echo "torn down one frame in: clean, list kept"

# A damaged list is ignored and replaced.
good="$(list "$OUT/save_plain")"
size=$(wc -c < "$good")
for damage in short byte version junk; do
   save="$OUT/save_$damage"
   mkdir -p "$save/Beetle PSX HW"
   bad="$save/Beetle PSX HW/$(basename "$good")"
   case $damage in
      short)   head -c $((size - 7)) "$good" > "$bad" ;;
      byte)    cp "$good" "$bad"
               printf '\377' | dd of="$bad" bs=1 seek=$((size / 2)) conv=notrunc 2>/dev/null ;;
      version) cp "$good" "$bad"
               printf '\002' | dd of="$bad" bs=1 seek=8 conv=notrunc 2>/dev/null ;;
      junk)    head -c "$size" /dev/urandom > "$bad" ;;
   esac
   run "damaged_$damage" "$save" "${P}_internal_resolution=1x"
   if ! grep -q "\[Vulkan pipeline list\] ignoring" "$OUT/damaged_$damage.log"; then
      echo "damaged_$damage: the list was not refused" >&2
      fail=1
   fi
   [ "$(built "damaged_$damage")" = "$(built plain_cold)" ] \
      || { echo "damaged_$damage: built $(built "damaged_$damage"), a first run builds $(built plain_cold)" >&2; fail=1; }
   same_frames plain_cold "damaged_$damage"
   run "repaired_$damage" "$save" "${P}_internal_resolution=1x"
   [ "$(built "repaired_$damage")" = 0 ] \
      || { echo "repaired_$damage: the rewritten list was not used" >&2; fail=1; }
   echo "damaged list ($damage): refused, replaced, used"
done

[ "$fail" = 0 ] && echo "pipeline_warm: ok" || { echo "pipeline_warm: FAILED" >&2; exit 1; }
