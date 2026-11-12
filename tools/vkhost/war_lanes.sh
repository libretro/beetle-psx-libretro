#!/bin/sh
# Write-after-read lane for the Vulkan renderer, run headless through
# vkhost (lavapipe works). Content is gen_war_exe.py with the display off,
# so no scanout separates the frames: each pass a sprite samples a block
# and a quad then draws over it, growing the render pass the sprite is in
# over the block it read. Every sprite in the readback must hold the
# colour from the pass before its own. The race shows on some runs and
# not others, so the content runs RUNS times; on a tree without the
# barrier ordering most runs fail. The validation layer reports nothing.
#
# Usage (from the repository root, after a HAVE_HW=1 build):
#   sh tools/vkhost/war_lanes.sh [core.so]
set -e
cd "$(dirname "$0")/../.."
CORE="${1:-./mednafen_psx_hw_libretro.so}"
case "$CORE" in /*) ;; *) CORE="$PWD/$CORE" ;; esac
OUT=/tmp/vk_war_lanes
SYS=/tmp/vkhost_sys
P=beetle_psx_hw
FRAMES=240
RUNS=8

make -s -C tools/vkhost vkhost
rm -rf "$OUT"
mkdir -p "$OUT" "$SYS"
[ -f "$SYS/scph5501.bin" ] || head -c 524288 /dev/zero > "$SYS/scph5501.bin"
python3 tools/vkhost/gen_war_exe.py "$OUT/war.exe" off

i=1
while [ "$i" -le "$RUNS" ]; do
   VKHOST_RAM_OUT="$OUT/run$i.bin:100000:100000" \
   VKHOST_VARS="${P}_renderer=hardware_vk;${P}_renderer_software_fb=disabled" \
      tools/vkhost/vkhost "$CORE" "$OUT/war.exe" - "$FRAMES" "$OUT/run$i" \
      > "$OUT/run$i.log" 2>&1 || { tail -20 "$OUT/run$i.log"; exit 1; }
   i=$((i + 1))
done

python3 - "$OUT" "$RUNS" <<'PY'
import re, struct, sys
out, runs = sys.argv[1], int(sys.argv[2])
def colour(p):
    c = (8 * (p & 31) + 7) >> 3
    return c | (c << 5)
ok = True
for r in range(1, runs + 1):
    vram = struct.unpack('<%dH' % (1024 * 512),
          open('%s/run%d.bin' % (out, r), 'rb').read()[:2 * 1024 * 512])
    bad = []
    for p in range(169, 201):
        s = p & 31
        got = set(vram[y * 1024 + x] for y in range(300, 316)
                  for x in range(16 * s, 16 * s + 16))
        if got != {colour(p - 1)}:
            bad.append(p)
    errors = int(re.search(r'(\d+) validation errors',
          open('%s/run%d.log' % (out, r)).read()).group(1))
    print('run %d: %d of 32 sprites read the quad drawn after them%s; %d validation errors'
          % (r, len(bad), ' (passes %s)' % bad if bad else '', errors))
    ok &= not bad and errors == 0
print('war_lanes: %s' % ('PASS' if ok else 'FAIL'))
sys.exit(0 if ok else 1)
PY
